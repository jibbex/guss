/**
 * @file config.cpp
 * @brief Configuration system implementation for Guss SSG using yaml-cpp.
 */
#include "guss/core/config.hpp"
#include <fstream>

namespace guss::config {

namespace {

// Helper to safely get optional string from YAML node
std::optional<std::string> get_optional_string(const YAML::Node& node, const std::string& key) {
    if (node[key] && !node[key].IsNull()) {
        return node[key].as<std::string>();
    }
    return std::nullopt;
}

// Helper to safely get string with default
std::string get_string(const YAML::Node& node, const std::string& key, const std::string& default_value = "") {
    if (node[key] && !node[key].IsNull()) {
        return node[key].as<std::string>();
    }
    return default_value;
}

// Helper to safely get int with default
int get_int(const YAML::Node& node, const std::string& key, int default_value) {
    if (node[key] && !node[key].IsNull()) {
        return node[key].as<int>();
    }
    return default_value;
}

// Helper to safely get bool with default
bool get_bool(const YAML::Node& node, const std::string& key, bool default_value) {
    if (node[key] && !node[key].IsNull()) {
        return node[key].as<bool>();
    }
    return default_value;
}

SiteConfig parse_site_config(const YAML::Node& node) {
    SiteConfig cfg {};
    if (!node) return cfg;

    cfg.title = get_string(node, "title");
    cfg.description = get_string(node, "description");
    cfg.url = get_string(node, "url");
    cfg.language = get_string(node, "language", "en");
    cfg.logo = get_optional_string(node, "logo");
    cfg.icon = get_optional_string(node, "icon");
    cfg.cover_image = get_optional_string(node, "cover_image");
    cfg.twitter = get_optional_string(node, "twitter");
    cfg.facebook = get_optional_string(node, "facebook");
    return cfg;
}

GhostAdapterConfig parse_ghost_config(const YAML::Node& node) {
    GhostAdapterConfig cfg;
    cfg.api_url = get_string(node, "api_url");
    cfg.content_api_key = get_string(node, "content_api_key");
    cfg.admin_api_key = get_optional_string(node, "admin_api_key");
    cfg.timeout_ms = get_int(node, "timeout_ms", 30000);
    return cfg;
}

WordPressAdapterConfig parse_wordpress_config(const YAML::Node& node) {
    WordPressAdapterConfig cfg;
    cfg.api_url = get_string(node, "api_url");
    cfg.username = get_optional_string(node, "username");
    cfg.app_password = get_optional_string(node, "app_password");
    cfg.timeout_ms = get_int(node, "timeout_ms", 30000);
    return cfg;
}

MarkdownAdapterConfig parse_markdown_config(const YAML::Node& node) {
    MarkdownAdapterConfig cfg;
    cfg.content_path = get_string(node, "content_path", "./content");
    cfg.pages_path = get_string(node, "pages_path", "./pages");
    cfg.authors_path = get_string(node, "authors_path", "./authors");
    cfg.recursive = get_bool(node, "recursive", true);
    return cfg;
}

AdapterConfig parse_adapter_config(const YAML::Node& node) {
    if (!node) {
        return MarkdownAdapterConfig{};
    }

    std::string type = get_string(node, "type", "markdown");

    if (type == "ghost") {
        return parse_ghost_config(node);
    } else if (type == "wordpress") {
        return parse_wordpress_config(node);
    } else {
        return parse_markdown_config(node);
    }
}

PermalinkConfig parse_permalink_config(const YAML::Node& node) {
    PermalinkConfig cfg;
    if (!node) return cfg;

    cfg.post_pattern = get_string(node, "post", cfg.post_pattern);
    cfg.page_pattern = get_string(node, "page", cfg.page_pattern);
    cfg.tag_pattern = get_string(node, "tag", cfg.tag_pattern);
    cfg.category_pattern = get_string(node, "category", cfg.category_pattern);
    cfg.author_pattern = get_string(node, "author", cfg.author_pattern);
    return cfg;
}

WatchConfig parse_watch_config(const YAML::Node& node) {
    WatchConfig cfg;
    if (!node) return cfg;

    cfg.enabled = get_bool(node, "enabled", false);
    cfg.filesystem_watch = get_bool(node, "filesystem_watch", true);
    cfg.webhook_enabled = get_bool(node, "webhook_enabled", false);
    cfg.webhook_port = get_int(node, "webhook_port", 9000);
    cfg.webhook_path = get_string(node, "webhook_path", "/webhook");
    cfg.polling_enabled = get_bool(node, "polling_enabled", false);
    cfg.polling_interval_seconds = get_int(node, "polling_interval_seconds", 300);
    return cfg;
}

OutputConfig parse_output_config(const YAML::Node& node) {
    OutputConfig cfg;
    if (!node) return cfg;

    cfg.output_dir = get_string(node, "output_dir", "./dist");
    cfg.assets_dir = get_string(node, "assets_dir", "./assets");
    cfg.generate_sitemap = get_bool(node, "generate_sitemap", true);
    cfg.generate_rss = get_bool(node, "generate_rss", true);
    cfg.minify_html = get_bool(node, "minify_html", false);
    cfg.copy_assets = get_bool(node, "copy_assets", true);
    return cfg;
}

TemplateConfig parse_template_config(const YAML::Node& node) {
    TemplateConfig cfg;
    if (!node) return cfg;

    cfg.templates_dir = get_string(node, "templates_dir", "./templates");
    cfg.default_post_template = get_string(node, "default_post_template", "post.html");
    cfg.default_page_template = get_string(node, "default_page_template", "page.html");
    cfg.index_template = get_string(node, "index_template", "index.html");
    cfg.tag_template = get_string(node, "tag_template", "tag.html");
    cfg.author_template = get_string(node, "author_template", "author.html");
    return cfg;
}

} // anonymous namespace

Config::Config(std::string_view config_path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(std::string(config_path));
    } catch (const YAML::Exception&) {
        // If file doesn't exist or can't be parsed, use defaults
        return;
    }

    _site = parse_site_config(root["site"]);
    _adapter = parse_adapter_config(root["source"]);
    _permalinks = parse_permalink_config(root["permalinks"]);
    _watch = parse_watch_config(root["watch"]);
    _output = parse_output_config(root["output"]);
    _templates = parse_template_config(root["templates"]);
    _parallel_workers = get_int(root, "parallel_workers", 0);
    _log_level = get_string(root, "log_level", "info");
}

const Config& Config::instance(std::optional<const std::string*> config_path) {
    static std::string cfg_path = "guss.yaml";

    if (config_path.has_value() && config_path.value() != nullptr && !config_path.value()->empty()) {
        cfg_path = *config_path.value();
    }

    static Config instance(cfg_path);
    return instance;
}

const Config& Config::instance() {
    return instance(std::nullopt);
}

error::VoidResult load_config(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return error::make_error(
            error::ErrorCode::ConfigNotFound,
            "Configuration file not found",
            path.string()
        );
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return error::make_error(
            error::ErrorCode::FileReadError,
            "Failed to open configuration file",
            path.string()
        );
    }

    // Initialize the singleton with this path
    const std::string path_str = path.string();
    try {
        Config::instance(&path_str);
    } catch (const YAML::Exception& e) {
        return error::make_error(
            error::ErrorCode::ConfigParseError,
            std::string("YAML parse error: ") + e.what(),
            path.string()
        );
    }

    return {};
}

error::VoidResult validate_yaml(const std::string& yaml_content) {
    try {
        YAML::Node root = YAML::Load(yaml_content);

        // Basic validation - check for required sections
        if (!root["site"]) {
            return error::make_error(
                error::ErrorCode::ConfigMissingField,
                "Missing required 'site' section",
                ""
            );
        }

        if (!root["site"]["title"]) {
            return error::make_error(
                error::ErrorCode::ConfigMissingField,
                "Missing required 'site.title' field",
                ""
            );
        }

        if (!root["site"]["url"]) {
            return error::make_error(
                error::ErrorCode::ConfigMissingField,
                "Missing required 'site.url' field",
                ""
            );
        }

        return {};

    } catch (const YAML::Exception& e) {
        return error::make_error(
            error::ErrorCode::ConfigParseError,
            std::string("YAML parse error: ") + e.what(),
            ""
        );
    }
}

} // namespace guss::config
