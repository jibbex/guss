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
  # ... adapter-specific config

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

| Field         | Type	  | Description                                                     |
|---------------|--------|:----------------------------------------------------------------|
| `title` 	     | string | Site title — available as `{{ site.title }}` in templates       |
| `description` | string | Short description — `{{ site.description }}`                    |
| `url`	        | string | Canonical base URL, no trailing slash — used in sitemap and RSS |
| `language`    | string | BCP 47 language code, e.g. en                                   |

## `source`

Set `type: rest_api` for REST CMS sources or `type: markdown` for local Markdown files.

**Markdown source:**

```yaml
source:
  type: markdown
  recursive: true        # scan subdirectories
  collection_paths:
    posts: "content/posts"
    pages: "content/pages"
```

Each key in `collection_paths` maps a collection name to a directory path. Files are matched to collections by
directory.

**REST API source:**

```yaml
source:
  type: rest_api
  base_url: "https://cms.example.com/api"
  auth:
    type: api_key          # api_key | basic | bearer | none
    header: "X-API-Key"
    key: "your-key-here"
  endpoints:
    posts:
      path: "/posts"
      response_key: "posts"
    tags:
      path: "/tags"
      response_key: "tags"
```

## `output`

| Field              | Type   | Default | Description                                |
|--------------------|--------|:--------|:-------------------------------------------|
| `output_dir`       | string | `dist`  | Output directory path                      |
| `copy_assets`      | bool   | `true`  | Copy `templates/assets/` to `dist/assets/` |
| `generate_sitemap` | bool   | `true`  | Write `dist/sitemap.xml `                  |
| `generate_rss`     | bool   | `true`  | Write `dist/feed.xml` (RSS 2.0)            |
| `minify_html`      | bool   | `false` | Minify all output HTML                     |

## `collections`

Each key is a collection name that must match a collection name in your source config.

```yaml
collections:
  posts:
    item_template: "post.html"       # template for individual item pages
    archive_template: "index.html"      # template for archive/listing pages
    permalink: "/{year}/{month}/{slug}/"
    paginate: 10                # 0 = single archive page
    context_key: "post"            # template variable name: {{ post.title }}
```

`permalink` tokens are plain field lookups on the item Value. Available tokens depend on the adapter:

- `{slug}`: the item's slug field
- `{year}`, `{month}`, `{day}`: extracted from `published_at` by the adapter
- Any other field present on the item

<!-- TODO: verify this... most likely not true! -->
**Archive pages** are only generated when both item_template and archive_template are set.

## `parallel_workers`

```yaml
parallel_workers: 0   # 0 = auto-detect (uses all CPU cores via OpenMP)
```