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
    core::CollectionMap items;
    core::Value         site;
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
    ContentAdapter(core::config::SiteConfig site_cfg, core::config::CollectionCfgMap collections)
        : site_cfg_(std::move(site_cfg))
        , collections_(std::move(collections)) {}

    virtual ~ContentAdapter() = default;

    /**
     * \brief Fetch all content and return a FetchResult.
     * \param progress Optional progress callback.
     * \return FetchResult on success, Error on failure.
     */
    virtual core::error::Result<FetchResult> fetch_all(FetchCallback progress = nullptr) = 0;

    /**
     * \brief Test connectivity without a full fetch.
     * \return VoidResult indicating success or Error.
     */
    virtual core::error::VoidResult ping() = 0;

    /**
     * \brief Get the adapter name for logging.
     * \retval std::string Adapter name (e.g. "rest_api", "markdown").
     */
    virtual std::string adapter_name() const = 0;

protected:
    core::config::SiteConfig       site_cfg_;
    core::config::CollectionCfgMap collections_;

    /**
     * \brief Convert site_cfg_ to a Value suitable for FetchResult::site.
     */
    core::Value build_site_value() const;

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
     * \retval core::Value  The resolved value, or null if the path cannot be resolved.
     */
    static core::Value resolve_path(const core::Value& v, std::string_view path);

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
        core::Value& item,
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
    void enrich_item(core::Value& item, const std::string& collection_name) const;

    /**
     * \brief Build a RenderItem from an enriched Value and its collection config.
     *
     * \details
     * Reads output_path from the Value, applies custom_template override if
     * item["custom_template"] is non-empty, and fills in context_key.
     * output_path and template_name are left empty if either is absent.
     *
     * \param v        Enriched item Value (modified in place to set data).
     * \param coll_cfg Collection configuration for this item.
     * \retval core::RenderItem  Fully populated RenderItem.
     */
    static core::RenderItem build_render_item(
        const core::Value& v,
        const core::config::CollectionConfig& coll_cfg);

    /**
     * \brief Wire cross-references into a FetchResult.
     *
     * \details
     * For each entry in \p cross_refs, finds the target and source collections
     * in \p result and injects matching source items as extra_context into each
     * target item under the source collection name.
     *
     * \param result     FetchResult whose items are mutated in place.
     * \param cross_refs Map of target collection name to CrossRefConfig.
     */
    void apply_cross_references(
        FetchResult& result,
        const core::config::CrossRefCfgMap& cross_refs) const;

    /**
     * \brief Add prev_post / next_post extra_context to a collection's items.
     *
     * \details
     * Iterates the named collection in order and injects the adjacent item's
     * data as "prev_post" / "next_post" extra_context.  No-op if the collection
     * is absent or has fewer than two items.
     *
     * \param result          FetchResult whose items are mutated in place.
     * \param collection_name Name of the collection to link (e.g. "posts").
     */
    static void apply_prev_next(FetchResult& result, const std::string& collection_name);
};

/** \brief Unique pointer type alias for adapters. */
using AdapterPtr = std::unique_ptr<ContentAdapter>;

/**
 * \brief Extract error information from an HTTP response.
 * \retval std::nullopt                   Response is successful (no error).
 * \retval std::unexpected<core::error::Error>  HTTP error with appropriate ErrorCode.
 */
std::optional<std::unexpected<core::error::Error>> get_error(const httplib::Result& res);

} // namespace guss::adapters
