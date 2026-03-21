---
slug: docs/cli
title: CLI Reference
---

The `guss` binary provides five commands.

## guss init

```bash
guss init [dir]
```

Scaffold a new Guss project. Creates `guss.yaml`, `templates/`, and `content/` with the
default Pinguin theme.

If `dir` is omitted, scaffolds in the current directory. If the directory does not exist,
it is created.

## guss build

```bash
guss build [-c config] [-v] [--clean]
```
Run the full 4-phase pipeline (Fetch → Prepare → Render → Write) with a progress bar.

| Flag        | Description                                           |
|-------------|-------------------------------------------------------|
| `-c config` | Path to the configuration file (default: `guss.yaml`) |
| `-v`        | Verbose output — sets log level to debug              |
| `--clean`   | Remove the output directory before building           |

On completion, reports item count, archive count, extras generated (sitemap, RSS), and
total duration.

## guss ping

```bash
guss ping [-c config]
```

Test the connection to the configured content source. For REST API adapters, makes a test
request to each endpoint and reports the HTTP status. For Markdown adapters, verifies the
configured paths exist and are readable.

Useful for validating credentials and connectivity before a full build.

## guss clean

```bash
guss clean [-c config]
```

Remove the output directory (`output.output_dir` from config, default `dist`). Equivalent
to `rm -rf dist/`.

## guss serve

```bash
guss serve [-d dir]
```

Start a local HTTP server serving the output directory. Wraps `python3 -m http.server` on
port 8080.

| Flag     | Description                          |
|----------|--------------------------------------|
| `-d dir` | Directory to serve (default: `dist`) |

This is a convenience command for local preview. For production, use a real web server or
CDN.