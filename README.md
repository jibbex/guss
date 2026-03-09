# 🔥 Guss — "I Am Not A Static Site Generator. I AM STATIC SITE GENERATION ITSELF!"

> **GUSS** (German: *der Guss* — "the casting") doesn't build pages. It CASTS them from molten CMS data into PERMANENT STATIC PERFECTION.

![Build](https://img.shields.io/badge/build-FORGED-brightgreen)
![Performance](https://img.shields.io/badge/threads-ALL%20OF%20THEM-blue)
![C++](https://img.shields.io/badge/C%2B%2B-23-red)
![Ego](https://img.shields.io/badge/ego-INHERITED%20FROM%20APEX-orange)

> *"Other SSGs fetch your content. Guss DEMANDS it."*

## 🌋 What Is This?

A pluggable static site generator written in C++23 that:

- **FORGES** pages from Ghost, WordPress, or Markdown at SIMD-accelerated speed
- **CASTS** them through a custom bytecode template engine with the precision of a German foundry
- **HARDENS** the output into static HTML that loads before your users even CLICK
- **WATCHES** your CMS for changes and rebuilds before you finish your coffee

## ⚡ Performance That Other SSGs Find Personally Offensive

```bash
$ guss build
🔥 GUSS BUILD — WITNESS PERFECTION
[████████████████████████████████████████] 100% Rendering 247/247 [00:00<00:00]

✅ 247 pages forged in 43ms
   Output: dist/

   ⚡ Under 100ms. Other SSGs are STILL LOADING CONFIG.
```

## 🛠 Prerequisites

- A C++23 compiler (GCC 14+ or Clang 18+)
- CMake 3.25+
- OpenSSL (for HTTPS)
- A machine WORTHY of running Guss

```bash
# Debian/Ubuntu
sudo apt install cmake g++-14 libssl-dev

# The actual build (CMake + CPM downloads everything else)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

No Conan. No vcpkg. No Python. No Node. Just CMake and a compiler. Like C++ INTENDED.

## 🚀 Usage

```bash
# Initialize — the BIRTH of greatness
guss init --adapter ghost

# Connect to your CMS — establish DOMINANCE
guss ping
# Pinging Ghost CMS (ghost)... ✅ Connected. It KNOWS its master.

# Build — FORGE your content
guss build

# Serve — with ISR and live rebuild
guss serve --port 4000
```

## 📐 Architecture

```
guss/
├── include/guss/
│   ├── core/        # Domain model, interfaces
│   ├── adapters/    # Ghost, WordPress, Markdown implementations
│   ├── render/      # custom bytecode template engine (Value, Context, Engine)
│   ├── builder/     # Parallel build pipeline (OpenMP)
│   ├── server/      # cpp-httplib serve layer
│   └── watch/       # efsw filesystem watcher
├── src/             # Implementation files
├── themes/          # Swappable themes (inja/Jinja2 templates)
├── examples/        # Example guss.yaml configs
└── tests/           # Google Test suite
```

## 🔧 Stack

| What         | Choice               | Why                                         |
|--------------|----------------------|---------------------------------------------|
| Language     | C++23                | Because we RESPECT the machine              |
| JSON parsing | simdjson             | SIMD-accelerated, gigabytes/second (adapters only) |
| Templates    | guss::render         | Custom bytecode compiler — one pass, linear scan |
| Markdown     | cmark                | GitHub Flavored, C-fast                     |
| HTTP client  | cpp-httplib          | Header-only, OpenSSL                        |
| CLI          | CLI11                | Header-only, elegant                        |
| Logging      | spdlog               | Console + syslog, sub-nanosecond            |
| Progress     | indicators           | Because builds should look GOOD             |
| File watch   | efsw                 | Cross-platform, lightweight                 |
| Parallelism  | OpenMP               | One pragma, all cores                       |
| Build system | CMake + CPM          | Zero external dependencies                  |
| Tests        | Google Test          | Industry standard                           |

## 📋 Configuration

`guss.yaml` — because YAML is readable and TOML is for Rust people:

```yaml
site:
  title: "michm.de"
  url: "https://michm.de"

source:
  adapter: ghost
  url: "http://10.10.10.4:2368"
  content_api_key: "abc123"

theme:
  name: default

build:
  output_dir: /var/www/michm.de/public
  workers: 0  # 0 = ALL cores. You're welcome.
  clean: true

permalinks:
  post: "/blog/{year}/{month}/{slug}"
  page: "/{slug}"
```

## 📜 License

MIT — Because even SUPREME PERFORMANCE believes in generosity.
