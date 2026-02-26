/**
 * \file adapter.hpp
 * \brief Content adapter interface for Apex SSG.
 *
 * \details
 * This file defines the abstract ContentAdapter interface for fetching content from
 * various CMS sources. Implementations include MarkdownAdapter (local files),
 * GhostAdapter (Ghost CMS API), and WordPressAdapter (WordPress REST API).
 *
 * The adapter pattern decouples content sourcing from the build pipeline, allowing
 * Apex SSG to work with multiple content sources through a unified interface.
 *
 * Example usage:
 * \code
 * #include "apex/adapter.hpp"
 * #include "apex/adapters/markdown_adapter.hpp"
 *
 * apex::config::MarkdownAdapterConfig cfg;
 * cfg.content_path = "./content";
 * cfg.pages_path = "./pages";
 *
 * auto adapter = std::make_unique<apex::adapter::MarkdownAdapter>(cfg);
 * auto result = adapter->fetch_all([](size_t current, size_t total) {
 *     std::cout << "Progress: " << current << "/" << total << std::endl;
 * });
 *
 * if (result) {
 *     std::cout << "Fetched " << result->posts.size() << " posts" << std::endl;
 * }
 * \endcode
 *
 * \author Manfred Michaelis
 * \date 2025
 */
#pragma once

#include <expected>


#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include "guss/core/error.hpp"
#include "guss/core/model/asset.hpp"
#include "guss/core/model/author.hpp"
#include "guss/core/model/page.hpp"
#include "guss/core/model/post.hpp"
#include "guss/core/model/taxonomy.hpp"

#include <httplib.h>

namespace guss::adapters {

/**
 * \brief Progress callback for fetch operations.
 * \param current Current item number.
 * \param total Total number of items.
 */
using FetchCallback = std::function<void(size_t current, size_t total)>;

/**
 * \brief Result of a fetch_all operation containing all content types.
 */
struct FetchResult {
    std::vector<model::Post> posts;
    std::vector<model::Page> pages;
    std::vector<model::Author> authors;
    std::vector<model::Tag> tags;
    std::vector<model::Category> categories;
    std::vector<model::Asset> assets;
};

/**
 * \brief Abstract interface for content adapters.
 *
 * \details
 * Implementations must provide methods to fetch posts, pages, authors,
 * tags, categories, and assets from their respective content sources.
 */
class ContentAdapter {
public:
    virtual ~ContentAdapter() = default;

    /**
     * \brief Fetch all content in one operation.
     * \param progress Optional progress callback.
     * \return FetchResult containing all content or an Error.
     */
    virtual error::Result<FetchResult> fetch_all(FetchCallback progress = nullptr) = 0;

    virtual error::Result<std::vector<model::Post>> fetch_posts(FetchCallback progress = nullptr) = 0;
    virtual error::Result<std::vector<model::Page>> fetch_pages(FetchCallback progress = nullptr) = 0;
    virtual error::Result<std::vector<model::Author>> fetch_authors() = 0;
    virtual error::Result<std::vector<model::Tag>> fetch_tags() = 0;
    virtual error::Result<std::vector<model::Category>> fetch_categories() = 0;
    virtual error::Result<std::vector<model::Asset>> fetch_assets() = 0;

    /**
     * \brief Get the adapter name for logging/identification.
     * \return Adapter name string (e.g., "markdown", "ghost", "wordpress").
     */
    virtual std::string adapter_name() const = 0;
};

/**
 * \brief Unique pointer type alias for adapters.
 */
using AdapterPtr = std::unique_ptr<ContentAdapter>;

/**
 * \brief Helper to extract error information from an HTTP response.
 * \param res The constance reference to HTTP response.
 * \retval std::unexpected<error::Error> An Error if the response indicates an error
 */
inline std::optional<std::unexpected<error::Error>> get_error(const httplib::Response &res) {
    switch (res.status) {
        case 400:
            return error::make_error(
                error::ErrorCode::AdapterBadRequest,
                "Bad request",
                res.body);
        case 401:
            return error::make_error(
                error::ErrorCode::AdapterAuthFailed,
                "Unauthorized",
                res.body);
        case 403:
            return error::make_error(
                error::ErrorCode::AdapterAuthFailed,
                "Forbidden",
                res.body);
        case 404:
            return error::make_error(
                error::ErrorCode::AdapterNotFound,
                "Not found",
                res.body);
        case 500:
            return error::make_error(
                error::ErrorCode::AdapterServerError,
                "Internal server error",
                res.body);
        default: return std::nullopt;
    }
}

} // namespace guss::adapters
