/**
 * @file inja_engine.hpp
 * @brief Inja template engine wrapper for Guss SSG.
 *
 * @details
 * Provides a template rendering interface using Inja (Jinja2-like syntax).
 * Includes custom callbacks for date formatting, string truncation, and URL encoding.
 *
 * Example usage:
 * @code
 * guss::config::SiteConfig site;
 * site.title = "My Blog";
 * site.url = "https://example.com";
 *
 * guss::config::TemplateConfig tmpl;
 * tmpl.templates_dir = "./templates";
 *
 * guss::render::InjaEngine engine(site, tmpl);
 * auto html = engine.render_post(post, all_posts);
 * @endcode
 */
#pragma once

#include "guss/core/config.hpp"
#include "guss/core/model/post.hpp"
#include "guss/core/model/page.hpp"
#include "guss/core/model/taxonomy.hpp"
#include "guss/core/model/author.hpp"
#include "guss/core/error.hpp"
#include <string>
#include <vector>
#include <memory>

namespace guss::render {

/**
 * @brief Template rendering engine using Inja.
 */
class InjaEngine {
public:
    /**
     * @brief Construct the engine with site and template configuration.
     * @param site_config Site metadata configuration.
     * @param template_config Template paths and defaults.
     */
    InjaEngine(config::SiteConfig  site_config, const config::TemplateConfig& template_config);

    /**
     * @brief Render a single post.
     * @param post The post to render.
     * @param all_posts All posts for navigation/related posts.
     * @param all_tags All tags for sidebar/navigation.
     * @return Rendered HTML or error.
     */
    [[nodiscard]] error::Result<std::string> render_post(
        const model::Post& post,
        const std::vector<model::Post>& all_posts,
        const std::vector<model::Tag>& all_tags = {}
    );

    /**
     * @brief Render a single page.
     * @param page The page to render.
     * @param all_pages All pages for navigation.
     * @return Rendered HTML or error.
     */
    [[nodiscard]] error::Result<std::string> render_page(
        const model::Page& page,
        const std::vector<model::Page>& all_pages
    );

    /**
     * @brief Render the index/home page.
     * @param posts Posts to display (usually paginated).
     * @param all_tags All tags for sidebar.
     * @param page_num Current page number (1-indexed).
     * @param total_pages Total number of pages.
     * @return Rendered HTML or error.
     */
    [[nodiscard]] error::Result<std::string> render_index(
        const std::vector<model::Post>& posts,
        const std::vector<model::Tag>& all_tags,
        int page_num = 1,
        int total_pages = 1
    );

    /**
     * @brief Render a tag archive page.
     * @param tag The tag being displayed.
     * @param posts Posts with this tag.
     * @param page_num Current page number.
     * @param total_pages Total pages in this archive.
     * @return Rendered HTML or error.
     */
    [[nodiscard]] error::Result<std::string> render_tag(
        const model::Tag& tag,
        const std::vector<model::Post>& posts,
        int page_num = 1,
        int total_pages = 1
    );

    /**
     * @brief Render an author archive page.
     * @param author The author being displayed.
     * @param posts Posts by this author.
     * @param page_num Current page number.
     * @param total_pages Total pages in this archive.
     * @return Rendered HTML or error.
     */
    [[nodiscard]] error::Result<std::string> render_author(
        const model::Author& author,
        const std::vector<model::Post>& posts,
        int page_num = 1,
        int total_pages = 1
    );

    /**
     * @brief Render an arbitrary template with custom data.
     * @param template_name Template file name.
     * @param data JSON string data for template context.
     * @return Rendered HTML or error.
     */
    [[nodiscard]] error::Result<std::string> render_template(
        const std::string& template_name,
        const std::string& data
    );

private:
    config::SiteConfig site_config_;
    config::TemplateConfig template_config_;

    /**
     * @brief Build base context with site metadata.
     */
    [[nodiscard]] std::string build_base_context() const;

    /**
     * @brief Build pagination context.
     */
    [[nodiscard]] static std::string build_pagination(int page_num, int total_pages, const std::string& base_path);

    /**
     * @brief Register custom callbacks with Inja.
     */
    void register_callbacks();

    /**
     * @brief Get the template to use for a post (custom or default).
     */
    [[nodiscard]] std::string get_post_template(const model::Post& post) const;

    /**
     * @brief Get the template to use for a page (custom or default).
     */
    [[nodiscard]] std::string get_page_template(const model::Page& page) const;
};

} // namespace guss::render
