# 🔥 Guss

> **I Am** Not A Static Site Generator. **I AM STATIC SITE GENERATION ITSELF!**

![Build](https://img.shields.io/badge/build-FORGED-brightgreen)
![Performance](https://img.shields.io/badge/threads-ALL%20OF%20THEM-blue)
![C++](https://img.shields.io/badge/C%2B%2B-23-red)

## 🌋 What Is This?

A pluggable static site generator written in C++23 that:

- **FORGES:** pages from any JSON CMS, Ghost, WordPress, or Markdown at SIMD-accelerated speed
- **CASTS:** them through a custom bytecode template engine with the precision of a German foundry
- **HARDENS:** the output into static HTML that loads before your users even CLICK
- **WATCHES:** your CMS for changes and rebuilds before you finish your coffee

> **GUSS** (German: *der Guss* — "the casting") doesn't build pages.
>
> _It CASTS them from molten CMS data into PERMANENT STATIC PERFECTION._


## ⚡ Performance That Other SSGs Find Personally Offensive

```bash
~/blog$ guss build
[2026-03-11 02:32:46.228] [console] [info] 🔥 GUSS BUILD, WITNESS PERFECTION
[2026-03-11 02:32:46.228] [console] [info] Loading configuration from guss.yaml
[2026-03-11 02:32:46.229] [console] [info] Using REST API adapter: https://ghost.michm.de/
[2026-03-11 02:32:46.229] [console] [info] Phase 1: Fetching content from rest_api
[2026-03-11 02:32:46.713] [console] [info] RestCmsAdapter: fetched 4 collections
[2026-03-11 02:32:46.713] [console] [info]   tags: 33 items
[2026-03-11 02:32:46.713] [console] [info]   authors: 1 items
[2026-03-11 02:32:46.713] [console] [info]   pages: 2 items
[2026-03-11 02:32:46.713] [console] [info]   posts: 40 items
[2026-03-11 02:32:46.713] [console] [info] Fetched 4 collections, 76 total items...
[2026-03-11 02:32:46.713] [console] [info] Phase 2: Preparing content
[2026-03-11 02:32:46.713] [console] [info] Phase 3: Rendering templates...
[2026-03-11 02:32:46.714] [console] [info] Phase 4: Writing 80 files
[==================================================] 100% [00m:00s] Writing files...
[2026-03-11 02:32:46.735] [console] [info] Build complete!
[2026-03-11 02:32:46.735] [console] [info]   Items:    76
[2026-03-11 02:32:46.735] [console] [info]   Archives: 4
[2026-03-11 02:32:46.735] [console] [info]   Assets:   1
[2026-03-11 02:32:46.735] [console] [info]   Duration: 506ms
```

---

## 🛠 Prerequisites

The recommended way to build Guss is with a container engine. No compiler, no OpenSSL, no CMake on the host. The image provides everything.

### Container build (recommended)

Any OCI-compatible runtime works. The examples use Docker Compose, but Podman Compose and equivalent tools work identically.

```bash
# Build the builder image (only needed once / when Dockerfile changes)
docker compose build

# Release build — artifacts land in ./cmake-build/
docker compose run --rm release

# Debug build with symbols — artifacts in ./cmake-build-debug/
docker compose run --rm debug

# Debug build + run all tests
docker compose run --rm test
```

Build artifacts are bind-mounted back to the host so binaries are immediately accessible after the container exits. The CPM dependency cache is baked into the image layer. Subsequent builds never hit the network.

### Native build

Requires:
- C++23 compiler (GCC 14+ or Clang 18+)
- CMake 3.25+
- OpenSSL

```bash
# Configure (CPM downloads all other dependencies automatically)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j$(nproc)

# Build with tests
cmake -B build -DGUSS_BUILD_TESTS=ON && cmake --build build -j$(nproc)

# Run tests
ctest --test-dir build --output-on-failure
```

No Conan. No vcpkg. No Python. No Node. Just CMake and a compiler. Like C++ INTENDED.

---

## 🚀 Usage

```bash
# Scaffold a new site in the current directory (or a named subdirectory)
guss init
guss init my-blog

# Build the site (reads guss.yaml by default)
guss build
guss build -c path/to/guss.yaml
guss build --clean      # wipe output directory first
guss build -v           # verbose / debug logging

# Test connectivity to the configured content source
guss ping
guss ping -c path/to/guss.yaml

# Wipe the output directory
guss clean

# Serve the output directory for local preview (wraps python3 -m http.server)
guss serve
guss serve -d dist      # explicit output directory
```

---

## 📐 Architecture

Guss is built around one core idea: **everything is a `Value`**. There are no `Post`, `Page`, `Author`, or `Tag` structs anywhere in the codebase. The moment data crosses the adapter boundary it becomes a `Value`, a C++23 discriminated union of scalars, maps, and arrays. And that is all the pipeline ever sees.

This single decision eliminates an entire class of complexity. The template engine does not know what a "post" is. The pipeline does not know what Ghost is. Everything is driven by configuration.

### The `Value` Type

```
Value = null
      | std::string_view   (zero-copy view into adapter-owned memory)
      | std::string         (owned, e.g. from filter output)
      | bool
      | int64_t | uint64_t | double
      | shared_ptr<ValueMap>    (key → Value, O(1) copy)
      | shared_ptr<ValueArray>  (Value[], O(1) copy)
```

Map and Array are heap-allocated through `shared_ptr`, copying a Value that wraps a large object costs exactly one atomic increment. The underlying data is never mutated after construction, making it safe to share across threads without locks.

### Library Structure

```
guss-core       Value, ValueMap, ValueArray, RenderItem, CollectionMap,
                Config, PermalinkGenerator
                └─ No simdjson. No CMS concepts.

guss-adapters   RestCmsAdapter, MarkdownAdapter
                └─ simdjson lives here and ONLY here.
                   from_simdjson() converts API responses → Value in one step.
                   Returns: FetchResult { CollectionMap items; Value site; }

guss-render     Lexer → Parser → Compiler → CompiledTemplate (bytecode) → Runtime
                └─ Depends on guss-core. Zero simdjson. Zero CMS concepts.

guss-builder    Pipeline: orchestrates Fetch → Prepare → Render → Write
                └─ Depends on guss-core + guss-adapters + guss-render

guss-server     HTTP server (cpp-httplib) — optional component
guss-watch      Filesystem watcher (efsw)
```

The simdjson boundary is a hard architectural rule. It enters with the raw HTTP response body and exits as a `Value` inside `from_simdjson()`. The render layer has never heard of it.

### Build Pipeline

```
┌─────────┐    ┌─────────┐    ┌──────────────────┐    ┌─────────┐
│  FETCH  │───▶│ PREPARE │───▶│  RENDER (OpenMP) │───▶│  WRITE  │
└─────────┘    └─────────┘    └──────────────────┘    └─────────┘
  Adapter        Permalinks     All cores, no locks      Disk
  → Value        Archives       SharedSiteData ptr       Assets
  → FetchResult  Pagination     per-thread Context
```

**Phase 1 — Fetch**: The adapter pulls content from the source (HTTP or filesystem) and converts everything to `Value` via `from_simdjson()` (REST) or frontmatter parsing (Markdown). The result is a `CollectionMap`, a flat `unordered_map<string, vector<RenderItem>>`, plus a `Value` carrying site metadata. No structs. No types. Just data.

**Phase 2 — Prepare**: The pipeline expands permalink patterns to output paths (`{slug}`, `{year}`, `{month}`, `{day}`, or any custom field), resolves Markdown to HTML via md4c, generates archive pages and paginated chunks, and serializes shared site data once into a `SharedSiteData` block wrapped in `shared_ptr<const>`.

**Phase 3 — Render**: OpenMP parallel loop. Each thread constructs its own `Context` with a shared pointer to `SharedSiteData` (one atomic increment per page, zero locks) plus per-page `Value` data. The bytecode engine renders to a string. No contention anywhere.

**Phase 4 — Write**: Output HTML to disk mirroring the permalink structure. Copy theme static assets. Optionally write `sitemap.xml` and `rss.xml`.

### Generic REST Adapter

There is no `GhostAdapter`. There is no `WordPressAdapter`. There is one `RestCmsAdapter` that speaks to any HTTP JSON API.

Every detail that differs between CMSes - URL paths, auth style, pagination strategy, field names, embedded relationships; is configuration, not code:

```yaml
source:
  type: rest_api
  base_url: "https://your-cms.example.com/"
  auth:
    type: api_key | basic | bearer | none
  pagination:
    json_next: "meta.pagination.next"       # Ghost-style: non-null means more pages
    # total_pages_header: "X-WP-TotalPages" # WordPress-style: header holds page count
  field_maps:
    posts:
      content: "html"         # rename response field "html" → "content"
      author:  "authors.0"    # promote first array element to scalar
  cross_references:
    tags:
      from: posts
      via: "tags.slug"        # inject matching posts into each tag page
```

Field mapping uses dot-path notation: `"content.rendered"` navigates nested objects, `"authors.0"` indexes arrays, `"tags.slug"` projects a field across every element of an array. Templates are fully CMS-agnostic.

---

## 🔌 Source Adapters

### Generic REST Adapter (`type: rest_api`)

#### Authentication

```yaml
auth:
  type: api_key       # none | api_key | basic | bearer
  param: key          # query param name: ?key=<value>
  value: "your-key"
```

```yaml
auth:
  type: basic
  username: "user"
  password: "app-password"
```

```yaml
auth:
  type: bearer
  value: "your-token"
```

#### Pagination

Configure whichever strategy your API uses (evaluated in priority order):

```yaml
pagination:
  page_param:  page    # query param for the page number
  limit_param: limit   # query param for page size
  limit: 15

  # Strategy 1 — total pages from HTTP header (WordPress-style X-WP-TotalPages):
  total_pages_header: "X-WP-TotalPages"

  # Strategy 2 — total item count from HTTP header; pages = ceil(count / limit):
  total_count_header: "X-WP-Total"

  # Strategy 3 — follow verbatim Link: rel="next" URL each round-trip:
  link_header: true

  # Strategy 4 — cursor token extracted from body each round-trip:
  json_cursor: "meta.next_cursor"   # dot-path to cursor token in body
  cursor_param: "cursor"            # query param name to send cursor value

  # Strategy 5 — body field contains full URL of next page:
  json_next_url: "meta.next"

  # Strategy 6 — non-null value at dot-path means there is a next page (Ghost-style):
  json_next: "meta.pagination.next"

  # Strategy 7 — blind GET page N+1 until empty or 404:
  optimistic_fetching: true

  # Strategy 8 — offset-based: offset = (page-1) * limit
  offset_param: "offset"
```

If no strategy is configured, the endpoint is assumed to return a single page.
Pagination can be overridden per endpoint.

#### Field Mapping

APIs return fields under CMS-specific names. `field_maps` renames them before
the template ever sees the data. Templates stay CMS-agnostic.

```yaml
field_maps:
  posts:
    content: "html"           # Ghost: rename "html" → "content"
    author:  "authors.0"      # promote authors[0] → singular "author"
    # WordPress:
    # published_at: "date"
    # content:      "content.rendered"
    # author:       "_embedded.author.0"
```

Dot-path notation supports:
- Named fields: `"content.rendered"` → nested object lookup
- Numeric indices: `"authors.0"` → first element of an array
- Array projection: `"tags.slug"` → collect `slug` from every element of `tags`

#### Cross-References

Taxonomy pages (tags, authors) need to know which items belong to them.
`cross_references` builds these relationships automatically after fetching:

```yaml
cross_references:
  tags:
    from: posts              # source collection to search
    via: "tags.slug"         # dot-path in source: post.tags[*].slug
    match_key: slug          # field to compare within array elements (default: slug)
  authors:
    from: posts
    via: "author.slug"       # scalar path after field_map promotes authors.0 → author
```

For each item in the target collection (e.g. a tag), Guss finds all items in the
source collection (e.g. posts) where the `via` path matches the target's slug.
The results are injected as a root-level template variable named after the source
collection (e.g. `posts`).

### Markdown Adapter (`type: markdown`)

```yaml
source:
  type: markdown
  content_path: "./content/posts"
```

- Reads all `.md` files recursively
- Parses YAML frontmatter into item fields
- Renders body with md4c (CommonMark + GFM extensions: tables, strikethrough, autolinks)
- Falls back to file mtime for `published_at` when not in frontmatter
- Parallel processing via OpenMP

---

## 📄 Collections

Collections are the bridge between fetched content and rendered pages.
Every collection key must match an endpoint key (for REST) or be configured
for the Markdown adapter.

```yaml
collections:
  posts:
    item_template:    "post.html"     # template for individual item pages
    archive_template: "index.html"    # template for the listing/archive page
    permalink:        "/{year}/{month}/{slug}/"
    paginate:         10              # items per archive page (0 = no pagination)
    context_key:      "post"          # variable name in templates: {{ post.title }}

  tags:
    item_template: "tag.html"         # one page per tag
    permalink:     "/tag/{slug}/"
    context_key:   "tag"             # {{ tag.name }}, {{ tag.description }}

  authors:
    item_template: "author.html"
    permalink:     "/author/{slug}/"
    context_key:   "author"
```

**`item_template`** — renders one page per item (post, tag, author, page).

**`archive_template`** — renders a listing page grouping all items in the collection.
Only generated when both `item_template` and `archive_template` are set.
Taxonomy collections (tags, authors) set only `item_template` — they get
per-item pages, not an aggregate listing.

**`context_key`** — the variable name under which the item is exposed in the template.
Set explicitly; Guss never guesses based on the collection name.

**`permalink`** — supports any token from the item's data fields:
`{slug}`, `{year}`, `{month}`, `{day}`, or any custom frontmatter field.

**`paginate`** — archive pages are split into chunks of this size.
Pagination context is injected automatically:

```html
{% if pagination.has_prev %}<a href="{{ pagination.prev_url }}">← Prev</a>{% endif %}
Page {{ pagination.current }} of {{ pagination.total }}
{% if pagination.has_next %}<a href="{{ pagination.next_url }}">Next →</a>{% endif %}
```

---

## 🎨 Template Engine

Guss ships a custom bytecode template engine. No inja, no nlohmann/json,
no external template library in the render path. Source templates are compiled
once at startup, cached, and executed by a tight bytecode loop on every render.

```
Source template (.html)
     │
     ▼
  Lexer          → token stream
     │
     ▼
  Parser         → AST
     │
     ▼
  Compiler       → flat bytecode (Instruction vector + interned tables)
     │            stack depth verified at compile time — zero overhead at runtime
     ▼
  Runtime        → rendered string  (parallel, lock-free)
```

The compiler performs a single AST pass and emits a flat `Instruction` stream.
Control-flow instructions carry pre-patched relative offsets so the executor never
searches for jump targets. String data, variable paths, filter names, and literal
constants are interned into parallel tables indexed by operand. The hot loop
touches only integers. After compilation, `Compiler::verify_stack_depths()` simulates
the value stack and loop stack statically; if either would overflow at runtime,
load returns an error. No overflow checks are needed during execution.

### Supported syntax

```html
{# This is a comment #}

{{ variable }}
{{ object.field }}
{{ array.0 }}
{{ value | filter }}
{{ value | filter("arg") }}

{% if condition %}…{% elif other %}…{% else %}…{% endif %}
{% for item in collection %}…{% endfor %}
{% for item in collection reversed %}…{% endfor %}

{% extends "base.html" %}
{% block content %}…{% endblock %}
{% block content %}{{ super() }}…{% endblock %}
{% include "partial.html" %}

{% set x = value %}
```

### Expressions and operators

Templates support full expression syntax inside `{{ }}` and `{% if %}`:

```html
{{ post.title | upper }}
{{ post.reading_minutes | default(1) }}
{% if post.featured and not post.draft %}…{% endif %}
{% if loop.index == 1 or loop.last %}…{% endif %}
{{ items | length > 0 }}
```

Supported binary operators: `==`, `!=`, `<`, `>`, `<=`, `>=`, `+`, `-`, `*`, `/`, `%`, `and`, `or`

Supported unary operators: `not`

### Loop variables

Inside any `{% for %}` block, a `loop` object is automatically available:

| Variable          | Value                                    |
|-------------------|------------------------------------------|
| `loop.index`      | 1-based iteration counter                |
| `loop.index0`     | 0-based iteration counter                |
| `loop.first`      | `true` on the first iteration            |
| `loop.last`       | `true` on the last iteration             |
| `loop.revindex`   | 1-based counter from the end             |
| `loop.revindex0`  | 0-based counter from the end             |

### Template context

Every template receives:

| Variable             | Type   | Source                                       |
|----------------------|--------|----------------------------------------------|
| `site`               | object | `guss.yaml` site section                     |
| `{{ context_key }}`  | object | the item being rendered (e.g. `post`, `tag`) |
| `item`               | object | alias for the above (always available)       |
| `posts` / `tags` / … | array  | cross-reference results (taxonomy pages)     |
| `pagination`         | object | archive pages only                           |

Archive pages additionally receive the full collection array under the
collection name (e.g. `posts` for the posts archive).

### Built-in filters

| Filter            | Example                                       | Result                                      |
|-------------------|-----------------------------------------------|---------------------------------------------|
| `upper`           | `{{ title \| upper }}`                        | `MY POST`                                   |
| `lower`           | `{{ title \| lower }}`                        | `my post`                                   |
| `capitalize`      | `{{ title \| capitalize }}`                   | first char upper, rest lower                |
| `escape`          | `{{ text \| escape }}`                        | HTML-escaped string                         |
| `safe`            | `{{ html \| safe }}`                          | raw HTML (no escaping)                      |
| `length`          | `{{ posts \| length }}`                       | item count (array/object) or codepoints (string) |
| `date`            | `{{ published_at \| date("%d.%m.%Y") }}`      | `15.03.2024`                                |
| `truncate`        | `{{ excerpt \| truncate(120) }}`              | max 120 UTF-8 codepoints + ellipsis         |
| `default`         | `{{ bio \| default("No bio") }}`              | fallback when null or falsy                 |
| `slugify`         | `{{ title \| slugify }}`                      | `my-post-title`                             |
| `join`            | `{{ tags \| join(", ") }}`                    | `tech, go, c++`                             |
| `first`           | `{{ tags \| first }}`                         | first element                               |
| `last`            | `{{ tags \| last }}`                          | last element                                |
| `reverse`         | `{{ items \| reverse }}`                      | reversed string or array                    |
| `sort`            | `{{ items \| sort }}`                         | array sorted ascending                      |
| `striptags`       | `{{ html \| striptags }}`                     | plain text, HTML tags removed               |
| `urlencode`       | `{{ url \| urlencode }}`                      | percent-encoded string (RFC 3986)           |
| `replace`         | `{{ text \| replace("a", "b") }}`             | all occurrences replaced                    |
| `trim`            | `{{ text \| trim }}`                          | leading/trailing whitespace removed         |
| `abs`             | `{{ value \| abs }}`                          | absolute value (int or float)               |
| `round`           | `{{ value \| round(2) }}`                     | rounded to N decimal places                 |
| `float`           | `{{ value \| float }}`                        | convert to double                           |
| `int`             | `{{ value \| int }}`                          | convert to integer (truncates)              |
| `wordcount`       | `{{ content \| wordcount }}`                  | whitespace-separated word count             |
| `reading_minutes` | `{{ content \| reading_minutes }}`            | estimated read time in minutes              |
| `items`           | `{% for pair in obj \| items %}`              | object → array of `[key, value]` pairs      |
| `dictsort`        | `{% for pair in obj \| dictsort %}`           | object → `[key, value]` pairs sorted by key |

`reading_minutes` strips HTML tags, counts words, and divides by 256 wpm (configurable:
`{{ content | reading_minutes(300) }}`). Minimum is 1 minute.

---

## 🔧 Stack

| What         | Choice         | Why                                                |
|--------------|----------------|----------------------------------------------------|
| Language     | C++23          | Because we RESPECT the machine                     |
| JSON parsing | simdjson       | SIMD-accelerated, gigabytes/second (adapters only) |
| Templates    | `guss::render` | Custom bytecode compiler — one pass, linear scan   |
| Markdown     | md4c           | CommonMark + GFM extensions, C-fast                            |
| HTTP client  | cpp-httplib    | Header-only, OpenSSL                               |
| CLI          | CLI11          | Header-only, elegant                               |
| Logging      | spdlog         | Console + syslog, sub-nanosecond                   |
| Progress     | indicators     | Because builds should look GOOD                    |
| File watch   | efsw           | Cross-platform, lightweight                        |
| Parallelism  | OpenMP         | One pragma, all cores                              |
| Build system | CMake + CPM    | Zero external tooling                              |
| Tests        | Google Test    | 448 tests, zero failures                           |

---

## 📋 Configuration Reference

Full `guss.yaml` with all options:

```yaml
# ─────────────────────────────────────────────────────────────
# SITE METADATA
# Populates {{ site.* }} in every template.
# ─────────────────────────────────────────────────────────────
site:
  title: "My Site"
  description: "A site built with Guss"
  url: "https://example.com"
  language: "en"


# ─────────────────────────────────────────────────────────────
# CONTENT SOURCE
# type: rest_api  →  any HTTP JSON API (Ghost, WordPress, …)
# type: markdown  →  local .md files
# ─────────────────────────────────────────────────────────────
source:
  type: rest_api
  base_url: "https://your-cms.example.com/"
  timeout_ms: 30000

  # ── Authentication ──────────────────────────────────────────
  auth:
    type: api_key          # none | api_key | basic | bearer
    param: key             # query param name for api_key auth
    value: "your-api-key"

  # ── Pagination ──────────────────────────────────────────────
  pagination:
    page_param:  page
    limit_param: limit
    limit: 15
    json_next: "meta.pagination.next"      # Ghost-style
    # total_pages_header: "X-WP-TotalPages"  # WordPress-style

  # ── Endpoints ───────────────────────────────────────────────
  # Keys must match collections: keys below.
  endpoints:
    posts:
      path: "ghost/api/content/posts/"
      response_key: "posts"        # key holding the items array in the response
      params:
        include: "authors,tags"    # fixed extra query parameters

    pages:
      path: "ghost/api/content/pages/"
      response_key: "pages"
      params:
        include: "authors"

    authors:
      path: "ghost/api/content/authors/"
      response_key: "authors"

    tags:
      path: "ghost/api/content/tags/"
      response_key: "tags"

  # ── Field mappings ──────────────────────────────────────────
  # target_field: "source.dot.path"
  # Applied before enrichment; templates always see target names.
  field_maps:
    posts:
      content: "html"              # rename "html" → "content"
      author:  "authors.0"        # promote first author to singular "author"
    pages:
      content: "html"
      author:  "authors.0"

  # ── Cross-references ────────────────────────────────────────
  # Injects a list of related items into taxonomy pages.
  cross_references:
    tags:
      from: posts
      via: "tags.slug"            # project slug from every element of tags array
    authors:
      from: posts
      via: "author.slug"          # scalar path (after field_map)


# ─────────────────────────────────────────────────────────────
# COLLECTIONS
# ─────────────────────────────────────────────────────────────
collections:
  posts:
    item_template:    "post.html"
    archive_template: "index.html"
    permalink:        "/{year}/{month}/{slug}/"
    paginate:         10
    context_key:      "post"

  pages:
    item_template: "page.html"
    permalink:     "/{slug}/"
    context_key:   "page"

  tags:
    item_template: "tag.html"
    permalink:     "/tag/{slug}/"
    context_key:   "tag"

  authors:
    item_template: "author.html"
    permalink:     "/author/{slug}/"
    context_key:   "author"


# ─────────────────────────────────────────────────────────────
# OUTPUT
# ─────────────────────────────────────────────────────────────
output:
  output_dir:       "./dist"
  generate_sitemap: true
  generate_rss:     true
  copy_assets:      true
  robots_txt:       
    sitemap_url: "https://example.com/sitemap.xml"
    agents:
      - name: "*"
        allow_paths:
          - "/"
        disallow_paths:
          - "/admin/"
        Crawl_delay: 5

# ─────────────────────────────────────────────────────────────
# BUILD
# ─────────────────────────────────────────────────────────────
parallel_workers: 0      # 0 = auto-detect (uses all available cores)
log_level: "info"        # debug | info | warn | error
```

### WordPress example

```yaml
source:
  type: rest_api
  base_url: "https://your-wordpress-site.com/"

  auth:
    type: basic
    username: "editor"
    password: "xxxx xxxx xxxx xxxx xxxx xxxx"   # application password

  pagination:
    page_param:  page
    limit_param: per_page
    limit: 100
    total_pages_header: "X-WP-TotalPages"

  endpoints:
    posts:
      path: "wp-json/wp/v2/posts"
      response_key: ""       # WordPress returns a root array
      params:
        _embed: "1"

  field_maps:
    posts:
      published_at: "date"
      content:      "content.rendered"
      excerpt:      "excerpt.rendered"
      author:       "_embedded.author.0"
      feature_image: "_embedded.wp:featuredmedia.0.source_url"
```

### Markdown example

```yaml
source:
  type: markdown
  content_path: "./content/posts"

collections:
  posts:
    item_template: "post.html"
    archive_template: "index.html"
    permalink: "/{year}/{month}/{slug}/"
    paginate: 10
    context_key: "post"
```

Frontmatter fields become template variables directly:

```markdown
---
title: My Post
slug: my-post
published_at: 2024-03-15T10:00:00Z
tags: [tech, golang]
author: Jane
---

Post body here...
```

---

## 📜 License

MIT — Because even SUPREME PERFORMANCE believes in generosity.
