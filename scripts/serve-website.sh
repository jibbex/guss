#!/usr/bin/env bash
# Sync forge theme, build guss-website, and serve it locally.
#
# Usage (via Docker):
#   docker compose run --rm --service-ports website
#
# Requires guss binary to be already built:
#   docker compose run --rm release
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
GUSS_BIN="$ROOT/cmake-build/guss"

if [ ! -x "$GUSS_BIN" ]; then
    echo "ERROR: $GUSS_BIN not found — run 'docker compose run --rm release' first."
    exit 1
fi

echo "==> Syncing forge theme into guss-website/templates..."
rm -rf "$ROOT/guss-website/templates"
mkdir -p "$ROOT/guss-website/templates"
cp "$ROOT/themes/forge"/*.html "$ROOT/guss-website/templates/"
cp -r "$ROOT/themes/forge/assets" "$ROOT/guss-website/templates/assets"

echo "==> Building guss-website..."
cd "$ROOT/guss-website" && "$GUSS_BIN" build

echo "==> Serving at http://localhost:8080 ..."
exec python3 -m http.server 8080 --directory "$ROOT/guss-website/dist"
