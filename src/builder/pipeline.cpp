/**
 * @file pipeline.cpp
 * @brief Build pipeline implementation for Guss SSG.
 */
#include "guss/builder/pipeline.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <ranges>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "guss/builder/generators.hpp"
#include "guss/core/permalink.hpp"
#include "guss/core/render_item.hpp"
#include "guss/core/value.hpp"
#include "guss/render/context.hpp"
#include "guss/render/runtime.hpp"

#ifdef GUSS_USE_OPENMP
#include <omp.h>
#endif

namespace guss::builder {

Pipeline::Pipeline(adapters::AdapterPtr adapter,
                   core::config::SiteConfig  site_config,
                   core::config::CollectionCfgMap  collections,
                   core::config::OutputConfig  output_config)
    : adapter_(std::move(adapter))
    , site_config_(std::move(site_config))
    , collections_(std::move(collections))
    , output_config_(std::move(output_config))
{}

std::expected<BuildStats, core::error::Error> Pipeline::build(const ProgressCallback& progress) const {
    BuildStats stats;
    auto total_start = std::chrono::steady_clock::now();

    if (progress) progress(0u);

    // Phase 1: Fetch
    spdlog::info("Phase 1: Fetching content from {}", adapter_->adapter_name());
    auto fetch_start = std::chrono::steady_clock::now();

    auto fetch_result = phase_fetch(progress);
    if (!fetch_result) {
        return std::unexpected(fetch_result.error());
    }
    auto& content = *fetch_result;

    stats.fetch_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - fetch_start);

    size_t total_collections = content.items.size();
    size_t total_items = 0;
    for (const auto &items: content.items | std::views::values) total_items += items.size();
    spdlog::info("Fetched {} collections, {} total items", total_collections, total_items);

    // Phase 2: Prepare
    spdlog::info("Phase 2: Preparing content");
    auto prepare_start = std::chrono::steady_clock::now();

    if (progress) progress(25u);
    auto [render_items, archive_count] = phase_prepare(content);

    stats.prepare_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - prepare_start);

    // Phase 3: Render
    spdlog::info("Phase 3: Rendering templates");
    auto render_start = std::chrono::steady_clock::now();

    auto render_result = phase_render(render_items, archive_count, content.site, stats, progress);
    if (!render_result) {
        return std::unexpected(render_result.error());
    }
    auto& files = *render_result;

    stats.render_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - render_start);

    // Phase 4: Write
    spdlog::info("Phase 4: Writing {} files", files.size());
    auto write_start = std::chrono::steady_clock::now();

    auto write_result = phase_write(files, stats, progress);
    if (!write_result) {
        return std::unexpected(write_result.error());
    }

    stats.write_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - write_start);

    // Copy assets
    auto assets_result = copy_assets(stats);
    if (!assets_result) {
        spdlog::warn("Failed to copy assets: {}", assets_result.error().format());
    }

    // Sitemap
    if (output_config_.generate_sitemap) {
        auto sitemap_result = generate_sitemap(files, content.site, stats);
        if (!sitemap_result)
            spdlog::warn("Sitemap generation failed: {}", sitemap_result.error().format());
    }

    // RSS feed
    if (output_config_.generate_rss) {
        auto rss_result = generate_rss(render_items, content.site, stats);
        if (!rss_result)
            spdlog::warn("RSS generation failed: {}", rss_result.error().format());
    }

    // Robots.txt
    auto robots_result = generate_robots_txt();
    if (!robots_result)
        spdlog::warn("robots.txt generation failed: {}", robots_result.error().format());

    stats.total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - total_start);

    if (progress) progress(100u);

    return stats;
}

core::error::VoidResult Pipeline::clean() const {
    std::error_code ec;
    if (std::filesystem::exists(output_config_.output_dir, ec) && !ec) {
        std::filesystem::remove_all(output_config_.output_dir, ec);
        if (ec) return core::error::make_error(
            core::error::ErrorCode::DirectoryCreateError,
            "Failed to clean output directory: " + ec.message(),
            output_config_.output_dir.string()
        );
        spdlog::info("Cleaned output directory: {}", output_config_.output_dir.string());
    }
    return {};
}

core::error::VoidResult Pipeline::ping() const {
    spdlog::info("Testing connection to {}...", adapter_->adapter_name());
    auto result = adapter_->ping();
    if (!result) {
        return core::error::make_error(
            core::error::ErrorCode::AdapterAuthFailed,
            "Could not connect to server",
            result.error().context
        );
    }
    spdlog::info("Connection successful.");
    return {};
}

std::expected<adapters::FetchResult, core::error::Error> Pipeline::phase_fetch(ProgressCallback progress) const {
    return adapter_->fetch_all([&progress](const size_t current, const size_t total) {
        if (progress && total > 0) {
            const std::uint8_t p = current / total * 100.0f;
            progress(p);
        }
    });
}

} // namespace guss::builder

namespace {

/// Return the static URL prefix of a permalink pattern before the first {token}.
/// e.g. "/{year}/{month}/{slug}/" -> "/"
///      "/tag/{slug}/"            -> "/tag/"
///      "/posts/"                 -> "/posts/"
std::string archive_url_prefix(const std::string& permalink) {
    auto pos = permalink.find('{');
    if (pos == std::string::npos) {
        // No tokens — the whole pattern is the prefix.
        return permalink;
    }
    return permalink.substr(0, pos);
}

} // anonymous namespace

namespace guss::builder {

std::pair<std::vector<core::RenderItem>, size_t> Pipeline::phase_prepare(adapters::FetchResult& result) const {
    std::vector<core::RenderItem> item_pages;
    std::vector<core::RenderItem> archive_pages;

    for (auto& [collection_name, items] : result.items) {
        // Add item pages (pre-built by adapter: non-empty output_path + template_name)
        for (auto& item : items) {
            if (!item.output_path.empty() && !item.template_name.empty()) {
                item_pages.push_back(item);
            }
        }

        // Only generate aggregate archive pages for collections that have BOTH
        // an item_template AND an archive_template. Collections with only an
        // archive_template (e.g. taxonomy pages like tags) generate per-item
        // pages via the adapter and do not get an aggregate archive here.
        auto cfg_it = collections_.find(collection_name);
        if (cfg_it == collections_.end()) continue;
        const auto& coll_cfg = cfg_it->second;
        if (coll_cfg.archive_template.empty() || coll_cfg.item_template.empty()) continue;

        // Build value array of all items for archive templates
        std::vector<core::Value> item_data_vec;
        item_data_vec.reserve(items.size());
        for (const auto& item : items) item_data_vec.push_back(item.data);

        // Collect all tags for use in archive pages
        std::vector<core::Value> all_tags;
        if (auto tag_it = result.items.find("tags"); tag_it != result.items.end()) {
            all_tags.reserve(tag_it->second.size());
            for (const auto& ti : tag_it->second) all_tags.push_back(ti.data);
        }

        const int per_page    = coll_cfg.paginate;
        const int total_items = static_cast<int>(item_data_vec.size());

        if (per_page > 0) {
            const int total_pages = (total_items + per_page - 1) / per_page;
            // URL prefix for this collection's archive, e.g. "/" or "/blog/"
            std::string base_url = archive_url_prefix(coll_cfg.permalink);
            if (!base_url.empty() && base_url.back() != '/') base_url += '/';

            for (int page = 1; page <= total_pages; ++page) {
                int start = (page - 1) * per_page;
                int end   = std::min(start + per_page, total_items);
                std::vector<core::Value> page_slice(
                    item_data_vec.begin() + start,
                    item_data_vec.begin() + end);

                std::filesystem::path out;
                if (page == 1) {
                    out = core::PermalinkGenerator::permalink_to_path(base_url);
                } else {
                    // Strip leading '/' from base_url before building path
                    std::string base_stripped = base_url;
                    if (!base_stripped.empty() && base_stripped.front() == '/')
                        base_stripped = base_stripped.substr(1);
                    out = std::filesystem::path(base_stripped) / "page"
                          / std::to_string(page) / "index.html";
                }

                // Build pagination map
                std::unordered_map<std::string, core::Value> pag;
                pag["current"]  = core::Value(static_cast<int64_t>(page));
                pag["total"]    = core::Value(static_cast<int64_t>(total_pages));
                pag["has_prev"] = core::Value(page > 1);
                pag["has_next"] = core::Value(page < total_pages);
                pag["prev_url"] = core::Value(page <= 1 ? std::string("")
                    : page == 2 ? base_url
                    : base_url + "page/" + std::to_string(page - 1) + "/");
                pag["next_url"] = core::Value(page < total_pages
                    ? base_url + "page/" + std::to_string(page + 1) + "/"
                    : std::string(""));

                core::RenderItem ri;
                ri.output_path   = std::move(out);
                ri.template_name = coll_cfg.archive_template;
                ri.context_key   = "";  // no single item; data is null
                ri.extra_context = {
                    {collection_name, core::Value(std::move(page_slice))},
                    {"tags",          core::Value(all_tags)},
                    {"pagination",    core::Value(std::move(pag))},
                };
                archive_pages.push_back(std::move(ri));
            }
        } else {
            // Single (non-paginated) aggregate archive page
            const std::string base_url = archive_url_prefix(coll_cfg.permalink);
            std::filesystem::path out =
                core::PermalinkGenerator::permalink_to_path(base_url);

            core::RenderItem ri;
            ri.output_path   = std::move(out);
            ri.template_name = coll_cfg.archive_template;
            ri.context_key   = "";
            ri.extra_context = {
                {collection_name, core::Value(std::move(item_data_vec))},
                {"collection",    core::Value(std::string(collection_name))},
            };
            archive_pages.push_back(std::move(ri));
        }
    }

    // Item pages first, archive pages last — phase_render uses the index boundary
    // only for stats counting (items vs archives).
    std::vector<core::RenderItem> all_items;
    all_items.reserve(item_pages.size() + archive_pages.size());
    all_items.insert(all_items.end(), item_pages.begin(), item_pages.end());
    all_items.insert(all_items.end(), archive_pages.begin(), archive_pages.end());
    return {std::move(all_items), archive_pages.size()};
}

core::error::Result<std::vector<std::pair<std::filesystem::path, std::string>>>
Pipeline::phase_render(const std::vector<core::RenderItem>& items,
                       size_t archive_count,
                       const core::Value& site,
                       BuildStats& stats,
                       const ProgressCallback& progress) {
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    files.resize(items.size());

    render::Runtime engine(std::filesystem::path("./templates"));
    const size_t item_boundary = items.size() - archive_count;

#ifdef GUSS_USE_OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (size_t i = 0; i < items.size(); ++i) {
        alignas(std::max_align_t) std::byte ctx_buf[CONTEXT_BUFFER_SIZE];
        std::pmr::monotonic_buffer_resource mbr(ctx_buf, CONTEXT_BUFFER_SIZE,
                                                std::pmr::new_delete_resource());
        render::Context ctx(&mbr);
        ctx.set("site", site);

        // Expose item data under its context_key (and always under "item" for compat).
        if (!items[i].context_key.empty() && !items[i].data.is_null()) {
            ctx.set("item", items[i].data);
            if (items[i].context_key != "item")
                ctx.set(items[i].context_key, items[i].data);
        }

        // Inject all extra context variables at root level.
        for (const auto& [key, val] : items[i].extra_context) {
            ctx.set(key, val);
        }

        auto result = engine.render(items[i].template_name, ctx);
        if (!result) {
            spdlog::error("Failed to render {}: {}",
                items[i].output_path.string(), result.error().message);
#ifdef GUSS_USE_OPENMP
            #pragma omp atomic
#endif
            ++stats.errors;
        } else {
            files[i] = {items[i].output_path, std::move(*result)};
            if (i < item_boundary) {
#ifdef GUSS_USE_OPENMP
                #pragma omp atomic
#endif
                ++stats.items_rendered;
            } else {
#ifdef GUSS_USE_OPENMP
                #pragma omp atomic
#endif
                ++stats.archives_rendered;
            }
        }
    }

    std::erase_if(files, [](const auto &p) {
        return p.first.empty();
    });

    return files;
}

core::error::VoidResult Pipeline::phase_write(
    const std::vector<std::pair<std::filesystem::path, std::string>>& files,
    BuildStats& stats,
    const ProgressCallback& progress
) const {
    // Ensure output directory exists
    {
        std::error_code ec;
        std::filesystem::create_directories(output_config_.output_dir, ec);
        if (ec) return core::error::make_error(
            core::error::ErrorCode::DirectoryCreateError,
            "Failed to create output directory: " + ec.message(),
            output_config_.output_dir.string()
        );
    }

    size_t written = 0;
    for (const auto& [rel_path, content] : files) {
        auto full_path = output_config_.output_dir / rel_path;

        std::error_code ec;
        std::filesystem::create_directories(full_path.parent_path(), ec);
        if (ec) {
            spdlog::error("Failed to create directory for {}: {}", rel_path.string(), ec.message());
            stats.errors++;
            continue;
        }

        // Write file
        std::ofstream file(full_path);
        if (!file) {
            spdlog::error("Failed to open file for writing: {}", full_path.string());
            stats.errors++;
            continue;
        }

        if (output_config_.minify_html && full_path.extension() == ".html") {
            file << minify_html(content);
            stats.files_minified++;
        } else {
            file << content;
        }
        written++;

        if (progress) {
            const std::uint8_t p = written / files.size() * 100;
            progress(p);
        }
    }

    spdlog::debug("Wrote {} files to {}", written, output_config_.output_dir.string());
    return {};
}

core::error::VoidResult Pipeline::copy_assets(BuildStats& stats) const {
    if (!output_config_.copy_assets) {
        return {};
    }

    auto theme_assets = std::filesystem::path("./templates") / "assets";

    {
        std::error_code ec;
        if (!std::filesystem::exists(theme_assets, ec) || ec) {
            spdlog::debug("No theme assets directory found at {}", theme_assets.string());
            return {};
        }
    }

    auto output_assets = output_config_.output_dir / "assets";

    {
        std::error_code ec;
        std::filesystem::create_directories(output_assets, ec);
        if (ec) return core::error::make_error(
            core::error::ErrorCode::FileWriteError,
            "Failed to create assets directory: " + ec.message(),
            output_assets.string()
        );
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(theme_assets)) {
        if (!entry.is_regular_file()) continue;

        auto rel_path = std::filesystem::relative(entry.path(), theme_assets);
        auto dest = output_assets / rel_path;

        std::error_code ec;
        std::filesystem::create_directories(dest.parent_path(), ec);
        if (ec) return core::error::make_error(
            core::error::ErrorCode::FileWriteError,
            "Failed to create asset subdirectory: " + ec.message(),
            dest.parent_path().string()
        );

        std::filesystem::copy_file(entry.path(), dest,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return core::error::make_error(
            core::error::ErrorCode::FileWriteError,
            "Failed to copy asset: " + ec.message(),
            dest.string()
        );

        stats.assets_copied++;
    }

    spdlog::debug("Copied {} assets", stats.assets_copied);

    return {};
}

core::error::VoidResult Pipeline::generate_sitemap(
    const std::vector<std::pair<std::filesystem::path, std::string>>& files,
    const core::Value& site,
    BuildStats& stats) const
{
    const std::string base_url = site["url"].to_string();
    const std::string xml = guss::builder::generate_sitemap_xml(files, base_url);

    auto out_path = output_config_.output_dir / "sitemap.xml";
    std::ofstream f(out_path);
    if (!f) return core::error::make_error(
        core::error::ErrorCode::FileWriteError,
        "Failed to write sitemap.xml",
        out_path.string());

    f << xml;
    stats.extras_generated++;
    const size_t url_count = std::ranges::count_if(files, [](const auto& p) {
        return !p.first.empty();
    });
    spdlog::debug("Generated sitemap.xml ({} URLs)", url_count);
    return {};
}

core::error::VoidResult Pipeline::generate_rss(
    const std::vector<core::RenderItem>& items,
    const core::Value& site,
    BuildStats& stats) const
{
    const std::string base_url = site["url"].to_string();
    const std::string xml = guss::builder::generate_rss_xml(items, base_url, site);

    auto out_path = output_config_.output_dir / "feed.xml";
    std::ofstream f(out_path);
    if (!f) return core::error::make_error(
        core::error::ErrorCode::FileWriteError,
        "Failed to write feed.xml",
        out_path.string());

    f << xml;
    stats.extras_generated++;
    spdlog::debug("Generated feed.xml");
    return {};
}

core::error::VoidResult Pipeline::generate_robots_txt() const {
    const std::string content = guss::builder::generate_robots_txt(output_config_.robots_txt);

    auto out_path = output_config_.output_dir / "robots.txt";
    std::ofstream f(out_path);
    if (!f) return core::error::make_error(
        core::error::ErrorCode::FileWriteError,
        "Failed to write robots.txt",
        out_path.string());

    f << content;
    spdlog::debug("Generated robots.txt");
    return {};

}

} // namespace guss::builder
