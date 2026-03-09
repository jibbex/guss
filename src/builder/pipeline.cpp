/**
 * @file pipeline.cpp
 * @brief Build pipeline implementation for Guss SSG.
 */
#include "guss/builder/pipeline.hpp"
#include <algorithm>
#include <ctime>
#include <expected>
#include <fstream>
#include <memory_resource>
#include <spdlog/spdlog.h>
#include <sstream>

#include "guss/core/model/author.hpp"
#include "guss/core/model/page.hpp"
#include "guss/core/model/post.hpp"
#include "guss/core/model/taxonomy.hpp"
#include "guss/core/permalink.hpp"
#include "guss/render/context.hpp"
#include "guss/render/engine.hpp"
#include "guss/render/value.hpp"

#ifdef GUSS_USE_OPENMP
#include <omp.h>
#endif

namespace guss::builder {

Pipeline::Pipeline(
    adapters::AdapterPtr adapter,
    const config::SiteConfig& site_config,
    const config::TemplateConfig& template_config,
    const config::PermalinkConfig& permalink_config,
    const config::OutputConfig& output_config,
    size_t posts_per_page
)
    : adapter_(std::move(adapter))
    , site_config_(site_config)
    , template_config_(template_config)
    , permalink_config_(permalink_config)
    , output_config_(output_config)
    , posts_per_page_(posts_per_page)
{}

std::expected<BuildStats, error::Error> Pipeline::build(const ProgressCallback& progress) const {
    BuildStats stats;
    auto total_start = std::chrono::steady_clock::now();

    if (progress) progress("Starting build...", 0.0f);

    // Phase 1: Fetch
    spdlog::info(fmt::format("Phase 1: Fetching content from {}", adapter_->adapter_name()));
    auto fetch_start = std::chrono::steady_clock::now();

    auto fetch_result = phase_fetch(progress);
    if (!fetch_result) {
        return std::unexpected(fetch_result.error());
    }
    auto& content = *fetch_result;

    stats.fetch_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - fetch_start);

    spdlog::info(fmt::format("Fetched {} posts, {} pages, {} authors, {} tags",
        content.posts.size(), content.pages.size(),
        content.authors.size(), content.tags.size()));

    // Phase 2: Prepare
    spdlog::info("Phase 2: Preparing content");
    auto prepare_start = std::chrono::steady_clock::now();

    if (progress) progress("Preparing content...", 0.25f);
    phase_prepare(content);

    stats.prepare_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - prepare_start);

    // Phase 3: Render
    spdlog::info("Phase 3: Rendering templates");
    auto render_start = std::chrono::steady_clock::now();

    auto render_result = phase_render(content, stats, progress);
    if (!render_result) {
        return std::unexpected(render_result.error());
    }
    auto& files = *render_result;

    stats.render_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - render_start);

    // Phase 4: Write
    spdlog::info(fmt::format("Phase 4: Writing {} files", files.size()));
    auto write_start = std::chrono::steady_clock::now();

    auto write_result = phase_write(files, stats, progress);
    if (!write_result) {
        return std::unexpected(write_result.error());
    }

    // Copy assets
    auto assets_result = copy_assets(stats);
    if (!assets_result) {
        spdlog::warn(fmt::format("Failed to copy assets: {}", assets_result.error().format()));
    }

    // Generate sitemap
    if (output_config_.generate_sitemap) {
        auto sitemap = generate_sitemap(content);
        auto sitemap_path = output_config_.output_dir / "sitemap.xml";
        std::ofstream sitemap_file(sitemap_path);
        if (sitemap_file) {
            sitemap_file << sitemap;
            spdlog::debug("Generated sitemap.xml");
        }
    }

    // Generate RSS
    if (output_config_.generate_rss) {
        auto rss = generate_rss(content);
        auto rss_path = output_config_.output_dir / "rss.xml";
        std::ofstream rss_file(rss_path);
        if (rss_file) {
            rss_file << rss;
            spdlog::debug("Generated rss.xml");
        }
    }

    stats.write_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - write_start);

    stats.total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - total_start);

    if (progress) progress("Build complete!", 1.0f);

    spdlog::info(fmt::format("Build complete in {}ms ({} posts, {} pages, {} archives)",
        stats.total_duration.count(),
        stats.posts_rendered,
        stats.pages_rendered,
        stats.tag_archives_rendered + stats.author_archives_rendered));

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
        spdlog::info(fmt::format("Cleaned output directory: {}", output_config_.output_dir.string()));
    }
    return {};
}

error::VoidResult Pipeline::ping() const {
    spdlog::info(fmt::format("Testing connection to {}...", adapter_->adapter_name()));

    // Try to fetch authors as a lightweight connectivity test
    auto result = adapter_->fetch_authors();
    if (!result) {
        return error::make_error(
            error::ErrorCode::AdapterAuthFailed,
            "Could not connect to server",
            output_config_.output_dir.string()
        );
    }

    spdlog::info(fmt::format("Connection successful! Found {} authors.", result->size()));
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

void Pipeline::phase_prepare(adapters::FetchResult& content) const {
    PermalinkGenerator gen(permalink_config_);

    // Sort posts by published date (newest first)
    std::ranges::sort(content.posts,
        [](const model::Post& a, const model::Post& b) {
            auto a_time = a.published_at.value_or(a.created_at);
            auto b_time = b.published_at.value_or(b.created_at);
            return a_time > b_time;
        });

    // Compute permalinks for posts
    for (auto& post : content.posts) {
        post.permalink = gen.for_post(post);
        post.output_path = PermalinkGenerator::permalink_to_path(post.permalink).string();
    }

    // Compute permalinks for pages
    for (auto& page : content.pages) {
        page.permalink = gen.for_page(page);
        page.output_path = PermalinkGenerator::permalink_to_path(page.permalink).string();
    }

    // Update tag post counts
    for (auto& tag : content.tags) {
        tag.post_count = std::ranges::count_if(content.posts,
            [&tag](const model::Post& post) {
                return std::ranges::any_of(post.tags,
                    [&tag](const model::Tag& t) { return t.id == tag.id; });
            });
    }

    spdlog::debug(fmt::format("Prepared {} posts and {} pages with permalinks",
        content.posts.size(), content.pages.size()));
}

namespace {

// ---------------------------------------------------------------------------
// Domain-struct → Value converters
// No JSON serialisation, no simdjson parsing — direct field mapping.
// ---------------------------------------------------------------------------

render::Value author_to_value(const model::Author& a) {
    std::unordered_map<std::string, render::Value> m;
    m["id"]   = render::Value(std::string_view(a.id));
    m["name"] = render::Value(std::string_view(a.name));
    m["slug"] = render::Value(std::string_view(a.slug));
    if (a.email)         m["email"]         = render::Value(std::string_view(*a.email));
    if (a.bio)           m["bio"]           = render::Value(std::string_view(*a.bio));
    if (a.profile_image) m["profile_image"] = render::Value(std::string_view(*a.profile_image));
    if (a.website)       m["website"]       = render::Value(std::string_view(*a.website));
    if (a.twitter)       m["twitter"]       = render::Value(std::string_view(*a.twitter));
    if (a.facebook)      m["facebook"]      = render::Value(std::string_view(*a.facebook));
    return render::Value(std::move(m));
}

render::Value tag_to_value(const model::Tag& t) {
    std::unordered_map<std::string, render::Value> m;
    m["id"]         = render::Value(std::string_view(t.id));
    m["name"]       = render::Value(std::string_view(t.name));
    m["slug"]       = render::Value(std::string_view(t.slug));
    m["post_count"] = render::Value(static_cast<int64_t>(t.post_count));
    if (t.description)   m["description"]   = render::Value(std::string_view(*t.description));
    if (t.feature_image) m["feature_image"] = render::Value(std::string_view(*t.feature_image));
    return render::Value(std::move(m));
}

render::Value post_to_value(const model::Post& post) {
    auto fmt_time = [](const std::chrono::system_clock::time_point& tp) {
        auto t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return std::string(buf);
    };

    std::unordered_map<std::string, render::Value> m;
    m["id"]               = render::Value(std::string_view(post.id));
    m["title"]            = render::Value(std::string_view(post.title));
    m["slug"]             = render::Value(std::string_view(post.slug));
    m["content"]          = render::Value(std::string_view(post.content_html));
    m["content_markdown"] = render::Value(std::string_view(post.content_markdown));
    m["excerpt"]          = render::Value(std::string_view(post.excerpt));
    m["status"]           = render::Value(model::post_status_to_string(post.status));
    m["permalink"]        = render::Value(std::string_view(post.permalink));
    m["output_path"]      = render::Value(std::string_view(post.output_path));

    if (post.feature_image)     m["feature_image"]     = render::Value(std::string_view(*post.feature_image));
    if (post.feature_image_alt) m["feature_image_alt"] = render::Value(std::string_view(*post.feature_image_alt));
    if (post.meta_title)        m["meta_title"]        = render::Value(std::string_view(*post.meta_title));
    if (post.meta_description)  m["meta_description"]  = render::Value(std::string_view(*post.meta_description));
    if (post.canonical_url)     m["canonical_url"]     = render::Value(std::string_view(*post.canonical_url));
    if (post.custom_template)   m["custom_template"]   = render::Value(std::string_view(*post.custom_template));

    m["created_at"] = render::Value(fmt_time(post.created_at));
    if (post.published_at) {
        m["published_at"] = render::Value(fmt_time(*post.published_at));
        auto t = std::chrono::system_clock::to_time_t(*post.published_at);
        std::tm tm = *std::gmtime(&t);
        m["year"]  = render::Value(static_cast<int64_t>(tm.tm_year + 1900));
        m["month"] = render::Value(static_cast<int64_t>(tm.tm_mon + 1));
        m["day"]   = render::Value(static_cast<int64_t>(tm.tm_mday));
    }
    if (post.updated_at) m["updated_at"] = render::Value(fmt_time(*post.updated_at));

    std::vector<render::Value> authors;
    for (const auto& a : post.authors) authors.push_back(author_to_value(a));
    m["authors"] = render::Value(std::move(authors));
    if (!post.authors.empty()) m["author"] = author_to_value(post.authors[0]);

    std::vector<render::Value> tags;
    for (const auto& t : post.tags) tags.push_back(tag_to_value(t));
    m["tags"] = render::Value(std::move(tags));

    return render::Value(std::move(m));
}

render::Value page_to_value(const model::Page& page) {
    auto fmt_time = [](const std::chrono::system_clock::time_point& tp) {
        auto t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return std::string(buf);
    };

    std::unordered_map<std::string, render::Value> m;
    m["id"]               = render::Value(std::string_view(page.id));
    m["title"]            = render::Value(std::string_view(page.title));
    m["slug"]             = render::Value(std::string_view(page.slug));
    m["content"]          = render::Value(std::string_view(page.content_html));
    m["content_markdown"] = render::Value(std::string_view(page.content_markdown));
    m["status"]           = render::Value(model::page_status_to_string(page.status));
    m["permalink"]        = render::Value(std::string_view(page.permalink));
    m["output_path"]      = render::Value(std::string_view(page.output_path));
    m["menu_order"]       = render::Value(static_cast<int64_t>(page.menu_order));
    m["show_in_menu"]     = render::Value(page.show_in_menu);

    if (page.feature_image)     m["feature_image"]     = render::Value(std::string_view(*page.feature_image));
    if (page.feature_image_alt) m["feature_image_alt"] = render::Value(std::string_view(*page.feature_image_alt));
    if (page.meta_title)        m["meta_title"]        = render::Value(std::string_view(*page.meta_title));
    if (page.meta_description)  m["meta_description"]  = render::Value(std::string_view(*page.meta_description));
    if (page.canonical_url)     m["canonical_url"]     = render::Value(std::string_view(*page.canonical_url));
    if (page.custom_template)   m["custom_template"]   = render::Value(std::string_view(*page.custom_template));
    if (page.parent_slug)       m["parent_slug"]       = render::Value(std::string_view(*page.parent_slug));

    m["created_at"] = render::Value(fmt_time(page.created_at));
    if (page.published_at) m["published_at"] = render::Value(fmt_time(*page.published_at));
    if (page.updated_at)   m["updated_at"]   = render::Value(fmt_time(*page.updated_at));

    std::vector<render::Value> authors;
    for (const auto& a : page.authors) authors.push_back(author_to_value(a));
    m["authors"] = render::Value(std::move(authors));
    if (!page.authors.empty()) m["author"] = author_to_value(page.authors[0]);

    return render::Value(std::move(m));
}

render::Value site_to_value(const config::SiteConfig& site) {
    std::unordered_map<std::string, render::Value> m;
    m["title"]       = render::Value(std::string_view(site.title));
    m["description"] = render::Value(std::string_view(site.description));
    m["url"]         = render::Value(std::string_view(site.url));
    m["language"]    = render::Value(std::string_view(site.language));
    if (site.logo)        m["logo"]        = render::Value(std::string_view(*site.logo));
    if (site.icon)        m["icon"]        = render::Value(std::string_view(*site.icon));
    if (site.cover_image) m["cover_image"] = render::Value(std::string_view(*site.cover_image));
    if (site.twitter)     m["twitter"]     = render::Value(std::string_view(*site.twitter));
    if (site.facebook)    m["facebook"]    = render::Value(std::string_view(*site.facebook));
    return render::Value(std::move(m));
}

render::Value make_pagination(int page_num, int total_pages, std::string base_path) {
    std::unordered_map<std::string, render::Value> m;
    m["current"]  = render::Value(static_cast<int64_t>(page_num));
    m["total"]    = render::Value(static_cast<int64_t>(total_pages));
    m["has_prev"] = render::Value(page_num > 1);
    m["has_next"] = render::Value(page_num < total_pages);
    if (page_num > 1) {
        m["prev_url"] = render::Value(
            (page_num == 2) ? base_path
                            : base_path + "page/" + std::to_string(page_num - 1) + "/");
    }
    if (page_num < total_pages) {
        m["next_url"] = render::Value(
            base_path + "page/" + std::to_string(page_num + 1) + "/");
    }
    return render::Value(std::move(m));
}

} // anonymous namespace

error::Result<std::vector<std::pair<std::filesystem::path, std::string>>>
Pipeline::phase_render(const adapters::FetchResult& content, BuildStats& stats, ProgressCallback progress) const {
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    render::Engine engine(template_config_.templates_dir);

    // ── Pre-convert domain structs → Values once, before any render loop.
    // Copying a Value that holds a shared_ptr<ValueMap> is O(1) (refcount bump).
    // Nothing below should call post_to_value() / page_to_value() inside a loop.

    const render::Value site_val = site_to_value(site_config_);

    std::vector<render::Value> post_values;
    post_values.reserve(content.posts.size());
    for (const auto& p : content.posts) post_values.push_back(post_to_value(p));

    std::vector<render::Value> page_values;
    page_values.reserve(content.pages.size());
    for (const auto& p : content.pages) page_values.push_back(page_to_value(p));

    std::vector<render::Value> tag_values;
    tag_values.reserve(content.tags.size());
    for (const auto& t : content.tags) tag_values.push_back(tag_to_value(t));

    std::vector<render::Value> author_values;
    author_values.reserve(content.authors.size());
    for (const auto& a : content.authors) author_values.push_back(author_to_value(a));

    // all_tags_val: shared O(1) copy into every post/index context.
    const render::Value all_tags_val{std::vector<render::Value>(tag_values)};

    // navigation: pages marked show_in_menu, shared across all page renders.
    std::vector<render::Value> nav_vec;
    for (size_t i = 0; i < content.pages.size(); ++i) {
        if (content.pages[i].show_in_menu) nav_vec.push_back(page_values[i]);
    }
    const render::Value nav_val(std::move(nav_vec));

    const size_t total_items = content.posts.size() + content.pages.size() +
                               content.tags.size() + content.authors.size() + 1;
    size_t current = 0;

    // Stack arena size: enough for ~30 map nodes × ~120 bytes each + bucket array.
    // Falls back to heap if a pathological template exceeds this.
    static constexpr std::size_t kCtxBufSize = 8192;

    // ── Render posts ──────────────────────────────────────────────────────────
    for (size_t i = 0; i < content.posts.size(); ++i) {
        alignas(std::max_align_t) std::byte ctx_buf[kCtxBufSize];
        std::pmr::monotonic_buffer_resource mbr(ctx_buf, kCtxBufSize,
            std::pmr::new_delete_resource());

        render::Context ctx(&mbr);
        ctx.set("site", site_val);
        ctx.set("post", post_values[i]);
        ctx.set("tags", all_tags_val);
        if (i > 0)
            ctx.set("prev_post", post_values[i - 1]);
        if (i + 1 < content.posts.size())
            ctx.set("next_post", post_values[i + 1]);

        const auto& post = content.posts[i];
        const std::string tpl = post.custom_template.value_or(template_config_.default_post_template);
        auto render_result = engine.render(tpl, ctx);
        if (!render_result) {
            spdlog::error(fmt::format("Failed to render post {}: {}", post.slug, render_result.error().message));
            stats.errors++;
        } else {
            files.emplace_back(post.output_path, std::move(*render_result));
            stats.posts_rendered++;
        }

        ++current;
        if (progress) {
            progress("Rendering posts...",
                0.25f + 0.5f * static_cast<float>(current) / static_cast<float>(total_items));
        }
    }

    // ── Render pages ──────────────────────────────────────────────────────────
    for (size_t i = 0; i < content.pages.size(); ++i) {
        alignas(std::max_align_t) std::byte ctx_buf[kCtxBufSize];
        std::pmr::monotonic_buffer_resource mbr(ctx_buf, kCtxBufSize,
            std::pmr::new_delete_resource());

        render::Context ctx(&mbr);
        ctx.set("site", site_val);
        ctx.set("page", page_values[i]);
        ctx.set("navigation", nav_val);

        const auto& page = content.pages[i];
        const std::string tpl = page.custom_template.value_or(template_config_.default_page_template);
        auto render_result = engine.render(tpl, ctx);
        if (!render_result) {
            spdlog::error(fmt::format("Failed to render page {}: {}", page.slug, render_result.error().message));
            stats.errors++;
        } else {
            files.emplace_back(page.output_path, std::move(*render_result));
            stats.pages_rendered++;
        }
        ++current;
    }

    // ── Render index pages ────────────────────────────────────────────────────
    const size_t total_index_pages =
        (content.posts.size() + posts_per_page_ - 1) / posts_per_page_;

    for (size_t page_num = 1; page_num <= total_index_pages; ++page_num) {
        const size_t start = (page_num - 1) * posts_per_page_;
        const size_t end   = std::min(start + posts_per_page_, content.posts.size());

        // Slice of pre-converted post Values — O(1) copies.
        std::vector<render::Value> posts_slice(
            post_values.begin() + static_cast<std::ptrdiff_t>(start),
            post_values.begin() + static_cast<std::ptrdiff_t>(end));

        alignas(std::max_align_t) std::byte ctx_buf[kCtxBufSize];
        std::pmr::monotonic_buffer_resource mbr(ctx_buf, kCtxBufSize,
            std::pmr::new_delete_resource());

        render::Context ctx(&mbr);
        ctx.set("site", site_val);
        ctx.set("posts", render::Value(std::move(posts_slice)));
        ctx.set("tags", all_tags_val);
        ctx.set("pagination", make_pagination(
            static_cast<int>(page_num),
            static_cast<int>(total_index_pages), "/"));

        const std::string path = (page_num == 1) ? "index.html"
            : "page/" + std::to_string(page_num) + "/index.html";
        auto render_result = engine.render(template_config_.index_template, ctx);
        if (!render_result) {
            spdlog::error(fmt::format("Failed to render index page {}: {}", page_num, render_result.error().message));
            stats.errors++;
        } else {
            files.emplace_back(path, std::move(*render_result));
            stats.index_pages_rendered++;
        }
    }

    // ── Render tag archives ───────────────────────────────────────────────────
    PermalinkGenerator gen(permalink_config_);
    for (size_t ti = 0; ti < content.tags.size(); ++ti) {
        const auto& tag = content.tags[ti];

        std::vector<render::Value> tag_posts;
        for (size_t i = 0; i < content.posts.size(); ++i) {
            for (const auto& t : content.posts[i].tags) {
                if (t.id == tag.id) { tag_posts.push_back(post_values[i]); break; }
            }
        }
        if (tag_posts.empty()) continue;

        alignas(std::max_align_t) std::byte ctx_buf[kCtxBufSize];
        std::pmr::monotonic_buffer_resource mbr(ctx_buf, kCtxBufSize,
            std::pmr::new_delete_resource());

        render::Context ctx(&mbr);
        ctx.set("site", site_val);
        ctx.set("tag", tag_values[ti]);
        ctx.set("posts", render::Value(std::move(tag_posts)));
        ctx.set("pagination", make_pagination(1, 1, "/tag/" + tag.slug + "/"));

        const auto output_path = PermalinkGenerator::permalink_to_path(gen.for_tag(tag));
        auto render_result = engine.render(template_config_.tag_template, ctx);
        if (!render_result) {
            spdlog::error(fmt::format("Failed to render tag {}: {}", tag.slug, render_result.error().message));
            stats.errors++;
        } else {
            files.emplace_back(output_path, std::move(*render_result));
            stats.tag_archives_rendered++;
        }

        ++current;
        if (progress) {
            progress("Rendering tag archives...",
                0.25f + 0.5f * static_cast<float>(current) / static_cast<float>(total_items));
        }
    }

    // ── Render author archives ────────────────────────────────────────────────
    for (size_t ai = 0; ai < content.authors.size(); ++ai) {
        const auto& author = content.authors[ai];

        std::vector<render::Value> author_posts;
        for (size_t i = 0; i < content.posts.size(); ++i) {
            for (const auto& a : content.posts[i].authors) {
                if (a.id == author.id) { author_posts.push_back(post_values[i]); break; }
            }
        }
        if (author_posts.empty()) continue;

        alignas(std::max_align_t) std::byte ctx_buf[kCtxBufSize];
        std::pmr::monotonic_buffer_resource mbr(ctx_buf, kCtxBufSize,
            std::pmr::new_delete_resource());

        render::Context ctx(&mbr);
        ctx.set("site", site_val);
        ctx.set("author", author_values[ai]);
        ctx.set("posts", render::Value(std::move(author_posts)));
        ctx.set("pagination", make_pagination(1, 1, "/author/" + author.slug + "/"));

        const auto output_path = PermalinkGenerator::permalink_to_path(gen.for_author(author));
        auto render_result = engine.render(template_config_.author_template, ctx);
        if (!render_result) {
            spdlog::error(fmt::format("Failed to render author {}: {}", author.slug, render_result.error().message));
            stats.errors++;
        } else {
            files.emplace_back(output_path, std::move(*render_result));
            stats.author_archives_rendered++;
        }
        ++current;
    }

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
            spdlog::error(fmt::format("Failed to create directory for {}: {}", rel_path.string(), ec.message()));
            stats.errors++;
            continue;
        }

        // Write file
        std::ofstream file(full_path);
        if (!file) {
            spdlog::error(fmt::format("Failed to open file for writing: {}", full_path.string()));
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

    spdlog::debug(fmt::format("Wrote {} files to {}", written, output_config_.output_dir.string()));
    return {};
}

error::VoidResult Pipeline::copy_assets(BuildStats& stats) const {
    if (!output_config_.copy_assets) {
        return {};
    }

    auto theme_assets = template_config_.templates_dir / "assets";
    if (!std::filesystem::exists(theme_assets)) {
        spdlog::debug(fmt::format("No theme assets directory found at {}", theme_assets.string()));
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

    spdlog::debug(fmt::format("Copied {} assets", stats.assets_copied));

    return {};
}

std::string Pipeline::generate_sitemap(const adapters::FetchResult& content) const {
    std::ostringstream ss;
    ss << R"(<?xml version="1.0" encoding="UTF-8"?>)" << "\n";
    ss << R"(<urlset xmlns="https://www.sitemaps.org/schemas/sitemap/0.9">)" << "\n";

    auto format_date = [](const std::chrono::system_clock::time_point& tp) {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time_t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        return std::string(buf);
    };

    // Add homepage
    ss << "  <url>\n";
    ss << "    <loc>" << site_config_.url << "/</loc>\n";
    ss << "    <changefreq>daily</changefreq>\n";
    ss << "    <priority>1.0</priority>\n";
    ss << "  </url>\n";

    // Add posts
    for (const auto& post : content.posts) {
        ss << "  <url>\n";
        ss << "    <loc>" << site_config_.url << post.permalink << "</loc>\n";
        if (post.updated_at) {
            ss << "    <lastmod>" << format_date(*post.updated_at) << "</lastmod>\n";
        } else if (post.published_at) {
            ss << "    <lastmod>" << format_date(*post.published_at) << "</lastmod>\n";
        }
        ss << "    <changefreq>weekly</changefreq>\n";
        ss << "    <priority>0.8</priority>\n";
        ss << "  </url>\n";
    }

    // Add pages
    for (const auto& page : content.pages) {
        ss << "  <url>\n";
        ss << "    <loc>" << site_config_.url << page.permalink << "</loc>\n";
        if (page.updated_at) {
            ss << "    <lastmod>" << format_date(*page.updated_at) << "</lastmod>\n";
        }
        ss << "    <changefreq>monthly</changefreq>\n";
        ss << "    <priority>0.6</priority>\n";
        ss << "  </url>\n";
    }

    ss << "</urlset>\n";
    return ss.str();
}

std::string Pipeline::generate_rss(const adapters::FetchResult& content) const {
    std::ostringstream ss;
    ss << R"(<?xml version="1.0" encoding="UTF-8"?>)" << "\n";
    ss << R"(<rss version="2.0" xmlns:atom="http://www.w3.org/2005/Atom">)" << "\n";
    ss << "  <channel>\n";
    ss << "    <title>" << site_config_.title << "</title>\n";
    ss << "    <link>" << site_config_.url << "</link>\n";
    ss << "    <description>" << site_config_.description << "</description>\n";
    ss << "    <language>" << site_config_.language << "</language>\n";
    ss << "    <atom:link href=\"" << site_config_.url << "/rss.xml\" rel=\"self\" type=\"application/rss+xml\"/>\n";

    auto format_rfc822 = [](const std::chrono::system_clock::time_point& tp) {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time_t);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
        return std::string(buf);
    };

    // Limit to 20 most recent posts
    size_t count = std::min(content.posts.size(), static_cast<size_t>(20));
    for (size_t i = 0; i < count; ++i) {
        const auto& post = content.posts[i];

        ss << "    <item>\n";
        ss << "      <title><![CDATA[" << post.title << "]]></title>\n";
        ss << "      <link>" << site_config_.url << post.permalink << "</link>\n";
        ss << "      <guid>" << site_config_.url << post.permalink << "</guid>\n";

        if (post.published_at) {
            ss << "      <pubDate>" << format_rfc822(*post.published_at) << "</pubDate>\n";
        }

        if (!post.excerpt.empty()) {
            ss << "      <description><![CDATA[" << post.excerpt << "]]></description>\n";
        }

        if (!post.authors.empty()) {
            ss << "      <author>" << post.authors[0].name << "</author>\n";
        }

        for (const auto& tag : post.tags) {
            ss << "      <category>" << tag.name << "</category>\n";
        }

        ss << "    </item>\n";
    }

    ss << "  </channel>\n";
    ss << "</rss>\n";
    return ss.str();
}

} // namespace guss::builder
