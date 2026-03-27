/**
 * \file generators.hpp
 * \brief HTML minification, sitemap, and RSS generation utilities for the build pipeline.
 */
#pragma once

#include "guss/core/config.hpp"
#include "guss/core/render_item.hpp"
#include "guss/core/value.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace guss::builder {

/**
 * \brief Minify an HTML string.
 *
 * \details
 * Strips HTML comments and collapses whitespace sequences to a single space.
 * Content inside <pre>, <script>, <style>, and <textarea> tags is preserved
 * verbatim. Safe to call on any UTF-8 HTML string.
 *
 * \param[in] html        Input HTML string.
 * \retval std::string    Minified HTML string.
 */
std::string minify_html(std::string_view html);

/**
 * \brief Generate a sitemap.xml body from a list of rendered files.
 *
 * \param[in] files     Rendered output pairs (relative path, html content).
 * \param[in] base_url  Site base URL (e.g. "https://example.com"). No trailing slash.
 * \retval std::string  Complete sitemap XML string.
 */
std::string generate_sitemap_xml(
    const std::vector<std::pair<std::filesystem::path, std::string>>& files,
    std::string_view base_url);

/**
 * \brief Generate an RSS 2.0 feed XML body from rendered item pages.
 *
 * \details
 * Only items with a non-empty context_key (actual content pages, not archives)
 * are included. Items are sorted by published_at descending.
 *
 * \param[in] items     All render items (archives filtered out automatically).
 * \param[in] base_url  Site base URL. No trailing slash.
 * \param[in] site      Site Value containing title, description, language fields.
 * \retval std::string  Complete RSS 2.0 XML string.
 */
std::string generate_rss_xml(
    const std::vector<core::RenderItem>& items,
    std::string_view base_url,
    const core::Value& site);

/**
 * \brief Generate a robots.txt body from the given crawler-access configuration.
 *
 * \details
 * Produces a standards-compliant robots.txt string containing one or more
 * User-agent / Disallow directive blocks as described by \p cfg.agents.
 * If \p cfg.sitemap_url is set, a Sitemap directive is appended at the end.
 *
 * \param[in] cfg      robots.txt configuration (user-agent rules and optional sitemap URL).
 * \retval std::string Complete robots.txt file content, ready to be written to disk.
 */
std::string generate_robots_txt(const core::config::RobotsTxtConfig& cfg);

} // namespace guss::builder
