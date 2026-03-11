#pragma once

#include <string_view>

namespace guss::cli {

/**
 * \brief Default configuration file content for `guss init`
 * 
 * \details 
 * This YAML template is written as a raw string literal for easy inclusion in the binary.
 * It is written in the guss.yml on init command and serves as a starting point for users to customize their site.
 */
constexpr std::string_view DEFAULT_CONFIG = R"(# ─────────────────────────────────────────────────────────────
# SITE METADATA
# Populates the "site" variable in every template.
# ─────────────────────────────────────────────────────────────
site:
  title: "michaelis_m"
  description: "A site built with Guss"
  url: "https://michm.de"
  language: "de"
  # logo: "/images/logo.png"
  # twitter: "@myhandle"


# ─────────────────────────────────────────────────────────────
# CONTENT SOURCE
# type: rest_api  →  generic HTTP CMS (Ghost, WordPress, …)
# type: markdown  →  local filesystem
# ─────────────────────────────────────────────────────────────
source:
  type: rest_api
  base_url: "https://ghost.michm.de/"
  timeout_ms: 30000

  # ── Authentication ──────────────────────────────────────────
  auth:
    type: api_key          # api_key | basic | bearer | none
    param: key             # appended as ?key=<value>
    value: "31b7ae5a41617d284c438e8648"
    # WordPress: type: basic, username: "user", password: "app-password"
    # Bearer:    type: bearer, token: "mytoken"

  # ── Pagination defaults (overrideable per endpoint) ─────────
  pagination:
    page_param:  page      # ?page=N
    limit_param: limit     # ?limit=N
    limit: 15
    # Ghost: non-null JSON value at this path = there is a next page
    json_next: "meta.pagination.next"
    # WordPress alternative:
    # total_pages_header: "X-WP-TotalPages"

  # ── Per-collection API endpoints ────────────────────────────
  # endpoint keys must match the top-level collections: keys
  endpoints:
    posts:
      path: "ghost/api/content/posts/"
      response_key: "posts"    # JSON key holding the items array
      params:                  # fixed extra query parameters
        include: "authors,tags"
      # WordPress: path: "wp-json/wp/v2/posts", response_key: "", params: {_embed: 1}

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
  # target_field: "source.path"
  # Dot-path:  "authors.0"        → authors[0]  (first array element)
  #            "content.rendered" → nested field (WordPress)
  # Applied before anything else; templates always see target names.
  field_maps:
    posts:
      content: "html"          # Ghost returns "html"; templates expect "content"
      author:  "authors.0"     # promote first element → singular "author"
      # WordPress:
      # published_at: "date"
      # content:      "content.rendered"
      # excerpt:      "excerpt.rendered"
      # author:       "_embedded.author.0"
    pages:
      content: "html"
      author:  "authors.0"

  # ── Cross-references ────────────────────────────────────────
  # Adds a "posts" array to each item in taxonomy collections.
  # For each tag/author item, finds all posts where the given
  # dot-path in the post data matches the item's slug.
  cross_references:
    tags:
      from: posts
      via: "tags.slug"         # post.tags[*].slug == tag.slug
    authors:
      from: posts
      via: "author.slug"       # post.author.slug == author.slug (after field_map)


# ─────────────────────────────────────────────────────────────
# COLLECTIONS
# Drives page generation. Keys must match endpoints: keys.
# context_key: variable name exposed to the template for this item.
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
    item_template:    "tag.html"
    permalink:        "/tag/{slug}/"
    context_key:      "tag"

  authors:
    item_template:    "author.html"
    permalink:        "/author/{slug}/"
    context_key:      "author"


# ─────────────────────────────────────────────────────────────
# OUTPUT
# ─────────────────────────────────────────────────────────────
output:
  output_dir:       "./dist"
  generate_sitemap: true
  generate_rss:     true
  minify_html:      false
  copy_assets:      true


# ─────────────────────────────────────────────────────────────
# BUILD
# ─────────────────────────────────────────────────────────────
parallel_workers: 0      # 0 = auto-detect
log_level: "info"        # debug | info | warn | error
)";

/**
 * \brief Default post template for `guss init`
 * 
 * \details
 * This is a simple HTML template using Tailwind CSS for styling and highlight.js for syntax highlighting.
 * It demonstrates how to access the "post" and "site" variables, render content, and include metadata. Users can 
 * customize this template or create their own. The "post" variable is populated from the content adapter's field mappings.
 * The template includes a responsive navbar, a hero section with an optional feature image, and a styled article layout.
 * 
 * \note In a real project, you might want to split this into separate template files and include them as needed.
 * For simplicity, it's included as a single template here. It is copied to templates/post.html on `guss init`.
 */
constexpr std::string_view DEFAULT_POST_TEMPLATE = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{{ post.title }} - {{ site.title }}</title>
    <meta name="description" content="{{ post.excerpt | truncate(160) }}">
        <!-- Fonts -->
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:ital,opsz,wght@0,14..32,300;0,14..32,400;0,14..32,500;0,14..32,600;0,14..32,700;0,14..32,800&display=swap">
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:ital,wght@0,400;0,500;1,400&display=swap">
    <link rel="stylesheet" href="/assets/style.css">
    <script src="https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4"></script>
    <style type="text/tailwindcss">
        @variant dark (&:where(.dark, .dark *));
        @theme {
            --color-surface:                oklch(0.98  0.002 260);
            --color-surface-alt:            oklch(0.94  0.006 260);
            --color-surface-elevated:       oklch(1     0     0);
            --color-on-surface:             oklch(0.40  0.016 260);
            --color-on-surface-strong:      oklch(0.12  0.010 260);
            --color-on-surface-muted:       oklch(0.60  0.012 260);
            --color-primary:                oklch(0.52  0.22  265);
            --color-primary-light:          oklch(0.65  0.18  265);
            --color-on-primary:             oklch(1     0     0);
            --color-accent:                 oklch(0.72  0.20  35);
            --color-outline:                oklch(0.88  0.006 260);
            --color-outline-strong:         oklch(0.72  0.014 260);
            --color-surface-dark:           oklch(0.10  0.018 265);
            --color-surface-dark-alt:       oklch(0.15  0.022 265);
            --color-surface-dark-elevated:  oklch(0.18  0.020 265);
            --color-on-surface-dark:        oklch(0.65  0.016 265);
            --color-on-surface-dark-strong: oklch(0.96  0.005 265);
            --color-on-surface-dark-muted:  oklch(0.45  0.012 265);
            --color-primary-dark:           oklch(0.74  0.18  265);
            --color-on-primary-dark:        oklch(0.10  0.018 265);
            --color-accent-dark:            oklch(0.80  0.18  35);
            --color-outline-dark:           oklch(0.24  0.018 265);
            --color-outline-dark-strong:    oklch(0.38  0.022 265);
            --radius-radius: var(--radius-xl);
            --font-sans:    'Inter', system-ui, sans-serif;
            --font-heading: 'Inter', system-ui, sans-serif;
        }
        html { font-family: var(--font-sans); }

        [x-cloak] { display: none !important; }
    </style>
    <script>
        (() => {
            const stored = localStorage.getItem('theme');
            if (stored === 'dark' || (!stored && window.matchMedia('(prefers-color-scheme: dark)').matches))
                document.documentElement.classList.add('dark');
        })();
    </script>
    <script defer src="https://cdn.jsdelivr.net/npm/@alpinejs/collapse@3.x.x/dist/cdn.min.js"></script>
    <script defer src="https://cdn.jsdelivr.net/npm/alpinejs@3.x.x/dist/cdn.min.js"></script>

    <!-- highlight.js syntax highlighting -->
    <link id="hljs-light" rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/github.min.css">
    <link id="hljs-dark"  rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/github-dark.min.css" disabled>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js"></script>
    <!-- Extra language modules not in the default bundle -->
    <script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/languages/c.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/languages/cpp.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/languages/cmake.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/languages/rust.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/languages/go.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/languages/javascript.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/languages/typescript.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/languages/bash.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/languages/java.min.js"></script>
    <script>
        // Run after DOM ready
        document.addEventListener('DOMContentLoaded', () => {
            // Ghost outputs language-clike — register as alias for c
            hljs.registerAliases(['clike'], { languageName: 'c' });
            // Apply highlighting to all code blocks
            document.querySelectorAll('pre code').forEach(block => hljs.highlightElement(block));

            // Sync theme with dark mode toggle
            const syncHljs = () => {
                const dark = document.documentElement.classList.contains('dark');
                document.getElementById('hljs-light').disabled = dark;
                document.getElementById('hljs-dark').disabled  = !dark;
            };
            syncHljs();

            // Watch for dark class changes on <html>
            new MutationObserver(syncHljs).observe(
                document.documentElement,
                { attributeFilter: ['class'] }
            );
        });
    </script>
</head>
<body class="bg-surface dark:bg-surface-dark text-on-surface dark:text-on-surface-dark transition-colors duration-300">

<!-- Navbar -->
<nav x-data="{ open: false }" x-on:click.away="open = false"
     class="fixed top-0 inset-x-0 z-50 border-b border-outline/50 dark:border-outline-dark/50 bg-surface/80 dark:bg-surface-dark/80 backdrop-blur-xl">
    <div class="mx-auto max-w-7xl px-6 flex h-16 items-center justify-between gap-6">
        <a href="/" class="font-thin tracking-tight text-2xl italic text-on-surface-strong dark:text-on-surface-dark-strong hover:text-primary dark:hover:text-primary-dark transition-colors">{{ site.title }}</a>
        <div class="flex items-center gap-3">
            <button id="themeBtn" type="button" aria-label="Toggle theme"
                    class="size-9 rounded-full border border-outline dark:border-outline-dark bg-surface-alt dark:bg-surface-dark-alt grid place-items-center text-on-surface dark:text-on-surface-dark hover:border-primary dark:hover:border-primary-dark hover:text-primary dark:hover:text-primary-dark transition-all hover:scale-110">
                <svg id="iconSun" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.75" stroke="currentColor" class="size-[1.1rem] hidden"><path stroke-linecap="round" stroke-linejoin="round" d="M12 3v2m0 14v2M3 12H1m22 0h-2m-2.636-6.364-1.414 1.414M6.05 17.95l-1.414 1.414m0-12.728 1.414 1.414M17.95 17.95l1.414 1.414M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8Z"/></svg>
                <svg id="iconMoon" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.75" stroke="currentColor" class="size-[1.1rem]"><path stroke-linecap="round" stroke-linejoin="round" d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79Z"/></svg>
            </button>
            <button x-on:click="open = !open" x-bind:class="open ? 'fixed top-4 right-6 z-50' : ''"
                    type="button" aria-label="Menu"
                    class="md:hidden size-9 grid place-items-center rounded-full border border-outline dark:border-outline-dark bg-surface-alt dark:bg-surface-dark-alt text-on-surface dark:text-on-surface-dark hover:text-primary dark:hover:text-primary-dark transition-colors">
                <svg x-cloak x-show="!open" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor" class="size-5"><path stroke-linecap="round" stroke-linejoin="round" d="M3.75 6.75h16.5M3.75 12h16.5m-16.5 5.25h16.5"/></svg>
                <svg x-cloak x-show="open"  xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor" class="size-5"><path stroke-linecap="round" stroke-linejoin="round" d="M6 18 18 6M6 6l12 12"/></svg>
            </button>
        </div>
    </div>
    <div x-cloak x-show="open"
         x-transition:enter="transition ease-out duration-200" x-transition:enter-start="opacity-0 -translate-y-2" x-transition:enter-end="opacity-100 translate-y-0"
         x-transition:leave="transition ease-in duration-150" x-transition:leave-end="opacity-0 -translate-y-2"
         class="border-t border-outline dark:border-outline-dark bg-surface dark:bg-surface-dark px-6 py-5 md:hidden">
        <a href="/" class="block py-2 text-sm font-medium text-on-surface dark:text-on-surface-dark hover:text-primary dark:hover:text-primary-dark">Home</a>
    </div>
</nav>

<!-- Hero with feature image or gradient -->
<div class="pt-16">
    <div class="relative w-full aspect-[21/9] sm:aspect-[21/7] overflow-hidden bg-gradient-to-br from-primary/80 via-primary-light/60 to-accent/60 dark:from-primary-dark/80 dark:via-primary-dark/50 dark:to-accent-dark/50">
        {% if post.feature_image %}
        <img src="{{ post.feature_image }}" alt="{{ post.feature_image_alt }}"
             class="absolute inset-0 w-full h-full object-cover opacity-50 mix-blend-overlay">
        {% endif %}
        <div class="absolute inset-0 bg-gradient-to-t from-surface dark:from-surface-dark via-transparent to-transparent"></div>
        <!-- Decorative blobs -->
        <div class="absolute inset-0 pointer-events-none">
            <div class="absolute top-0 right-0 w-96 h-96 rounded-full bg-accent/20 dark:bg-accent-dark/20 blur-3xl -translate-y-1/2 translate-x-1/4"></div>
            <div class="absolute bottom-0 left-0 w-80 h-80 rounded-full bg-primary/20 dark:bg-primary-dark/20 blur-3xl translate-y-1/2 -translate-x-1/4"></div>
        </div>
    </div>
</div>

<!-- Article -->
<div class="mx-auto max-w-3xl px-6 -mt-24 relative z-10 pb-16">
    <article>
        <header class="mb-12">
            <time datetime="{{ post.published_at }}"
                  class="text-[0.65rem] font-bold uppercase tracking-[0.2em] text-primary dark:text-primary-dark mb-4 block">
                {{ post.published_at | date("%d.%m.%Y") }}
            </time>
            <h1 class="font-thin tracking-tight text-4xl sm:text-6xl text-on-surface-strong dark:text-on-surface-dark-strong leading-none tracking-tight mb-6">
                {{ post.title }}
            </h1>
            {% if post.author %}
            <div class="flex items-center gap-3 pt-6 border-t border-outline dark:border-outline-dark">
                <div class="size-9 rounded-full bg-gradient-to-br from-primary to-accent dark:from-primary-dark dark:to-accent-dark flex items-center justify-center text-white font-bold text-sm shrink-0">
                    {{ post.author.name | truncate(1, false, '') }}
                </div>
                <a href="/author/{{ post.author.slug }}/"
                   class="text-sm font-medium text-on-surface-strong dark:text-on-surface-dark-strong hover:text-primary dark:hover:text-primary-dark transition-colors no-underline">
                    {{ post.author.name }}
                </a>
            </div>
            {% endif %}
        </header>

        <!-- Post body — | safe required for rendered HTML -->
        <div class="prose text-on-surface dark:text-on-surface-dark">
            {{ post.content | safe }}
        </div>

        <!-- Tags -->
        {% if post.tags %}
        <footer class="mt-14 pt-8 border-t border-outline dark:border-outline-dark">
            <p class="text-[0.65rem] font-bold uppercase tracking-[0.18em] text-on-surface-muted dark:text-on-surface-dark-muted mb-3">Tagged</p>
            <ul class="flex flex-wrap gap-2">
                {% for tag in post.tags %}
                <li>
                    <a href="/tag/{{ tag.slug }}/"
                       class="px-4 py-1.5 rounded-full border border-outline dark:border-outline-dark bg-surface-alt dark:bg-surface-dark-alt text-xs font-medium text-on-surface dark:text-on-surface-dark hover:bg-primary hover:text-on-primary hover:border-primary dark:hover:bg-primary-dark dark:hover:text-on-primary-dark dark:hover:border-primary-dark transition-all no-underline">
                        {{ tag.name }}
                    </a>
                </li>
                {% endfor %}
            </ul>
        </footer>
        {% endif %}
    </article>

    <!-- Prev / Next -->
    <nav class="mt-16 grid grid-cols-2 gap-4" aria-label="post navigation">
        {% if prev_post %}
        <a href="{{ prev_post.permalink }}"
           class="group p-5 rounded-2xl border border-outline dark:border-outline-dark bg-surface-elevated dark:bg-surface-dark-elevated hover:border-primary/50 dark:hover:border-primary-dark/50 hover:shadow-lg hover:shadow-primary/5 transition-all no-underline">
            <p class="text-[0.65rem] font-bold uppercase tracking-widest text-on-surface-muted dark:text-on-surface-dark-muted mb-2 flex items-center gap-1">
                <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" class="size-3"><path fill-rule="evenodd" d="M9.78 4.22a.75.75 0 0 1 0 1.06L7.06 8l2.72 2.72a.75.75 0 1 1-1.06 1.06L5.47 8.53a.75.75 0 0 1 0-1.06l3.25-3.25a.75.75 0 0 1 1.06 0Z" clip-rule="evenodd"/></svg>
                Previous
            </p>
            <p class="text-sm font-medium text-on-surface-strong dark:text-on-surface-dark-strong group-hover:text-primary dark:group-hover:text-primary-dark transition-colors line-clamp-2">{{ prev_post.title }}</p>
        </a>
        {% else %}<div></div>{% endif %}

        {% if next_post %}
        <a href="{{ next_post.permalink }}"
           class="group p-5 rounded-2xl border border-outline dark:border-outline-dark bg-surface-elevated dark:bg-surface-dark-elevated hover:border-primary/50 dark:hover:border-primary-dark/50 hover:shadow-lg hover:shadow-primary/5 transition-all no-underline text-right">
            <p class="text-[0.65rem] font-bold uppercase tracking-widest text-on-surface-muted dark:text-on-surface-dark-muted mb-2 flex items-center justify-end gap-1">
                Next
                <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" class="size-3"><path fill-rule="evenodd" d="M6.22 4.22a.75.75 0 0 1 1.06 0l3.25 3.25a.75.75 0 0 1 0 1.06l-3.25 3.25a.75.75 0 0 1-1.06-1.06L8.94 8 6.22 5.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/></svg>
            </p>
            <p class="text-sm font-medium text-on-surface-strong dark:text-on-surface-dark-strong group-hover:text-primary dark:group-hover:text-primary-dark transition-colors line-clamp-2">{{ next_post.title }}</p>
        </a>
        {% else %}<div></div>{% endif %}
    </nav>
</div>

<footer class="border-t border-outline dark:border-outline-dark mt-8">
    <div class="mx-auto max-w-7xl px-6 py-8 flex flex-col sm:flex-row items-center justify-between gap-6 text-xs text-on-surface-muted dark:text-on-surface-dark-muted">
        <span class="font-semibold text-on-surface-strong dark:text-on-surface-dark-strong">{{ site.title }}</span>
        <nav class="flex items-center gap-6 flex-wrap justify-center sm:justify-end">
            <a href="/datenschutz/" class="hover:text-primary dark:hover:text-primary-dark transition-colors no-underline">Datenschutz</a>
            <a href="/impressum/"   class="hover:text-primary dark:hover:text-primary-dark transition-colors no-underline">Impressum</a>
            <span>&copy; {{ site.title }}</span>
        </nav>
    </div>
</footer>

<script>
    (() => {
        const btn  = document.getElementById('themeBtn');
        const sun  = document.getElementById('iconSun');
        const moon = document.getElementById('iconMoon');
        const html = document.documentElement;
        const syncIcons = () => {
            const dark = html.classList.contains('dark');
            sun.classList.toggle('hidden', !dark);
            moon.classList.toggle('hidden', dark);
        };
        syncIcons();
        btn.addEventListener('click', () => {
            const dark = html.classList.toggle('dark');
            localStorage.setItem('theme', dark ? 'dark' : 'light');
            syncIcons();
        });
    })();
</script>
</body>
</html>
)";

/** 
 * \brief Default index template for `guss init`
 * 
 * \details 
 * This is a simple HTML template using Tailwind CSS for styling and highlight.js for syntax highlighting.
 * It demonstrates how to access the "site" variable, render content, and include metadata. Users can customize this
 * template or create their own. The "site" variable is populated from the configuration file. The template includes
 * a responsive navbar, a hero section with an optional feature image, and a styled article layout.
 * 
 * \note In a real project, you might want to split this into separate template files and include them as needed.
 * For simplicity, it's included as a single template here. It is copied to templates/index.html on `guss init`.
 */
constexpr std::string_view DEFAULT_INDEX_TEMPLATE = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{{ site.title }}</title>
    <meta name="description" content="{{ site.description }}">
    <link rel="alternate" type="application/rss+xml" title="{{ site.title }}" href="/rss.xml">
        <!-- Fonts -->
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:ital,opsz,wght@0,14..32,300;0,14..32,400;0,14..32,500;0,14..32,600;0,14..32,700;0,14..32,800&display=swap">
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:ital,wght@0,400;0,500;1,400&display=swap">
    <link rel="stylesheet" href="/assets/style.css">
    <script src="https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4"></script>
    <style type="text/tailwindcss">
        /* ── Fix: class-based dark mode for Tailwind v4 ── */
        @variant dark (&:where(.dark, .dark *));

        @theme {
            --color-surface:                oklch(0.98  0.002 260);
            --color-surface-alt:            oklch(0.94  0.006 260);
            --color-surface-elevated:       oklch(1     0     0);
            --color-on-surface:             oklch(0.40  0.016 260);
            --color-on-surface-strong:      oklch(0.12  0.010 260);
            --color-on-surface-muted:       oklch(0.60  0.012 260);

            --color-primary:                oklch(0.52  0.22  265);
            --color-primary-light:          oklch(0.65  0.18  265);
            --color-on-primary:             oklch(1     0     0);

            --color-accent:                 oklch(0.72  0.20  35);
            --color-accent-light:           oklch(0.85  0.14  35);

            --color-outline:                oklch(0.88  0.006 260);
            --color-outline-strong:         oklch(0.72  0.014 260);

            /* ── Dark ── */
            --color-surface-dark:           oklch(0.10  0.018 265);
            --color-surface-dark-alt:       oklch(0.15  0.022 265);
            --color-surface-dark-elevated:  oklch(0.18  0.020 265);
            --color-on-surface-dark:        oklch(0.65  0.016 265);
            --color-on-surface-dark-strong: oklch(0.96  0.005 265);
            --color-on-surface-dark-muted:  oklch(0.45  0.012 265);

            --color-primary-dark:           oklch(0.74  0.18  265);
            --color-primary-dark-light:     oklch(0.84  0.14  265);
            --color-on-primary-dark:        oklch(0.10  0.018 265);

            --color-accent-dark:            oklch(0.80  0.18  35);

            --color-outline-dark:           oklch(0.24  0.018 265);
            --color-outline-dark-strong:    oklch(0.38  0.022 265);

            --radius-radius: var(--radius-xl);
            --font-sans:    'Inter', system-ui, sans-serif;
            --font-heading: 'Inter', system-ui, sans-serif;
            --font-mono:    'DM Mono',           monospace;
        }

        html { font-family: var(--font-sans); }

        [x-cloak] { display: none !important; }
    </style>

    <!-- Instant dark mode — before any render -->
    <script>
        (() => {
            const stored = localStorage.getItem('theme');
            const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
            if (stored === 'dark' || (!stored && prefersDark)) {
                document.documentElement.classList.add('dark');
            }
        })();
    </script>
    <script defer src="https://cdn.jsdelivr.net/npm/@alpinejs/collapse@3.x.x/dist/cdn.min.js"></script>
    <script defer src="https://cdn.jsdelivr.net/npm/alpinejs@3.x.x/dist/cdn.min.js"></script>

</head>
<body class="bg-surface dark:bg-surface-dark text-on-surface dark:text-on-surface-dark transition-colors duration-300">

<!-- ══════════════════════════════════════════════════════
     NAVBAR
══════════════════════════════════════════════════════ -->
<nav x-data="{ open: false }" x-on:click.away="open = false"
     class="fixed top-0 inset-x-0 z-50 border-b border-outline/50 dark:border-outline-dark/50 bg-surface/80 dark:bg-surface-dark/80 backdrop-blur-xl">
    <div class="mx-auto max-w-7xl px-6 flex h-16 items-center justify-between gap-6">

        <a href="/" class="font-thin tracking-tight text-2xl italic text-on-surface-strong dark:text-on-surface-dark-strong hover:text-primary dark:hover:text-primary-dark transition-colors">
            {{ site.title }}
        </a>

        <ul class="hidden md:flex items-center gap-1 text-sm font-medium">
            <li><a href="/" class="px-4 py-2 rounded-full text-on-surface dark:text-on-surface-dark hover:bg-surface-alt dark:hover:bg-surface-dark-alt hover:text-on-surface-strong dark:hover:text-on-surface-dark-strong transition-colors">Home</a></li>
        </ul>

        <div class="flex items-center gap-3">
            <!-- Theme toggle -->
            <button id="themeBtn" type="button" aria-label="Toggle theme"
                    class="size-9 rounded-full border border-outline dark:border-outline-dark bg-surface-alt dark:bg-surface-dark-alt grid place-items-center text-on-surface dark:text-on-surface-dark hover:border-primary dark:hover:border-primary-dark hover:text-primary dark:hover:text-primary-dark transition-all hover:scale-110">
                <svg id="iconSun" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.75" stroke="currentColor" class="size-[1.1rem] hidden">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M12 3v2m0 14v2M3 12H1m22 0h-2m-2.636-6.364-1.414 1.414M6.05 17.95l-1.414 1.414m0-12.728 1.414 1.414M17.95 17.95l1.414 1.414M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8Z"/>
                </svg>
                <svg id="iconMoon" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.75" stroke="currentColor" class="size-[1.1rem]">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79Z"/>
                </svg>
            </button>

            <button x-on:click="open = !open" x-bind:class="open ? 'fixed top-4 right-6 z-50' : ''"
                    type="button" aria-label="Menu"
                    class="md:hidden size-9 grid place-items-center rounded-full border border-outline dark:border-outline-dark bg-surface-alt dark:bg-surface-dark-alt text-on-surface dark:text-on-surface-dark hover:text-primary dark:hover:text-primary-dark transition-colors">
                <svg x-cloak x-show="!open" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor" class="size-5"><path stroke-linecap="round" stroke-linejoin="round" d="M3.75 6.75h16.5M3.75 12h16.5m-16.5 5.25h16.5"/></svg>
                <svg x-cloak x-show="open"  xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor" class="size-5"><path stroke-linecap="round" stroke-linejoin="round" d="M6 18 18 6M6 6l12 12"/></svg>
            </button>
        </div>
    </div>

    <!-- Mobile menu -->
    <div x-cloak x-show="open"
         x-transition:enter="transition ease-out duration-200" x-transition:enter-start="opacity-0 -translate-y-2" x-transition:enter-end="opacity-100 translate-y-0"
         x-transition:leave="transition ease-in duration-150" x-transition:leave-end="opacity-0 -translate-y-2"
         class="border-t border-outline dark:border-outline-dark bg-surface dark:bg-surface-dark px-6 py-5 md:hidden">
        <a href="/" class="block py-2 text-sm font-medium text-on-surface dark:text-on-surface-dark hover:text-primary dark:hover:text-primary-dark">Home</a>
    </div>
</nav>

<!-- ══════════════════════════════════════════════════════
     HERO
══════════════════════════════════════════════════════ -->
<section class="pt-32 pb-20 px-6 relative overflow-hidden">
    <!-- Mesh gradient background -->
    <div class="absolute inset-0 -z-10 pointer-events-none">
        <div class="absolute -top-40 -left-40 w-[600px] h-[600px] rounded-full bg-primary/10 dark:bg-primary-dark/15 blur-3xl"></div>
        <div class="absolute -bottom-20 -right-20 w-[500px] h-[500px] rounded-full bg-accent/10 dark:bg-accent-dark/10 blur-3xl"></div>
    </div>
    <div class="mx-auto max-w-7xl">
        <p class="text-xs font-semibold uppercase tracking-[0.2em] text-primary dark:text-primary-dark mb-4">Journal</p>
        <h1 class="font-thin tracking-tight text-6xl sm:text-8xl text-on-surface-strong dark:text-on-surface-dark-strong leading-none tracking-tight max-w-2xl mb-6">
            {{ site.title }}
        </h1>
        <p class="text-lg text-on-surface-muted dark:text-on-surface-dark-muted max-w-lg">{{ site.description }}</p>
    </div>
</section>

<!-- ══════════════════════════════════════════════════════
     TAGS BAR — infinite marquee
══════════════════════════════════════════════════════ -->
<style>
    @keyframes marquee {
        from { transform: translateX(0); }
        to   { transform: translateX(-50%); }
    }
    .marquee-track {
        display: flex;
        width: max-content;
        animation: marquee 80s linear infinite;
    }
    .marquee-track:hover {
        animation-play-state: paused;
    }
</style>

<div class="sticky top-16 z-40 border-y border-outline/60 dark:border-outline-dark/60 bg-surface/90 dark:bg-surface-dark/90 backdrop-blur-lg">
    <div class="flex items-center">
        <!-- Label — sits outside the scroll track -->
        <div class="shrink-0 pl-6 pr-4 py-3 border-r border-outline/60 dark:border-outline-dark/60 bg-surface/90 dark:bg-surface-dark/90 z-10">
            <span class="text-[0.65rem] font-bold uppercase tracking-widest text-on-surface-muted dark:text-on-surface-dark-muted whitespace-nowrap">Topics</span>
        </div>

        <!-- Fade edges -->
        <div class="relative flex-1 overflow-hidden">
            <div class="absolute left-0 inset-y-0 w-12 bg-gradient-to-r from-surface/90 dark:from-surface-dark/90 to-transparent z-10 pointer-events-none"></div>
            <div class="absolute right-0 inset-y-0 w-12 bg-gradient-to-l from-surface/90 dark:from-surface-dark/90 to-transparent z-10 pointer-events-none"></div>

            <!-- Marquee track — tags duplicated for seamless loop -->
            <div class="marquee-track py-3 gap-2">
                {% for tag in tags %}
                <a href="/tag/{{ tag.slug }}/"
                   class="shrink-0 mx-1 px-3 py-1 rounded-full border border-outline dark:border-outline-dark bg-surface-elevated dark:bg-surface-dark-elevated text-xs font-medium text-on-surface dark:text-on-surface-dark hover:bg-primary hover:text-on-primary hover:border-primary dark:hover:bg-primary-dark dark:hover:text-on-primary-dark dark:hover:border-primary-dark transition-all no-underline whitespace-nowrap">
                    {{ tag.name }}
                </a>
                {% endfor %}
                <!-- Duplicate for seamless infinite loop -->
                {% for tag in tags %}
                <a href="/tag/{{ tag.slug }}/"
                   aria-hidden="true"
                   class="shrink-0 mx-1 px-3 py-1 rounded-full border border-outline dark:border-outline-dark bg-surface-elevated dark:bg-surface-dark-elevated text-xs font-medium text-on-surface dark:text-on-surface-dark hover:bg-primary hover:text-on-primary hover:border-primary dark:hover:bg-primary-dark dark:hover:text-on-primary-dark dark:hover:border-primary-dark transition-all no-underline whitespace-nowrap">
                    {{ tag.name }}
                </a>
                {% endfor %}
            </div>
        </div>
    </div>
</div>

<!-- ══════════════════════════════════════════════════════
     POST GRID  –  magazine layout
══════════════════════════════════════════════════════ -->
<main class="mx-auto max-w-7xl px-6 py-16">

    <!-- Post grid — first post is featured hero, rest are cards -->
    <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-6">
        {% for post in posts %}

        {% if loop.first %}
        <!-- Featured hero card — spans full width -->
        <a href="{{ post.permalink }}" class="group col-span-1 sm:col-span-2 lg:col-span-3 block no-underline mb-4">
            <div class="relative rounded-2xl overflow-hidden aspect-[21/8] bg-gradient-to-br from-primary/80 via-primary-light/60 to-accent/50 dark:from-primary-dark/70 dark:via-primary-dark/50 dark:to-accent-dark/40">
                {% if post.feature_image %}
                <img src="{{ post.feature_image }}" alt="{{ post.title }}"
                     class="absolute inset-0 w-full h-full object-cover mix-blend-overlay opacity-60">
                {% endif %}
                <div class="absolute inset-0 bg-gradient-to-t from-black/70 via-black/20 to-transparent"></div>
                <div class="absolute inset-0 p-8 sm:p-12 flex flex-col justify-end">
                    <p class="text-xs font-bold uppercase tracking-[0.18em] text-white/70 mb-3">Featured</p>
                    <h2 class="font-thin tracking-tight text-3xl sm:text-5xl text-white leading-tight mb-3 group-hover:text-accent-light transition-colors">
                        {{ post.title }}
                    </h2>
                    {% if post.excerpt %}
                    <p class="text-white/70 text-sm max-w-xl line-clamp-2">{{ post.excerpt | truncate(160) }}</p>
                    {% endif %}
                    <time datetime="{{ post.published_at }}" class="mt-4 block text-white/60 text-xs font-medium">{{ post.published_at | date("%d.%m.%Y") }}</time>
                </div>
            </div>
        </a>

        {% else %}
        <!-- Regular card -->
        <a href="{{ post.permalink }}" class="group flex flex-col rounded-2xl overflow-hidden border border-outline dark:border-outline-dark bg-surface-elevated dark:bg-surface-dark-elevated hover:border-primary/50 dark:hover:border-primary-dark/50 hover:shadow-xl hover:shadow-primary/5 transition-all duration-300 no-underline">
            <div class="relative aspect-video overflow-hidden">
                {% if post.feature_image %}
                <img src="{{ post.feature_image }}" alt="{{ post.title }}"
                     class="w-full h-full object-cover group-hover:scale-105 transition-transform duration-500">
                {% else %}
                <div class="w-full h-full bg-gradient-to-br from-primary/25 to-accent/35 dark:from-primary-dark/25 dark:to-accent-dark/25 flex items-center justify-center">
                    <svg viewBox="0 0 100 100" class="w-16 h-16 fill-white/20"><circle cx="50" cy="50" r="40"/></svg>
                </div>
                {% endif %}
            </div>
            <div class="flex flex-col flex-1 p-5">
                <time datetime="{{ post.published_at }}" class="text-[0.65rem] font-bold uppercase tracking-[0.14em] text-on-surface-muted dark:text-on-surface-dark-muted mb-2">{{ post.published_at | date("%d.%m.%Y") }}</time>
                <h3 class="font-thin tracking-tight text-lg text-on-surface-strong dark:text-on-surface-dark-strong leading-snug mb-2 group-hover:text-primary dark:group-hover:text-primary-dark transition-colors flex-1">
                    {{ post.title }}
                </h3>
                {% if post.excerpt %}
                <p class="text-xs text-on-surface dark:text-on-surface-dark leading-relaxed line-clamp-2 mt-1">{{ post.excerpt | truncate(120) }}</p>
                {% endif %}
                <div class="mt-4 flex items-center gap-1 text-xs font-semibold text-primary dark:text-primary-dark">
                    Read
                    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" class="size-3 group-hover:translate-x-1 transition-transform">
                        <path fill-rule="evenodd" d="M6.22 4.22a.75.75 0 0 1 1.06 0l3.25 3.25a.75.75 0 0 1 0 1.06l-3.25 3.25a.75.75 0 0 1-1.06-1.06L8.94 8 6.22 5.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/>
                    </svg>
                </div>
            </div>
        </a>
        {% endif %}

        {% endfor %}

    <!-- Pagination -->
    {% if pagination.total > 1 %}
    <nav class="mt-16 flex items-center justify-center gap-3" aria-label="pagination">
        {% if pagination.has_prev %}
        <a href="{{ pagination.prev_url }}"
           class="inline-flex items-center gap-2 px-5 py-2.5 rounded-full border border-outline dark:border-outline-dark bg-surface-elevated dark:bg-surface-dark-elevated text-sm font-medium text-on-surface dark:text-on-surface-dark hover:border-primary dark:hover:border-primary-dark hover:text-primary dark:hover:text-primary-dark transition-all no-underline">
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" class="size-4"><path fill-rule="evenodd" d="M9.78 4.22a.75.75 0 0 1 0 1.06L7.06 8l2.72 2.72a.75.75 0 1 1-1.06 1.06L5.47 8.53a.75.75 0 0 1 0-1.06l3.25-3.25a.75.75 0 0 1 1.06 0Z" clip-rule="evenodd"/></svg>
            Newer
        </a>
        {% endif %}
        <span class="px-4 py-2 text-sm text-on-surface-muted dark:text-on-surface-dark-muted tabular-nums">
            {{ pagination.current }} / {{ pagination.total }}
        </span>
        {% if pagination.has_next %}
        <a href="{{ pagination.next_url }}"
           class="inline-flex items-center gap-2 px-5 py-2.5 rounded-full border border-outline dark:border-outline-dark bg-surface-elevated dark:bg-surface-dark-elevated text-sm font-medium text-on-surface dark:text-on-surface-dark hover:border-primary dark:hover:border-primary-dark hover:text-primary dark:hover:text-primary-dark transition-all no-underline">
            Older
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" class="size-4"><path fill-rule="evenodd" d="M6.22 4.22a.75.75 0 0 1 1.06 0l3.25 3.25a.75.75 0 0 1 0 1.06l-3.25 3.25a.75.75 0 0 1-1.06-1.06L8.94 8 6.22 5.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/></svg>
        </a>
        {% endif %}
    </nav>
    {% endif %}
</main>

<footer class="border-t border-outline dark:border-outline-dark mt-8">
    <div class="mx-auto max-w-7xl px-6 py-8 flex flex-col sm:flex-row items-center justify-between gap-6 text-xs text-on-surface-muted dark:text-on-surface-dark-muted">
        <span class="font-semibold text-on-surface-strong dark:text-on-surface-dark-strong">{{ site.title }}</span>
        <nav class="flex items-center gap-6 flex-wrap justify-center sm:justify-end">
            <a href="/datenschutz/" class="hover:text-primary dark:hover:text-primary-dark transition-colors no-underline">Datenschutz</a>
            <a href="/impressum/"   class="hover:text-primary dark:hover:text-primary-dark transition-colors no-underline">Impressum</a>
            <span>&copy; {{ site.title }}</span>
        </nav>
    </div>
</footer>

<script>
    (() => {
        const btn  = document.getElementById('themeBtn');
        const sun  = document.getElementById('iconSun');
        const moon = document.getElementById('iconMoon');
        const html = document.documentElement;

        const syncIcons = () => {
            const dark = html.classList.contains('dark');
            sun.classList.toggle('hidden', !dark);
            moon.classList.toggle('hidden', dark);
        };

        syncIcons();

        btn.addEventListener('click', () => {
            const dark = html.classList.toggle('dark');
            localStorage.setItem('theme', dark ? 'dark' : 'light');
            syncIcons();
        });
    })();
</script>
</body>
</html>
)";

/** 
 * \brief Default page template for `guss init`
 * 
 * \details 
 * This is a simple HTML template for individual pages/posts using Tailwind CSS for styling. It includes a responsive navbar,
 * a hero section with the post title and metadata, and a content area where the post body will be rendered. The template
 * demonstrates how to access the "site" and "page" variables, render content, and include metadata. Users can customize this
 * template or create their own. The "site" variable is populated from the configuration file, while the "page" variable
 * contains data specific to the current page/post being rendered.
 * 
 * \note In a real project, you might want to split this into separate template files (e.g., header.html, footer.html) and include them as needed.
 * For simplicity, it's included as a single template here. It is copied to templates/page.html on `guss init`.
 */
constexpr std::string_view DEFAULT_PAGE_TEMPLATE = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{{ page.title }} - {{ site.title }}</title>
        <!-- Fonts -->
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:ital,opsz,wght@0,14..32,300;0,14..32,400;0,14..32,500;0,14..32,600;0,14..32,700;0,14..32,800&display=swap">
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:ital,wght@0,400;0,500;1,400&display=swap">
    <link rel="stylesheet" href="/assets/style.css">
    <script src="https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4"></script>
    <style type="text/tailwindcss">
        @variant dark (&:where(.dark, .dark *));
        @theme {
            --color-surface: oklch(0.98 0.002 260); --color-surface-alt: oklch(0.94 0.006 260); --color-surface-elevated: oklch(1 0 0);
            --color-on-surface: oklch(0.40 0.016 260); --color-on-surface-strong: oklch(0.12 0.010 260); --color-on-surface-muted: oklch(0.60 0.012 260);
            --color-primary: oklch(0.52 0.22 265); --color-primary-light: oklch(0.65 0.18 265); --color-on-primary: oklch(1 0 0);
            --color-accent: oklch(0.72 0.20 35); --color-outline: oklch(0.88 0.006 260); --color-outline-strong: oklch(0.72 0.014 260);
            --color-surface-dark: oklch(0.10 0.018 265); --color-surface-dark-alt: oklch(0.15 0.022 265); --color-surface-dark-elevated: oklch(0.18 0.020 265);
            --color-on-surface-dark: oklch(0.65 0.016 265); --color-on-surface-dark-strong: oklch(0.96 0.005 265); --color-on-surface-dark-muted: oklch(0.45 0.012 265);
            --color-primary-dark: oklch(0.74 0.18 265); --color-on-primary-dark: oklch(0.10 0.018 265);
            --color-accent-dark: oklch(0.80 0.18 35); --color-outline-dark: oklch(0.24 0.018 265); --color-outline-dark-strong: oklch(0.38 0.022 265);
            --radius-radius: var(--radius-xl); --font-sans: 'Inter', system-ui, sans-serif; --font-heading: 'Inter', system-ui, sans-serif;
        }
        html { font-family: var(--font-sans); }

        [x-cloak] { display: none !important; }
    </style>
    <script>(() => { const s = localStorage.getItem('theme'); if (s === 'dark' || (!s && window.matchMedia('(prefers-color-scheme: dark)').matches)) document.documentElement.classList.add('dark'); })();</script>
    <script defer src="https://cdn.jsdelivr.net/npm/@alpinejs/collapse@3.x.x/dist/cdn.min.js"></script>
    <script defer src="https://cdn.jsdelivr.net/npm/alpinejs@3.x.x/dist/cdn.min.js"></script>

</head>
<body class="bg-surface dark:bg-surface-dark text-on-surface dark:text-on-surface-dark transition-colors duration-300">

<nav x-data="{ open: false }" x-on:click.away="open = false"
     class="fixed top-0 inset-x-0 z-50 border-b border-outline/50 dark:border-outline-dark/50 bg-surface/80 dark:bg-surface-dark/80 backdrop-blur-xl">
    <div class="mx-auto max-w-7xl px-6 flex h-16 items-center justify-between gap-6">
        <a href="/" class="font-thin tracking-tight text-2xl italic text-on-surface-strong dark:text-on-surface-dark-strong hover:text-primary dark:hover:text-primary-dark transition-colors">{{ site.title }}</a>
        <div class="flex items-center gap-3">
            <button id="themeBtn" type="button" aria-label="Toggle theme" class="size-9 rounded-full border border-outline dark:border-outline-dark bg-surface-alt dark:bg-surface-dark-alt grid place-items-center text-on-surface dark:text-on-surface-dark hover:border-primary dark:hover:border-primary-dark hover:text-primary dark:hover:text-primary-dark transition-all hover:scale-110">
                <svg id="iconSun" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.75" stroke="currentColor" class="size-[1.1rem] hidden"><path stroke-linecap="round" stroke-linejoin="round" d="M12 3v2m0 14v2M3 12H1m22 0h-2m-2.636-6.364-1.414 1.414M6.05 17.95l-1.414 1.414m0-12.728 1.414 1.414M17.95 17.95l1.414 1.414M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8Z"/></svg>
                <svg id="iconMoon" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.75" stroke="currentColor" class="size-[1.1rem]"><path stroke-linecap="round" stroke-linejoin="round" d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79Z"/></svg>
            </button>
        </div>
    </div>
</nav>

<div class="pt-28 pb-16 px-6 relative overflow-hidden">
    <div class="absolute -top-40 -right-40 w-96 h-96 rounded-full bg-primary/8 dark:bg-primary-dark/10 blur-3xl pointer-events-none -z-10"></div>
    <div class="mx-auto max-w-7xl">
        <h1 class="font-thin tracking-tight text-5xl sm:text-6xl text-on-surface-strong dark:text-on-surface-dark-strong leading-none tracking-tight mb-10 pb-8 border-b border-outline dark:border-outline-dark">
            {{ page.title }}
        </h1>
        <div class="prose text-on-surface dark:text-on-surface-dark">
            {{ page.content | safe }}
        </div>
    </div>
</div>

<footer class="border-t border-outline dark:border-outline-dark mt-8">
    <div class="mx-auto max-w-7xl px-6 py-8 flex flex-col sm:flex-row items-center justify-between gap-6 text-xs text-on-surface-muted dark:text-on-surface-dark-muted">
        <span class="font-semibold text-on-surface-strong dark:text-on-surface-dark-strong">{{ site.title }}</span>
        <nav class="flex items-center gap-6 flex-wrap justify-center sm:justify-end">
            <a href="/datenschutz/" class="hover:text-primary dark:hover:text-primary-dark transition-colors no-underline">Datenschutz</a>
            <a href="/impressum/"   class="hover:text-primary dark:hover:text-primary-dark transition-colors no-underline">Impressum</a>
            <span>&copy; {{ site.title }}</span>
        </nav>
    </div>
</footer>

<script>
    (() => {
        const btn = document.getElementById('themeBtn'), sun = document.getElementById('iconSun'), moon = document.getElementById('iconMoon'), html = document.documentElement;
        const sync = () => { const d = html.classList.contains('dark'); sun.classList.toggle('hidden', !d); moon.classList.toggle('hidden', d); };
        sync();
        btn.addEventListener('click', () => { const d = html.classList.toggle('dark'); localStorage.setItem('theme', d ? 'dark' : 'light'); sync(); });
    })();
</script>
</body>
</html>
)";

/** 
 * \brief Default page template for `guss init`
 * 
 * \details 
 * This is a simple HTML template for individual pages/posts using Tailwind CSS for styling. It includes a responsive navbar,
 * a hero section with the post title and metadata, and a content area where the post body will be rendered. The template
 * demonstrates how to access the "site" and "page" variables, render content, and include metadata. Users can customize this
 * template or create their own. The "site" variable is populated from the configuration file, while the "page" variable
 * contains data specific to the current page/post being rendered.
 * 
 * \note In a real project, you might want to split this into separate template files (e.g., header.html, footer.html) and include them as needed.
 * For simplicity, it's included as a single template here. It is copied to templates/page.html on `guss init`.
 */
constexpr std::string_view DEFAULT_TAG_TEMPLATE = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{{ tag.name }} - {{ site.title }}</title>
        <!-- Fonts -->
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:ital,opsz,wght@0,14..32,300;0,14..32,400;0,14..32,500;0,14..32,600;0,14..32,700;0,14..32,800&display=swap">
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:ital,wght@0,400;0,500;1,400&display=swap">
    <link rel="stylesheet" href="/assets/style.css">
    <script src="https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4"></script>
    <style type="text/tailwindcss">
        @variant dark (&:where(.dark, .dark *));
        @theme {
            --color-surface: oklch(0.98 0.002 260); --color-surface-alt: oklch(0.94 0.006 260); --color-surface-elevated: oklch(1 0 0);
            --color-on-surface: oklch(0.40 0.016 260); --color-on-surface-strong: oklch(0.12 0.010 260); --color-on-surface-muted: oklch(0.60 0.012 260);
            --color-primary: oklch(0.52 0.22 265); --color-primary-light: oklch(0.65 0.18 265); --color-on-primary: oklch(1 0 0);
            --color-accent: oklch(0.72 0.20 35); --color-outline: oklch(0.88 0.006 260); --color-outline-strong: oklch(0.72 0.014 260);
            --color-surface-dark: oklch(0.10 0.018 265); --color-surface-dark-alt: oklch(0.15 0.022 265); --color-surface-dark-elevated: oklch(0.18 0.020 265);
            --color-on-surface-dark: oklch(0.65 0.016 265); --color-on-surface-dark-strong: oklch(0.96 0.005 265); --color-on-surface-dark-muted: oklch(0.45 0.012 265);
            --color-primary-dark: oklch(0.74 0.18 265); --color-on-primary-dark: oklch(0.10 0.018 265);
            --color-accent-dark: oklch(0.80 0.18 35); --color-outline-dark: oklch(0.24 0.018 265); --color-outline-dark-strong: oklch(0.38 0.022 265);
            --radius-radius: var(--radius-xl); --font-sans: 'Inter', system-ui, sans-serif; --font-heading: 'Inter', system-ui, sans-serif;
        }
        html { font-family: var(--font-sans); }

        [x-cloak] { display: none !important; }
    </style>
    <script>(() => { const s = localStorage.getItem('theme'); if (s === 'dark' || (!s && window.matchMedia('(prefers-color-scheme: dark)').matches)) document.documentElement.classList.add('dark'); })();</script>
    <script defer src="https://cdn.jsdelivr.net/npm/@alpinejs/collapse@3.x.x/dist/cdn.min.js"></script>
    <script defer src="https://cdn.jsdelivr.net/npm/alpinejs@3.x.x/dist/cdn.min.js"></script>

</head>
<body class="bg-surface dark:bg-surface-dark text-on-surface dark:text-on-surface-dark transition-colors duration-300">

<nav x-data="{ open: false }" x-on:click.away="open = false"
     class="fixed top-0 inset-x-0 z-50 border-b border-outline/50 dark:border-outline-dark/50 bg-surface/80 dark:bg-surface-dark/80 backdrop-blur-xl">
    <div class="mx-auto max-w-7xl px-6 flex h-16 items-center justify-between gap-6">
        <a href="/" class="font-thin tracking-tight text-2xl italic text-on-surface-strong dark:text-on-surface-dark-strong hover:text-primary dark:hover:text-primary-dark transition-colors">{{ site.title }}</a>
        <div class="flex items-center gap-3">
            <button id="themeBtn" type="button" aria-label="Toggle theme" class="size-9 rounded-full border border-outline dark:border-outline-dark bg-surface-alt dark:bg-surface-dark-alt grid place-items-center text-on-surface dark:text-on-surface-dark hover:border-primary dark:hover:border-primary-dark hover:text-primary dark:hover:text-primary-dark transition-all hover:scale-110">
                <svg id="iconSun" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.75" stroke="currentColor" class="size-[1.1rem] hidden"><path stroke-linecap="round" stroke-linejoin="round" d="M12 3v2m0 14v2M3 12H1m22 0h-2m-2.636-6.364-1.414 1.414M6.05 17.95l-1.414 1.414m0-12.728 1.414 1.414M17.95 17.95l1.414 1.414M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8Z"/></svg>
                <svg id="iconMoon" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.75" stroke="currentColor" class="size-[1.1rem]"><path stroke-linecap="round" stroke-linejoin="round" d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79Z"/></svg>
            </button>
        </div>
    </div>
</nav>

<!-- Tag hero -->
<div class="pt-16 relative overflow-hidden">
    <div class="absolute inset-0 -z-10 bg-gradient-to-br from-primary/15 via-transparent to-accent/10 dark:from-primary-dark/20 dark:to-accent-dark/10"></div>
    <div class="mx-auto max-w-7xl px-6 py-20">
        <p class="text-[0.65rem] font-bold uppercase tracking-[0.2em] text-primary dark:text-primary-dark mb-4">Tag</p>
        <div class="flex flex-wrap items-end gap-4 mb-3">
            <h1 class="font-thin tracking-tight text-6xl sm:text-7xl text-on-surface-strong dark:text-on-surface-dark-strong leading-none tracking-tight">{{ tag.name }}</h1>
            <span class="px-3 py-1 rounded-full bg-primary/10 dark:bg-primary-dark/20 text-primary dark:text-primary-dark text-sm font-semibold mb-2">{{ posts | length }} posts</span>
        </div>
        {% if tag.description %}<p class="text-on-surface-muted dark:text-on-surface-dark-muted max-w-lg">{{ tag.description }}</p>{% endif %}
    </div>
</div>

<!-- Post grid -->
<main class="mx-auto max-w-7xl px-6 py-10 pb-20">
    <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-6">
        {% for post in posts %}
        <a href="{{ post.permalink }}"
           class="group flex flex-col rounded-2xl overflow-hidden border border-outline dark:border-outline-dark bg-surface-elevated dark:bg-surface-dark-elevated hover:border-primary/50 dark:hover:border-primary-dark/50 hover:shadow-xl hover:shadow-primary/5 transition-all duration-300 no-underline">
            <div class="aspect-video bg-gradient-to-br from-primary/20 to-accent/30 dark:from-primary-dark/20 dark:to-accent-dark/20 overflow-hidden">
                {% if post.feature_image %}
                <img src="{{ post.feature_image }}" alt="{{ post.title }}" class="w-full h-full object-cover group-hover:scale-105 transition-transform duration-500">
                {% endif %}
            </div>
            <div class="flex flex-col flex-1 p-5">
                <time datetime="{{ post.published_at }}" class="text-[0.65rem] font-bold uppercase tracking-[0.14em] text-on-surface-muted dark:text-on-surface-dark-muted mb-2">{{ post.published_at | date("%d.%m.%Y") }}</time>
                <h2 class="font-thin tracking-tight text-lg text-on-surface-strong dark:text-on-surface-dark-strong leading-snug group-hover:text-primary dark:group-hover:text-primary-dark transition-colors flex-1">{{ post.title }}</h2>
                <div class="mt-4 flex items-center gap-1 text-xs font-semibold text-primary dark:text-primary-dark">
                    Read
                    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" class="size-3 group-hover:translate-x-1 transition-transform"><path fill-rule="evenodd" d="M6.22 4.22a.75.75 0 0 1 1.06 0l3.25 3.25a.75.75 0 0 1 0 1.06l-3.25 3.25a.75.75 0 0 1-1.06-1.06L8.94 8 6.22 5.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/></svg>
                </div>
            </div>
        </a>
        {% endfor %}
    </div>
</main>

<footer class="border-t border-outline dark:border-outline-dark mt-8">
    <div class="mx-auto max-w-7xl px-6 py-8 flex flex-col sm:flex-row items-center justify-between gap-6 text-xs text-on-surface-muted dark:text-on-surface-dark-muted">
        <span class="font-semibold text-on-surface-strong dark:text-on-surface-dark-strong">{{ site.title }}</span>
        <nav class="flex items-center gap-6 flex-wrap justify-center sm:justify-end">
            <a href="/datenschutz/" class="hover:text-primary dark:hover:text-primary-dark transition-colors no-underline">Datenschutz</a>
            <a href="/impressum/"   class="hover:text-primary dark:hover:text-primary-dark transition-colors no-underline">Impressum</a>
            <span>&copy; {{ site.title }}</span>
        </nav>
    </div>
</footer>

<script>
    (() => {
        const btn = document.getElementById('themeBtn'), sun = document.getElementById('iconSun'), moon = document.getElementById('iconMoon'), html = document.documentElement;
        const sync = () => { const d = html.classList.contains('dark'); sun.classList.toggle('hidden', !d); moon.classList.toggle('hidden', d); };
        sync();
        btn.addEventListener('click', () => { const d = html.classList.toggle('dark'); localStorage.setItem('theme', d ? 'dark' : 'light'); sync(); });
    })();
</script>
</body>
</html>
)";

/** 
 * \brief Default page template for `guss init`
 * 
 * \details 
 * This is a simple HTML template for individual pages/posts using Tailwind CSS for styling. It includes a responsive navbar,
 * a hero section with the post title and metadata, and a content area where the post body will be rendered. The template
 * demonstrates how to access the "site" and "page" variables, render content, and include metadata. Users can customize this
 * template or create their own. The "site" variable is populated from the configuration file, while the "page" variable
 * contains data specific to the current page/post being rendered.
 * 
 * \note In a real project, you might want to split this into separate template files (e.g., header.html, footer.html) and include them as needed.
 * For simplicity, it's included as a single template here. It is copied to templates/page.html on `guss init`.
 */
constexpr std::string_view DEFAULT_AUTHOR_TEMPLATE = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{{ author.name }} - {{ site.title }}</title>
        <!-- Fonts -->
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:ital,opsz,wght@0,14..32,300;0,14..32,400;0,14..32,500;0,14..32,600;0,14..32,700;0,14..32,800&display=swap">
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:ital,wght@0,400;0,500;1,400&display=swap">
    <link rel="stylesheet" href="/assets/style.css">
    <script src="https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4"></script>
    <style type="text/tailwindcss">
        @variant dark (&:where(.dark, .dark *));
        @theme {
            --color-surface: oklch(0.98 0.002 260); --color-surface-alt: oklch(0.94 0.006 260); --color-surface-elevated: oklch(1 0 0);
            --color-on-surface: oklch(0.40 0.016 260); --color-on-surface-strong: oklch(0.12 0.010 260); --color-on-surface-muted: oklch(0.60 0.012 260);
            --color-primary: oklch(0.52 0.22 265); --color-primary-light: oklch(0.65 0.18 265); --color-on-primary: oklch(1 0 0);
            --color-accent: oklch(0.72 0.20 35); --color-outline: oklch(0.88 0.006 260); --color-outline-strong: oklch(0.72 0.014 260);
            --color-surface-dark: oklch(0.10 0.018 265); --color-surface-dark-alt: oklch(0.15 0.022 265); --color-surface-dark-elevated: oklch(0.18 0.020 265);
            --color-on-surface-dark: oklch(0.65 0.016 265); --color-on-surface-dark-strong: oklch(0.96 0.005 265); --color-on-surface-dark-muted: oklch(0.45 0.012 265);
            --color-primary-dark: oklch(0.74 0.18 265); --color-on-primary-dark: oklch(0.10 0.018 265);
            --color-accent-dark: oklch(0.80 0.18 35); --color-outline-dark: oklch(0.24 0.018 265); --color-outline-dark-strong: oklch(0.38 0.022 265);
            --radius-radius: var(--radius-xl); --font-sans: 'Inter', system-ui, sans-serif; --font-heading: 'Inter', system-ui, sans-serif;
        }
        html { font-family: var(--font-sans); }

        [x-cloak] { display: none !important; }
    </style>
    <script>(() => { const s = localStorage.getItem('theme'); if (s === 'dark' || (!s && window.matchMedia('(prefers-color-scheme: dark)').matches)) document.documentElement.classList.add('dark'); })();</script>
    <script defer src="https://cdn.jsdelivr.net/npm/@alpinejs/collapse@3.x.x/dist/cdn.min.js"></script>
    <script defer src="https://cdn.jsdelivr.net/npm/alpinejs@3.x.x/dist/cdn.min.js"></script>

</head>
<body class="bg-surface dark:bg-surface-dark text-on-surface dark:text-on-surface-dark transition-colors duration-300">

<nav x-data="{ open: false }" x-on:click.away="open = false"
     class="fixed top-0 inset-x-0 z-50 border-b border-outline/50 dark:border-outline-dark/50 bg-surface/80 dark:bg-surface-dark/80 backdrop-blur-xl">
    <div class="mx-auto max-w-7xl px-6 flex h-16 items-center justify-between gap-6">
        <a href="/" class="font-thin tracking-tight text-2xl italic text-on-surface-strong dark:text-on-surface-dark-strong hover:text-primary dark:hover:text-primary-dark transition-colors">{{ site.title }}</a>
        <div class="flex items-center gap-3">
            <button id="themeBtn" type="button" aria-label="Toggle theme" class="size-9 rounded-full border border-outline dark:border-outline-dark bg-surface-alt dark:bg-surface-dark-alt grid place-items-center text-on-surface dark:text-on-surface-dark hover:border-primary dark:hover:border-primary-dark hover:text-primary dark:hover:text-primary-dark transition-all hover:scale-110">
                <svg id="iconSun" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.75" stroke="currentColor" class="size-[1.1rem] hidden"><path stroke-linecap="round" stroke-linejoin="round" d="M12 3v2m0 14v2M3 12H1m22 0h-2m-2.636-6.364-1.414 1.414M6.05 17.95l-1.414 1.414m0-12.728 1.414 1.414M17.95 17.95l1.414 1.414M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8Z"/></svg>
                <svg id="iconMoon" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.75" stroke="currentColor" class="size-[1.1rem]"><path stroke-linecap="round" stroke-linejoin="round" d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79Z"/></svg>
            </button>
        </div>
    </div>
</nav>

<!-- Author hero -->
<div class="pt-16 relative overflow-hidden">
    <div class="absolute inset-0 -z-10 bg-gradient-to-b from-primary/10 via-transparent to-transparent dark:from-primary-dark/15"></div>
    <div class="mx-auto max-w-7xl px-6 py-20 flex flex-col sm:flex-row items-start sm:items-end gap-8">
        {% if author.profile_image %}
        <div class="relative shrink-0">
            <div class="absolute inset-0 rounded-full bg-gradient-to-br from-primary to-accent dark:from-primary-dark dark:to-accent-dark blur-xl opacity-40 scale-110"></div>
            <img src="{{ author.profile_image }}" alt="{{ author.name }}"
                 class="relative size-28 rounded-full object-cover border-2 border-surface dark:border-surface-dark ring-2 ring-primary/30 dark:ring-primary-dark/30">
        </div>
        {% else %}
        <div class="relative shrink-0">
            <div class="absolute inset-0 rounded-full bg-gradient-to-br from-primary to-accent dark:from-primary-dark dark:to-accent-dark blur-xl opacity-40 scale-110"></div>
            <div class="relative size-28 rounded-full bg-gradient-to-br from-primary to-accent dark:from-primary-dark dark:to-accent-dark flex items-center justify-center text-white font-heading text-4xl">
                {{ author.name | truncate(1, false, '') }}
            </div>
        </div>
        {% endif %}
        <div>
            <p class="text-[0.65rem] font-bold uppercase tracking-[0.2em] text-primary dark:text-primary-dark mb-2">Author</p>
            <h1 class="font-thin tracking-tight text-5xl sm:text-6xl text-on-surface-strong dark:text-on-surface-dark-strong leading-none tracking-tight mb-3">{{ author.name }}</h1>
            {% if author.bio %}<p class="text-on-surface-muted dark:text-on-surface-dark-muted max-w-lg">{{ author.bio }}</p>{% endif %}
        </div>
    </div>
</div>

<!-- Posts grid -->
<main class="mx-auto max-w-7xl px-6 pb-20">
    <p class="text-[0.65rem] font-bold uppercase tracking-[0.2em] text-on-surface-muted dark:text-on-surface-dark-muted mb-6">Posts</p>
    <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-6">
        {% for post in posts %}
        <a href="{{ post.permalink }}"
           class="group flex flex-col rounded-2xl overflow-hidden border border-outline dark:border-outline-dark bg-surface-elevated dark:bg-surface-dark-elevated hover:border-primary/50 dark:hover:border-primary-dark/50 hover:shadow-xl hover:shadow-primary/5 transition-all duration-300 no-underline">
            <div class="aspect-video bg-gradient-to-br from-primary/20 to-accent/30 dark:from-primary-dark/20 dark:to-accent-dark/20 overflow-hidden">
                {% if post.feature_image %}
                <img src="{{ post.feature_image }}" alt="{{ post.title }}" class="w-full h-full object-cover group-hover:scale-105 transition-transform duration-500">
                {% endif %}
            </div>
            <div class="flex flex-col flex-1 p-5">
                <time datetime="{{ post.published_at }}" class="text-[0.65rem] font-bold uppercase tracking-[0.14em] text-on-surface-muted dark:text-on-surface-dark-muted mb-2">{{ post.published_at | date("%d.%m.%Y") }}</time>
                <h2 class="font-thin tracking-tight text-lg text-on-surface-strong dark:text-on-surface-dark-strong leading-snug group-hover:text-primary dark:group-hover:text-primary-dark transition-colors flex-1">{{ post.title }}</h2>
                <div class="mt-4 flex items-center gap-1 text-xs font-semibold text-primary dark:text-primary-dark">
                    Read <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" class="size-3 group-hover:translate-x-1 transition-transform"><path fill-rule="evenodd" d="M6.22 4.22a.75.75 0 0 1 1.06 0l3.25 3.25a.75.75 0 0 1 0 1.06l-3.25 3.25a.75.75 0 0 1-1.06-1.06L8.94 8 6.22 5.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/></svg>
                </div>
            </div>
        </a>
        {% endfor %}
    </div>
</main>

<footer class="border-t border-outline dark:border-outline-dark mt-8">
    <div class="mx-auto max-w-7xl px-6 py-8 flex flex-col sm:flex-row items-center justify-between gap-6 text-xs text-on-surface-muted dark:text-on-surface-dark-muted">
        <span class="font-semibold text-on-surface-strong dark:text-on-surface-dark-strong">{{ site.title }}</span>
        <nav class="flex items-center gap-6 flex-wrap justify-center sm:justify-end">
            <a href="/datenschutz/" class="hover:text-primary dark:hover:text-primary-dark transition-colors no-underline">Datenschutz</a>
            <a href="/impressum/"   class="hover:text-primary dark:hover:text-primary-dark transition-colors no-underline">Impressum</a>
            <span>&copy; {{ site.title }}</span>
        </nav>
    </div>
</footer>

<script>
    (() => {
        const btn = document.getElementById('themeBtn'), sun = document.getElementById('iconSun'), moon = document.getElementById('iconMoon'), html = document.documentElement;
        const sync = () => { const d = html.classList.contains('dark'); sun.classList.toggle('hidden', !d); moon.classList.toggle('hidden', d); };
        sync();
        btn.addEventListener('click', () => { const d = html.classList.toggle('dark'); localStorage.setItem('theme', d ? 'dark' : 'light'); sync(); });
    })();
</script>
</body>
</html>
)";

/** 
 * \brief Default CSS for `guss init`
 * 
 * \details 
 * This CSS provides basic styling for the generated site, including typography, layout, and components like code blocks and blockquotes. It uses modern CSS
 * features and is designed to work well with the default HTML templates. Users can customize this CSS or create their own to achieve the desired look and feel
 * for their site. It is copied to assets/style.css on `guss init`. The CSS includes styles for the "prose" class, which is used in the page template to style
 * the content of posts/pages. It also includes styles for hiding scrollbars and an Alpine.js utility for hiding elements until they are needed.
 * 
 * \note It is copied to assets/style.css on `guss init`. You can modify it to change the appearance of your site.
 */
constexpr std::string_view DEFAULT_CSS = R"(html, body {
    min-height: 100lvh;
}

body {
    display: flex;
    flex-direction: column;
}

main, body > div {
    flex: 1;
}

/* ── Alpine cloak ─────────────────────────────── */
[x-cloak] { display: none !important; }

/* ── Hide scrollbars (topics bar etc.) ───────── */
.scrollbar-none {
    scrollbar-width: none;
    -ms-overflow-style: none;
}
.scrollbar-none::-webkit-scrollbar {
    display: none;
}

/* ── Prose (post/page body) ───────────────────── */
.prose {
    font-size: 1.0625rem;
    line-height: 1.8;
    font-family: 'Inter', system-ui, sans-serif;
}
.prose h2,
.prose h3,
.prose h4 {
    font-family: 'Inter', system-ui, sans-serif;
    font-weight: 700;
    letter-spacing: -0.02em;
    margin-top: 2.25rem;
    margin-bottom: 0.6rem;
    line-height: 1.2;
    color: inherit;
}
.prose h2 { font-size: 1.6rem; }
.prose h3 { font-size: 1.25rem; }
.prose p  { margin-bottom: 1.25rem; }
.prose a  { text-decoration: underline; text-underline-offset: 3px; }
.prose strong { font-weight: 600; }
.prose em { font-style: italic; }
.prose blockquote {
    border-left: 2px solid currentColor;
    opacity: 0.7;
    padding: 0.5rem 1.25rem;
    font-style: italic;
    margin: 1.75rem 0;
}
.prose pre {
    padding: 1.25rem;
    border-radius: 0.75rem;
    overflow-x: auto;
    font-size: 0.85rem;
    margin: 1.75rem 0;
}
.prose code {
    font-family: 'JetBrains Mono', 'Fira Code', monospace;
    font-size: 0.85em;
    padding: 0.15em 0.4em;
    border-radius: 4px;
}
.prose pre code { padding: 0; background: transparent; }
.prose img {
    max-width: 100%;
    height: auto;
    border-radius: 0.75rem;
    margin: 1.75rem auto;
    display: block;
    box-shadow: 0 2px 25px -4px rgb(0 0 0 / 22%), 0 0 1px rgb(83 72 72 / 67%);
}
.dark .prose img { box-shadow: 0 2px 25px -4px rgb(0 0 0 / 22%), 0 0 1px rgb(83 72 72 / 67%); }
.prose ul  { padding-left: 1.5rem; list-style: disc;    margin-bottom: 1.25rem; }
.prose ol  { padding-left: 1.5rem; list-style: decimal; margin-bottom: 1.25rem; }
.prose li  { margin-bottom: 0.4rem; }
.prose hr  { border: none; border-top: 1px solid; opacity: 0.15; margin: 2.5rem 0; }
.prose table { width: 100%; border-collapse: collapse; margin: 1.75rem 0; font-size: 0.9rem; }
.prose th, .prose td { padding: 0.6rem 0.9rem; text-align: left; border-bottom: 1px solid; }
.prose th { font-weight: 600; opacity: 0.65; }

/* ── Code blocks ──────────────────────────────── */
.prose pre {
    background: oklch(0.93 0.008 260);
    border: 1px solid oklch(0.86 0.010 260);
    border-radius: 0.875rem;
    overflow: hidden;
    margin: 1.75rem 0;
    padding: 0;          /* padding lives on code/hljs, not pre */
    box-shadow: 0 2px 25px -4px rgb(0 0 0 / 22%), 0 0 1px rgb(83 72 72 / 67%);
}
.dark .prose pre {
    background: oklch(0.14 0.018 265);
    border-color: oklch(0.22 0.018 265);
    box-shadow: 0 2px 25px -4px rgb(0 0 0 / 22%), 0 0 1px rgb(83 72 72 / 67%);
}

/* hljs adds .hljs to the <code> element — let it own padding & font */
.hljs {
    background: transparent !important;  /* our pre provides the bg */
    border-radius: 0 !important;
    font-size: 0.84rem !important;
    line-height: 1.65 !important;
    padding: 1.25rem 1.5rem !important;
    font-family: 'JetBrains Mono', 'Fira Code', ui-monospace, monospace !important;
}

/* Fallback for non-highlighted pre code (no hljs class) */
.prose pre > code:not(.hljs) {
    display: block;
    padding: 1.25rem 1.5rem;
    font-size: 0.84rem;
    line-height: 1.65;
    font-family: 'JetBrains Mono', 'Fira Code', ui-monospace, monospace;
    background: transparent;
    color: inherit;
}

/* ── Inline code ──────────────────────────────── */
:not(pre) > code {
    font-family: 'JetBrains Mono', 'Fira Code', ui-monospace, monospace;
    font-size: 0.82em;
    padding: 0.2em 0.5em;
    border-radius: 0.375rem;
    background: oklch(0.91 0.010 260);
    border: 1px solid oklch(0.84 0.012 260);
    color: oklch(0.38 0.14 265);
}
.dark :not(pre) > code {
    background: oklch(0.18 0.022 265);
    border-color: oklch(0.26 0.022 265);
    color: oklch(0.80 0.14 265);
}

/* ── Ghost bookmark card (kg-bookmark) ───────── */
.kg-bookmark-container {
    display: flex;
    border: 1px solid oklch(0.86 0.010 260);
    border-radius: 0.875rem;
    overflow: hidden;
    text-decoration: none !important;
    color: inherit !important;
    transition: border-color 0.15s, box-shadow 0.15s;
    margin: 1.75rem 0;
    background: oklch(0.97 0.003 260);
}
.dark .kg-bookmark-container {
    border-color: oklch(0.22 0.018 265);
    background: oklch(0.15 0.020 265);
}
.kg-bookmark-container:hover {
    border-color: oklch(0.52 0.22 265);
    box-shadow: 0 0 0 3px oklch(0.52 0.22 265 / 0.12);
}
.dark .kg-bookmark-container:hover {
    border-color: oklch(0.74 0.18 265);
    box-shadow: 0 0 0 3px oklch(0.74 0.18 265 / 0.14);
}

.kg-bookmark-content {
    flex: 1;
    padding: 1rem 1.25rem;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
}

.kg-bookmark-title {
    font-size: 0.9rem;
    font-weight: 600;
    letter-spacing: -0.01em;
    line-height: 1.3;
    color: oklch(0.14 0.010 260);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}
.dark .kg-bookmark-title {
    color: oklch(0.94 0.005 265);
}

.kg-bookmark-description {
    font-size: 0.78rem;
    line-height: 1.5;
    color: oklch(0.50 0.012 260);
    display: -webkit-box;
    -webkit-line-clamp: 2;
    -webkit-box-orient: vertical;
    overflow: hidden;
}
.dark .kg-bookmark-description {
    color: oklch(0.55 0.012 265);
}

.kg-bookmark-metadata {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    margin-top: 0.5rem;
    font-size: 0.72rem;
    color: oklch(0.55 0.010 260);
    white-space: nowrap;
    overflow: hidden;
}
.dark .kg-bookmark-metadata {
    color: oklch(0.50 0.012 265);
}

.kg-bookmark-icon {
    width: 1rem;
    height: 1rem;
    object-fit: contain;
    flex-shrink: 0;
}

.kg-bookmark-publisher {
    font-weight: 500;
    flex-shrink: 0;
}

.kg-bookmark-author::before {
    content: '·';
    margin-right: 0.35rem;
    opacity: 0.5;
}

.kg-bookmark-thumbnail {
    width: 9rem;
    flex-shrink: 0;
    overflow: hidden;
}
.kg-bookmark-thumbnail img {
    width: 100%;
    height: 100%;
    object-fit: cover;
    margin: 0 !important;
    border-radius: 0 !important;
    display: block;
}

@media (max-width: 640px) {
    .kg-bookmark-thumbnail { display: none; }
}
/* ── Download button (Ghost file card) ───────── */
.prose button {
    display: inline-flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.5rem 1.1rem;
    border-radius: 0.625rem;
    border: 1px solid oklch(0.86 0.010 260);
    background: oklch(0.97 0.003 260);
    color: oklch(0.40 0.016 260);
    font-family: 'Inter', system-ui, sans-serif;
    font-size: 0.82rem;
    font-weight: 500;
    cursor: pointer;
    transition: border-color 0.15s, background 0.15s, color 0.15s, box-shadow 0.15s;
    white-space: nowrap;
}
.prose button:hover {
    border-color: oklch(0.52 0.22 265);
    background: oklch(0.52 0.22 265);
    color: oklch(1 0 0);
    box-shadow: 0 2px 12px oklch(0.52 0.22 265 / 0.25);
}
.prose .dark button {
    border-color: oklch(0.26 0.018 265);
    background: oklch(0.16 0.020 265);
    color: oklch(0.65 0.016 265);
}
.prose .dark button:hover {
    border-color: oklch(0.74 0.18 265);
    background: oklch(0.74 0.18 265);
    color: oklch(0.10 0.018 265);
    box-shadow: 0 2px 12px oklch(0.74 0.18 265 / 0.25);
}
.prose button:has(svg) svg {
    flex-shrink: 0;
}
.hl-container.flex-container {
    display: flex;
    flex-wrap: wrap;
    gap: 1em;
    border-radius: 1em;
}
.hl-container.flex-container > div {
    flex: 1;
    min-width: 420px;
    padding: 1em;
}
)";

}
