/**
 * @file permalink.cpp
 * @brief Permalink generation implementation for Guss SSG.
 */
#include "guss/core/permalink.hpp"
#include <iomanip>
#include <sstream>
#include <cctype>
#include <regex>

namespace guss {

PermalinkGenerator::PermalinkGenerator(const config::PermalinkConfig& config)
    : config_(config) {}

std::string PermalinkGenerator::for_post(const model::Post& post) const {
    return expand_pattern(
        config_.post_pattern,
        post.slug,
        post.id,
        post.published_at,
        post.title
    );
}

std::string PermalinkGenerator::for_page(const model::Page& page) const {
    return expand_pattern(
        config_.page_pattern,
        page.slug,
        page.id,
        page.published_at,
        page.title
    );
}

std::string PermalinkGenerator::for_tag(const model::Tag& tag) const {
    return expand_pattern(
        config_.tag_pattern,
        tag.slug,
        tag.id,
        std::nullopt,
        tag.name
    );
}

std::string PermalinkGenerator::for_category(const model::Category& category) const {
    return expand_pattern(
        config_.category_pattern,
        category.slug,
        category.id,
        std::nullopt,
        category.name
    );
}

std::string PermalinkGenerator::for_author(const model::Author& author) const {
    return expand_pattern(
        config_.author_pattern,
        author.slug,
        author.id,
        std::nullopt,
        author.name
    );
}

std::filesystem::path PermalinkGenerator::permalink_to_path(const std::string& permalink) {
    std::string path = permalink;

    // Remove leading slash
    if (!path.empty() && path[0] == '/') {
        path = path.substr(1);
    }

    // If ends with slash, add index.html
    if (path.empty()) {
        return "index.html";
    }

    if (path.back() == '/') {
        path += "index.html";
    } else if (path.find('.') == std::string::npos) {
        // No extension and no trailing slash, add /index.html
        path += "/index.html";
    }

    return std::filesystem::path(path);
}

std::string PermalinkGenerator::expand_pattern(
    const std::string& pattern,
    const std::string& slug,
    const std::string& id,
    const std::optional<std::chrono::system_clock::time_point>& timestamp,
    const std::string& title
) {
    std::string result = pattern;

    // Replace {slug}
    std::regex slug_re(R"(\{slug\})");
    result = std::regex_replace(result, slug_re, slug);

    // Replace {id}
    std::regex id_re(R"(\{id\})");
    result = std::regex_replace(result, id_re, id);

    // Replace {title}
    std::regex title_re(R"(\{title\})");
    result = std::regex_replace(result, title_re, slugify(title));

    // Replace date tokens if timestamp is provided
    if (timestamp) {
        auto time_t = std::chrono::system_clock::to_time_t(*timestamp);
        std::tm tm = *std::gmtime(&time_t);

        // {year}
        std::regex year_re(R"(\{year\})");
        std::ostringstream year_ss;
        year_ss << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900);
        result = std::regex_replace(result, year_re, year_ss.str());

        // {month}
        std::regex month_re(R"(\{month\})");
        std::ostringstream month_ss;
        month_ss << std::setfill('0') << std::setw(2) << (tm.tm_mon + 1);
        result = std::regex_replace(result, month_re, month_ss.str());

        // {day}
        std::regex day_re(R"(\{day\})");
        std::ostringstream day_ss;
        day_ss << std::setfill('0') << std::setw(2) << tm.tm_mday;
        result = std::regex_replace(result, day_re, day_ss.str());
    } else {
        // Remove date tokens if no timestamp (replace with empty or default)
        std::regex year_re(R"(\{year\})");
        std::regex month_re(R"(\{month\})");
        std::regex day_re(R"(\{day\})");
        result = std::regex_replace(result, year_re, "0000");
        result = std::regex_replace(result, month_re, "00");
        result = std::regex_replace(result, day_re, "00");
    }

    // Ensure leading slash
    if (result.empty() || result[0] != '/') {
        result = "/" + result;
    }

    // Normalize double slashes
    std::regex double_slash(R"(/+)");
    result = std::regex_replace(result, double_slash, "/");

    return result;
}

std::string PermalinkGenerator::url_encode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : str) {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
        }
    }

    return escaped.str();
}

std::string PermalinkGenerator::slugify(const std::string& str) {
    std::string result;
    result.reserve(str.size());

    bool last_was_dash = false;
    for (char c : str) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            last_was_dash = false;
        } else if (c == ' ' || c == '-' || c == '_') {
            if (!last_was_dash && !result.empty()) {
                result += '-';
                last_was_dash = true;
            }
        }
        // Skip other characters
    }

    // Remove trailing dash
    if (!result.empty() && result.back() == '-') {
        result.pop_back();
    }

    return result;
}

} // namespace guss
