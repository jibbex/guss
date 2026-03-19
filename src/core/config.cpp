/**
 * @file config.cpp
 * @brief Configuration system implementation for Guss SSG using yaml-cpp.
 */
#include "guss/core/config.hpp"

#include <unordered_map>
#include <yaml-cpp/yaml.h>

namespace guss::core::config {

namespace {

std::optional<std::string> get_optional_string(const YAML::Node& node, const std::string& key) {
    if (node[key] && !node[key].IsNull())
        return node[key].as<std::string>();
    return std::nullopt;
}

std::string get_string(const YAML::Node& node, const std::string& key,
                       const std::string& default_value = "") {
    if (node[key] && !node[key].IsNull())
        return node[key].as<std::string>();
    return default_value;
}

int get_int(const YAML::Node& node, const std::string& key, int default_value) {
    if (node[key] && !node[key].IsNull())
        return node[key].as<int>();
    return default_value;
}

bool get_bool(const YAML::Node& node, const std::string& key, bool default_value) {
    if (node[key] && !node[key].IsNull())
        return node[key].as<bool>();
    return default_value;
}

SiteConfig parse_site_config(const YAML::Node& node) {
    SiteConfig cfg{};
    if (!node) return cfg;

    cfg.title       = get_string(node, "title");
    cfg.description = get_string(node, "description");
    cfg.url         = get_string(node, "url");
    cfg.language    = get_string(node, "language", "en");
    cfg.logo        = get_optional_string(node, "logo");
    cfg.icon        = get_optional_string(node, "icon");
    cfg.cover_image = get_optional_string(node, "cover_image");
    cfg.twitter     = get_optional_string(node, "twitter");
    cfg.facebook    = get_optional_string(node, "facebook");
    return cfg;
}

AuthConfig parse_auth_config(const YAML::Node& node) {
    AuthConfig cfg;
    if (!node) return cfg;

    std::string type_str = get_string(node, "type", "none");
    if      (type_str == "api_key") cfg.type = AuthConfig::Type::ApiKey;
    else if (type_str == "basic")   cfg.type = AuthConfig::Type::Basic;
    else if (type_str == "bearer")  cfg.type = AuthConfig::Type::Bearer;
    else                            cfg.type = AuthConfig::Type::None;

    cfg.param    = get_string(node, "param");
    cfg.value    = get_string(node, "value");
    cfg.username = get_string(node, "username");
    cfg.password = get_string(node, "password");
    return cfg;
}

PaginationConfig parse_pagination_config(const YAML::Node& node) {
    PaginationConfig cfg;
    if (!node) return cfg;

    cfg.page_param          = get_optional_string(node, "page_param");
    cfg.limit_param         = get_optional_string(node, "limit_param");
    cfg.json_next           = get_optional_string(node, "json_next");
    cfg.total_pages_header  = get_optional_string(node, "total_pages_header");
    cfg.json_cursor         = get_optional_string(node, "json_cursor");
    cfg.cursor_param        = get_optional_string(node, "cursor_param");
    cfg.total_count_header  = get_optional_string(node, "total_count_header");
    cfg.json_next_url       = get_optional_string(node, "json_next_url");
    cfg.offset_param        = get_optional_string(node, "offset_param");
    cfg.optimistic_fetching = get_bool(node, "optimistic_fetching", false);
    cfg.link_header         = get_bool(node, "link_header", false);
    cfg.limit               = get_int(node, "limit", 15);
    return cfg;
}

EndpointConfig parse_endpoint_config(const YAML::Node& node) {
    EndpointConfig cfg;
    if (!node) return cfg;

    cfg.path         = get_string(node, "path");
    cfg.response_key = get_string(node, "response_key");

    if (node["params"] && node["params"].IsMap()) {
        for (const auto& kv : node["params"])
            cfg.params[kv.first.as<std::string>()] = kv.second.as<std::string>();
    }

    if (node["pagination"])
        cfg.pagination = parse_pagination_config(node["pagination"]);

    return cfg;
}

RestApiConfig parse_rest_api_config(const YAML::Node& node) {
    RestApiConfig cfg;
    cfg.base_url   = get_string(node, "base_url");
    cfg.timeout_ms = get_int(node, "timeout_ms", 30000);
    cfg.auth       = parse_auth_config(node["auth"]);
    cfg.pagination = parse_pagination_config(node["pagination"]);

    if (node["endpoints"] && node["endpoints"].IsMap()) {
        for (const auto& kv : node["endpoints"])
            cfg.endpoints[kv.first.as<std::string>()] = parse_endpoint_config(kv.second);
    }

    if (node["field_maps"] && node["field_maps"].IsMap()) {
        for (const auto& coll : node["field_maps"]) {
            const std::string coll_name = coll.first.as<std::string>();
            if (!coll.second.IsMap()) continue;
            for (const auto& field : coll.second)
                cfg.field_maps[coll_name][field.first.as<std::string>()] =
                    field.second.as<std::string>();
        }
    }

    if (node["cross_references"] && node["cross_references"].IsMap()) {
        for (const auto& kv : node["cross_references"]) {
            CrossRefConfig cr;
            cr.from      = get_string(kv.second, "from");
            cr.via       = get_string(kv.second, "via");
            if (kv.second["match_key"])
                cr.match_key = get_string(kv.second, "match_key");
            cfg.cross_references[kv.first.as<std::string>()] = cr;
        }
    }

    return cfg;
}

MarkdownAdapterConfig parse_markdown_config(const YAML::Node& node) {
    MarkdownAdapterConfig cfg;
    cfg.content_path = get_string(node, "content_path", "./content");
    cfg.pages_path   = get_string(node, "pages_path",   "./pages");
    cfg.authors_path = get_string(node, "authors_path", "./authors");
    cfg.recursive    = get_bool(node, "recursive", true);
    return cfg;
}

AdapterConfig parse_adapter_config(const YAML::Node& node) {
    if (!node) return MarkdownAdapterConfig{};

    std::string type = get_string(node, "type", "markdown");

    if (type == "rest_api" || type == "ghost" || type == "wordpress") {
        return parse_rest_api_config(node);
    }
    return parse_markdown_config(node);
}

WatchConfig parse_watch_config(const YAML::Node& node) {
    WatchConfig cfg;
    if (!node) return cfg;

    cfg.enabled                  = get_bool(node, "enabled", false);
    cfg.filesystem_watch         = get_bool(node, "filesystem_watch", true);
    cfg.webhook_enabled          = get_bool(node, "webhook_enabled", false);
    cfg.webhook_port             = get_int(node,  "webhook_port", 9000);
    cfg.webhook_path             = get_string(node, "webhook_path", "/webhook");
    cfg.polling_enabled          = get_bool(node, "polling_enabled", false);
    cfg.polling_interval_seconds = get_int(node,  "polling_interval_seconds", 300);
    return cfg;
}

OutputConfig parse_output_config(const YAML::Node& node) {
    OutputConfig cfg;
    if (!node) return cfg;

    cfg.output_dir       = get_string(node, "output_dir", "./dist");
    cfg.assets_dir       = get_string(node, "assets_dir", "./assets");
    cfg.generate_sitemap = get_bool(node, "generate_sitemap", true);
    cfg.generate_rss     = get_bool(node, "generate_rss", true);
    cfg.minify_html      = get_bool(node, "minify_html", false);
    cfg.copy_assets      = get_bool(node, "copy_assets", true);
    return cfg;
}

CollectionConfig parse_collection_config(const YAML::Node& node) {
    CollectionConfig cfg;
    if (!node) return cfg;

    cfg.item_template    = get_string(node, "item_template");
    cfg.archive_template = get_string(node, "archive_template");
    cfg.permalink        = get_string(node, "permalink");
    cfg.paginate         = get_int(node,    "paginate", 0);
    cfg.context_key      = get_string(node, "context_key", "item");
    return cfg;
}

CollectionCfgMap parse_collections_config(const YAML::Node& node) {
    CollectionCfgMap result;
    if (!node || !node.IsMap()) return result;

    for (const auto& entry : node)
        result[entry.first.as<std::string>()] = parse_collection_config(entry.second);
    return result;
}

} // anonymous namespace

Config::Config(std::string_view config_path)
    : adapter_(MarkdownAdapterConfig{})
    , parallel_workers_(0)
    , log_level_("info")
{
    YAML::Node root;
    try {
        root = YAML::LoadFile(std::string(config_path));
    } catch (const YAML::Exception&) {
        return;
    }

    site_             = parse_site_config(root["site"]);
    adapter_          = parse_adapter_config(root["source"]);
    watch_            = parse_watch_config(root["watch"]);
    output_           = parse_output_config(root["output"]);
    collections_      = parse_collections_config(root["collections"]);
    parallel_workers_ = get_int(root, "parallel_workers", 0);
    log_level_        = get_string(root, "log_level", "info");
}

error::Result<Config> load_config(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return error::make_error(
            error::ErrorCode::ConfigNotFound,
            "Configuration file not found",
            path.string()
        );
    }
    return Config(path.string());
}

error::VoidResult validate_yaml(const std::string& yaml_content) {
    try {
        YAML::Node root = YAML::Load(yaml_content);

        if (!root["site"]) {
            return error::make_error(
                error::ErrorCode::ConfigMissingField,
                "Missing required 'site' section", "");
        }
        if (!root["site"]["title"]) {
            return error::make_error(
                error::ErrorCode::ConfigMissingField,
                "Missing required 'site.title' field", "");
        }
        if (!root["site"]["url"]) {
            return error::make_error(
                error::ErrorCode::ConfigMissingField,
                "Missing required 'site.url' field", "");
        }
        return {};
    } catch (const YAML::Exception& e) {
        return error::make_error(
            error::ErrorCode::ConfigParseError,
            std::string("YAML parse error: ") + e.what(), "");
    }
}

} // namespace guss::core::config
