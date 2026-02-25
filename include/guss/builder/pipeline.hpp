/**
 * @file pipeline.hpp
 * @brief Build pipeline orchestration for Guss SSG.
 *
 * @details
 * The pipeline executes in four phases:
 * 1. Fetch - Pull content from CMS adapter
 * 2. Prepare - Compute permalinks, prepare render contexts
 * 3. Render - Generate HTML from templates (parallel)
 * 4. Write - Output files to disk, copy assets
 *
 * Example usage:
 * @code
 * auto adapter = std::make_unique<guss::adapters::GhostAdapter>(cfg);
 * guss::builder::Pipeline pipeline(
 *     std::move(adapter),
 *     site_config,
 *     template_config,
 *     permalink_config,
 *     output_config
 * );
 *
 * auto result = pipeline.build([](const std::string& msg, float progress) {
 *     std::cout << msg << " (" << (progress * 100) << "%)" << std::endl;
 * });
 * @endcode
 */
#pragma once

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include "guss/adapters/adapter.hpp"
#include "guss/core/config.hpp"
#include "guss/core/error.hpp"
#include "guss/core/permalink.hpp"
#include "guss/render/inja_engine.hpp"

namespace guss::builder {

/**
 * @brief Statistics from a build run.
 */
struct BuildStats {
    size_t posts_rendered = 0;
    size_t pages_rendered = 0;
    size_t tag_archives_rendered = 0;
    size_t author_archives_rendered = 0;
    size_t index_pages_rendered = 0;
    size_t assets_copied = 0;
    size_t errors = 0;

    std::chrono::milliseconds fetch_duration{0};
    std::chrono::milliseconds prepare_duration{0};
    std::chrono::milliseconds render_duration{0};
    std::chrono::milliseconds write_duration{0};
    std::chrono::milliseconds total_duration{0};

    [[nodiscard]] size_t total_rendered() const {
        return posts_rendered + pages_rendered + tag_archives_rendered +
               author_archives_rendered + index_pages_rendered;
    }
};

/**
 * @brief Progress callback for build operations.
 * @param message Current operation description.
 * @param progress Progress value between 0.0 and 1.0.
 */
using ProgressCallback = std::function<void(const std::string& message, float progress)>;

/**
 * @brief Build pipeline orchestrating the full SSG workflow.
 */
class Pipeline {
public:
    /**
     * @brief Construct a pipeline with all required components.
     * @param adapter Content adapter for fetching content.
     * @param site_config Site metadata configuration.
     * @param template_config Template configuration.
     * @param permalink_config Permalink pattern configuration.
     * @param output_config Output directory configuration.
     * @param posts_per_page Number of posts per index/archive page.
     */
    Pipeline(
        adapters::AdapterPtr adapter,
        const config::SiteConfig& site_config,
        const config::TemplateConfig& template_config,
        const config::PermalinkConfig& permalink_config,
        const config::OutputConfig& output_config,
        size_t posts_per_page = 10
    );

    /**
     * @brief Execute the full build pipeline.
     * @param progress Optional progress callback.
     * @return Build statistics or error.
     */
    [[nodiscard]] std::expected<BuildStats, error::Error> build(const ProgressCallback &progress = nullptr) const;
    error::VoidResult clean() const;

    /**
     * @brief Clean the output directory.
     * @return Success or error.
     */
    [[nodiscard]] error::VoidResult clean();

    /**
     * @brief Test connection to the content source.
     * @return Success or error with details.
     */
    [[nodiscard]] error::VoidResult ping() const;

private:
    adapters::AdapterPtr adapter_;
    config::SiteConfig site_config_;
    config::TemplateConfig template_config_;
    config::PermalinkConfig permalink_config_;
    config::OutputConfig output_config_;
    size_t posts_per_page_;

    /**
     * @brief Phase 1: Fetch content from adapter.
     */
    [[nodiscard]] error::Result<adapters::FetchResult> phase_fetch(ProgressCallback progress) const;

    /**
     * @brief Phase 2: Prepare content (compute permalinks, etc).
     */
    void phase_prepare(adapters::FetchResult& content) const;

    /**
     * @brief Phase 3: Render all content to HTML.
     */
    [[nodiscard]] error::Result<std::vector<std::pair<std::filesystem::path, std::string>>>
    phase_render(const adapters::FetchResult& content, BuildStats& stats, ProgressCallback progress) const;

    /**
     * @brief Phase 4: Write files to disk.
     */
    [[nodiscard]] error::VoidResult phase_write(
        const std::vector<std::pair<std::filesystem::path, std::string>>& files,
        BuildStats& stats,
        ProgressCallback progress
    ) const;

    /**
     * @brief Copy static assets from theme.
     */
    [[nodiscard]] error::VoidResult copy_assets(BuildStats& stats) const;

    /**
     * @brief Generate sitemap.xml.
     */
    [[nodiscard]] std::string generate_sitemap(const adapters::FetchResult& content) const;

    /**
     * @brief Generate RSS feed.
     */
    [[nodiscard]] std::string generate_rss(const adapters::FetchResult& content) const;
};

} // namespace guss::builder
