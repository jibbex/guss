---
slug: internals/value-system
title: Value System
---

`guss::core::Value` is a discriminated union of native C++ types. It exists because
passing simdjson objects into the template runtime is not feasible — for five
independent reasons.

## Why not pass JSON directly?

### 1. simdjson On-demand is single-pass and destructive

Iterating an array consumes it. Template loops need re-traversal — a hard blocker.
A `{% for post in posts %}` loop that iterates 100 items and a sidebar that also
references `posts` would require two passes over the same array. With simdjson
On-demand that is impossible.

### 2. simdjson values are buffer views

Every simdjson value is a pointer into the original input buffer. The buffer must
outlive every access to its values. In an OpenMP parallel build with hundreds of
threads rendering concurrently, managing that lifetime is a minefield. A freed
buffer produces a dangling pointer; the compiler cannot catch it.

### 3. simdjson is read-only

The adapter enriches every item with computed fields: `year`, `month`, `day`
(extracted from `published_at`), `permalink`, and `output_path`. None of these exist
in the CMS API response. simdjson DOM objects are read-only — new fields cannot be
inserted.

### 4. simdjson field lookup is O(n)

The template engine accesses many fields per page — title, slug, tags, author, date.
simdjson On-demand requires a linear scan for each lookup. `ValueMap` uses
`std::unordered_map` for O(1) access.

### 5. The boundary is architectural

`guss-render` has zero knowledge of simdjson. The dependency is intentionally
one-directional: `guss-adapters` depends on `guss-core`; `guss-render` depends on
`guss-core`; neither `guss-render` nor `guss-builder` has any simdjson dependency.
This boundary makes the render layer independently testable and keeps compile times
down.

## The variant

```cpp
using ValueVariant = std::variant<
    NullTag,
    std::string_view,   // TODO: to be removed
    std::string,        // owned string (filter output, enriched fields)
    bool,
    int64_t,
    uint64_t,
    double,
    std::shared_ptr<ValueMap>,    // key→Value object
    std::shared_ptr<ValueArray>   // indexed Value array
>;
```



> **Deprecated:** [std::string_view variant](/how-guss-works/#the-value-type) — Why it will be removed.

The adapter converts `simdjson → Value` exactly once per item at fetch time. After
that boundary, simdjson does not exist anywhere in the codebase.

## `shared_ptr` — O(1) copies

Maps and arrays are heap-allocated through `shared_ptr`. Copying a `Value` that holds
a map or array is a reference count increment — O(1), zero data copied.

This matters for archive pages. A paginated blog archive has one page per N posts.
Each archive page's `extra_context` holds the `posts` array for that page. With
`shared_ptr`, those arrays share the underlying `ValueArray` allocation. Copying zero
bytes of post data, regardless of how many posts exist.

## `unordered_map` vs `flat_map`

A blog post is a `Value` holding a `ValueMap` — a map from field names to values.
Which data structure should that map use?

| Criterion         | `unordered_map`        | `flat_map` (C++23)            |
|-------------------|------------------------|-------------------------------|
| Lookup complexity | O(1) amortised         | O(log n)                      |
| Cache behaviour   | Poor (pointer chasing) | Excellent (contiguous arrays) |
| Speed at n < ~50  | Slower in practice     | Faster in practice            |
| Insert complexity | O(1) amortised         | O(n) — shifts sorted vectors  |
| Memory overhead   | High (node + bucket)   | Minimal                       |

A typical blog post has 10–30 fields. At that scale `flat_map` would win on lookups
because all keys fit in two cache lines. However, `ValueMap` uses `unordered_map`
for one reason: **enrichment is user-configurable**. `field_maps`, computed fields
(`year`, `month`, `permalink`), and cross-reference injection all perform
post-construction insertions. The number of insertions is unknown at construction
time. Every insertion into a `flat_map` is O(n) because the sorted backing vectors
must shift. The batch-construct optimisation that makes `flat_map` worthwhile requires
knowing all keys upfront — not possible here.

## `Context` uses `pmr::unordered_map`

`Context::locals_` (the per-scope variable binding map) uses `std::pmr::unordered_map`
with a `monotonic_buffer_resource` backed by an 8 KiB stack buffer:

```cpp
std::byte buf[8192];
std::pmr::monotonic_buffer_resource arena(buf, sizeof(buf));
Context ctx(&arena);
```

This eliminates heap allocation for variable bindings in the common case. A
`monotonic_buffer_resource` allocates by bumping a pointer — faster than `malloc`.

Why not `pmr::flat_map`? Because `flat_map` backs itself with two `pmr::vector`s
that grow. With a monotonic allocator, growth allocates a new block and abandons the
old one (monotonic cannot free). The abandoned blocks waste arena space. `unordered_map`
nodes are fixed-size and pack cleanly into the arena without waste.

## Dotted-path resolution

The template variable `post.author.name` is resolved by navigating the Value hierarchy
one segment at a time:

```
"post.author.name"
    ↓ find '.'
ctx.resolve("post")         → Value (object)
    ↓ next segment: "author"
value["author"]             → Value (object)
    ↓ next segment: "name"
value["name"]               → Value ("Alice")
```

Each step is a `string_view::find('.')` on the remaining path — no split, no heap
allocation. Numeric segments are array indices: `"tags.0.slug"` navigates into the
first tag's slug field. Array segments project across all elements: `"tags.slug"`
returns an array of all slug strings.

## Hot path rules

Inside `Runtime::execute()`:

- The value stack is a fixed C-array of 64 `Value` slots on the stack frame.
  Stack depth is verified at compile time — no overflow check needed in the loop.
- Copying a leaf `Value` (string, int, bool) is a `std::variant` copy — cheap.
- Copying a compound `Value` (`ValueMap`, `ValueArray`) is a `shared_ptr` reference
  count bump — O(1).
- `string_view` Values are never stored in the template context (their lifetime is
  adapter-internal). The template engine always receives owned `string` or `shared_ptr`
  compound values.
