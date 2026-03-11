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
#include <string>
#include <unordered_map>
#include <vector>
#include <spdlog/spdlog.h>
#include "guss/core/permalink.hpp"
#include "guss/core/render_item.hpp"
#include "guss/render/context.hpp"
#include "guss/render/engine.hpp"
#include "guss/render/value.hpp"

#ifdef GUSS_USE_OPENMP
#include <omp.h>
#endif

namespace guss::builder {

Pipeline::Pipeline(adapters::AdapterPtr adapter,
                   const config::SiteConfig& site_config,
                   const config::CollectionCfgMap& collections,
                   const config::OutputConfig& output_config)
    : adapter_(std::move(adapter))
    , site_config_(site_config)
    , collections_(collections)
    , output_config_(output_config)
{}

std::expected<BuildStats, error::Error> Pipeline::build(const ProgressCallback& progress) const {
    BuildStats stats;
    auto total_start = std::chrono::steady_clock::now();

    if (progress) progress("Starting build...", 0.0f);

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
    for (const auto& [name, items] : content.items) total_items += items.size();
    spdlog::info("Fetched {} collections, {} total items", total_collections, total_items);

    // Phase 2: Prepare
    spdlog::info("Phase 2: Preparing content");
    auto prepare_start = std::chrono::steady_clock::now();

    if (progress) progress("Preparing content...", 0.25f);
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

    // Copy assets
    auto assets_result = copy_assets(stats);
    if (!assets_result) {
        spdlog::warn("Failed to copy assets: {}", assets_result.error().format());
    }

    stats.write_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - write_start);

    stats.total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - total_start);

    if (progress) progress("Build complete!", 1.0f);

    spdlog::info("Build complete in {}ms ({} items, {} archives)",
        stats.total_duration.count(),
        stats.items_rendered,
        stats.archives_rendered);

    return stats;
}

error::VoidResult Pipeline::clean() const {
    std::error_code ec;
    if (std::filesystem::exists(output_config_.output_dir, ec) && !ec) {
        std::filesystem::remove_all(output_config_.output_dir, ec);
        if (ec) return error::make_error(
            error::ErrorCode::DirectoryCreateError,
            "Failed to clean output directory: " + ec.message(),
            output_config_.output_dir.string()
        );
        spdlog::info("Cleaned output directory: {}", output_config_.output_dir.string());
    }
    return {};
}

error::VoidResult Pipeline::ping() const {
    spdlog::info("Testing connection to {}...", adapter_->adapter_name());
    auto result = adapter_->ping();
    if (!result) {
        return error::make_error(
            error::ErrorCode::AdapterAuthFailed,
            "Could not connect to server",
            result.error().context
        );
    }
    spdlog::info("Connection successful.");
    return {};
}

std::expected<adapters::FetchResult, error::Error> Pipeline::phase_fetch(ProgressCallback progress) const {
    return adapter_->fetch_all([&progress](size_t current, size_t total) {
        if (progress && total > 0) {
            float p = 0.25f * static_cast<float>(current) / static_cast<float>(total);
            progress("Fetching content...", p);
        }
    });
}

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

std::pair<std::vector<render::RenderItem>, size_t> Pipeline::phase_prepare(adapters::FetchResult& result) const {
    std::vector<render::RenderItem> item_pages;
    std::vector<render::RenderItem> archive_pages;

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
        std::vector<render::Value> item_data_vec;
        item_data_vec.reserve(items.size());
        for (const auto& item : items) item_data_vec.push_back(item.data);

        // Collect all tags for use in archive pages
        std::vector<render::Value> all_tags;
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
                std::vector<render::Value> page_slice(
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
                std::unordered_map<std::string, render::Value> pag;
                pag["current"]  = render::Value(static_cast<int64_t>(page));
                pag["total"]    = render::Value(static_cast<int64_t>(total_pages));
                pag["has_prev"] = render::Value(page > 1);
                pag["has_next"] = render::Value(page < total_pages);
                pag["prev_url"] = render::Value(page <= 1 ? std::string("")
                    : page == 2 ? base_url
                    : base_url + "page/" + std::to_string(page - 1) + "/");
                pag["next_url"] = render::Value(page < total_pages
                    ? base_url + "page/" + std::to_string(page + 1) + "/"
                    : std::string(""));

                render::RenderItem ri;
                ri.output_path   = std::move(out);
                ri.template_name = coll_cfg.archive_template;
                ri.context_key   = "";  // no single item; data is null
                ri.extra_context = {
                    {collection_name, render::Value(std::move(page_slice))},
                    {"tags",          render::Value(all_tags)},
                    {"pagination",    render::Value(std::move(pag))},
                };
                archive_pages.push_back(std::move(ri));
            }
        } else {
            // Single (non-paginated) aggregate archive page
            const std::string base_url = archive_url_prefix(coll_cfg.permalink);
            std::filesystem::path out =
                core::PermalinkGenerator::permalink_to_path(base_url);

            render::RenderItem ri;
            ri.output_path   = std::move(out);
            ri.template_name = coll_cfg.archive_template;
            ri.context_key   = "";
            ri.extra_context = {
                {collection_name, render::Value(std::move(item_data_vec))},
                {"collection",    render::Value(std::string(collection_name))},
            };
            archive_pages.push_back(std::move(ri));
        }
    }

    // Item pages first, archive pages last — phase_render uses the index boundary
    // only for stats counting (items vs archives).
    std::vector<render::RenderItem> all_items;
    all_items.reserve(item_pages.size() + archive_pages.size());
    all_items.insert(all_items.end(), item_pages.begin(), item_pages.end());
    all_items.insert(all_items.end(), archive_pages.begin(), archive_pages.end());
    return {std::move(all_items), archive_pages.size()};
}

error::Result<std::vector<std::pair<std::filesystem::path, std::string>>>
Pipeline::phase_render(const std::vector<render::RenderItem>& items,
                       size_t archive_count,
                       const render::Value& site,
                       BuildStats& stats,
                       ProgressCallback progress) const {
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    files.resize(items.size());

    render::Engine engine(std::filesystem::path("./templates"));

    static constexpr std::size_t kCtxBufSize = 8192;
    const size_t item_boundary = items.size() - archive_count;

#ifdef GUSS_USE_OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (size_t i = 0; i < items.size(); ++i) {
        alignas(std::max_align_t) std::byte ctx_buf[kCtxBufSize];
        std::pmr::monotonic_buffer_resource mbr(ctx_buf, kCtxBufSize,
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

    files.erase(std::remove_if(files.begin(), files.end(),
        [](const auto& p){ return p.first.empty(); }), files.end());

    return files;
}

error::VoidResult Pipeline::phase_write(
    const std::vector<std::pair<std::filesystem::path, std::string>>& files,
    BuildStats& stats,
    ProgressCallback progress
) const {
    // Ensure output directory exists
    {
        std::error_code ec;
        std::filesystem::create_directories(output_config_.output_dir, ec);
        if (ec) return error::make_error(
            error::ErrorCode::DirectoryCreateError,
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

        file << content;
        written++;

        if (progress) {
            float p = 0.75f + 0.25f * static_cast<float>(written) / static_cast<float>(files.size());
            progress("Writing files...", p);
        }
    }

    spdlog::debug("Wrote {} files to {}", written, output_config_.output_dir.string());
    return {};
}

error::VoidResult Pipeline::copy_assets(BuildStats& stats) const {
    if (!output_config_.copy_assets) {
        return {};
    }

    auto theme_assets = std::filesystem::path("./templates") / "assets";
    std::error_code ec;
    if (!std::filesystem::exists(theme_assets, ec) || ec) {
        spdlog::debug("No theme assets directory found at {}", theme_assets.string());
        return {};
    }

    auto output_assets = output_config_.output_dir / "assets";

    {
        std::error_code ec;
        std::filesystem::create_directories(output_assets, ec);
        if (ec) return error::make_error(
            error::ErrorCode::FileWriteError,
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
        if (ec) return error::make_error(
            error::ErrorCode::FileWriteError,
            "Failed to create asset subdirectory: " + ec.message(),
            dest.parent_path().string()
        );

        std::filesystem::copy_file(entry.path(), dest,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return error::make_error(
            error::ErrorCode::FileWriteError,
            "Failed to copy asset: " + ec.message(),
            dest.string()
        );

        stats.assets_copied++;
    }

    spdlog::debug("Copied {} assets", stats.assets_copied);

    return {};
}

} // namespace guss::builder
