#!/usr/bin/env bash
# Guss container build entrypoint.
# All variables have sane defaults so `docker run guss:builder` works as-is.
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-cmake-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_TESTS="${BUILD_TESTS:-OFF}"
RUN_TESTS="${RUN_TESTS:-OFF}"

# Build all theme assets (CSS + JS)
echo "==> Building theme assets..."
/app/scripts/build-themes.sh

# shellcheck disable=SC2086   # CMAKE_FLAGS intentionally word-splits
cmake -B "$BUILD_DIR" \
      -G Ninja \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DGUSS_BUILD_TESTS="$BUILD_TESTS" \
      ${CMAKE_FLAGS:-}

cmake --build "$BUILD_DIR" -j"$(nproc)"

if [ "$RUN_TESTS" = "ON" ]; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure -j"$(nproc)"
fi
