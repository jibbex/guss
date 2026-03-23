---
slug: internals/pipeline
title: Build Pipeline
---

The build pipeline has four phases. Each phase has a single responsibility and a defined
contract with the next. Phases communicate through plain value types — no shared state,
no global objects.

## Core types

**`RenderItem`** — the unit of work the pipeline passes between phases:

```cpp
struct RenderItem {
    std::filesystem::path output_path;     // where to write the rendered HTML
    std::string           template_name;   // template filename to render with
    std::string           context_key;     // template variable name (e.g. "post")
    core::Value           data;            // all fields available in the template
    std::unordered_map<std::string, core::Value> extra_context; // archive extras
};
```

`data` holds everything the template can access via `{{ post.title }}`,
`{{ post.tags }}`, etc. `extra_context` carries archive-specific variables like the
`posts` array, `pagination` map, and `tags` array for listing pages.

**`CollectionMap`** — `unordered_map<string, vector<RenderItem>>`. The adapter fills
this; the pipeline flattens it into a single ordered vector for the render phase.

## Phase 1 — Fetch

Calls `adapter_->fetch_all()`. The adapter returns a `FetchResult`:

```cpp
struct FetchResult {
    CollectionMap items;  // collection_name → vector<RenderItem>
    Value         site;   // site metadata (title, description, url, …)
};
```

The adapter is responsible for all network I/O, pagination, field mapping, type
conversion, cross-reference injection, and `prev_item`/`next_item` linking. After this
phase, no adapter-specific types exist in the pipeline — only `Value` and `RenderItem`.

All REST API collections are fetched in parallel via OpenMP. Markdown files are also
parsed in parallel per collection directory.

## Phase 2 — Prepare

Iterates `FetchResult.items` to build a flat `vector<RenderItem>` for the render phase.

**Item pages** arrive pre-built from the adapter with non-empty `output_path` and
`template_name`. They are passed through as-is.

**Archive pages** are generated here for any collection that has both `item_template`
and `archive_template` set in the config:

- `paginate > 0`: page 1 at the base URL; pages 2..N at `<base>/page/N/index.html`.
  Each page's `extra_context` includes a `pagination` map with `current_page`,
  `total_pages`, `has_prev`, `has_next`, `prev_url`, `next_url`.
- `paginate == 0`: a single archive page. `extra_context` includes the full collection
  array and the `tags` array (if a `tags` collection exists).

Item pages are placed first in the vector; the `archive_count` boundary is tracked
separately in `BuildStats`.

## Phase 3 — Render

OpenMP parallel loop over all `RenderItems`. Per thread:

1. Stack-allocates an 8 KiB arena (`std::byte buf[8192]`) for a
   `pmr::monotonic_buffer_resource`.
2. Constructs a `Context` using that arena — zero heap for variable bindings.
3. Sets `site`, `item`, `<context_key>`, and all `extra_context` variables.
4. Calls `engine.render(template_name, ctx)` → `Result<string>`.

The `Runtime` is constructed once per build and its template cache is read-only during
the OMP loop — concurrent `render()` calls are safe. Template loading (compilation)
happens before the loop and is not thread-safe.

Render errors are counted but do not abort the build. Other pages continue rendering.

## Phase 4 — Write

Sequential write of all rendered strings to `output_dir`. Parent directories are
created as needed. Entries with an empty `output_path` (failed renders) are skipped.

After all HTML files are written:
- `templates/assets/` → `dist/assets/` if `copy_assets: true`
- `sitemap.xml` written if `generate_sitemap: true`
- `feed.xml` (RSS 2.0) written if `generate_rss: true`
- HTML minification applied if `minify_html: true`

## Build stats

The pipeline returns a `BuildStats` struct on success:

```cpp
struct BuildStats {
    size_t items_rendered;      // individual item pages written
    size_t archives_rendered;   // archive/listing pages written
    size_t assets_copied;       // asset files copied to dist/assets/
    size_t files_minified;      // HTML files minified
    size_t extras_generated;    // sitemap.xml + feed.xml
    size_t errors;              // render errors (pages skipped)
    std::chrono::milliseconds fetch_duration;
    std::chrono::milliseconds prepare_duration;
    std::chrono::milliseconds render_duration;
    std::chrono::milliseconds write_duration;
    std::chrono::milliseconds total_duration;
};
```

The CLI prints a summary line on completion:

```
✓ Build complete!  Items: 76  Archives: 4  Duration: 506ms
```
