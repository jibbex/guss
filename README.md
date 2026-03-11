# 🔥 Guss

> **I Am** Not A Static Site Generator. **I AM STATIC SITE GENERATION ITSELF!**

![Build](https://img.shields.io/badge/build-FORGED-brightgreen)
![Performance](https://img.shields.io/badge/threads-ALL%20OF%20THEM-blue)
![C++](https://img.shields.io/badge/C%2B%2B-23-red)

## 🌋 What Is This?

A pluggable static site generator written in C++23 that:

- **FORGES** pages from Ghost, WordPress, or Markdown at SIMD-accelerated speed
- **CASTS** them through a custom bytecode template engine with the precision of a German foundry
- **HARDENS** the output into static HTML that loads before your users even CLICK
- **WATCHES** your CMS for changes and rebuilds before you finish your coffee

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
[2026-03-11 02:32:46.713] [console] [info] Phase 3: Rendering templateshing content...
[2026-03-11 02:32:46.714] [console] [info] Phase 4: Writing 80 files
[==================================================] 100% [00m:00s] Writing files...
[2026-03-11 02:32:46.735] [console] [info] Build complete in 506ms (76 items, 4 archives)

[2026-03-11 02:32:46.735] [console] [info] Build complete!
[2026-03-11 02:32:46.735] [console] [info]   Items:    76
[2026-03-11 02:32:46.735] [console] [info]   Archives: 4
[2026-03-11 02:32:46.735] [console] [info]   Assets:   1
[2026-03-11 02:32:46.735] [console] [info]   Duration: 506ms
```

---

## 🛠 Prerequisites

- C++23 compiler (GCC 14+ or Clang 18+)
- CMake 3.25+
- OpenSSL (for HTTPS)

```bash
# Debian/Ubuntu
sudo apt install cmake g++-14 libssl-dev

# Configure and build (CPM downloads all other dependencies automatically)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

No Conan. No vcpkg. No Python. No Node. Just CMake and a compiler. Like C++ INTENDED.

---

## 🚀 Usage

```bash
# Initialize a new site
guss init --adapter ghost

# Test connectivity to your CMS
guss ping

# Build the site
guss build

# Serve with live rebuild
guss serve --port 4000
```

---

## 📐 Architecture

```
guss/
├── include/guss/
│   ├── core/        # Value, RenderItem, CollectionMap, Config, permalink
│   ├── adapters/    # ContentAdapter base class + RestCmsAdapter, MarkdownAdapter
│   ├── render/      # Custom bytecode template engine (Lexer→Parser→Compiler→VM)
│   ├── builder/     # Parallel build pipeline (OpenMP)
│   ├── server/      # HTTP server (cpp-httplib)
│   └── watch/       # Filesystem watcher (efsw)
├── src/
├── themes/          # Jinja2-style HTML templates
└── tests/           # Google Test suite (369 tests)
```

### Build Pipeline

```
┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐
│  FETCH  │───▶│ PREPARE │───▶│ RENDER  │───▶│  WRITE  │
└─────────┘    └─────────┘    └─────────┘    └─────────┘
   REST API      Permalink      OpenMP          Disk
   Markdown      Archive        parallel        Assets
   Pagination    pages          rendering
```

1. **Fetch** — Pull content from any REST API or local Markdown files. All data is
   converted to `Value` (variant type) immediately; no domain model structs anywhere.
2. **Prepare** — Build archive pages, resolve permalink patterns to output paths.
3. **Render** — Parallel template rendering via OpenMP. Each thread gets its own
   render `Context`. No locks, no contention.
4. **Write** — Write HTML to disk, copy theme assets.

---

## 🔌 Source Adapters

### Generic REST Adapter (`type: rest_api`)

Guss uses a single, fully configurable REST adapter that works with any HTTP CMS. 
No special cases for Ghost, WordPress, Contentful, or any JSON API. There are no
hardcoded Ghost or WordPress paths anywhere in the code. Everything is driven by 
`guss.yaml`.

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

Two strategies — configure whichever your API uses:

```yaml
pagination:
  page_param:  page    # query param for the page number
  limit_param: limit   # query param for page size
  limit: 15

  # Strategy 1 — JSON body sentinel (Ghost-style):
  # A non-null value at this dot-path means there is a next page.
  json_next: "meta.pagination.next"

  # Strategy 2 — HTTP response header (WordPress-style):
  # The header value is the total number of pages.
  # total_pages_header: "X-WP-TotalPages"
```

If neither is set, the endpoint is assumed to return a single page.
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
- Renders body with cmark (GitHub Flavored Markdown)
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
no external template library in the render path.

```
Source template
     │
     ▼
  Lexer          → token stream
     │
     ▼
  Parser         → AST
     │
     ▼
  Compiler       → flat bytecode (OpCode vector)
     │
     ▼
  Executor       → rendered string
```

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
{% include "partial.html" %}

{% set x = value %}
```

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

| Filter     | Example                                  | Result                 |
|------------|------------------------------------------|------------------------|
| `upper`    | `{{ title \| upper }}`                   | `MY POST`              |
| `lower`    | `{{ title \| lower }}`                   | `my post`              |
| `strip`    | `{{ text \| strip }}`                    | trimmed string         |
| `length`   | `{{ posts \| length }}`                  | item count             |
| `safe`     | `{{ html \| safe }}`                     | raw HTML (no escaping) |
| `date`     | `{{ published_at \| date("%d.%m.%Y") }}` | `15.03.2024`           |
| `truncate` | `{{ excerpt \| truncate(120) }}`         | max 120 chars          |
| `default`  | `{{ bio \| default("No bio") }}`         | fallback value         |
| `slugify`  | `{{ title \| slugify }}`                 | `my-post-title`        |

---

## 🔧 Stack

| What         | Choice         | Why                                                |
|--------------|----------------|----------------------------------------------------|
| Language     | C++23          | Because we RESPECT the machine                     |
| JSON parsing | simdjson       | SIMD-accelerated, gigabytes/second (adapters only) |
| Templates    | `guss::render` | Custom bytecode compiler — one pass, linear scan   |
| Markdown     | cmark          | GitHub Flavored, C-fast                            |
| HTTP client  | cpp-httplib    | Header-only, OpenSSL                               |
| CLI          | CLI11          | Header-only, elegant                               |
| Logging      | spdlog         | Console + syslog, sub-nanosecond                   |
| Progress     | indicators     | Because builds should look GOOD                    |
| File watch   | efsw           | Cross-platform, lightweight                        |
| Parallelism  | OpenMP         | One pragma, all cores                              |
| Build system | CMake + CPM    | Zero external tooling                              |
| Tests        | Google Test    | 369 tests, zero failures                           |

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
