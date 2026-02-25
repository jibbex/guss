/**
 * @file ghost_adapter.cpp
 * @brief Ghost CMS Content API adapter implementation.
 */
#include "guss/adapters/adapter.hpp"
#include "guss/adapters/ghost_adapter.hpp"
#include "guss/adapters/ghost/json_parser.hpp"

#include <httplib.h>
#include <spdlog/spdlog.h>
#include <regex>

namespace guss::adapters {

namespace {

// Extract host and path from URL
struct UrlParts {
    std::string scheme;
    std::string host;
    int port;
    std::string base_path;
};

UrlParts parse_url(const std::string& url) {
    UrlParts parts;
    parts.port = 443; // Default HTTPS

    std::regex url_regex(R"(^(https?)://([^/:]+)(?::(\d+))?(.*)$)");
    std::smatch match;

    if (std::regex_match(url, match, url_regex)) {
        parts.scheme = match[1].str();
        parts.host = match[2].str();
        if (match[3].matched) {
            parts.port = std::stoi(match[3].str());
        } else if (parts.scheme == "http") {
            parts.port = 80;
        }
        parts.base_path = match[4].str();
        if (parts.base_path.empty()) {
            parts.base_path = "";
        }
        // Remove trailing slash
        if (!parts.base_path.empty() && parts.base_path.back() == '/') {
            parts.base_path.pop_back();
        }
    }

    return parts;
}

} // anonymous namespace

GhostAdapter::GhostAdapter(const config::GhostAdapterConfig& cfg)
    : config_(cfg) {}

error::Result<std::string> GhostAdapter::api_get(const std::string& endpoint) const {
    auto url_parts = parse_url(config_.api_url);

    httplib::SSLClient client { url_parts.host, url_parts.port };
    client.set_connection_timeout(std::chrono::milliseconds(config_.timeout_ms));
    client.set_read_timeout(std::chrono::milliseconds(config_.timeout_ms));

    // Build full path with API key
    std::string path = url_parts.base_path + endpoint;
    if (path.find('?') != std::string::npos) {
        path += "&key=" + config_.content_api_key;
    } else {
        path += "?key=" + config_.content_api_key;
    }

    spdlog::debug("Ghost API request: " + path);

    auto res = client.Get(path);

    if (auto error = get_error(res.value())) {
        spdlog::error("API request failed: " + error.value().error().format());
        return error::make_error(error.value().error().code,
            "API request failed: " + error.value().error().format(),
            config_.api_url + endpoint);
    }

    return res->body;
}

error::Result<FetchResult> GhostAdapter::fetch_all(FetchCallback progress) {
    FetchResult result;
    size_t total_steps = 4; // posts, pages, authors, tags
    size_t current_step = 0;

    if (progress) progress(current_step, total_steps);

    // Fetch posts
    auto posts_result = fetch_posts(nullptr);
    if (!posts_result) {
        return error::make_error(posts_result.error().code,
            "Failed to fetch posts: " + posts_result.error().format(),
            config_.api_url);
    }
    result.posts = std::move(*posts_result);
    if (progress) progress(++current_step, total_steps);

    // Fetch pages
    auto pages_result = fetch_pages(nullptr);
    if (!pages_result) {
        return error::make_error(pages_result.error().code,
            "Failed to fetch pages: " + pages_result.error().format(),
            config_.api_url);
    }
    result.pages = std::move(*pages_result);
    if (progress) progress(++current_step, total_steps);

    // Fetch authors
    auto authors_result = fetch_authors();
    if (!authors_result) {
        return error::make_error(authors_result.error().code,
            "Failed to fetch authors: " + authors_result.error().format(),
            config_.api_url);
    }
    result.authors = std::move(*authors_result);
    if (progress) progress(++current_step, total_steps);

    // Fetch tags
    auto tags_result = fetch_tags();
    if (!tags_result) {
        return error::make_error(tags_result.error().code,
            "Failed to fetch tags: " + tags_result.error().format(),
            config_.api_url);
    }
    result.tags = std::move(*tags_result);
    if (progress) progress(++current_step, total_steps);

    // Ghost doesn't have categories, leave empty
    // Assets are embedded in posts, not fetched separately

    return result;
}

error::Result<std::vector<model::Post>> GhostAdapter::fetch_posts(const FetchCallback progress) {
    std::vector<model::Post> all_posts;
    int page = 1;
    bool has_more = true;

    while (has_more) {
        std::string endpoint = "/ghost/api/content/posts/?include=authors,tags&page=" + std::to_string(page);

        auto response = api_get(endpoint);
        if (!response) {
            return error::make_error(response.error().code,
                "Failed to fetch posts: " + response.error().format(),
                config_.api_url);
        }

        auto parsed = ghost::parse_posts_response(*response);
        if (!parsed) {
            return error::make_error(parsed.error().code,
                "Failed to parse posts response: " + parsed.error().format(),
                config_.api_url);
        }

        auto& [posts, more] = *parsed;
        all_posts.insert(all_posts.end(),
            std::make_move_iterator(posts.begin()),
            std::make_move_iterator(posts.end()));

        has_more = more;
        page++;

        if (progress) {
            progress(all_posts.size(), all_posts.size() + (has_more ? 1 : 0));
        }
    }

    spdlog::info("Fetched posts from Ghost");
    return all_posts;
}

error::Result<std::vector<model::Page>> GhostAdapter::fetch_pages(const FetchCallback progress) {
    std::vector<model::Page> all_pages;
    int page = 1;
    bool has_more = true;

    while (has_more) {
        std::string endpoint = "/ghost/api/content/pages/?include=authors&page=" + std::to_string(page);

        auto response = api_get(endpoint);
        if (!response) {
            return error::make_error(response.error().code,
                "Failed to fetch pages: " + response.error().format(),
                config_.api_url);
        }

        auto parsed = ghost::parse_pages_response(*response);
        if (!parsed) {
            return error::make_error(parsed.error().code,
                "Failed to parse pages response: " + parsed.error().format(),
                config_.api_url);
        }

        auto& [pages, more] = *parsed;
        all_pages.insert(all_pages.end(),
            std::make_move_iterator(pages.begin()),
            std::make_move_iterator(pages.end()));

        has_more = more;
        page++;

        if (progress) {
            progress(all_pages.size(), all_pages.size() + (has_more ? 1 : 0));
        }
    }

    spdlog::info("Fetched pages from Ghost");
    return all_pages;
}

error::Result<std::vector<model::Author>> GhostAdapter::fetch_authors() {
    std::string endpoint = "/ghost/api/content/authors/?limit=all";

    auto response = api_get(endpoint);
    if (!response) {
        return error::make_error(response.error().code,
            "Failed to fetch authors: " + response.error().format(),
            config_.api_url);
    }

    auto result = ghost::parse_authors_response(*response);
    if (result) {
        spdlog::info("Fetched authors from Ghost");
    }
    return result;
}

error::Result<std::vector<model::Tag>> GhostAdapter::fetch_tags() {
    std::string endpoint = "/ghost/api/content/tags/?limit=all&include=count.posts";

    auto response = api_get(endpoint);
    if (!response) {
        return error::make_error(response.error().code,
            "Failed to fetch tags: " + response.error().format(),
            config_.api_url);
    }

    auto result = ghost::parse_tags_response(*response);
    if (result) {
        spdlog::info("Fetched tags from Ghost");

    }
    return result;
}

error::Result<std::vector<model::Category>> GhostAdapter::fetch_categories() {
    // Ghost doesn't have categories, only tags
    return std::vector<model::Category>{};
}

error::Result<std::vector<model::Asset>> GhostAdapter::fetch_assets() {
    // Ghost embeds assets in post HTML, we don't fetch them separately
    // A future enhancement could parse feature_image URLs
    return std::vector<model::Asset>{};
}

} // namespace guss::adapters
