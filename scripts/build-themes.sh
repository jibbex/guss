#!/usr/bin/env bash
# Build all themes in themes/*/
# Requires: bun, tailwindcss standalone binary (v4.2.2)
#
# Usage:
#   ./scripts/build-themes.sh           # build all themes
#   ./scripts/build-themes.sh pinguin   # build one theme by name
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THEMES_DIR="$SCRIPT_DIR/../themes"

build_theme() {
    local theme_dir="$1"
    local name
    name="$(basename "$theme_dir")"
    local src_dir="$theme_dir/src"
    local assets_dir="$theme_dir/assets"

    if [ ! -d "$src_dir" ]; then
        echo "  skip: $name (no src/ directory)"
        return 0
    fi

    echo "Building theme: $name"

    # Install npm dependencies if a package.json exists (e.g. fontsource)
    if [ -f "$theme_dir/package.json" ]; then
        echo "  bun install..."
        bun install --cwd "$theme_dir" --frozen-lockfile
    fi

    mkdir -p "$assets_dir"

    # CSS — compile each .css file independently
    for css_file in "$src_dir"/*.css; do
        [ -f "$css_file" ] || continue
        local out_name
        out_name="$(basename "$css_file")"
        echo "  CSS: tailwindcss ($out_name)..."
        tailwindcss \
            --input  "$css_file" \
            --output "$assets_dir/$out_name" \
            --minify
    done

    # JS — compile each .js file independently
    for js_file in "$src_dir"/*.js; do
        [ -f "$js_file" ] || continue
        local out_name
        out_name="$(basename "$js_file")"
        echo "  JS: bun build $out_name..."
        bun build "$js_file" \
            --outfile "$assets_dir/$out_name" \
            --minify
    done

    echo "  done: $name"
}

# Build one or all themes
if [ "${1:-}" != "" ]; then
    build_theme "$THEMES_DIR/$1"
else
    for theme_dir in "$THEMES_DIR"/*/; do
        [ -d "$theme_dir" ] || continue
        build_theme "$theme_dir"
    done
fi

echo "All themes built."
