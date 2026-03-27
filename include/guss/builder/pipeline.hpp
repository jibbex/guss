/**
 * \file pipeline.hpp
 * \brief Build pipeline orchestration for Guss SSG.
 *
 * \details
 * The pipeline executes in four phases:
 * 1. Fetch   - Pull content from CMS adapter
 * 2. Prepare - Expand CollectionMap into flat RenderItem list with archive pages
 * 3. Render  - Parallel template rendering (OpenMP)
 * 4. Write   - Output files to disk, copy assets
 */
#pragma once

#include "guss/adapters/adapter.hpp"
#include "guss/core/config.hpp"
#include "guss/core/render_item.hpp"
#include "guss/core/error.hpp"
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>

namespace guss::builder {

/**
 * \brief Statistics from a build run.
 */
struct BuildStats {
    size_t items_rendered    = 0;  ///< Individual content pages rendered
    size_t archives_rendered = 0;  ///< Archive/listing pages rendered
    size_t assets_copied     = 0;
    size_t files_minified    = 0;  ///< HTML files minified (when minify_html: true)
    size_t extras_generated  = 0;  ///< Extra files written (sitemap.xml, feed.xml)
    size_t errors            = 0;
    std::chrono::milliseconds fetch_duration{};
    std::chrono::milliseconds prepare_duration{};
    std::chrono::milliseconds render_duration{};
    std::chrono::milliseconds write_duration{};
    std::chrono::milliseconds total_duration{};
};

/**
 * \brief Progress callback for build operations.
 */
using ProgressCallback = std::function<void(std::string_view label, float fraction)>;

/**
 * \brief Build pipeline orchestrating the full SSG workflow.
 */
class Pipeline final {
public:
    /**
     * \brief Construct a pipeline.
     * \param adapter        Content adapter.
     * \param site_config    Site metadata (used as fallback if adapter site Value is empty).
     * \param collections    Per-collection rendering configuration (from collections: YAML block).
     * \param output_config  Output directory and asset settings.
     */
    Pipeline(adapters::AdapterPtr adapter,
             const core::config::SiteConfig& site_config,
             const core::config::CollectionCfgMap& collections,
             const core::config::OutputConfig& output_config);

    /**
     * \brief Execute the full build pipeline.
     * \param progress Optional progress callback.
     * \return Build statistics or error.
     */
    [[nodiscard]] std::expected<BuildStats, core::error::Error>
    build(const ProgressCallback& progress = nullptr) const;

    /**
     * \brief Clean the output directory.
     */
    [[nodiscard]] core::error::VoidResult clean() const;

    /**
     * \brief Test connectivity to the content source.
     */
    [[nodiscard]] core::error::VoidResult ping() const;

private:
    [[nodiscard]] std::expected<adapters::FetchResult, core::error::Error>
    phase_fetch(ProgressCallback progress) const;

    /// Returns flat list of all RenderItems (item pages + archive pages) and archive page count.
    [[nodiscard]] std::pair<std::vector<core::RenderItem>, size_t>
    phase_prepare(adapters::FetchResult& result) const;

    [[nodiscard]] core::error::Result<std::vector<std::pair<std::filesystem::path, std::string>>>
    phase_render(const std::vector<core::RenderItem>& items,
                 size_t archive_count,
                 const core::Value& site,
                 BuildStats& stats,
                 ProgressCallback progress) const;

    [[nodiscard]] core::error::VoidResult phase_write(
        const std::vector<std::pair<std::filesystem::path, std::string>>& files,
        BuildStats& stats,
        ProgressCallback progress) const;

    [[nodiscard]] core::error::VoidResult copy_assets(BuildStats& stats) const;

    [[nodiscard]] core::error::VoidResult
    generate_sitemap(const std::vector<std::pair<std::filesystem::path, std::string>>& files,
                     const core::Value& site,
                     BuildStats& stats) const;

    [[nodiscard]] core::error::VoidResult
    generate_rss(const std::vector<core::RenderItem>& items,
                 const core::Value& site,
                 BuildStats& stats) const;

	[[nodiscard]] core::error::VoidResult generate_robots_txt() const;

    adapters::AdapterPtr              adapter_;
    core::config::SiteConfig          site_config_;
    core::config::CollectionCfgMap    collections_;
    core::config::OutputConfig        output_config_;
};

} // namespace guss::builder
