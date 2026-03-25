# syntax=docker/dockerfile:1

# ── Stage 1: toolchain ───────────────────────────────────────────────────────
# Installs the compiler and build tools. This layer is rebuilt only when the
# package list changes. BuildKit cache mounts keep /var/cache/apt off the
# image filesystem entirely.
FROM ubuntu:24.04 AS toolchain

ENV DEBIAN_FRONTEND=noninteractive \
    CC=gcc-14 \
    CXX=g++-14

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        gcc-14 \
        g++-14 \
        git \
        python3 \
        libssl-dev \
        libomp-dev \
        ca-certificates \
        curl

RUN git config --global --add safe.directory /app

# ── Frontend toolchain (bun + Tailwind CSS standalone binary) ─────────────────
COPY --from=oven/bun:latest /usr/local/bin/bun /usr/local/bin/bun
RUN curl -fsSL https://github.com/tailwindlabs/tailwindcss/releases/download/v4.2.2/tailwindcss-linux-x64 \
        -o /usr/local/bin/tailwindcss && chmod +x /usr/local/bin/tailwindcss

# ── Stage 2: CPM source cache ────────────────────────────────────────────────
# Runs cmake configure-only (no compilation) to download all CPM packages
# into CPM_SOURCE_CACHE. The downloaded sources are baked into this layer, so
# subsequent docker runs and CI jobs never hit the network for dependencies.
FROM toolchain AS cpm-cache

ENV CPM_SOURCE_CACHE=/opt/cpm-cache

WORKDIR /tmp/cpm-bootstrap
COPY CMakeLists.txt .

# CPMAddPackage() calls (lines 53-130) run before add_library() (line 135+),
# so all packages land in CPM_SOURCE_CACHE before cmake checks source files.
# Source-file errors are non-fatal — cmake exits non-zero but packages are
# already downloaded. Use ; instead of && so we don't abort on that exit code,
# then verify the cache was populated before committing the layer.
RUN cmake -B /tmp/cpm-build -G Ninja \
        -DGUSS_BUILD_TESTS=ON \
        -DCMAKE_BUILD_TYPE=Release; \
    test -d "${CPM_SOURCE_CACHE}" && rm -rf /tmp/cpm-build

# ── Stage 3: builder (final image) ───────────────────────────────────────────
# Thin runtime layer: toolchain + CPM source cache.
# Source is bind-mounted at runtime; build artefacts accumulate in a named
# Docker volume so incremental builds are fast across container restarts.
FROM cpm-cache AS builder

ENV CPM_SOURCE_CACHE=/opt/cpm-cache

COPY docker-entrypoint.sh /usr/local/bin/guss-build
RUN chmod +x /usr/local/bin/guss-build

WORKDIR /app
ENTRYPOINT ["guss-build"]
