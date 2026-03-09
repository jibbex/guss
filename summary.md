# Guss — Project Summary

**Guss** (*der Guss* — German for "the casting") is a pluggable static site generator written in C++23. It fetches content from any supported CMS, casts it through Jinja2-style templates, and outputs static HTML — fast, parallel, and with zero runtime dependencies.

## The Problem

Most static site generators force you into a single content source. Hugo assumes Markdown files. Gatsby assumes React. Ghost themes assume Ghost. If you run a Ghost blog today but want WordPress tomorrow, you're rewriting your entire theme layer. And none of them give you the raw performance of compiled native code.

## What Guss Does

Guss decouples content sourcing from rendering. A normalized data model sits between the CMS and the template engine — swap your CMS without touching your theme, swap your theme without touching your CMS.

The build pipeline works in four phases:

1. **Fetch** — Pull content from Ghost, WordPress, or Markdown files through a unified adapter interface. Each adapter normalizes CMS-specific data into Guss's domain types (Post, Page, Author, Taxonomy, Asset).

2. **Prepare** — Resolve Markdown to HTML via cmark-gfm, compute permalink URLs from configurable patterns, and serialize shared site data exactly once into a single allocation shared across all page renders.

3. **Render** — Parallel template rendering via OpenMP. Each page gets its own content payload plus a shared pointer to the site-wide data. Inja (~~Jinja2 for C++~~) handles template inheritance, loops, conditionals, and filters. Thread count is auto-detected or configurable.

4. **Write** — Output HTML to disk with directory structure mirroring the permalink pattern. Copy static assets from the theme. Optionally generate sitemap and RSS feed.

## Architecture

```
include/guss/
├── core/          Domain model, interfaces, config, permalink resolver, markdown renderer
├── adapters/      Ghost (Content API + simdjson), WordPress (REST v2), Markdown (filesystem + YAML frontmatter)
├── render/        custom template engine using simdjson views 
├── builder/       Parallel build pipeline (OpenMP), progress reporting
├── server/        Static file serving, webhook endpoint, ISR
└── watch/         Filesystem watcher (efsw), polling, trigger coordination
```

All configuration lives in a single `guss.yaml` file parsed via yaml-cpp into typed C++ structs using `std::variant` for the adapter source.

## Technology Choices

The stack prioritizes native performance and minimal dependencies:

- **simdjson** for parsing CMS API responses at SIMD-accelerated speed, **nlohmann/json** for template contexts (required by inja).
- **cmark-gfm** for GitHub Flavored Markdown — tables, autolinks, strikethrough, task lists.
- **cpp-httplib** (header-only, OpenSSL) for both the HTTP client (CMS fetching) and the dev server.
- **spdlog** with dual sinks: colored console output for interactive use, syslog for daemon/systemd mode. Detected automatically via `isatty()`.
- **indicators** for terminal progress bars during builds.
- **CMake + CPM** for zero-external-dependency builds — all libraries downloaded at configure time from source. No Conan, no vcpkg, no Python, no Node.

## Key Design Decisions

**Shared-nothing parallel rendering.** The `SharedSiteData` struct is serialized once and wrapped in `std::shared_ptr<const>`. Each thread in the OpenMP parallel loop constructs its own `RenderContext` with a shared pointer (reference count bump) plus a per-page JSON payload. No locks, no contention, no cloning.

**`std::expected<T, Error>` over exceptions.** Error handling uses monadic return types throughout the hot path. No `try`/`catch` in the render loop, no stack unwinding overhead. Errors propagate cleanly to the caller.

**simdjson for the fast path, nlohmann for the template path.** CMS API responses are parsed with simdjson's zero-allocation DOM parser directly into typed C++ structs. ~~Only when data reaches the template engine does it get converted to nlohmann/json (which inja requires). This avoids an intermediate dynamic JSON representation for the entire content model.~~

**Configurable permalinks with date tokens.** Patterns like `/blog/{year}/{month}/{slug}` are resolved per-item with compile-time format strings. Date tokens are stripped gracefully when no publish date exists.

## Rebuild Triggers

Guss supports four trigger sources, all feeding a unified coordinator:

- **Filesystem** — efsw watches theme directories and content folders for changes, with configurable debounce.
- **Webhook** — CMS platforms like Ghost can POST to `/_guss/webhook` on publish, triggering a rebuild.
- **Polling** — Configurable interval for CMS sources that don't support webhooks.
- **Manual** — CLI command or POST to `/_guss/build`.

## Intended Deployment

~~Caddy serves the static output directory. Guss writes to `dist/` on rebuild. No application server sits between Caddy and the user for normal page loads. The optional axum-equivalent (cpp-httplib) server handles webhook ingestion, ISR cache misses, and the dev workflow — but production serves pure static files.~~

## Status

The Ghost adapter, Markdown adapter, ~~inja engine~~, parallel build pipeline, config parser, permalink resolver, and CLI are implemented. WordPress adapter and the HTTP server are stubbed and ready for implementation. Watch coordinator (efsw) is designed but not yet wired. This branch is for the implementation of the new template engine.