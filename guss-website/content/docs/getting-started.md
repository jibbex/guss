---
slug: docs/getting-started
title: Getting Started
---

## Prerequisites

|                | Docker (recommended) | Native               |
|----------------|----------------------|----------------------|
| **Works on**   | Any OS with Docker   | Linux / macOS        |
| **Compiler**   | Provided in image    | GCC 14+ or Clang 18+ |
| **CMake**      | Provided in image    | 3.25+                |
| **OpenSSL**    | Provided in image    | Required (for HTTPS) |
| **Setup time** | ~2 min (image pull)  | ~5 min               |

Docker is recommended because the image pins the exact compiler version (GCC 14) and all
system libraries. Native builds are faster for iterative development once dependencies
are installed.

## Build

**Docker — works on any OS:**

```bash
# Build and run all tests
docker compose run --rm test

# Build the release binary (lands at ./cmake-build/guss)
docker compose run --rm release
```

**Native:**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# With tests
cmake -B build -DGUSS_BUILD_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build
```

Optional build flags:

| Flag                          | Effect                                     |
|-------------------------------|--------------------------------------------|
| `-DGUSS_RUNTIME_CHECKS=ON`    | Enable assertions in hot path of runtime   |
| `-DGUSS_BUILD_TESTS=ON`       | Build the test suite (`guss-tests` target) |
| `-DGUSS_BUILD_SERVER=ON`      | Build the optional HTTP server component   |
| `-DGUSS_ENABLE_SANITIZERS=ON` | Enable AddressSanitizer + UBSan            |

## Quick Start

Scaffold a new site and build it:

```bash
guss init my-site
cd my-site
guss build
```

`guss init` writes `guss.yaml` and a default theme (`templates/`, `content/`) using the
embedded Pinguin theme. `guss build` runs the full 4-phase pipeline (Fetch → Prepare →
Render → Write) and writes HTML to `dist/`.

Preview the output locally:

```bash
guss serve
```

Opens a local HTTP server on port 8080 serving `dist/`.

## What `guss init` generates

```
my-site/
├── guss.yaml             ← site config
├── content/
│   └── sample-post.md   ← example Markdown item
└── templates/
    ├── index.html        ← archive/listing template
    ├── post.html         ← single item template
    └── assets/
        └── style.css
```

Edit `guss.yaml` to point at your CMS or content directory, adjust the templates, write
your content, then run `guss build`.

## Dependency downloads

Guss uses [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) to fetch all C++ dependencies
at configure time. No `apt install` or Conan invocation is required — CMake downloads and
builds everything automatically on first configure. Subsequent builds use a local cache.

Dependencies fetched automatically: yaml-cpp, simdjson, spdlog, cpp-httplib, md4c,
indicators, efsw, GoogleTest.
