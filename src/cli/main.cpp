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

#include "guss/adapters/markdown/markdown_adapter.hpp"
#include "guss/adapters/rest/rest_adapter.hpp"
#include "guss/builder/pipeline.hpp"
#include "guss/core/config.hpp"
#include "guss/core/error.hpp"
#include "guss/cli/constants.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdlib.h>

namespace fs = std::filesystem;


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

    spdlog::info("Initializing Guss project in " + project_dir.string());

    // Create directories
    for (const auto& dir : {
        project_dir,
        project_dir / "templates",
        project_dir / "templates" / "assets",
        project_dir / "content"
    }) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            spdlog::error("Failed to create directory {}: {}", dir.string(), ec.message());
            return 1;
        }
    }

    // Write config file
    auto config_path = project_dir / "guss.yaml";
    if (!fs::exists(config_path)) {
        std::ofstream config_file(config_path);
        config_file << guss::cli::DEFAULT_CONFIG;
        spdlog::info("Created guss.yaml");
    }

    // ---

    auto write_file = [&project_dir](std::string_view dir, std::string_view name, std::string_view content) {
        auto path = project_dir / dir / name;
        if (!fs::exists(path)) {
            std::ofstream file(path);
            file << content;
            spdlog::info("Created {}/{}", dir, name);
        }
    };

    // Write templates
    write_file("templates", "post.html", guss::cli::DEFAULT_POST_TEMPLATE);
    write_file("templates", "page.html", guss::cli::DEFAULT_PAGE_TEMPLATE);
    write_file("templates", "index.html", guss::cli::DEFAULT_INDEX_TEMPLATE);
    write_file("templates", "tag.html", guss::cli::DEFAULT_TAG_TEMPLATE);
    write_file("templates", "author.html", guss::cli::DEFAULT_AUTHOR_TEMPLATE);

    // Write Assets
    write_file("assets", "style.css", guss::cli::DEFAULT_STYLE_CSS);
    write_file("assets", "main.js", guss::cli::DEFAULT_MAIN_JS);
    write_file("assets", "reveal.js", guss::cli::DEFAULT_REVEAL_JS);
    write_file("assets", "reading-progress.js", guss::cli::DEFAULT_READING_PROGRESS_JS);
    write_file("assets", "toc.js", guss::cli::DEFAULT_TOC_JS);

    spdlog::info("Project initialized successfully!");
    spdlog::info("Next steps:");
    spdlog::info("  1. Edit guss.yaml to configure your content source");
    spdlog::info("  2. Customize templates in the templates/ directory");
    spdlog::info("  3. Run 'guss build' to generate your site");

    return 0;
}

int cmd_build(const std::string& config_path, bool verbose, bool clean_first) {
    setup_logging(verbose ? "debug" : "info");

    spdlog::info("🔥 GUSS BUILD, WITNESS PERFECTION");
    spdlog::info("Loading configuration from {}", config_path);

    // Load config
    guss::core::config::Config config(config_path);

    // Create adapter based on config
    guss::adapters::AdapterPtr adapter;

    if (std::holds_alternative<guss::core::config::RestApiConfig>(config.adapter())) {
        const auto& rest_cfg = std::get<guss::core::config::RestApiConfig>(config.adapter());
        adapter = std::make_unique<guss::adapters::rest::RestCmsAdapter>(
            rest_cfg, config.site(), config.collections());
        spdlog::info("Using REST API adapter: {}", rest_cfg.base_url);
    } else if (std::holds_alternative<guss::core::config::MarkdownAdapterConfig>(config.adapter())) {
        const auto& markdown_cfg = std::get<guss::core::config::MarkdownAdapterConfig>(config.adapter());
        adapter = std::make_unique<guss::adapters::MarkdownAdapter>(
            markdown_cfg, config.site(), config.collections());
        spdlog::info("Using Markdown adapter: {}", markdown_cfg.content_path.string());
    } else {
        spdlog::error("Unknown adapter type");
        return 1;
    }

    // Create pipeline
    guss::builder::Pipeline pipeline(
        std::move(adapter),
        config.site(),
        config.collections(),
        config.output()
    );

    // Clean if requested
    if (clean_first) {
        auto clean_result = pipeline.clean();
        if (!clean_result) {
            spdlog::error("Clean failed: {}", clean_result.error().format());
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
    auto result = pipeline.build([&bar](std::string_view message, float progress) {
        bar.set_progress(static_cast<size_t>(progress * 100));
        bar.set_option(indicators::option::PostfixText{std::string(message)});
    });

    indicators::show_console_cursor(true);
    std::cout << std::endl;

    if (!result) {
        spdlog::error("Build failed: {}", result.error().format());
        return 1;
    }

    const auto& stats = *result;
    spdlog::info("Build complete!");
    spdlog::info("  Items:    {}", stats.items_rendered);
    spdlog::info("  Archives: {}", stats.archives_rendered);
    spdlog::info("  Assets:   {}", stats.assets_copied);
    if (stats.errors > 0) {
        spdlog::warn("  Errors:   {}", stats.errors);
    }
    spdlog::info("  Duration: {}ms", stats.total_duration.count());

    return stats.errors > 0 ? 1 : 0;
}

int cmd_ping(const std::string& config_path) {
    setup_logging("info");

    spdlog::info("Testing connection...");

    // Load config
    guss::core::config::Config config(config_path);

    // Create adapter
    guss::adapters::AdapterPtr adapter;

    if (std::holds_alternative<guss::core::config::RestApiConfig>(config.adapter())) {
        const auto& rest_cfg = std::get<guss::core::config::RestApiConfig>(config.adapter());
        adapter = std::make_unique<guss::adapters::rest::RestCmsAdapter>(
            rest_cfg, config.site(), config.collections());
        spdlog::info("Adapter: REST API ({})", rest_cfg.base_url);
    } else if (std::holds_alternative<guss::core::config::MarkdownAdapterConfig>(config.adapter())) {
        const auto& markdown_cfg = std::get<guss::core::config::MarkdownAdapterConfig>(config.adapter());
        adapter = std::make_unique<guss::adapters::MarkdownAdapter>(
            markdown_cfg, config.site(), config.collections());
        spdlog::info("Adapter: Markdown ({})", markdown_cfg.content_path.string());
    } else {
        spdlog::error("Unsupported adapter type");
        return 1;
    }

    // Create minimal pipeline just for ping
    guss::builder::Pipeline pipeline(
        std::move(adapter),
        config.site(),
        config.collections(),
        config.output()
    );

    auto result = pipeline.ping();
    if (!result) {
        spdlog::error("Connection failed: {}", result.error().format());
        return 1;
    }

    spdlog::info("Connection successful!");
    return 0;
}

int cmd_clean(const std::string& config_path) {
    setup_logging("info");

    guss::core::config::Config config(config_path);

    spdlog::info("Cleaning output directory: {}", config.output().output_dir.string());

    std::error_code ec;
    if (fs::exists(config.output().output_dir, ec) && !ec) {
        fs::remove_all(config.output().output_dir, ec);
        if (ec) {
            spdlog::error("Failed to clean: {}", ec.message());
            return 1;
        }
        spdlog::info("Output directory cleaned");
    } else {
        spdlog::info("Output directory does not exist");
    }

    return 0;
}

int cmd_serve(std::string_view config_path) {
    spdlog::info("Starting development server...");
    return system(std::format(
        "python3 -m http.server --bind 127.0.0.1 --directory {} 8000",
        config_path).c_str());
}

int main(int argc, char** argv) {
    CLI::App app{"Guss - A pluggable static site generator"};
    app.require_subcommand(1);

    // Global options
    std::string config_path = "guss.yaml";
    std::string serve_path = "dist";

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

    // serve command
    auto serve_cmd = app.add_subcommand("serve", "Start a local development server");
    serve_cmd->add_option("-d, --directory", serve_path, "Directory to serve (default: dist)");

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

    if (serve_cmd->parsed()) {
        return cmd_serve(serve_path);
    }

    return 0;
}
