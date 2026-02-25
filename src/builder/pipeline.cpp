/**
 * @file pipeline.cpp
 * @brief Build pipeline implementation for Guss SSG.
 */
#include "guss/builder/pipeline.hpp"
#include <algorithm>
#include <ctime>
#include <expected>
#include <fstream>
#include <spdlog/spdlog.h>
#include <sstream>

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
    try {
        if (std::filesystem::exists(output_config_.output_dir)) {
            std::filesystem::remove_all(output_config_.output_dir);
            spdlog::info(fmt::format("Cleaned output directory: {}", output_config_.output_dir.string()));
        }
        return {};
    } catch (const std::filesystem::filesystem_error& e) {
        return error::make_error(
            error::ErrorCode::DirectoryCreateError,
            std::string("Failed to clean output directory: ") + e.what(),
            output_config_.output_dir.string()
        );
    }
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

error::Result<adapters::FetchResult> Pipeline::phase_fetch(ProgressCallback progress) const {
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

error::Result<std::vector<std::pair<std::filesystem::path, std::string>>>
Pipeline::phase_render(const adapters::FetchResult& content, BuildStats& stats, ProgressCallback progress) const {
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    render::InjaEngine engine(site_config_, template_config_);

    size_t total_items = content.posts.size() + content.pages.size() +
                         content.tags.size() + content.authors.size() + 1; // +1 for index
    size_t current = 0;

    // Render posts
    for (const auto& post : content.posts) {
        auto result = engine.render_post(post, content.posts, content.tags);
        if (!result) {
            spdlog::error(fmt::format("Failed to render post {}: {}", post.slug, result.error().format()));
            stats.errors++;
            continue;
        }

        files.emplace_back(post.output_path, *result);
        stats.posts_rendered++;
        current++;

        if (progress) {
            float p = 0.25f + 0.5f * static_cast<float>(current) / static_cast<float>(total_items);
            progress("Rendering posts...", p);
        }
    }

    // Render pages
    for (const auto& page : content.pages) {
        auto result = engine.render_page(page, content.pages);
        if (!result) {
            spdlog::error(fmt::format("Failed to render page {}: {}", page.slug, result.error().format()));
            stats.errors++;
            continue;
        }

        files.emplace_back(page.output_path, *result);
        stats.pages_rendered++;
        current++;
    }

    // Render index pages
    size_t total_index_pages = (content.posts.size() + posts_per_page_ - 1) / posts_per_page_;
    for (size_t page_num = 1; page_num <= total_index_pages; ++page_num) {
        size_t start = (page_num - 1) * posts_per_page_;
        size_t end = std::min(start + posts_per_page_, content.posts.size());

        std::vector<model::Post> page_posts(
            content.posts.begin() + static_cast<std::ptrdiff_t>(start),
            content.posts.begin() + static_cast<std::ptrdiff_t>(end)
        );

        auto result = engine.render_index(page_posts, content.tags,
            static_cast<int>(page_num), static_cast<int>(total_index_pages));

        if (!result) {
            spdlog::error(fmt::format("Failed to render index page {}: {}", page_num, result.error().format()));
            stats.errors++;
            continue;
        }

        std::string path = (page_num == 1) ? "index.html" :
            "page/" + std::to_string(page_num) + "/index.html";
        files.emplace_back(path, *result);
        stats.index_pages_rendered++;
    }

    // Render tag archives
    PermalinkGenerator gen(permalink_config_);
    for (const auto& tag : content.tags) {
        // Filter posts for this tag
        std::vector<model::Post> tag_posts;
        for (const auto& post : content.posts) {
            for (const auto& t : post.tags) {
                if (t.id == tag.id) {
                    tag_posts.push_back(post);
                    break;
                }
            }
        }

        if (tag_posts.empty()) continue;

        auto result = engine.render_tag(tag, tag_posts);
        if (!result) {
            spdlog::error(fmt::format("Failed to render tag {}: {}", tag.slug, result.error().format()));
            stats.errors++;
            continue;
        }

        auto permalink = gen.for_tag(tag);
        auto output_path = PermalinkGenerator::permalink_to_path(permalink);
        files.emplace_back(output_path, *result);
        stats.tag_archives_rendered++;
        current++;

        if (progress) {
            float p = 0.25f + 0.5f * static_cast<float>(current) / static_cast<float>(total_items);
            progress("Rendering tag archives...", p);
        }
    }

    // Render author archives
    for (const auto& author : content.authors) {
        // Filter posts for this author
        std::vector<model::Post> author_posts;
        for (const auto& post : content.posts) {
            for (const auto& a : post.authors) {
                if (a.id == author.id) {
                    author_posts.push_back(post);
                    break;
                }
            }
        }

        if (author_posts.empty()) continue;

        auto result = engine.render_author(author, author_posts);
        if (!result) {
            spdlog::error(fmt::format("Failed to render author {}: {}", author.slug, result.error().format()));
            stats.errors++;
            continue;
        }

        auto permalink = gen.for_author(author);
        auto output_path = PermalinkGenerator::permalink_to_path(permalink);
        files.emplace_back(output_path, *result);
        stats.author_archives_rendered++;
        current++;
    }

    return files;
}

error::VoidResult Pipeline::phase_write(
    const std::vector<std::pair<std::filesystem::path, std::string>>& files,
    BuildStats& stats,
    ProgressCallback progress
) const {
    // Ensure output directory exists
    try {
        std::filesystem::create_directories(output_config_.output_dir);
    } catch (const std::filesystem::filesystem_error& e) {
        return error::make_error(
            error::ErrorCode::DirectoryCreateError,
            std::string("Failed to create output directory: ") + e.what(),
            output_config_.output_dir.string()
        );
    }

    size_t written = 0;
    for (const auto& [rel_path, content] : files) {
        auto full_path = output_config_.output_dir / rel_path;

        // Create parent directories
        try {
            std::filesystem::create_directories(full_path.parent_path());
        } catch (const std::filesystem::filesystem_error& e) {
            spdlog::error(fmt::format("Failed to create directory for {}: {}", rel_path.string(), e.what()));
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

    try {
        std::filesystem::create_directories(output_assets);

        for (const auto& entry : std::filesystem::recursive_directory_iterator(theme_assets)) {
            if (!entry.is_regular_file()) continue;

            auto rel_path = std::filesystem::relative(entry.path(), theme_assets);
            auto dest = output_assets / rel_path;

            std::filesystem::create_directories(dest.parent_path());
            std::filesystem::copy_file(entry.path(), dest,
                std::filesystem::copy_options::overwrite_existing);

            stats.assets_copied++;
        }

        spdlog::debug(fmt::format("Copied {} assets", stats.assets_copied));

    } catch (const std::filesystem::filesystem_error& e) {
        return error::make_error(
            error::ErrorCode::FileWriteError,
            std::string("Failed to copy assets: ") + e.what(),
            ""
        );
    }

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
