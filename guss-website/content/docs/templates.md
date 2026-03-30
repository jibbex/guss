---
slug: docs/templates
title: Template Syntax
---

Guss templates use a Jinja2-like syntax. Templates are compiled to bytecode once at
startup; all subsequent renders are pure bytecode execution with no re-parsing.

## Output expressions

```jinja
{{ variable }}
{{ post.title }}
{{ post.author.name }}
```

Double-braces output the value HTML-escaped. Dot notation navigates nested objects and
arrays. `post.tags.0.slug` is the slug of the first tag.

## Filters

```jinja
{{ post.title | upper }}
{{ post.title | truncate(60) }}
{{ post.published_at | date("%B %d, %Y") }}
{{ post.html | safe }}
{{ post.tags | join(", ") }}
```

Pipe `|` applies a filter to the preceding value. Filters chain left-to-right. Arguments
are passed in parentheses. `| safe` suppresses HTML escaping. See the
[Filters Reference](/internals/filters/) for all 27 filters.

## Variables: `{% set %}`

```jinja
{% set year = post.published_at | date("%Y") %}
{% set title = post.title | upper | truncate(40) %}
<h1>{{ title }} ({{ year }})</h1>
```

`{% set %}` evaluates the right-hand expression and stores the result in the current
scope. The variable is visible for all subsequent statements in the same scope.

## Conditionals: `{% if %}`

```jinja
{% if post.featured %}
  <span class="badge">Featured</span>
{% endif %}

{% if post.tags | length > 0 %}
  <ul>
    {% for tag in post.tags %}
      <li>{{ tag.name }}</li>
    {% endfor %}
  </ul>
{% else %}
  <p>No tags.</p>
{% endif %}
```

Supports `{% if %}`, `{% elif %}`, `{% else %}`, `{% endif %}`. Operators: `==`, `!=`,
`<`, `>`, `<=`, `>=`, `and`, `or`, `not`.

## Loops: `{% for %}`

```jinja
{% for post in posts %}
  <article>
    <h2><a href="{{ post.permalink }}">{{ post.title }}</a></h2>
    <time>{{ post.published_at | date("%Y-%m-%d") }}</time>
    {% if not loop.last %}<hr>{% endif %}
  </article>
{% endfor %}
```

**`loop.*` variables** available inside every `{% for %}` body:

| Variable      | Type | Description               |
|---------------|------|---------------------------|
| `loop.index`  | int  | 1-based counter           |
| `loop.index0` | int  | 0-based counter           |
| `loop.first`  | bool | `true` on first iteration |
| `loop.last`   | bool | `true` on last iteration  |
| `loop.length` | int  | Total number of elements  |

## Partials: `{% include %}`

```jinja
{% include "partials/nav.html" %}
{% include "partials/footer.html" %}
```

Renders the named template inline. All variables in the current scope are available
inside the partial. Included templates are compiled and cached at startup — no file I/O
inside the render loop.

## Template inheritance: `{% extends %}` and `{% block %}`

**base.html:**
```html
<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
  <title>{% block title %}{{ site.title }}{% endblock %}</title>
</head>
<body>
  {% block content %}{% endblock %}
  {% block scripts %}{% endblock %}
</body>
</html>
```

**post.html:**
```html
{% extends "base.html" %}

{% block title %}{{ post.title }} — {{ site.title }}{% endblock %}

{% block content %}
  <h1>{{ post.title }}</h1>
  {{ post.html | safe }}
{% endblock %}
```

Child templates override only the blocks they declare. Everything outside blocks in
`base.html` (including nav, footer, and scripts) is always rendered.

### `{{ super() }}`

Renders the parent block's content inside an override, then appends or prepends to it:

```html
{% block title %}{{ super() }} — My Blog{% endblock %}
```

### Multi-level inheritance

Inheritance chains to any depth. The deepest override of each block wins.

## Template variables

These variables are available in every template:

| Variable                              | Description                                                                     |
|---------------------------------------|---------------------------------------------------------------------------------|
| `site`                                | Site metadata (`site.title`, `site.description`, `site.url`, `site.navigation`) |
| `page` (or configured `context_key`)  | The current item's data                                                         |
| `page.html`                           | md4c-rendered HTML body (Markdown adapter)                                      |
| `page.slug`                           | Item slug                                                                       |
| `page.permalink`                      | Computed permalink URL                                                          |
| `page.year`, `page.month`, `page.day` | Date parts extracted from `published_at`                                        |
| `page.prev_item`, `page.next_item`    | Adjacent items (if `prev_next` enabled)                                         |

Archive templates additionally receive:

| Variable                     | Description                                            |
|------------------------------|--------------------------------------------------------|
| `posts` (or collection name) | Array of all items on this archive page                |
| `tags`                       | Array of all tag items (if a `tags` collection exists) |
| `pagination`                 | Pagination metadata (see below)                        |

**`pagination` object** (paginated archives only):

| Field                     | Type   | Description                      |
|---------------------------|--------|----------------------------------|
| `pagination.current_page` | int    | Current page number (1-based)    |
| `pagination.total_pages`  | int    | Total number of pages            |
| `pagination.has_prev`     | bool   | `true` if a previous page exists |
| `pagination.has_next`     | bool   | `true` if a next page exists     |
| `pagination.prev_url`     | string | URL of the previous page         |
| `pagination.next_url`     | string | URL of the next page             |

## Comments

```jinja
{# This is a comment — not rendered in output #}
```

