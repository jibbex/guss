/**
 * @file main.cpp
 * @brief CLI entry point for Guss SSG.
 *
 * @details
 * Commands:
 * - guss init [directory] - Scaffold a new Guss project
 * - guss build [-c config] [-v] - Run the full build pipeline
 * - guss ping [-c config] - Test CMS connection
 * - guss clean [-c config] - Clean output directory
 */

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <indicators/progress_bar.hpp>
#include <indicators/cursor_control.hpp>

#include "guss/adapters/ghost/ghost_adapter.hpp"
#include "guss/builder/pipeline.hpp"
#include "guss/core/config.hpp"
#include "guss/core/error.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// Default configuration template
constexpr std::string_view DEFAULT_CONFIG = R"(# Guss Static Site Generator Configuration

site:
  title: "My Site"
  description: "A site built with Guss"
  url: "https://example.com"
  language: "en"
  # logo: "/images/logo.png"
  # twitter: "@myhandle"

# Content source configuration
# Supported types: ghost, wordpress, markdown
source:
  type: ghost
  api_url: "https://demo.ghost.io"
  content_api_key: "22444f78447824223cefc48062"
  # admin_api_key: ""  # Optional, for private content
  timeout_ms: 30000

# URL permalink patterns
# Tokens: {slug}, {year}, {month}, {day}, {id}
permalinks:
  post: "/{year}/{month}/{slug}/"
  page: "/{slug}/"
  tag: "/tag/{slug}/"
  author: "/author/{slug}/"

# Output settings
output:
  output_dir: "./dist"
  assets_dir: "./assets"
  generate_sitemap: true
  generate_rss: true
  minify_html: false
  copy_assets: true

# Template settings
templates:
  templates_dir: "./templates"
  default_post_template: "post.html"
  default_page_template: "page.html"
  index_template: "index.html"
  tag_template: "tag.html"
  author_template: "author.html"

# Build settings
parallel_workers: 0  # 0 = auto-detect
log_level: "info"    # debug, info, warn, error
)";

// Default post template
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
                {{ post.published_at }}
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

// Default index template
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
                    <time datetime="{{ post.published_at }}" class="mt-4 block text-white/60 text-xs font-medium">{{ post.published_at }}</time>
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
                <time datetime="{{ post.published_at }}" class="text-[0.65rem] font-bold uppercase tracking-[0.14em] text-on-surface-muted dark:text-on-surface-dark-muted mb-2">{{ post.published_at }}</time>
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

// Default page template
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
    <div class="mx-auto max-w-3xl">
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

// Default tag template
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
                <time datetime="{{ post.published_at }}" class="text-[0.65rem] font-bold uppercase tracking-[0.14em] text-on-surface-muted dark:text-on-surface-dark-muted mb-2">{{ post.published_at }}</time>
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

// Default author template
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
                <time datetime="{{ post.published_at }}" class="text-[0.65rem] font-bold uppercase tracking-[0.14em] text-on-surface-muted dark:text-on-surface-dark-muted mb-2">{{ post.published_at }}</time>
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

// Default CSS
constexpr std::string_view DEFAULT_CSS = R"(/* ── Alpine cloak ─────────────────────────────── */
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
)";

void setup_logging(const std::string& level) {
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);

    if (level == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (level == "warn") {
        spdlog::set_level(spdlog::level::warn);
    } else if (level == "error") {
        spdlog::set_level(spdlog::level::err);
    } else {
        spdlog::set_level(spdlog::level::info);
    }
}

int cmd_init(const std::string& directory) {
    fs::path project_dir = directory.empty() ? fs::current_path() : fs::path(directory);

    spdlog::info("Initializing Guss project in " + project_dir.string());

    // Create directories
    for (const auto& dir : {
            project_dir,
            project_dir / "templates",
            project_dir / "templates" / "assets",
            project_dir / "content",
            project_dir / "dist"}) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            spdlog::error("Failed to create directory {}: {}", dir.string(), ec.message());
            return 1;
        }
    }

    // Write config file
    auto config_path = project_dir / "guss.yaml";
    if (!fs::exists(config_path)) {
        std::ofstream config_file(config_path);
        config_file << DEFAULT_CONFIG;
        spdlog::info("Created guss.yaml");
    }

    // Write templates
    auto write_template = [&](const std::string& name, std::string_view content) {
        auto path = project_dir / "templates" / name;
        if (!fs::exists(path)) {
            std::ofstream file(path);
            file << content;
            spdlog::info("Created templates/" + name);
        }
    };

    write_template("post.html", DEFAULT_POST_TEMPLATE);
    write_template("page.html", DEFAULT_PAGE_TEMPLATE);
    write_template("index.html", DEFAULT_INDEX_TEMPLATE);
    write_template("tag.html", DEFAULT_TAG_TEMPLATE);
    write_template("author.html", DEFAULT_AUTHOR_TEMPLATE);

    // Write CSS
    auto css_path = project_dir / "templates" / "assets" / "style.css";
    if (!fs::exists(css_path)) {
        std::ofstream css_file(css_path);
        css_file << DEFAULT_CSS;
        spdlog::info("Created templates/assets/style.css");
    }

    spdlog::info("Project initialized successfully!");
    spdlog::info("Next steps:");
    spdlog::info("  1. Edit guss.yaml to configure your content source");
    spdlog::info("  2. Customize templates in the templates/ directory");
    spdlog::info("  3. Run 'guss build' to generate your site");

    return 0;
}

int cmd_build(const std::string& config_path, bool verbose, bool clean_first) {
    setup_logging(verbose ? "debug" : "info");

    spdlog::info("Guss Static Site Generator");
    spdlog::info("Loading configuration from " + config_path);

    // Load config
    guss::config::Config config(config_path);

    // Create adapter based on config
    guss::adapters::AdapterPtr adapter;

    if (std::holds_alternative<guss::config::GhostAdapterConfig>(config.adapter())) {
        const auto& ghost_cfg = std::get<guss::config::GhostAdapterConfig>(config.adapter());
        adapter = std::make_unique<guss::adapters::GhostAdapter>(ghost_cfg);
        spdlog::info("Using Ghost adapter: " + ghost_cfg.api_url);
    } else if (std::holds_alternative<guss::config::WordPressAdapterConfig>(config.adapter())) {
        spdlog::error("WordPress adapter not yet implemented");
        return 1;
    } else {
        spdlog::error("Markdown adapter not yet implemented");
        return 1;
    }

    // Create pipeline
    guss::builder::Pipeline pipeline(
        std::move(adapter),
        config.site(),
        config.templates(),
        config.permalinks(),
        config.output()
    );

    // Clean if requested
    if (clean_first) {
        auto clean_result = pipeline.clean();
        if (!clean_result) {
            spdlog::error("Clean failed: {}", clean_result.error().format());
            return 1;
        }
    }

    // Setup progress bar
    indicators::ProgressBar bar{
        indicators::option::BarWidth{50},
        indicators::option::Start{"["},
        indicators::option::End{"]"},
        indicators::option::ForegroundColor{indicators::Color::green},
        indicators::option::ShowPercentage{true},
        indicators::option::ShowElapsedTime{true},
        indicators::option::PostfixText{"Starting..."}
    };

    indicators::show_console_cursor(false);

    // Run build
    auto result = pipeline.build([&bar](const std::string &message, float progress) {
        bar.set_progress(static_cast<size_t>(progress * 100));
        bar.set_option(indicators::option::PostfixText{message});
    });

    indicators::show_console_cursor(true);
    std::cout << std::endl;

    if (!result) {
        spdlog::error("Build failed: " + result.error().format());
        return 1;
    }

    const auto& stats = *result;
    spdlog::info("Build complete!");
    spdlog::info("  Posts:    " + std::to_string(stats.posts_rendered));
    spdlog::info("  Pages:    " + std::to_string(stats.pages_rendered));
    spdlog::info("  Tags:     " + std::to_string(stats.tag_archives_rendered));
    spdlog::info("  Authors:  " + std::to_string(stats.author_archives_rendered));
    spdlog::info("  Index:    " + std::to_string(stats.index_pages_rendered));
    spdlog::info("  Assets:   " + std::to_string(stats.assets_copied));
    if (stats.errors > 0) {
        spdlog::warn("  Errors:   " + std::to_string(stats.errors));
    }
    spdlog::info("  Duration: " + std::to_string(stats.total_duration.count()) + "ms");

    return stats.errors > 0 ? 1 : 0;
}

int cmd_ping(const std::string& config_path) {
    setup_logging("info");

    spdlog::info("Testing connection...");

    // Load config
    guss::config::Config config(config_path);

    // Create adapter
    guss::adapters::AdapterPtr adapter;

    if (std::holds_alternative<guss::config::GhostAdapterConfig>(config.adapter())) {
        const auto& ghost_cfg = std::get<guss::config::GhostAdapterConfig>(config.adapter());
        adapter = std::make_unique<guss::adapters::GhostAdapter>(ghost_cfg);
        spdlog::info("Adapter: Ghost ({})", ghost_cfg.api_url);
    } else {
        spdlog::error("Unsupported adapter type");
        return 1;
    }

    // Create minimal pipeline just for ping
    guss::builder::Pipeline pipeline(
        std::move(adapter),
        config.site(),
        config.templates(),
        config.permalinks(),
        config.output()
    );

    auto result = pipeline.ping();
    if (!result) {
        spdlog::error("Connection failed: {}", result.error().format());
        return 1;
    }

    spdlog::info("Connection successful!");
    return 0;
}

int cmd_clean(const std::string& config_path) {
    setup_logging("info");

    guss::config::Config config(config_path);

    spdlog::info("Cleaning output directory: {}", config.output().output_dir.string());

    std::error_code ec;
    if (fs::exists(config.output().output_dir, ec) && !ec) {
        fs::remove_all(config.output().output_dir, ec);
        if (ec) {
            spdlog::error("Failed to clean: {}", ec.message());
            return 1;
        }
        spdlog::info("Output directory cleaned");
    } else {
        spdlog::info("Output directory does not exist");
    }

    return 0;
}

int main(int argc, char** argv) {
    CLI::App app{"Guss - A pluggable static site generator"};
    app.require_subcommand(1);

    // Global options
    std::string config_path = "guss.yaml";

    // init subcommand
    auto init_cmd = app.add_subcommand("init", "Initialize a new Guss project");
    std::string init_dir;
    init_cmd->add_option("directory", init_dir, "Project directory (default: current)");

    // build subcommand
    auto build_cmd = app.add_subcommand("build", "Build the static site");
    bool verbose = false;
    bool clean_first = false;
    build_cmd->add_option("-c,--config", config_path, "Configuration file path");
    build_cmd->add_flag("-v,--verbose", verbose, "Enable verbose output");
    build_cmd->add_flag("--clean", clean_first, "Clean output directory first");

    // ping subcommand
    auto ping_cmd = app.add_subcommand("ping", "Test connection to content source");
    ping_cmd->add_option("-c,--config", config_path, "Configuration file path");

    // clean subcommand
    auto clean_cmd = app.add_subcommand("clean", "Clean the output directory");
    clean_cmd->add_option("-c,--config", config_path, "Configuration file path");

    CLI11_PARSE(app, argc, argv);

    if (init_cmd->parsed()) {
        return cmd_init(init_dir);
    }

    if (build_cmd->parsed()) {
        return cmd_build(config_path, verbose, clean_first);
    }

    if (ping_cmd->parsed()) {
        return cmd_ping(config_path);
    }

    if (clean_cmd->parsed()) {
        return cmd_clean(config_path);
    }

    return 0;
}
