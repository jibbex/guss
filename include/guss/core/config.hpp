/**
 * \file config.hpp
 * \brief Configuration system for Guss SSG using yaml-cpp.
 *
 * \details
 * Defines configuration structures parsed from a YAML file (typically `guss.yaml`).
 * Uses std::variant to support multiple adapter types (RestApi, Markdown) and
 * provides typed structs for all configuration sections including site metadata,
 * output settings, and watch triggers.
 *
 * \author Manfred Michaelis
 */
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "guss/core/error.hpp"

namespace guss::core::config {

// ---------------------------------------------------------------------------
// Auth configuration
// ---------------------------------------------------------------------------

/**
 * \brief Authentication configuration for REST API adapters.
 */
struct AuthConfig {
    enum class Type { None, ApiKey, Basic, Bearer };
    Type        type     = Type::None;
    std::string param;    ///< api_key: query param name
    std::string value;    ///< api_key: key value; bearer: token
    std::string username; ///< basic auth username
    std::string password; ///< basic auth password
};

// ---------------------------------------------------------------------------
// Pagination configuration
// ---------------------------------------------------------------------------

/**
 * \brief Pagination configuration for REST API endpoints.
 *
 * \details
 * Supports multiple pagination strategies (evaluated in priority order):
 *  1. `total_pages_header` — one header check on first response, total pages known
 *  2. `total_count_header` — one header check, derive pages via ceil(count / limit)
 *  3. `link_header`        — follow verbatim `Link: rel="next"` URL each round-trip
 *  4. `json_cursor`        — extract cursor token from body each round-trip
 *  5. `json_next_url`      — body field contains full URL to follow verbatim
 *  6. `json_next`          — dot-path non-null sentinel; increment page counter
 *  7. `optimistic_fetching`— blind GET N+1 until empty/404
 *  8. none                 — single page fetch
 */
struct PaginationConfig {
    /**
     * \brief Page parameter name (e.g. "page"); if not set, pagination is disabled.
     */
    std::optional<std::string> page_param;
    /**
     * \brief Limit parameter name (e.g. "limit"); if not set, no limit param is sent. Default value is 15.
     */
    std::optional<std::string>  limit_param;
    /**
     * \brief JSON cursor path (e.g. "meta.next_cursor"); if not set, cursor-based pagination is not used.
     */
    std::optional<std::string> json_cursor;
    /**
     * \brief Cursor query parameter name (e.g. "cursor"); required if json_cursor is set.
     */
    std::optional<std::string> cursor_param;
    /**
     * \brief JSON next page path (e.g. "meta.next_page"); if not set, next page URL is not determined from response body.
     */
    std::optional<std::string> json_next;
    /**
     * \brief Total pages header name (e.g. "X-Total-Pages"); if not set, total pages are not determined from headers.
     */
    std::optional<std::string> total_pages_header;

    std::optional<std::string> total_count_header;  ///< HTTP header with total item count; pages = ceil(count / limit)
    std::optional<std::string> json_next_url;       ///< dot-path whose string value is the full URL of the next page
    std::optional<std::string> offset_param;        ///< query param name for item offset; offset = (page-1)*limit

    int                        limit                = 15;       ///< Default items per page.
    bool                       link_header          = false;    ///< Whether to parse Link header for pagination info.
    bool                       optimistic_fetching  = false;    ///< Whether to blindly fetch page N+1 until empty/404.
};

// ---------------------------------------------------------------------------
// Endpoint configuration
// ---------------------------------------------------------------------------

/// Fixed query params for an endpoint (param name -> value).
using EndpointParams = std::unordered_map<std::string, std::string>;

/**
 * \brief Configuration for a single REST API endpoint (one collection).
 */
struct EndpointConfig {
    std::string path;
    std::string response_key;               ///< JSON key holding items array; empty = root array
    EndpointParams params;                  ///< extra fixed query params
    std::optional<PaginationConfig> pagination; ///< overrides global pagination if set
};

// ---------------------------------------------------------------------------
// Cross-reference configuration
// ---------------------------------------------------------------------------

/**
 * \brief Describes a cross-reference from one collection to another.
 *
 * \details
 * Used to build taxonomy pages (e.g. tags) from data embedded in post items.
 * Example: tags cross-reference from posts via "tags.slug".
 */
struct CrossRefConfig {
    std::string from;                  ///< source collection name (e.g. "posts")
    std::string via;                   ///< dot-path to the linking field (e.g. "tags")
    std::string match_key = "slug";    ///< field within array elements to compare
                                       ///< against target slug; ignored for scalar via values
};

// ---------------------------------------------------------------------------
// Type aliases for REST API config maps
// ---------------------------------------------------------------------------

/// Map of collection name to its endpoint configuration.
using EndpointCfgMap = std::unordered_map<std::string, EndpointConfig>;

/// Map of collection name to its field renaming rules (target field -> source dot-path).
using FieldMapCfg = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

/// Map of collection name to its cross-reference configuration.
using CrossRefCfgMap = std::unordered_map<std::string, CrossRefConfig>;

// ---------------------------------------------------------------------------
// REST API adapter configuration
// ---------------------------------------------------------------------------

/**
 * \brief Configuration for a generic REST CMS adapter.
 *
 * \details
 * Replaces Ghost-specific and WordPress-specific configs.
 * Any REST-based CMS (Ghost, WordPress, Contentful, etc.) can be
 * configured using this struct.
 */
struct RestApiConfig {
    std::string      base_url;
    int              timeout_ms = 30000;
    AuthConfig       auth;
    PaginationConfig pagination;
    EndpointCfgMap   endpoints;
    FieldMapCfg      field_maps;
    CrossRefCfgMap   cross_references;
};

// ---------------------------------------------------------------------------
// Markdown adapter configuration
// ---------------------------------------------------------------------------

/**
 * \brief Configuration for the local Markdown file adapter.
 */
struct MarkdownAdapterConfig {
    std::unordered_map<std::string, std::filesystem::path> collection_paths;
    bool           recursive        = true;
    FieldMapCfg    field_maps       = {};
    CrossRefCfgMap cross_references = {};
};

/**
 * \brief Variant type for adapter configuration selection.
 */
using AdapterConfig = std::variant<RestApiConfig, MarkdownAdapterConfig>;

// ---------------------------------------------------------------------------
// Watch / Output configuration
// ---------------------------------------------------------------------------

/**
 * \brief Watch mode configuration for automatic rebuilds.
 */
struct WatchConfig {
    bool        enabled                  = false;
    bool        filesystem_watch         = true;
    bool        webhook_enabled          = false;
    int         webhook_port             = 9000;
    std::string webhook_path             = "/webhook";
    bool        polling_enabled          = false;
    int         polling_interval_seconds = 300;
};

/**
 * \brief Output and generation settings.
 */
struct OutputConfig {
    std::filesystem::path output_dir    = "./dist";
    std::filesystem::path assets_dir    = "./assets";
    bool generate_sitemap               = true;
    bool generate_rss                   = true;
    bool minify_html                    = false;
    bool copy_assets                    = true;
};

// ---------------------------------------------------------------------------
// Collection configuration
// ---------------------------------------------------------------------------

/**
 * \brief Per-collection rendering configuration.
 *
 * \details
 * Any collection name is valid; no hardcoded types in the pipeline.
 *
 * \par Permalink tokens
 * Tokens like {slug} are plain field lookups against the item's Value.
 * The adapter is responsible for populating all fields the pattern references.
 * Date convenience fields (year, month, day) must be pre-computed by the adapter.
 */
struct CollectionConfig {
    std::string item_template;            ///< Template for individual item pages. Empty = no item pages.
    std::string archive_template;         ///< Template for archive/listing pages. Empty = no archive pages.
    std::string permalink;                ///< Pattern with {token} placeholders; tokens are field lookups.
    int         paginate    = 0;          ///< Items per archive page. 0 = single archive page.
    std::string context_key = "item";     ///< Template variable name for individual item data.
};

/// Map of collection name to its rendering configuration.
using CollectionCfgMap = std::unordered_map<std::string, CollectionConfig>;

// ---------------------------------------------------------------------------
// Site metadata
// ---------------------------------------------------------------------------

/**
 * \brief Site metadata configuration.
 */
struct NavItem {
    std::string label;
    std::string url;
    bool        external = false;
};

struct SiteConfig {
    std::string title;
    std::string description;
    std::string url;
    std::string language = "en";
    std::optional<std::string> logo;
    std::optional<std::string> icon;
    std::optional<std::string> cover_image;
    std::optional<std::string> twitter;
    std::optional<std::string> facebook;
    std::unordered_map<std::string, std::vector<NavItem>> navigation;
};

// ---------------------------------------------------------------------------
// Config class
// ---------------------------------------------------------------------------

class Config final {
public:
    /**
     * \brief Constructs a Config object by reading configuration from a file.
     *
     * \param config_path Path to the YAML configuration file.
     * If the file does not exist or cannot be parsed, all sections use their defaults.
     */
    explicit Config(std::string_view config_path);

    [[nodiscard]] const SiteConfig&       site()        const { return site_; }
    [[nodiscard]] const AdapterConfig&    adapter()     const { return adapter_; }
    [[nodiscard]] const WatchConfig&      watch()       const { return watch_; }
    [[nodiscard]] const OutputConfig&     output()      const { return output_; }
    [[nodiscard]] const CollectionCfgMap& collections() const { return collections_; }
    [[nodiscard]] int                     parallel_workers() const { return parallel_workers_; }
    [[nodiscard]] const std::string&      log_level()   const { return log_level_; }

private:
    SiteConfig       site_;
    AdapterConfig    adapter_;
    WatchConfig      watch_;
    OutputConfig     output_;
    CollectionCfgMap collections_;
    int              parallel_workers_;
    std::string      log_level_;
};

/**
 * \brief Load and validate configuration from a YAML file.
 * \param path Path to the YAML configuration file.
 * \return Config object on success, or Error if the file is missing or unparseable.
 */
error::Result<Config> load_config(const std::filesystem::path& path);

/**
 * \brief Validate YAML configuration content.
 * \param yaml_content Raw YAML content string.
 * \return VoidResult indicating success or Error.
 */
error::VoidResult validate_yaml(const std::string& yaml_content);

} // namespace guss::core::config
