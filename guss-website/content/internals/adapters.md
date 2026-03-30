---
slug: internals/adapters
title: Adapters
---

Adapters implement the `ContentAdapter` base class and are responsible for all data
acquisition. They convert raw source data into `Value` objects the pipeline can render.

## ContentAdapter (base class)

```cpp
class ContentAdapter {
public:
    ContentAdapter(core::config::SiteConfig site_cfg,
                   core::config::CollectionCfgMap collections);

    virtual ~ContentAdapter() = default;

    virtual core::error::Result<FetchResult> fetch_all(FetchCallback progress = nullptr) = 0;
    virtual core::error::VoidResult ping() = 0;
    virtual std::string adapter_name() const = 0;

protected:
    core::config::SiteConfig       site_cfg_;
    core::config::CollectionCfgMap collections_;

    core::Value build_site_value() const;

    static core::Value resolve_path(const core::Value& v, std::string_view path);

    static void apply_field_map(
        core::Value& item,
        const std::unordered_map<std::string, std::string>& field_map);

    void enrich_item(core::Value& item, const std::string& collection_name) const;

    static core::RenderItem build_render_item(
        const core::Value& v,
        const core::config::CollectionConfig& coll_cfg);

    void apply_cross_references(
        FetchResult& result,
        const core::config::CrossRefCfgMap& cross_refs) const;

    static void apply_prev_next(FetchResult& result, const std::string& collection_name);
};
```

**Protected helpers available to all adapters:**

- `build_site_value()` — converts `SiteConfig` to a `Value` for `FetchResult::site`.
- `resolve_path(value, "dot.path")` — navigates a `Value` by dot-path. Numeric
  segments are array indices. Array-of-object segments project a field across all
  elements: `"tags.slug"` → array of slug strings.
- `apply_field_map(item, field_map)` — renames or projects fields in-place.
  Entries are `target_field: "source.dot.path"`.
- `enrich_item(item, collection_name)` — extracts `year`/`month`/`day` from
  `published_at`, computes `permalink` and `output_path` from the collection's
  permalink pattern.
- `build_render_item(value, coll_cfg)` — builds a `RenderItem` from a `Value` and
  its collection config.
- `apply_cross_references()` — for each cross-ref config, looks up the referenced
  collection and injects the resolved items into the source collection's items as
  `item[<cross_ref_key>]`.
- `apply_prev_next()` — for a collection with items sorted by `published_at`,
  injects `prev_item` and `next_item` into each item.

## MarkdownAdapter

Reads Markdown files from one or more directories. Each file becomes one `RenderItem`.

```yaml
source:
  type: markdown
  recursive: true
  collection_paths:
    posts: "content/posts"
    pages: "content/pages"
    tags:  "content/tags"
```

**File processing:**

1. YAML frontmatter (between `---` delimiters) is parsed into the item's `Value`.
2. The Markdown body is converted to HTML by md4c (GFM: tables, strikethrough,
   autolinks) and stored as `item["html"]`.
3. If no `slug` key is in frontmatter, it falls back to the file stem. An explicit
   `slug: ""` is preserved (enabling root-level output via `/{slug}/` → `index.html`).
4. `published_at` falls back to the file creation time (birth time on macOS/Linux via
   `statx`, mtime fallback on older kernels).
5. `custom_template` in frontmatter overrides the collection's `item_template`.
6. `field_maps` are applied before `enrich_item()`, same as the REST adapter.
7. Taxonomy synthesis: if a collection has no files of its own but appears as a
   `cross_references` target, Guss synthesises its items from data embedded in the
   source collection. Tags embedded in posts become a full `tags` collection.
8. `apply_prev_next()` is called for the `"posts"` collection to inject
   `prev_item`/`next_item` on each post.

Files are parsed in parallel via OpenMP across collection directories.

## RestCmsAdapter

Fetches collections from any REST CMS (Ghost, WordPress, Contentful, or any JSON API).

```yaml
source:
  type: rest_api
  base_url: "https://cms.example.com/api/v2"
  auth:
    type: api_key
    header: "X-Ghost-Key"
    key: "your-content-api-key"
  endpoints:
    posts:
      path: "/posts"
      response_key: "posts"
      params:
        include: "tags,authors"
    tags:
      path: "/tags"
      response_key: "tags"
```

All collections are fetched in parallel via OpenMP. The adapter applies `field_maps`,
calls `enrich_item()` per item, builds cross-references, and injects `prev_item`/
`next_item` into the `"posts"` collection.

**Pagination strategies — evaluated in priority order:**

| Priority | Strategy               | Config field                          |
|----------|------------------------|---------------------------------------|
| 1        | Total pages header     | `total_pages_header: "X-Total-Pages"` |
| 2        | Total count header     | `total_count_header: "X-Total-Count"` |
| 3        | Link header (RFC 5988) | `link_header: true`                   |
| 4        | JSON cursor            | `json_cursor: "meta.next_cursor"`     |
| 5        | JSON next URL          | `json_next_url: "meta.next_page_url"` |
| 6        | JSON next sentinel     | `json_next: "meta.pagination.next"`   |
| 7        | Optimistic fetching    | `optimistic_fetching: true`           |
| 8        | Single page            | *(none set)*                          |

The adapter selects the first matching strategy from the config and runs it for the
lifetime of that endpoint's fetch. Strategies 3–6 are per-request (the next URL or
token is extracted from each response). Strategies 1–2 determine the total upfront
from the first response's headers.

**Field maps** rename or project fields from the API response before enrichment:

```yaml
source:
  field_maps:
    posts:
      body: "html"                  # rename "html" → "body"
      author_name: "authors.0.name" # project nested value
      tag_slugs: "tags.slug"        # project array field
```

**Cross-references** inject related collection items:

```yaml
source:
  cross_references:
    tags:
      from: "posts"
      via: "tags.slug"
      match_key: "slug"
```

This injects matching `tag` objects from the `tags` collection into each `post` item's
`tags` array, replacing the raw embedded tag data with the full fetched tag objects.

**Item-level template override:** if an item's `Value` contains a `custom_template`
field, that template is used instead of the collection's `item_template`.

**`optimistic_fetching`** stops silently on HTTP 404, an empty response, or a parse
error — no error is propagated. This allows safe use against APIs that return 404
instead of an empty last page.
