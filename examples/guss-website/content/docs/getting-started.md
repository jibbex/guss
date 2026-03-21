---
slug: docs/getting-started
title: Getting Started
---

## Prerequisites

Guss requires a C++23 compiler and CMake 3.25+ for native builds. On any platform you can
use the provided Docker setup instead.

**Docker (recommended):**
- Docker with Compose

**Native:**
- GCC 14+ or Clang 18+
- CMake 3.25+
- OpenSSL (for HTTPS via cpp-httplib)

## Build

**With Docker — recommended, works on any OS:**

```bash
# Build and run all tests
docker compose run --rm test

# Build release binary (output: ./cmake-build/guss)
docker compose run --rm release
```

**Native build:**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Quick Start

Initialize a new site and build it:

```bash
guss init my-site
cd my-site
guss build
```

`guss init` scaffolds `guss.yaml` and a default theme (`templates/`, `content/`). `guss build` runs the full 4-phase
pipeline and writes HTML to `dist/`.

Preview the output:

```bash
guss serve
```

This starts a local HTTP server serving `dist/` on port 8080.

## What guss init generates

```
my-site/
├── guss.yaml
├── content/
│   └── sample-post.md
└── templates/
    ├── index.html
    ├── post.html
    └── assets/
    └── style.css
```

Edit `guss.yaml` to point at your CMS, add templates, write content; Then `guss build`.