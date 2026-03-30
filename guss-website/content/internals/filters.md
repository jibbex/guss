---
slug: internals/filters
title: Filters Reference
---

Guss has 27 built-in filters. They are registered at `Runtime` construction and
resolved to integer IDs at template compile time — no string lookup during rendering.

Filter signature:

```cpp
Value fn(const Value& subject, std::span<const Value> args);
```

Usage in templates: `{{ value | filter_name }}` or `{{ value | filter_name(arg1, arg2) }}`.

## String filters

### `truncate(n=255)`

Truncates to at most `n` UTF-8 code points and appends `…` (U+2026) when the string
is actually truncated.

```jinja
{{ post.title | truncate(60) }}
{{ post.excerpt | truncate }}        {# defaults to 255 #}
```

### `escape`

HTML-escapes `&`, `<`, `>`, `"`, and `'`. Applied automatically by `{{ }}`;
use this explicitly when you need the escaped value mid-pipeline.

```jinja
{{ post.title | escape }}
```

### `safe`

Suppresses HTML auto-escaping. The compiler emits `Op::EmitRaw` at the call site.

```jinja
{{ post.html | safe }}
{{ post.custom_block | upper | safe }}
```

### `lower`

Converts to ASCII lowercase.

```jinja
{{ post.title | lower }}
```

### `upper`

Converts to ASCII uppercase.

```jinja
{{ post.category | upper }}
```

### `capitalize`

Uppercases the first ASCII character, lowercases the rest.

```jinja
{{ author.name | capitalize }}
```

### `slugify`

Converts a string to a URL-safe slug: lowercase, spaces and underscores replaced
with hyphens, non-alphanumeric characters removed, consecutive hyphens collapsed.

```jinja
<a href="/tags/{{ tag.name | slugify }}/">{{ tag.name }}</a>
```

### `trim`

Strips leading and trailing ASCII whitespace.

```jinja
{{ post.bio | trim }}
```

### `replace(needle, replacement)`

Replaces all occurrences of `needle` with `replacement`.

```jinja
{{ post.title | replace("&", "and") }}
```

### `urlencode`

Percent-encodes every byte that is not an unreserved URI character (`A-Z`, `a-z`,
`0-9`, `-`, `_`, `.`, `~`).

```jinja
<a href="/search?q={{ query | urlencode }}">Search</a>
```

### `striptags`

Removes all HTML tags from a string.

```jinja
<meta name="description" content="{{ post.html | striptags | truncate(160) }}">
```

## Array and object filters

### `join(separator="")`

Joins an array into a string. Each element is stringified via `to_string()`.

```jinja
{{ post.tags | join(", ") }}
{{ items | join(" · ") }}
```

### `first`

Returns the first element of an array, or null if empty.

```jinja
{{ post.images | first }}
```

### `last`

Returns the last element of an array, or null if empty.

```jinja
{{ post.images | last }}
```

### `reverse`

Reverses a string (ASCII) or array. Returns a new array; the original is unchanged.

```jinja
{% for post in posts | reverse %}{{ post.title }}{% endfor %}
```

### `sort`

Sorts an array ascending. Strings are compared lexicographically; numbers numerically.

```jinja
{% for tag in tags | sort %}{{ tag.name }}{% endfor %}
```

### `items`

Converts an object (ValueMap) to an array of `[key, value]` pairs. Iteration order
is unspecified (hash map order).

```jinja
{% for pair in metadata | items %}
  {{ pair.0 }}: {{ pair.1 }}
{% endfor %}
```

### `dictsort`

Converts an object to an array of `[key, value]` pairs sorted lexicographically by key.

```jinja
{% for pair in config | dictsort %}
  <dt>{{ pair.0 }}</dt><dd>{{ pair.1 }}</dd>
{% endfor %}
```

## Numeric filters

### `abs`

Returns the absolute value of `int64_t` or `double`.

```jinja
{{ score | abs }}
```

### `round(places=0)`

Rounds to `places` decimal places. Returns a `double`.

```jinja
{{ rating | round(1) }}
{{ price | round }}       {# → integer-valued double #}
```

### `float_`

Converts `int64_t`, `double`, or a parseable `string` to `double`.

```jinja
{{ "3.14" | float_ }}
```

### `int_`

Converts `double`, `int64_t`, or a parseable `string` to `int64_t` (truncates towards zero).

```jinja
{{ "42" | int_ }}
{{ 3.9 | int_ }}          {# → 3 #}
```

## Type-aware filters

### `length`

Returns the number of elements (array/object) or UTF-8 code points (string). Returns
`0` for null and other types.

```jinja
{% if post.tags | length > 0 %}…{% endif %}
{{ post.title | length }} characters
```

### `default_(fallback="")`

Returns `subject` if truthy, otherwise returns `args[0]` (or null if no argument).

```jinja
{{ post.subtitle | default_("No subtitle") }}
{{ post.cover | default_("/assets/default-cover.jpg") }}
```

### `wordcount`

Counts whitespace-separated words. No HTML stripping — use `striptags` first if needed.

```jinja
{{ post.body | striptags | wordcount }} words
```

### `reading_minutes(wpm=256)`

Strips HTML tags, counts words, divides by words-per-minute (default 256). Minimum 1.
Pass a custom rate as `args[0]`.

```jinja
{{ post.html | reading_minutes }} min read
{{ post.html | reading_minutes(200) }} min read
```

## Date filter

### `date(format="%Y-%m-%d")`

Parses an ISO 8601 datetime string (e.g. `"2024-01-15T10:30:00Z"`) and reformats it
using `strftime`. Returns the original value unchanged if parsing fails.

```jinja
{{ post.published_at | date("%B %d, %Y") }}   {# → "January 15, 2024" #}
{{ post.published_at | date("%Y") }}           {# → "2024" #}
{{ post.published_at | date }}                 {# → "2024-01-15" (default) #}
```

Common `strftime` format codes:

| Code    | Example   | Meaning                |
|---------|-----------|------------------------|
| `%Y`    | `2024`    | 4-digit year           |
| `%m`    | `01`      | 2-digit month          |
| `%d`    | `15`      | 2-digit day            |
| `%B`    | `January` | Full month name        |
| `%b`    | `Jan`     | Abbreviated month name |
| `%H:%M` | `10:30`   | Hour and minute        |

