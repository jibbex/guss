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
    ContentAdapter(core::config::SiteConfig site_cfg, core::config::CollectionCfgMap collections)
        : site_cfg_(std::move(site_cfg))
        , collections_(std::move(collections)) {}

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

Protected helpers available to all adapters:

- `build_site_value()`: converts `SiteConfig` to a `Value` for `FetchResult::site`.
- `resolve_path(value, "dot.path")`: navigates a `Value` by dot-path. Numeric segments are array indices. Array segments project a field across all elements; `"tags.slug"` → array of slug strings.
- `apply_field_map(item, field_map)`: renames or projects fields in-place. `field_map` entries are target; `"source.dot.path"` YAML.
- `enrich_item(item, collection_name)`: extracts `year`/`month`/`day` from `published_at`, computes `permalink` and `output_path` from the collection's permalink pattern.
- `build_render_item(value, coll_cfg)`: builds a `RenderItem` from a `Value` and its collection config. Applies the collection's `item_template` and `permalink` pattern.
- `apply_cross_references()`: for each cross-ref config, looks up the referenced collection and injects the resolved items into the source collection's items as `item[<cross_ref_key>]`.
- `apply_prev_next()`: for a collection with `prev_next: true`, injects `prev_item` and `next_item` into each item based on the sorted order of `published_at`.

## MarkdownAdapter

Reads Markdown files from one or more directories. Each file becomes one `RenderItem`.

```yaml
source:
  type: markdown
  recursive: true
  collection_paths:
    posts: "content/posts"
    pages: "content/pages"
```

**File processing:**

1. YAML frontmatter (between `---` delimiters) is parsed into the item's `Value`.
2. The body is converted to HTML by md4c and stored as `item["html"]`.
3. If no `slug` key is in the frontmatter, it falls back to the file stem. An explicit `slug: ""` is preserved (enabling root-level output via `/{slug}/` → `index.html`).
4. `custom_template` in frontmatter overrides the collection's `item_template`.
5. `permalink` and `output_path` are computed inline (the `MarkdownAdapter` does not delegate to the base class `enrich_item()` - enrichment logic lives directly in `process_file()`).

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

All collections are fetched in parallel via OpenMP. Eight pagination strategies are supported; the adapter selects the
appropriate one based on the response headers and body shape.

**Field maps** rename or project fields from the API response:

```yaml
source:
  field_maps:
    posts:
      body: "html"                    # rename "html" field to "body"
      author_name: "authors.0.name"   # project nested value
```

This injects the matching _tags_ items directly into each _post_ `Value`.
