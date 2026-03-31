---
title: "How Guss Works: Building a Bytecode Template Compiler and CMS-Agnostic SSG in C++23"
slug: how-guss-works
description: "A deep dive into the architecture of Guss — from SIMD-accelerated JSON parsing to zero-allocation template rendering on monotonic stack arenas."
---

## How Guss Works

Guss is a static site generator written in C++23. It builds a complete website. 77 items across 4 collections, a RSS feed and a sitemap, 82 output files, from a live Ghost CMS in **517 milliseconds**. That time includes DNS resolution, HTTP round-trips, JSON parsing, Markdown processing, template compilation, parallel rendering, and writing files to disk.

This page explains how.

---

### The Problem With Existing SSGs

Static site generators are not a new idea. Hugo, Gatsby, Eleventy, Jekyll, Astro — there are dozens. Most of them work. So why build another one?

Because every existing SSG forces a compromise that only becomes visible after you've committed to it.

**Gatsby** is flexible with data sources but requires a GraphQL layer, a React rendering pipeline, and hundreds of megabytes of `node_modules`. A site with 40 posts takes minutes to build. Upgrading between major versions is a multi-day migration project. I know this because I ran Gatsby 2.0 on my own blog for years and never upgraded — the effort wasn't worth the result.

**Hugo** is fast. Genuinely fast. But it locks you into its data model and its template language. Want to pull content from a REST API? Write a script that dumps JSON files before Hugo runs. Want to switch from one CMS to another? Rewrite your templates, because the data shape changed.

**Next.js** markets itself as a framework but is architecturally a sales funnel for Vercel's infrastructure. ISR, Edge Functions, Image Optimization — each feature pulls you closer to a single hosting vendor.

The common thread: **tight coupling between content source and presentation**. Changing your CMS means changing your SSG configuration, your data fetching logic, and often your templates. That coupling is the problem Guss solves.

---

### Architecture Overview

Guss operates as a strict four-phase pipeline:

```bash
FETCH  →  PREPARE  →  RENDER  →  WRITE
```

Each phase has one job, clean inputs, and clean outputs. The phases communicate through two data structures: `Value` (a discriminated union) and `CollectionMap` (a flat map of collection names to render items). No phase knows what a "Ghost API" or a "WordPress response" looks like. By the time data enters the pipeline, it's already been converted to `Value`.

The entire architecture rests on one decision: **there are no domain types**. There is no `Post` struct, no `Author` class, no `Tag` model. Everything is a `Value`.

---

### The Value Type

`Value` is a `std::variant` of nine alternatives:

```cpp
std::variant<
    NullTag,
    std::string_view,       // deprecated
    std::string,            // owned string (e.g. from filter output)
    bool,
    int64_t,
    uint64_t,
    double,
    std::shared_ptr<ValueMap>,    // key → Value
    std::shared_ptr<ValueArray>   // Value[]
>
```

> ⚠️
>
> **Note on `std::string_view`:** The `string_view` variant is **deprecated** and will be removed in a future release. The original idea was elegant. Reference directly into the `simdjson::ondemand` parsed buffer and pass `string_view` down to the runtime with zero allocation for strings. No copies, no heap usage, just pointers into the already-loaded JSON.
>
> But `simdjson::ondemand` objects are **transient**. They are valid only while the parser holds the underlying buffer and while the iteration position hasn't advanced. Once the adapter finishes converting a batch of items, the `ondemand::document` is destroyed and its buffer is freed. Any `string_view` pointing into that buffer becomes a dangling reference and the render phase runs **much later**, potentially on different threads, long after the adapter has been destroyed.
>
> This is not a bug in simdjson; it's a deliberate design choice for performance. The on-demand parser reuses buffers and advances through documents sequentially. To make zero-copy work across phases, I would need to keep every CMS response buffer alive for the entire build, which would spike memory usage significantly.
>
> So the `string_view` variant remains as a vestigial organ, never used at all. All strings reaching the runtime are either owned `std::string` (from filters or enrichment) or live inside `shared_ptr<ValueMap>` allocations that outlive the adapter. The deprecation note exists because I was busy debugging real issues.

The recursive structure — a Value can contain a map of Values, each of which can contain more maps or arrays — mirrors JSON's shape exactly. But unlike JSON libraries, `Value` is a native C++ type with no parsing overhead in the render path.

The critical design choice is `shared_ptr` for maps and arrays. When you copy a `Value` that wraps a blog post with 20 fields, nested author objects, and an array of tags, the copy costs exactly one atomic increment. Not a deep copy. Not a serialization round-trip. One integer operation.

This matters because the render phase copies page data into per-thread contexts. With deep copies, parallelism would create a bottleneck at the data distribution step. With `shared_ptr`, distributing data to 32 threads is essentially free.

The underlying data is never mutated after construction. This means `Value` is safe to share across threads without locks, mutexes, or any synchronization mechanism beyond the atomic reference count that `shared_ptr` already provides.

> **Deep dive:** [Value System](/internals/value-system/) — why `shared_ptr`, why `unordered_map` not `flat_map`, and dotted-path resolution internals.

---

### Phase 1: Fetch — The CMS-Agnostic Adapter

This is the phase that took five attempts across four programming languages to get right.

#### The Problem

Every CMS returns data differently. Ghost returns posts at `/ghost/api/content/posts/` with content in a field called `html`. WordPress returns them at `/wp-json/wp/v2/posts` with content nested inside `content.rendered`. Ghost paginates by including a `meta.pagination.next` field in the response body. WordPress puts the total page count in an `X-WP-TotalPages` HTTP header. Ghost uses API key authentication via a query parameter. WordPress uses Basic Auth with application passwords.

A naive adapter handles this by writing code for each CMS. A `GhostAdapter` that knows Ghost's URL structure, field names, and pagination style. A `WordPressAdapter` that knows WordPress's. Every new CMS requires a new adapter class.

This approach doesn't scale, and it means switching CMSes requires code changes.

#### The Solution: Configuration-Driven Mapping

Guss has exactly one adapter for REST APIs: `RestCmsAdapter`. It handles any HTTP JSON API through configuration:

```yaml
source:
  type: rest_api
  base_url: "https://ghost.example.com/"

  auth:
    type: api_key        # also: basic, bearer, none
    param: key
    value: "your-api-key"

  pagination:
    json_next: "meta.pagination.next"    # Ghost-style
    # total_pages_header: "X-WP-TotalPages"  # WordPress-style

  endpoints:
    posts:
      path: "ghost/api/content/posts/"
      response_key: "posts"

  field_maps:
    posts:
      content: "html"        # Ghost's "html" → templates see "content"
      author: "authors.0"    # first author element → singular "author"
```

#### `resolve_path`: The Key Function

The entire CMS-agnostic design rests on a single function: `resolve_path`. It takes a dot-separated string and walks the `Value` tree.

```cpp
Value resolve_path(const Value& v, std::string_view path);
```

- `"html"` → direct field lookup
- `"content.rendered"` → nested object traversal
- `"authors.0"` → array index
- `"tags.slug"` → array projection (collects `slug` from every element)

This function is roughly 40 lines of C++. It handles string segments as object keys, numeric segments as array indices, and non-numeric segments applied to arrays as projections across all elements.

`apply_field_map` uses `resolve_path` to rename fields before the template layer ever sees them:

```cpp
void apply_field_map(Value& item, const field_map_t& field_map) {
    for (const auto& [target, source_path] : field_map) {
        item.set(target, resolve_path(item, source_path));
    }
}
```

That's it. That's the entire CMS abstraction. Templates always see `content` and `author` regardless of whether the source is Ghost, WordPress, or any other API. Switching CMSes means editing YAML keys, not rewriting code.

#### Pagination Strategies

REST APIs paginate in at least eight different ways. I know because I hit all eight in real-world usage:

1. **Total pages from HTTP header** — WordPress-style `X-WP-TotalPages`
2. **Total count from HTTP header** — calculate pages from item count
3. **Link header** — follow `rel="next"` URLs
4. **Cursor tokens** — extract a cursor from the response body, send it back
5. **Next-page URL in body** — response contains the full URL of the next page
6. **Sentinel value** — a non-null value at a JSON path means more pages exist (Ghost)
7. **Optimistic fetching** — keep requesting page N+1 until empty or 404
8. **Offset-based** — send `offset = (page-1) * limit`

All eight are handled through configuration. No code changes, no plugins.

#### simdjson Boundary

JSON parsing uses simdjson, which processes data at gigabytes per second using SIMD CPU instructions. But simdjson is confined to the adapter layer behind a hard architectural boundary.

The function `from_simdjson()` converts a simdjson document into a `Value` tree. Once that conversion is done, simdjson does not exist for the rest of the pipeline. The render layer has never included a simdjson header. This boundary exists because simdjson's lazy parsing model produces transient references that become dangling if the source document is destroyed. By converting eagerly at the adapter boundary, the rest of the system works with owned, safe, `shared_ptr`-backed data.

> **Deep dive:** [Adapters & Field Mapping](/internals/adapters/) — how `resolve_path` works, all 8 pagination strategies, and cross-reference injection.

---

### Phase 2: Prepare

The prepare phase handles three tasks:

1. **Markdown processing**: If the source is local Markdown files, the body is rendered to HTML via md4c (a C library implementing CommonMark with GFM extensions). This happens here, not in the adapter, because the adapter's job is to fetch — not to transform.

2. **Permalink expansion**: Patterns like `/{year}/{month}/{slug}/` are expanded using data from the `Value` itself. The `enrich_item` function extracts year, month, and day from `published_at`, then calls `PermalinkGenerator::expand` to produce the final URL and output path.

3. **Archive generation**: Collections with `archive_template` and `paginate` settings get paginated listing pages. The pagination context (current page, total pages, prev/next URLs) is injected automatically.

After this phase, every `RenderItem` has a fully resolved `output_path`, a `template_name`, and a `Value` containing all the data the template needs.

---

### Phase 3: Render — The Bytecode Template Engine

This is the core of Guss. Instead of using an existing template library, Guss compiles templates into bytecode and executes them on a custom virtual machine.

#### Why Not Use an Existing Library?

Performance. Template rendering is the innermost loop of the entire system. Every page passes through the template engine. If that engine allocates memory on every variable lookup, or interprets a parse tree by walking it recursively, or converts values to JSON strings for comparison, it becomes the bottleneck.

Guss eliminates every one of those costs.

#### The Compilation Pipeline

```bash
Source (.html) → Lexer → Parser → Compiler → Verifier → Runtime
```

**Lexer**: Tokenizes the template source into a stream of tokens. Uses `std::string_view` to point directly into the source buffer. Zero allocations. The lexer never copies a single byte — it records positions and lengths.

**Parser**: A recursive-descent parser consumes tokens and builds an Abstract Syntax Tree. Dotted variable paths like `post.author.name` are resolved using a "span" technique — pointer arithmetic into the source buffer rather than string concatenation. Parsing `{{ post.author.name | truncate(120) }}` involves no heap-allocated strings.

**Compiler**: Performs a single pass over the AST and emits a flat vector of bytecode instructions. Each instruction is 5 bytes: one opcode byte and a 4-byte operand. All string data, variable paths, filter names, and literal constants are interned into parallel tables. The bytecode itself contains only integer indices into these tables.

Control-flow instructions (jumps for `if`/`else`/`endif`, loop heads and tails) carry pre-patched relative offsets. The executor never searches for a matching `endif` — the jump target is baked into the instruction at compile time.

**Verifier**: Before any template executes, `verify_stack_depths()` simulates the entire bytecode sequence. It traces every possible execution path and tracks the value stack pointer and loop stack pointer at each instruction. If any path would cause either stack to overflow or underflow, compilation fails with an error.

This is a compile-time guarantee. It means the runtime loop never needs bounds checks on stack operations. No `if (sp >= stack_size)` on every push. That branch removal matters in a tight loop that executes thousands of times per page.

**Runtime**: A `switch`-based jump table over opcodes. The executor reads instructions sequentially from the bytecode vector, manipulates a fixed-size stack, and appends output to a string buffer. Variable lookups traverse the `Value` tree through the `Context`. The entire execution is a linear scan with no recursion, no dynamic dispatch, and no allocation.

#### The Context and Memory Model

Each render thread constructs a `Context` containing:

- A `shared_ptr<const SharedSiteData>` — site metadata and shared collections, constructed once and shared across all threads. One atomic increment per page, zero locks.
- Per-page `Value` data — the specific item being rendered.
- A `pmr::monotonic_buffer_resource` backed by an **8 KiB buffer on the stack frame**.

The monotonic arena is the key performance feature. `pmr::unordered_map` for `{% set %}` variables, intermediate filter results, and loop variable storage — all allocate from this arena. No `malloc`, no system calls, no heap fragmentation. When the render function returns, the arena evaporates with the stack frame. Deallocation cost: zero.

8 KiB is enough because template execution is shallow. Variable lookups hit the `Value` tree (which lives in `shared_ptr`-managed heap memory, shared across threads). The arena only holds per-page temporaries. The verifier guarantees that stack depth never exceeds the fixed allocation.

#### Parallelism

OpenMP distributes pages across all available CPU cores:

```cpp
#pragma omp parallel for schedule(dynamic)
for (size_t i = 0; i < pages.size(); ++i) {
    // Each thread: own Context, own arena, shared site data
}
```

`schedule(dynamic)` means threads grab the next available page when they finish one, rather than being assigned a fixed block. This handles the variance in page complexity — a tag page with 3 posts renders faster than one with 50.

There is zero contention between threads. Each has its own stack arena, its own output buffer, its own `Context`. The only shared state is `SharedSiteData` behind `shared_ptr<const>`, which is immutable and thread-safe by construction.

> **Deep dive:** [Template Engine Bytecode](/internals/template-engine/) — complete opcode list, stack verification, `{% set %}` and `{% for %}` internals.

---

### Phase 4: Write

The simplest phase. Each rendered page is a string paired with an output path. The write phase creates the directory structure, writes HTML files, copies static theme assets, and optionally generates `sitemap.xml` and `rss.xml`.

---

### The Template Language

Guss templates use a Jinja2-like syntax:

```html
{{ post.title }}
{{ post.content | safe }}
{% if post.featured %}★{% endif %}
{% for tag in post.tags %}
  <a href="{{ tag.url }}">{{ tag.name }}</a>
{% endfor %}
{% extends "base.html" %}
{% block content %}...{% endblock %}
```

The engine supports template inheritance with `{% extends %}` and `{% block %}`, including multi-level inheritance and `{{ super() }}`. It includes 27 built-in filters ranging from `truncate` and `date` formatting to `reading_minutes` (which strips HTML, counts words, and divides by a configurable WPM rate).

Full expression syntax is supported in conditions: `==`, `!=`, `<`, `>`, `and`, `or`, `not`, arithmetic operators, and filter chains.

Loop variables (`loop.index`, `loop.first`, `loop.last`, `loop.revindex`) are injected automatically into every `{% for %}` block.

---

### Build System and Testing

Guss builds with CMake and CPM (CMake Package Manager). All dependencies are downloaded automatically on first build. No Conan, no vcpkg, no Python, no Node. The project also provides a Docker Compose setup that packages the entire toolchain — a clean build requires only a container runtime.

The test suite contains 448 unit tests covering every filter, every bytecode instruction, every pagination strategy, field mapping edge cases, template inheritance scenarios, and error conditions. Tests run as part of the CI pipeline. Every commit is verified.

---

### Performance Breakdown

For a site with 41 posts, 33 tags, 1 author, 2 pages (77 total items, 82 output files):

| Phase     | Time       | Notes                                              |
|-----------|------------|----------------------------------------------------|
| Fetch     | 480ms      | Dominated by network round-trips (4 HTTP requests) |
| Prepare   | <1ms       | Permalink expansion, archive generation            |
| Render    | 4ms        | 82 pages across all cores, bytecode execution      |
| Write     | 33ms       | Disk I/O                                           |
| **Total** | **~517ms** | Including DNS, TLS handshake, everything           |

The prepare phase is a rounding error in the total time. The render phase at 4ms is measurable but still accounts for less than 1% of the total. The network is the only bottleneck left.

For local Markdown sources with no network involved, total build times drop to double-digit milliseconds.

#### Guss vs Gatsby — Same Content, Same CMS, Same Hardware

My blog currently runs on Gatsby 5 with a Ghost CMS backend. Both Guss and Gatsby fetch from the same Ghost instance on the same machine. This is not a synthetic benchmark — it's the same site, built by two different tools.

Gatsby's cached build (no image processing, all assets already generated):

```
success load plugins - 0.610s
success source and transform nodes - 0.611s
success building schema - 0.422s
success createPages - 0.350s
success extract queries from components - 1.114s
# ... (bootstrap finished - 5.591s)
success run page queries - 1.699s
success Building production JavaScript and CSS bundles - 7.085s
success Building HTML renderer - 0.890s
success Building static HTML for pages - 0.302s - 89/89 294.70/s
success onPostBuild - 0.107s
info Done building in 16.016331681 sec
```

Guss building the same content:

```
[2026-03-30 21:52:17.834] [console] [info] 🔥 GUSS BUILD, WITNESS PERFECTION
[2026-03-30 21:52:17.834] [console] [info] Loading configuration from guss.yaml
[2026-03-30 21:52:17.834] [console] [info] Using REST API adapter: https://ghost.michm.de/
[2026-03-30 21:52:17.835] [console] [info] Phase 1: Fetching content from rest_api
[2026-03-30 21:52:18.315] [console] [info] RestCmsAdapter: fetched 4 collections
[2026-03-30 21:52:18.315] [console] [info]   tags: 33 items
[2026-03-30 21:52:18.315] [console] [info]   authors: 1 items
[2026-03-30 21:52:18.315] [console] [info]   pages: 2 items
[2026-03-30 21:52:18.315] [console] [info]   posts: 41 items
[2026-03-30 21:52:18.315] [console] [info] Fetched 4 collections, 77 total itemsd...
[2026-03-30 21:52:18.315] [console] [info] Phase 2: Preparing content
[2026-03-30 21:52:18.315] [console] [info] Phase 3: Rendering templateshing content...
[2026-03-30 21:52:18.319] [console] [info] Phase 4: Writing 82 files
[==================================================] 100% [00m:00s] Writing files...
[2026-03-30 21:52:18.352] [console] [info] Build complete in 517ms (77 items, 5 archives, 2 extras, 0 minified)

[2026-03-30 21:52:18.353] [console] [info] Build complete!
[2026-03-30 21:52:18.353] [console] [info]   Items:    77
[2026-03-30 21:52:18.353] [console] [info]   Archives: 5
[2026-03-30 21:52:18.353] [console] [info]   Assets:   12
[2026-03-30 21:52:18.353] [console] [info]   Duration: 517ms
```

> ⚠️
>
> The garbled output above (`itemsd...`, `templateshing`) is a known race condition of the progress bar. Tracked in [issue#1](https://github.com/jibbex/guss/issues/1).


|                        | Gatsby (images pre-cached) | Guss             |
|------------------------|----------------------------|------------------|
| **Total build time**   | 16016ms                    | 517ms            |
| **Fetch content**      | 611ms _¹_                  | 480ms _²_        |
| **HTML rendering**     | 1192ms                     | 4ms              |
| **JS bundle**          | 7085ms                     | —                |
| **GraphQL overhead**   | ~3600ms                    | —                |
| **Speed factor**       | 1×                         | **~31× faster**  |
| **HTML render factor** | 1×                         | **~298× faster** |
| **Warnings**           | 4                          | 0                |
| **Runtime**            | Node.js                    | Single binary    |
| **Dependencies**       | node_modules               | Zero             |

_¹ source and transform nodes, Ghost content cached_  
_² network-bound, fetching live from Ghost API_

The total build comparison is striking enough: ~31× faster. But the HTML rendering comparison is the one worth pausing on. Gatsby's HTML generation, compiling the renderer and building static HTML for all pages, takes 1.192s. Guss renders 82 pages in 4ms. That's a ~298× difference in the step where the template engine is doing actual work.

The reason is architectural. Gatsby renders pages through React's server-side rendering pipeline, which involves a JavaScript VM, a virtual DOM, component tree reconciliation, and garbage collection. Guss executes pre-compiled bytecode against a fixed stack with an 8 KiB arena context. There is no VM, no DOM, no GC. The bytecode executor is a tight `switch` loop touching only integers until the moment it writes output to a buffer.

Gatsby's remaining ~14.8 seconds are consumed by plugin loading, GraphQL schema construction, query extraction and execution, and webpack bundling. Guss has no equivalent steps; there is no plugin system, no query layer, no bundler. Data arrives as JSON, gets converted to `Value`, and flows directly through the pipeline.

> **Note:** Image optimization is excluded from the Gatsby timing. Guss does not yet implement this feature, so including it would not be a fair comparison.

---

### What I Learned

I tried to build this system five times across four programming languages before Guss. Each attempt failed at the same point: the adapter layer. Making a tool that fetches from one API is easy. Making it truly CMS-agnostic — where the abstraction doesn't leak, where switching sources is a config change, where the template layer is completely decoupled from the data source — is an architectural problem that I couldn't solve until I found the right combination of a flexible enough value type and a simple enough path resolution system.

`resolve_path` and `field_maps` are embarrassingly simple in hindsight. But getting to that simplicity required understanding the problem space deeply enough to know what to throw away.

Guss is a personal project. I built it to replace Gatsby on my own blog. But it's also a case study in how much performance is available when you control every layer — from SIMD parsing to memory layout to bytecode execution — and make each layer's design decisions with full awareness of what the other layers need.

The source code is available at [github.com/jibbex/guss](https://github.com/jibbex/guss).

---

### Next steps

- **[Build Pipeline](/internals/pipeline/):** exact struct definitions and build stats
- **[Template Syntax](/docs/templates/):** full language reference with examples
- **[Filters Reference](/internals/filters/):** all 27 built-in filters and their signatures
- **[Configuration](/docs/configuration/):** every `guss.yaml` option explained

---

*Guss (German: der Guss — "the casting") casts websites from raw data into permanent static HTML.*
