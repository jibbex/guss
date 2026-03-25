#!/usr/bin/env bash
# Scaffold a new Guss theme.
# Usage: ./scripts/new-theme.sh <theme-name>
set -euo pipefail

THEME_NAME="${1:-}"
if [ -z "$THEME_NAME" ]; then
    echo "Usage: $0 <theme-name>" >&2
    exit 1
fi

THEMES_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/themes"
THEME_DIR="$THEMES_DIR/$THEME_NAME"

if [ -d "$THEME_DIR" ]; then
    echo "Error: themes/$THEME_NAME already exists." >&2
    exit 1
fi

mkdir -p "$THEME_DIR/src" "$THEME_DIR/assets" "$THEME_DIR/partials"

cat > "$THEME_DIR/src/style.css" << 'EOF'
@import "tailwindcss";

@theme {
  --color-primary: oklch(0.627 0.233 303.9);
  --font-sans: "Inter", sans-serif;
  --font-mono: "JetBrains Mono", monospace;
}
EOF

cat > "$THEME_DIR/src/main.js" << 'EOF'
// Theme entry point
EOF

cat > "$THEME_DIR/index.html" << 'EOF'
<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{{ site.title }}</title>
  <link rel="stylesheet" href="/assets/style.css">
</head>
<body>
  <main>
    {% for item in items %}
    <article>
      <h2><a href="{{ item.permalink }}">{{ item.title }}</a></h2>
    </article>
    {% endfor %}
  </main>
  <script src="/assets/main.js"></script>
</body>
</html>
EOF

cat > "$THEME_DIR/post.html" << 'EOF'
<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{{ post.title }} — {{ site.title }}</title>
  <link rel="stylesheet" href="/assets/style.css">
</head>
<body>
  <main>
    <article>
      <h1>{{ post.title }}</h1>
      {{ post.html | safe }}
    </article>
  </main>
  <script src="/assets/main.js"></script>
</body>
</html>
EOF

echo ""
echo "Created: themes/$THEME_NAME/"
echo ""
echo "Next steps:"
echo "  1. Edit themes/$THEME_NAME/src/style.css  — design tokens and styles"
echo "  2. Edit themes/$THEME_NAME/src/main.js    — JS entry point"
echo "  3. Edit themes/$THEME_NAME/index.html, post.html — templates"
echo "  4. Build:  ./scripts/build-themes.sh $THEME_NAME"
