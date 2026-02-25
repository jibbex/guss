/**
 * \file config.hpp
 * \brief Configuration system for Apex SSG using yaml-cpp.
 *
 * \details
 * This file defines the configuration structures for Apex SSG, parsed from a YAML file
 * (typically `apex.yaml`). It uses std::variant to support multiple adapter types
 * (Markdown, Ghost CMS, WordPress) and provides typed structs for all configuration
 * sections including site metadata, permalinks, output settings, and watch triggers.
 *
 * Example usage:
 * \code
 * #include "apex/config.hpp"
 * #include <iostream>
 *
 * int main() {
 *     auto result = apex::config::load_config("apex.yaml");
 *     if (!result) {
 *         std::cerr << "Error: " << result.error().format() << std::endl;
 *         return 1;
 *     }
 *
 *     const auto& cfg = *result;
 *     std::cout << "Site: " << cfg.site.title << std::endl;
 *     std::cout << "Output: " << cfg.output.output_dir << std::endl;
 *
 *     // Check adapter type
 *     if (std::holds_alternative<apex::config::MarkdownAdapterConfig>(cfg.adapter)) {
 *         std::cout << "Using Markdown adapter" << std::endl;
 *     }
 *
 *     return 0;
 * }
 * \endcode
 *
 * \author Manfred Michaelis
 * \date 2025
 */
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "guss/core/error.hpp"

namespace guss::config {

/**
 * \brief Configuration for the Ghost CMS adapter.
 */
struct GhostAdapterConfig {
    std::string api_url;
    std::string content_api_key;
    std::optional<std::string> admin_api_key;
    int timeout_ms = 30000;
};

/**
 * \brief Configuration for the WordPress REST API adapter.
 */
struct WordPressAdapterConfig {
    std::string api_url;
    std::optional<std::string> username;
    std::optional<std::string> app_password;
    int timeout_ms = 30000;
};

/**
 * \brief Configuration for the local Markdown file adapter.
 */
struct MarkdownAdapterConfig {
    std::filesystem::path content_path;
    std::filesystem::path pages_path;
    std::filesystem::path authors_path;
    bool recursive = true;
};

/**
 * \brief Variant type for adapter configuration selection.
 */
using AdapterConfig = std::variant<GhostAdapterConfig, WordPressAdapterConfig, MarkdownAdapterConfig>;

/**
 * \brief Permalink pattern configuration with date token support.
 *
 * \details
 * Supported tokens: {slug}, {year}, {month}, {day}, {id}, {title}
 */
struct PermalinkConfig {
    std::string post_pattern = "/{year}/{month}/{slug}/";
    std::string page_pattern = "/{slug}/";
    std::string tag_pattern = "/tag/{slug}/";
    std::string category_pattern = "/category/{slug}/";
    std::string author_pattern = "/author/{slug}/";
};

/**
 * \brief Watch mode configuration for automatic rebuilds.
 */
struct WatchConfig {
    bool enabled = false;
    bool filesystem_watch = true;
    bool webhook_enabled = false;
    int webhook_port = 9000;
    std::string webhook_path = "/webhook";
    bool polling_enabled = false;
    int polling_interval_seconds = 300;
};

/**
 * \brief Output and generation settings.
 */
struct OutputConfig {
    std::filesystem::path output_dir = "./dist";
    std::filesystem::path assets_dir = "./assets";
    bool generate_sitemap = true;
    bool generate_rss = true;
    bool minify_html = false;
    bool copy_assets = true;
};

/**
 * \brief Template configuration for Inja rendering.
 */
struct TemplateConfig {
    std::filesystem::path templates_dir = "./templates";
    std::string default_post_template = "post.html";
    std::string default_page_template = "page.html";
    std::string index_template = "index.html";
    std::string tag_template = "tag.html";
    std::string author_template = "author.html";
};

/**
 * \brief Site metadata configuration.
 */
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
};

class Config final {
public:
    /**
     * \brief Returns the singleton instance of the Config class.
     *
     * \param config_path Pointer to the path of the configuration file.
     * The configuration will be initialized with this path on the first call.
     * Subsequent calls will return the already initialized instance, ignoring this parameter.
     * \return A const reference to the singleton instance of the Config.
     */
    static const Config& instance(std::optional<const std::string *> config_path);
    static const Config& instance();

    const SiteConfig &site() const { return _site; }
    const AdapterConfig& adapter() const { return _adapter; }
    const PermalinkConfig& permalinks() const { return _permalinks; }
    const WatchConfig& watch() const { return _watch; }
    const OutputConfig& output() const { return _output; }
    const TemplateConfig& templates() const { return _templates; }
    int parallel_workers() const { return _parallel_workers; }
    const std::string& log_level() const { return _log_level; }

    Config() = delete;             ///< Deleted default constructor to prevent direct instantiation.
    Config(const Config&) = delete; ///< Deleted copy constructor to prevent copying.
    Config& operator=(const Config&) = delete; ///< Deleted copy assignment operator to prevent copying.
    Config(Config&&) = delete;      ///< Deleted move constructor to prevent moving.

private:
    /**
     * \brief Constructs a Config object by reading configuration from a specified file.
     *
     * \param config_path Path to the configuration file.
     */
    explicit Config(std::string_view config_path);

    /**
     * \defgroup ConfigMembers Private member variables for configuration sections.
     * \{
     * \brief Configuration sections for site metadata, adapter settings, permalinks, watch mode, output settings, and templates.
     * Each section is represented by a struct that encapsulates related configuration options.
    */

    SiteConfig _site;
    AdapterConfig _adapter;
    PermalinkConfig _permalinks;
    WatchConfig _watch;
    OutputConfig _output;
    TemplateConfig _templates;
    int _parallel_workers  { 0 };
    std::string _log_level { "info" };

    /**
    * \}
    */
};

/**
 * \brief Load and validate configuration from a YAML file.
 * \param path Path to the YAML configuration file.
 * \return VoidResult indicating success or Error.
 * \note This initializes the singleton Config::instance() with the given path.
 */
error::VoidResult load_config(const std::filesystem::path& path);

/**
 * \brief Validate YAML configuration content.
 * \param yaml_content Raw YAML content string.
 * \return VoidResult indicating success or Error.
 */
error::VoidResult validate_yaml(const std::string& yaml_content);

} // namespace guss::config
