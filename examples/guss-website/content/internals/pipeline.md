---
slug: internals/pipeline
title: Build Pipeline
---

The build pipeline has four phases. Each phase has a single responsibility and a defined
contract with the next.

## Phase 1 - Fetch

The configured `ContentAdapter` is asked for all content via `fetch_all()`. It returns a
`FetchResult`:

```cpp
struct FetchResult {
    CollectionMap items;  // collection_name → vector<RenderItem>
    Value         site;   // site metadata (title, description, url, …)
};
```

`CollectionMap` is `unordered_map<string, vector<RenderItem>>`. The adapter is responsible for all network I/O, pagination,
field mapping, and type conversion. After this phase, no adapter-specific types exist in the pipeline. Only `Value`.

## Phase 2 - Prepare

Iterates `FetchResult.items` to build a flat `vector<RenderItem>` for the render phase.

**Item pages** are already fully prepared by the adapter (non-empty `output_path` and `template_name`).
They are passed through as-is.

<!-- TODO: VERIFY! -->
**Archive pages** are generated here for any collection that has both `item_template` and `archive_template` set in the config:

- If `paginate > 0`: page 1 at the base URL, pages 2..N at `<base>/page/N/index.html`
- If `paginate == 0`: a single archive at `archive_url_prefix(permalink)`

Archive items carry `extra_context` with the collection array, `tags` array (if present), and a `pagination` map.

## Phase 3 - Render

OpenMP parallel loop over all `RenderItems`. Per thread:

1. Stack-allocates an 8 KiB arena for a `pmr::monotonic_buffer_resource`
2. Constructs a `Context` using that arena (zero heap for variable bindings)
3. Sets `site`, `item`, `<context_key>,` and all `extra_context` variables
4. Calls `engine.render(template_name, ctx)` → `Result<string>`

Render errors are counted but do not abort the build. Other pages continue rendering.

The `Runtime` is constructed once per build. Template loading happens before the OMP loop (not thread-safe); execution
inside the loop is read-safe.

## Phase 4 - Write

Sequential write of all rendered files to `output_dir`. Parent directories are created as needed. Entries with empty
`output_path` are skipped.

After all HTML files are written:

- `templates/assets/` is copied to `dist/assets/` if `copy_assets: true`
- `sitemap.xml` is written if `generate_sitemap: true` (the default)
- `feed.xml` (RSS 2.0) is written if `generate_rss: true` (the default)

## Build Stats

The pipeline returns a `BuildStats` struct on success:

```cpp
struct BuildStats {
    size_t items_rendered;
    size_t archives_rendered;
    size_t assets_copied;
    size_t files_minified;
    size_t extras_generated;   // sitemap.xml, feed.xml
    size_t errors;
    std::chrono::milliseconds fetch_duration;
    std::chrono::milliseconds prepare_duration;
    std::chrono::milliseconds render_duration;
    std::chrono::milliseconds write_duration;
    std::chrono::milliseconds total_duration;
};
```
