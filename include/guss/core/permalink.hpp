/**
 * @file permalink.hpp
 * @brief Permalink generation for Guss SSG.
 *
 * @details
 * Generates URL permalinks from configurable patterns with token expansion.
 * Supports tokens: {slug}, {year}, {month}, {day}, {id}, {title}
 *
 * Example usage:
 * @code
 * guss::config::PermalinkConfig cfg;
 * cfg.post_pattern = "/{year}/{month}/{slug}/";
 *
 * guss::PermalinkGenerator gen(cfg);
 * auto permalink = gen.for_post(post);
 * // Result: "/2024/01/my-post-title/"
 *
 * auto path = guss::PermalinkGenerator::permalink_to_path(permalink);
 * // Result: "2024/01/my-post-title/index.html"
 * @endcode
 */
#pragma once

#include "guss/core/model/taxonomy.hpp"
#include "guss/core/model/author.hpp"
#include <string>
#include <chrono>
#include <filesystem>

#include "config.hpp"
#include "model/page.hpp"
#include "model/post.hpp"

namespace guss {

namespace fs = std::filesystem;

/**
 * @brief Generates permalinks from configurable URL patterns.
 */
class PermalinkGenerator {
public:
    /**
     * @brief Construct a PermalinkGenerator with the given configuration.
     * @param config Permalink pattern configuration.
     */
    explicit PermalinkGenerator(const config::PermalinkConfig& config);

    /**
     * @brief Generate permalink for a post.
     * @param post The post to generate a permalink for.
     * @return The generated permalink URL.
     */
    [[nodiscard]] std::string for_post(const model::Post& post) const;

    /**
     * @brief Generate permalink for a page.
     * @param page The page to generate a permalink for.
     * @return The generated permalink URL.
     */
    [[nodiscard]] std::string for_page(const model::Page& page) const;

    /**
     * @brief Generate permalink for a tag.
     * @param tag The tag to generate a permalink for.
     * @return The generated permalink URL.
     */
    [[nodiscard]] std::string for_tag(const model::Tag& tag) const;

    /**
     * @brief Generate permalink for a category.
     * @param category The category to generate a permalink for.
     * @return The generated permalink URL.
     */
    [[nodiscard]] std::string for_category(const model::Category& category) const;

    /**
     * @brief Generate permalink for an author.
     * @param author The author to generate a permalink for.
     * @return The generated permalink URL.
     */
    [[nodiscard]] std::string for_author(const model::Author& author) const;

    /**
     * @brief Convert a permalink URL to an output file path.
     *
     * @details
     * Transforms permalinks to filesystem paths:
     * - "/blog/my-post/" -> "blog/my-post/index.html"
     * - "/about/" -> "about/index.html"
     * - "/tag/tech/" -> "tag/tech/index.html"
     *
     * @param permalink The permalink URL.
     * @return The filesystem path relative to output directory.
     */
    [[nodiscard]] static fs::path permalink_to_path(const std::string& permalink);

    /**
     * @brief Expand tokens in a pattern string.
     *
     * @details
     * Supported tokens:
     * - {slug} - URL slug
     * - {id} - Content ID
     * - {year} - 4-digit year
     * - {month} - 2-digit month (01-12)
     * - {day} - 2-digit day (01-31)
     * - {title} - URL-encoded title
     *
     * @param pattern The pattern with tokens.
     * @param slug The slug value.
     * @param id The ID value.
     * @param timestamp Optional timestamp for date tokens.
     * @param title Optional title for {title} token.
     * @return The expanded permalink string.
     */
    [[nodiscard]] static std::string expand_pattern(
        const std::string& pattern,
        const std::string& slug,
        const std::string& id = "",
        const std::optional<std::chrono::system_clock::time_point>& timestamp = std::nullopt,
        const std::string& title = ""
    );

private:
    config::PermalinkConfig config_;

    /**
     * @brief URL-encode a string.
     */
    [[nodiscard]] static std::string url_encode(const std::string& str);

    /**
     * @brief Slugify a string (lowercase, replace spaces with hyphens, remove special chars).
     */
    [[nodiscard]] static std::string slugify(const std::string& str);
};

} // namespace guss
