---
slug: docs/configuration
title: Configuration
---

Guss is configured by a single `guss.yaml` file in the project root.

## Minimal example

```yaml
site:
  title: "My Site"
  description: "A site built with Guss"
  url: "https://example.com"
  language: "en"

source:
  type: rest_api
  base_url: "https://cms.example.com"

output:
  output_dir: "dist"
  copy_assets: true

collections:
  posts:
    item_template: "post.html"
    archive_template: "index.html"
    permalink: "/{year}/{month}/{slug}/"
    paginate: 10
    context_key: "post"
```

## `site`

| Field | Type | Description |
|---|---|---|
| `title` | string | Site title — available as `{{ site.title }}` in all templates |
| `description` | string | Short description — `{{ site.description }}` |
| `url` | string | Canonical base URL, no trailing slash |
| `language` | string | BCP 47 language code, e.g. `en` |
| `logo` | string | Optional logo URL |
| `icon` | string | Optional favicon URL |
| `twitter` | string | Optional Twitter handle |
| `facebook` | string | Optional Facebook page URL |
| `navigation` | map | Named nav groups — `{{ site.navigation.<group> }}` |

## `source`

Set `type: rest_api` for REST CMS sources or `type: markdown` for local Markdown files.

### Markdown source

```yaml
source:
  type: markdown
  recursive: true
  collection_paths:
    posts: "content/posts"
    pages: "content/pages"
```

Each key in `collection_paths` maps a collection name to a directory. All `.md` files
in that directory become items in that collection.

### REST API source

```yaml
source:
  type: rest_api
  base_url: "https://cms.example.com/api/v2"
  timeout_ms: 30000
  auth:
    type: api_key
    header: "X-API-Key"
    key: "your-key-here"
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

### Auth types

| Type | Fields | Description |
|---|---|---|
| `none` | — | No authentication |
| `api_key` | `header`, `key` | Sends `header: key` as a request header |
| `basic` | `username`, `password` | HTTP Basic Authentication |
| `bearer` | `value` | Sends `Authorization: Bearer <value>` |

### `field_maps`

Rename or project fields before enrichment. Works for both Markdown and REST adapters.

```yaml
source:
  field_maps:
    posts:
      body: "html"                  # rename source field "html" → "body"
      author_name: "authors.0.name" # project nested value into new field
      tag_slugs: "tags.slug"        # project array field across all elements
```

Entries are `target_field: "source.dot.path"`. Numeric path segments index into arrays;
array-of-object segments project a sub-field across every element.

### `cross_references`

Inject related items across collections, or synthesize a taxonomy collection from data
embedded in another collection (e.g. build a `tags` collection from the tags embedded in
each post):

```yaml
source:
  cross_references:
    tags:
      from: "posts"       # source collection
      via: "tags.slug"    # dot-path to the linking field on each source item
      match_key: "slug"   # field within each linked element to match against
```

With this config, each `post` item gets its `tags` array resolved to full `tag` objects.
If `tags` has no files of its own, Guss synthesizes the collection from the embedded data.

## `output`

| Field | Type | Default | Description |
|---|---|---|---|
| `output_dir` | string | `dist` | Output directory path |
| `copy_assets` | bool | `true` | Copy `templates/assets/` to `dist/assets/` |
| `generate_sitemap` | bool | `true` | Write `dist/sitemap.xml` |
| `generate_rss` | bool | `true` | Write `dist/feed.xml` (RSS 2.0) |
| `minify_html` | bool | `false` | Minify all output HTML |

## `collections`

Each key must match a collection name in your source config.

| Field | Type | Default | Description |
|---|---|---|---|
| `item_template` | string | — | Template for individual item pages. Empty = no item pages. |
| `archive_template` | string | — | Template for archive/listing pages. Empty = no archive. |
| `permalink` | string | — | Pattern with `{token}` placeholders. |
| `paginate` | int | `0` | Items per archive page. `0` = single archive page. |
| `context_key` | string | `item` | Template variable name for the item. |

**Archive pages** are only generated when both `item_template` and `archive_template` are set.

**Permalink tokens** are plain field lookups on the item's Value:

| Token | Source |
|---|---|
| `{slug}` | Item's `slug` field |
| `{year}` | Extracted from `published_at` by the adapter |
| `{month}` | Extracted from `published_at` by the adapter |
| `{day}` | Extracted from `published_at` by the adapter |
| `{<field>}` | Any other field on the item |

Example: `permalink: "/{year}/{month}/{slug}/"` + `slug: hello-world`, `year: 2024`,
`month: 01` → writes to `dist/2024/01/hello-world/index.html`.

## Pagination strategies

When using a REST API source, Guss supports eight pagination strategies evaluated
in priority order. Set the field(s) that match your CMS's API:

| Priority | Config field | Behavior |
|---|---|---|
| 1 | `total_pages_header: "X-Total-Pages"` | Read header on first response — total pages known upfront |
| 2 | `total_count_header: "X-Total-Count"` | Read header → derive pages via `ceil(count / limit)` |
| 3 | `link_header: true` | Follow verbatim `Link: rel="next"` URL each round-trip (RFC 5988) |
| 4 | `json_cursor: "meta.next_cursor"` | Extract cursor from body, append as `?<cursor_param>=<token>` |
| 5 | `json_next_url: "meta.next_page_url"` | Extract full next-page URL from body, follow verbatim |
| 6 | `json_next: "meta.pagination.next"` | Dot-path non-null sentinel; increment page counter (Ghost-style) |
| 7 | `optimistic_fetching: true` | Blind GET N+1 until 404, empty body, or parse error |
| 8 | *(none set)* | Single page fetch |

Configure globally or override per endpoint:

```yaml
source:
  pagination:
    page_param: "page"
    limit_param: "limit"
    limit: 15
    json_next: "meta.pagination.next"
  endpoints:
    posts:
      path: "/posts"
      response_key: "posts"
      pagination:                      # overrides global for this endpoint only
        total_pages_header: "X-Pages"
```

Additional pagination fields:

| Field | Description |
|---|---|
| `cursor_param` | Query param name for cursor-based pagination (default: `"cursor"`) |
| `offset_param` | Query param name for item offset, sent alongside `page_param` when needed |

## `parallel_workers`

```yaml
parallel_workers: 0   # 0 = auto-detect (all CPU cores via OpenMP)
```

## `log_level`

```yaml
log_level: "info"   # debug | info | warn | error
```