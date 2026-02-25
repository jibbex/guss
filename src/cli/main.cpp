/**
 * @file main.cpp
 * @brief CLI entry point for Guss SSG.
 *
 * @details
 * Commands:
 * - guss init [directory] - Scaffold a new Guss project
 * - guss build [-c config] [-v] - Run the full build pipeline
 * - guss ping [-c config] - Test CMS connection
 * - guss clean [-c config] - Clean output directory
 */

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <indicators/progress_bar.hpp>
#include <indicators/cursor_control.hpp>

#include "guss/core/config.hpp"
#include "guss/core/error.hpp"
#include "guss/adapters/ghost_adapter.hpp"
#include "guss/builder/pipeline.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// Default configuration template
constexpr std::string_view DEFAULT_CONFIG = R"(# Guss Static Site Generator Configuration

site:
  title: "My Site"
  description: "A site built with Guss"
  url: "https://example.com"
  language: "en"
  # logo: "/images/logo.png"
  # twitter: "@myhandle"

# Content source configuration
# Supported types: ghost, wordpress, markdown
source:
  type: ghost
  api_url: "https://demo.ghost.io"
  content_api_key: "22444f78447824223cefc48062"
  # admin_api_key: ""  # Optional, for private content
  timeout_ms: 30000

# URL permalink patterns
# Tokens: {slug}, {year}, {month}, {day}, {id}
permalinks:
  post: "/{year}/{month}/{slug}/"
  page: "/{slug}/"
  tag: "/tag/{slug}/"
  author: "/author/{slug}/"

# Output settings
output:
  output_dir: "./dist"
  assets_dir: "./assets"
  generate_sitemap: true
  generate_rss: true
  minify_html: false
  copy_assets: true

# Template settings
templates:
  templates_dir: "./templates"
  default_post_template: "post.html"
  default_page_template: "page.html"
  index_template: "index.html"
  tag_template: "tag.html"
  author_template: "author.html"

# Build settings
parallel_workers: 0  # 0 = auto-detect
log_level: "info"    # debug, info, warn, error
)";

// Default post template
constexpr std::string_view DEFAULT_POST_TEMPLATE = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{{ post.title }} - {{ site.title }}</title>
    <meta name="description" content="{{ post.excerpt | truncate(160) }}">
    <link rel="stylesheet" href="/assets/style.css">
</head>
<body>
    <header>
        <nav>
            <a href="/">{{ site.title }}</a>
        </nav>
    </header>

    <main>
        <article>
            <header>
                <h1>{{ post.title }}</h1>
                <time datetime="{{ post.published_at }}">
                    {{ date_format(post.published_at, "%B %d, %Y") }}
                </time>
                {% if post.author %}
                <span class="author">by {{ post.author.name }}</span>
                {% endif %}
            </header>

            {% if post.feature_image %}
            <img src="{{ post.feature_image }}" alt="{{ post.feature_image_alt }}" class="feature-image">
            {% endif %}

            <div class="content">
                {{ post.content }}
            </div>

            {% if post.tags %}
            <footer>
                <ul class="tags">
                    {% for tag in post.tags %}
                    <li><a href="/tag/{{ tag.slug }}/">{{ tag.name }}</a></li>
                    {% endfor %}
                </ul>
            </footer>
            {% endif %}
        </article>

        <nav class="post-navigation">
            {% if prev_post %}
            <a href="{{ prev_post.permalink }}" class="prev">&larr; {{ prev_post.title }}</a>
            {% endif %}
            {% if next_post %}
            <a href="{{ next_post.permalink }}" class="next">{{ next_post.title }} &rarr;</a>
            {% endif %}
        </nav>
    </main>

    <footer>
        <p>&copy; {{ site.title }}</p>
    </footer>
</body>
</html>
)";

// Default index template
constexpr std::string_view DEFAULT_INDEX_TEMPLATE = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{{ site.title }}</title>
    <meta name="description" content="{{ site.description }}">
    <link rel="stylesheet" href="/assets/style.css">
    <link rel="alternate" type="application/rss+xml" title="{{ site.title }}" href="/rss.xml">
</head>
<body>
    <header>
        <h1>{{ site.title }}</h1>
        <p>{{ site.description }}</p>
    </header>

    <main>
        <section class="posts">
            {% for post in posts %}
            <article>
                <h2><a href="{{ post.permalink }}">{{ post.title }}</a></h2>
                <time datetime="{{ post.published_at }}">
                    {{ date_format(post.published_at, "%B %d, %Y") }}
                </time>
                {% if post.excerpt %}
                <p>{{ post.excerpt | truncate(200) }}</p>
                {% endif %}
            </article>
            {% endfor %}
        </section>

        {% if pagination.total > 1 %}
        <nav class="pagination">
            {% if pagination.has_prev %}
            <a href="{{ pagination.prev_url }}">&larr; Newer</a>
            {% endif %}
            <span>Page {{ pagination.current }} of {{ pagination.total }}</span>
            {% if pagination.has_next %}
            <a href="{{ pagination.next_url }}">Older &rarr;</a>
            {% endif %}
        </nav>
        {% endif %}
    </main>

    <aside>
        <h3>Tags</h3>
        <ul class="tags">
            {% for tag in tags %}
            <li><a href="/tag/{{ tag.slug }}/">{{ tag.name }} ({{ tag.post_count }})</a></li>
            {% endfor %}
        </ul>
    </aside>

    <footer>
        <p>&copy; {{ site.title }}</p>
    </footer>
</body>
</html>
)";

// Default page template
constexpr std::string_view DEFAULT_PAGE_TEMPLATE = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{{ page.title }} - {{ site.title }}</title>
    <link rel="stylesheet" href="/assets/style.css">
</head>
<body>
    <header>
        <nav>
            <a href="/">{{ site.title }}</a>
        </nav>
    </header>

    <main>
        <article>
            <h1>{{ page.title }}</h1>
            <div class="content">
                {{ page.content }}
            </div>
        </article>
    </main>

    <footer>
        <p>&copy; {{ site.title }}</p>
    </footer>
</body>
</html>
)";

// Default tag template
constexpr std::string_view DEFAULT_TAG_TEMPLATE = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{{ tag.name }} - {{ site.title }}</title>
    <link rel="stylesheet" href="/assets/style.css">
</head>
<body>
    <header>
        <nav>
            <a href="/">{{ site.title }}</a>
        </nav>
    </header>

    <main>
        <h1>Posts tagged "{{ tag.name }}"</h1>
        {% if tag.description %}
        <p>{{ tag.description }}</p>
        {% endif %}

        <section class="posts">
            {% for post in posts %}
            <article>
                <h2><a href="{{ post.permalink }}">{{ post.title }}</a></h2>
                <time datetime="{{ post.published_at }}">
                    {{ date_format(post.published_at, "%B %d, %Y") }}
                </time>
            </article>
            {% endfor %}
        </section>
    </main>

    <footer>
        <p>&copy; {{ site.title }}</p>
    </footer>
</body>
</html>
)";

// Default author template
constexpr std::string_view DEFAULT_AUTHOR_TEMPLATE = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{{ author.name }} - {{ site.title }}</title>
    <link rel="stylesheet" href="/assets/style.css">
</head>
<body>
    <header>
        <nav>
            <a href="/">{{ site.title }}</a>
        </nav>
    </header>

    <main>
        <header class="author-header">
            {% if author.profile_image %}
            <img src="{{ author.profile_image }}" alt="{{ author.name }}" class="avatar">
            {% endif %}
            <h1>{{ author.name }}</h1>
            {% if author.bio %}
            <p class="bio">{{ author.bio }}</p>
            {% endif %}
        </header>

        <section class="posts">
            <h2>Posts by {{ author.name }}</h2>
            {% for post in posts %}
            <article>
                <h3><a href="{{ post.permalink }}">{{ post.title }}</a></h3>
                <time datetime="{{ post.published_at }}">
                    {{ date_format(post.published_at, "%B %d, %Y") }}
                </time>
            </article>
            {% endfor %}
        </section>
    </main>

    <footer>
        <p>&copy; {{ site.title }}</p>
    </footer>
</body>
</html>
)";

// Default CSS
constexpr std::string_view DEFAULT_CSS = R"(* {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
}

body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
    line-height: 1.6;
    color: #333;
    max-width: 800px;
    margin: 0 auto;
    padding: 2rem;
}

header, main, aside, footer {
    margin-bottom: 2rem;
}

h1, h2, h3 {
    line-height: 1.2;
    margin-bottom: 1rem;
}

a {
    color: #0066cc;
}

article {
    margin-bottom: 2rem;
    padding-bottom: 2rem;
    border-bottom: 1px solid #eee;
}

time {
    color: #666;
    font-size: 0.9rem;
}

.feature-image {
    max-width: 100%;
    height: auto;
    margin: 1rem 0;
}

.content img {
    max-width: 100%;
    height: auto;
}

.tags {
    list-style: none;
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
}

.tags li a {
    background: #f0f0f0;
    padding: 0.25rem 0.5rem;
    border-radius: 3px;
    text-decoration: none;
    font-size: 0.9rem;
}

.pagination {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 1rem 0;
}

.post-navigation {
    display: flex;
    justify-content: space-between;
    padding: 1rem 0;
}

footer {
    text-align: center;
    color: #666;
    padding-top: 2rem;
    border-top: 1px solid #eee;
}
)";

void setup_logging(const std::string& level) {
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);

    if (level == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (level == "warn") {
        spdlog::set_level(spdlog::level::warn);
    } else if (level == "error") {
        spdlog::set_level(spdlog::level::err);
    } else {
        spdlog::set_level(spdlog::level::info);
    }
}

int cmd_init(const std::string& directory) {
    fs::path project_dir = directory.empty() ? fs::current_path() : fs::path(directory);

    spdlog::info(fmt::format("Initializing Guss project in {}", project_dir.string()));

    // Create directories
    try {
        fs::create_directories(project_dir);
        fs::create_directories(project_dir / "templates");
        fs::create_directories(project_dir / "templates" / "assets");
        fs::create_directories(project_dir / "content");
        fs::create_directories(project_dir / "dist");
    } catch (const fs::filesystem_error& e) {
        spdlog::error(fmt::format("Failed to create directories: {}", e.what()));
        return 1;
    }

    // Write config file
    auto config_path = project_dir / "guss.yaml";
    if (!fs::exists(config_path)) {
        std::ofstream config_file(config_path);
        config_file << DEFAULT_CONFIG;
        spdlog::info("Created guss.yaml");
    }

    // Write templates
    auto write_template = [&](const std::string& name, std::string_view content) {
        auto path = project_dir / "templates" / name;
        if (!fs::exists(path)) {
            std::ofstream file(path);
            file << content;
            spdlog::info(fmt::format("Created templates/{}", name));
        }
    };

    write_template("post.html", DEFAULT_POST_TEMPLATE);
    write_template("page.html", DEFAULT_PAGE_TEMPLATE);
    write_template("index.html", DEFAULT_INDEX_TEMPLATE);
    write_template("tag.html", DEFAULT_TAG_TEMPLATE);
    write_template("author.html", DEFAULT_AUTHOR_TEMPLATE);

    // Write CSS
    auto css_path = project_dir / "templates" / "assets" / "style.css";
    if (!fs::exists(css_path)) {
        std::ofstream css_file(css_path);
        css_file << DEFAULT_CSS;
        spdlog::info("Created templates/assets/style.css");
    }

    spdlog::info("Project initialized successfully!");
    spdlog::info("Next steps:");
    spdlog::info("  1. Edit guss.yaml to configure your content source");
    spdlog::info("  2. Customize templates in the templates/ directory");
    spdlog::info("  3. Run 'guss build' to generate your site");

    return 0;
}

int cmd_build(const std::string& config_path, bool verbose, bool clean_first) {
    setup_logging(verbose ? "debug" : "info");

    spdlog::info("Guss Static Site Generator");
    spdlog::info(fmt::format("Loading configuration from {}", config_path));

    // Load config
    const std::string& cfg_path = config_path;
    const auto& config = guss::config::Config::instance(&cfg_path);

    // Create adapter based on config
    guss::adapters::AdapterPtr adapter;

    if (std::holds_alternative<guss::config::GhostAdapterConfig>(config.adapter())) {
        const auto& ghost_cfg = std::get<guss::config::GhostAdapterConfig>(config.adapter());
        adapter = std::make_unique<guss::adapters::GhostAdapter>(ghost_cfg);
        spdlog::info(fmt::format("Using Ghost adapter: {}", ghost_cfg.api_url));
    } else if (std::holds_alternative<guss::config::WordPressAdapterConfig>(config.adapter())) {
        spdlog::error("WordPress adapter not yet implemented");
        return 1;
    } else {
        spdlog::error("Markdown adapter not yet implemented");
        return 1;
    }

    // Create pipeline
    guss::builder::Pipeline pipeline(
        std::move(adapter),
        config.site(),
        config.templates(),
        config.permalinks(),
        config.output()
    );

    // Clean if requested
    if (clean_first) {
        auto clean_result = pipeline.clean();
        if (!clean_result) {
            spdlog::error(fmt::format("Clean failed: {}", clean_result.error().format()));
            return 1;
        }
    }

    // Setup progress bar
    indicators::ProgressBar bar{
        indicators::option::BarWidth{50},
        indicators::option::Start{"["},
        indicators::option::End{"]"},
        indicators::option::ForegroundColor{indicators::Color::green},
        indicators::option::ShowPercentage{true},
        indicators::option::ShowElapsedTime{true},
        indicators::option::PostfixText{"Starting..."}
    };

    indicators::show_console_cursor(false);

    // Run build
    auto result = pipeline.build([&bar](const std::string &message, float progress) {
        bar.set_progress(static_cast<size_t>(progress * 100));
        bar.set_option(indicators::option::PostfixText{message});
    });

    indicators::show_console_cursor(true);
    std::cout << std::endl;

    if (!result) {
        spdlog::error(fmt::format("Build failed: {}", result.error().format()));
        return 1;
    }

    const auto& stats = *result;
    spdlog::info("Build complete!");
    spdlog::info(fmt::format("  Posts:    {}", stats.posts_rendered));
    spdlog::info(fmt::format("  Pages:    {}", stats.pages_rendered));
    spdlog::info(fmt::format("  Tags:     {}", stats.tag_archives_rendered));
    spdlog::info(fmt::format("  Authors:  {}", stats.author_archives_rendered));
    spdlog::info(fmt::format("  Index:    {}", stats.index_pages_rendered));
    spdlog::info(fmt::format("  Assets:   {}", stats.assets_copied));
    if (stats.errors > 0) {
        spdlog::warn(fmt::format("  Errors:   {}", stats.errors));
    }
    spdlog::info(fmt::format("  Duration: {}ms", stats.total_duration.count()));

    return stats.errors > 0 ? 1 : 0;
}

int cmd_ping(const std::string& config_path) {
    setup_logging("info");

    spdlog::info("Testing connection...");

    // Load config
    const std::string& cfg_path = config_path;
    const auto& config = guss::config::Config::instance(&cfg_path);

    // Create adapter
    guss::adapters::AdapterPtr adapter;

    if (std::holds_alternative<guss::config::GhostAdapterConfig>(config.adapter())) {
        const auto& ghost_cfg = std::get<guss::config::GhostAdapterConfig>(config.adapter());
        adapter = std::make_unique<guss::adapters::GhostAdapter>(ghost_cfg);
        spdlog::info(fmt::format("Adapter: Ghost ({})", ghost_cfg.api_url));
    } else {
        spdlog::error("Unsupported adapter type");
        return 1;
    }

    // Create minimal pipeline just for ping
    guss::builder::Pipeline pipeline(
        std::move(adapter),
        config.site(),
        config.templates(),
        config.permalinks(),
        config.output()
    );

    auto result = pipeline.ping();
    if (!result) {
        spdlog::error(fmt::format("Connection failed: {}", result.error().format()));
        return 1;
    }

    spdlog::info("Connection successful!");
    return 0;
}

int cmd_clean(const std::string& config_path) {
    setup_logging("info");

    const std::string& cfg_path = config_path;
    const auto& config = guss::config::Config::instance(&cfg_path);

    spdlog::info(fmt::format("Cleaning output directory: {}", config.output().output_dir.string()));

    try {
        if (fs::exists(config.output().output_dir)) {
            fs::remove_all(config.output().output_dir);
            spdlog::info("Output directory cleaned");
        } else {
            spdlog::info("Output directory does not exist");
        }
    } catch (const fs::filesystem_error& e) {
        spdlog::error(fmt::format("Failed to clean: {}", e.what()));
        return 1;
    }

    return 0;
}

int main(int argc, char** argv) {
    CLI::App app{"Guss - A pluggable static site generator"};
    app.require_subcommand(1);

    // Global options
    std::string config_path = "guss.yaml";

    // init subcommand
    auto init_cmd = app.add_subcommand("init", "Initialize a new Guss project");
    std::string init_dir;
    init_cmd->add_option("directory", init_dir, "Project directory (default: current)");

    // build subcommand
    auto build_cmd = app.add_subcommand("build", "Build the static site");
    bool verbose = false;
    bool clean_first = false;
    build_cmd->add_option("-c,--config", config_path, "Configuration file path");
    build_cmd->add_flag("-v,--verbose", verbose, "Enable verbose output");
    build_cmd->add_flag("--clean", clean_first, "Clean output directory first");

    // ping subcommand
    auto ping_cmd = app.add_subcommand("ping", "Test connection to content source");
    ping_cmd->add_option("-c,--config", config_path, "Configuration file path");

    // clean subcommand
    auto clean_cmd = app.add_subcommand("clean", "Clean the output directory");
    clean_cmd->add_option("-c,--config", config_path, "Configuration file path");

    CLI11_PARSE(app, argc, argv);

    if (init_cmd->parsed()) {
        return cmd_init(init_dir);
    }

    if (build_cmd->parsed()) {
        return cmd_build(config_path, verbose, clean_first);
    }

    if (ping_cmd->parsed()) {
        return cmd_ping(config_path);
    }

    if (clean_cmd->parsed()) {
        return cmd_clean(config_path);
    }

    return 0;
}
