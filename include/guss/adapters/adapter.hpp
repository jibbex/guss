/**
 * \file adapter.hpp
 * \brief Content adapter interface for Guss SSG.
 *
 * \details
 * Defines the abstract ContentAdapter base class and FetchResult type.
 * Adapters convert CMS-specific content into a CollectionMap of RenderItems
 * plus a site-wide metadata Value.
 *
 * ContentAdapter provides protected helpers shared by all concrete adapters:
 * - build_site_value()  — converts SiteConfig to a Value
 * - resolve_path()      — navigates a dot-path ("authors.0.slug") through a Value
 * - apply_field_map()   — renames/projects fields in an item Value
 * - enrich_item()       — adds year/month/day and permalink to an item Value
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include "guss/core/config.hpp"
#include "guss/core/error.hpp"
#include "guss/core/render_item.hpp"
#include "guss/core/value.hpp"
#include <httplib.h>

namespace guss::adapters {

/**
 * \brief Progress callback for fetch operations.
 * \param current Current item count.
 * \param total   Total item count.
 */
using FetchCallback = std::function<void(size_t current, size_t total)>;

/**
 * \brief Result of a fetch_all operation.
 *
 * \details
 * items: CollectionMap keyed by collection name (e.g. "posts", "tags").
 *        Each RenderItem has a pre-computed output_path and template_name.
 *        The pipeline uses this map to generate archive pages and render all items.
 *
 * site:  Site-wide metadata Value (title, description, url, language, ...).
 *        Adapter may include shared summaries but does NOT put full collection
 *        arrays here — those live in items.
 */
struct FetchResult {
    render::CollectionMap items;
    render::Value         site;
};

/**
 * \brief Abstract base class for content adapters.
 *
 * \details
 * Holds SiteConfig and CollectionCfgMap so every concrete adapter can call
 * the shared helpers (build_site_value, resolve_path, apply_field_map, enrich_item)
 * without duplicating code.
 */
class ContentAdapter {
public:
    ContentAdapter(config::SiteConfig site_cfg, config::CollectionCfgMap collections)
        : site_cfg_(std::move(site_cfg))
        , collections_(std::move(collections)) {}

    virtual ~ContentAdapter() = default;

    /**
     * \brief Fetch all content and return a FetchResult.
     * \param progress Optional progress callback.
     * \return FetchResult on success, Error on failure.
     */
    virtual error::Result<FetchResult> fetch_all(FetchCallback progress = nullptr) = 0;

    /**
     * \brief Test connectivity without a full fetch.
     * \return VoidResult indicating success or Error.
     */
    virtual error::VoidResult ping() = 0;

    /**
     * \brief Get the adapter name for logging.
     * \retval std::string Adapter name (e.g. "rest_api", "markdown").
     */
    virtual std::string adapter_name() const = 0;

protected:
    config::SiteConfig       site_cfg_;
    config::CollectionCfgMap collections_;

    /**
     * \brief Convert site_cfg_ to a Value suitable for FetchResult::site.
     */
    render::Value build_site_value() const;

    /**
     * \brief Navigate a dot-path through a Value.
     *
     * \details
     * Splits \p path on '.' and traverses the Value chain. Numeric segments
     * are treated as array indices. Returns a null Value if any step fails.
     *
     * Examples:
     * - "authors.0"        → v["authors"][0]
     * - "content.rendered" → v["content"]["rendered"]
     *
     * \param v    Root Value to traverse.
     * \param path Dot-separated path string.
     * \retval render::Value  The resolved value, or null if the path cannot be resolved.
     */
    static render::Value resolve_path(const render::Value& v, std::string_view path);

    /**
     * \brief Apply a field map to an item Value.
     *
     * \details
     * For each (target, source_path) pair, sets item[target] = resolve_path(item, source_path).
     * Existing fields are overwritten; missing source paths produce null values.
     *
     * \param item      Object Value to modify in-place.
     * \param field_map Map of target field name to source dot-path.
     */
    static void apply_field_map(
        render::Value& item,
        const std::unordered_map<std::string, std::string>& field_map);

    /**
     * \brief Enrich an item Value with date fields, permalink, and output_path.
     *
     * \details
     * 1. If item["published_at"] has >= 10 characters, extracts year/month/day
     *    and sets them on the item.
     * 2. Looks up the collection config for \p collection_name.
     * 3. Calls PermalinkGenerator::expand + permalink_to_path.
     * 4. Sets item["permalink"] and item["output_path"].
     *
     * No-op if collection_name is not in collections_ or its permalink is empty.
     *
     * \param item            Object Value to enrich (must be an object).
     * \param collection_name Collection name to look up permalink pattern.
     */
    void enrich_item(render::Value& item, const std::string& collection_name) const;
};

/** \brief Unique pointer type alias for adapters. */
using AdapterPtr = std::unique_ptr<ContentAdapter>;

/**
 * \brief Extract error information from an HTTP response.
 * \retval std::nullopt                   Response is successful (no error).
 * \retval std::unexpected<error::Error>  HTTP error with appropriate ErrorCode.
 */
std::optional<std::unexpected<error::Error>> get_error(const httplib::Result& res);

} // namespace guss::adapters
