/**
 * \file rest_adapter.hpp
 * \brief Generic REST CMS adapter for Guss SSG.
 *
 * \details
 * RestCmsAdapter replaces the Ghost-specific and WordPress-specific adapters.
 * It is driven entirely by RestApiConfig, which describes endpoints, auth,
 * pagination, field mappings, and cross-references. No hardcoded content-type
 * logic lives here — all behaviour is data-driven from the config.
 *
 * JSON parsing is self-contained within this translation unit.
 * No simdjson types escape it.
 */
#pragma once

#include "guss/adapters/adapter.hpp"
#include "guss/core/config.hpp"
#include <string>

namespace guss::adapters::rest {

/**
 * \brief Generic REST CMS content adapter.
 *
 * \details
 * For each collection that has both an endpoint config and a collection config:
 *  1. Fetches all pages using the configured pagination strategy.
 *  2. Applies field_maps to normalize field names.
 *  3. Calls enrich_item to add year/month/day, permalink, and output_path.
 *  4. Sets context_key and template_name from the collection config.
 *  5. Builds cross-references (e.g. attaches "posts" list to each tag).
 *  6. Adds prev/next links to the posts collection.
 */
class RestCmsAdapter final : public ContentAdapter {
public:
    /**
     * \brief Construct a RestCmsAdapter.
     * \param cfg         REST API connection and endpoint configuration.
     * \param site_cfg    Site metadata passed to build_site_value().
     * \param collections Per-collection rendering configuration.
     */
    RestCmsAdapter(const core::config::RestApiConfig&    cfg,
                   const core::config::SiteConfig&       site_cfg,
                   const core::config::CollectionCfgMap& collections);

    core::error::Result<FetchResult> fetch_all(FetchCallback progress = nullptr) override;
    core::error::VoidResult          ping() override;
    std::string                adapter_name() const override { return "rest_api"; }

private:
    /// HTTP response carrying both the body and lowercased response headers.
    struct HttpResponse {
        std::string body;
        /// Headers stored with lowercase names for case-insensitive lookup.
        std::unordered_map<std::string, std::string> headers;

        /// Return the first header value matching \p name (case-insensitive via
        /// pre-lowercased keys), or empty string if absent.
        std::string header(std::string_view name) const;
    };

    /**
     * \brief Make an authenticated HTTP GET to \p path (relative to base_url).
     * \param path  URL path including any query string.
     * \return Response body and headers or error.
     */
    core::error::Result<HttpResponse> http_get(const std::string& path) const;

    /// Like http_get() but sends \p path verbatim without prepending base_path_.
    /// Used by link_header and json_next_url strategies to follow extracted URLs.
    core::error::Result<HttpResponse> http_get_raw_path(const std::string& path) const;

    /// Core HTTP GET implementation. \p full_path is sent verbatim (no modifications).
    core::error::Result<HttpResponse> do_get(const std::string& full_path) const;

    /**
     * \brief Fetch all pages of a single endpoint, returning raw Values.
     *
     * \details
     * Uses the endpoint's pagination config (or the global one if not overridden).
     * Stops when the pagination sentinel says there are no more pages.
     *
     * \param collection_name  Collection name used for logging.
     * \param ep               Endpoint configuration.
     * \return Vector of Values (one per item across all pages) or error.
     */
    core::error::Result<std::vector<core::Value>> fetch_endpoint(
        const std::string&                collection_name,
        const core::config::EndpointConfig& ep) const;

    /**
     * \brief Check whether the JSON response body indicates a next page exists.
     *
     * \details
     * Follows the dot-path in PaginationConfig::json_next through the parsed JSON
     * response. Returns true if the value at that path is non-null.
     *
     * \param body      Raw JSON response body.
     * \param json_next Dot-path to the "next page" sentinel field.
     * \return true if there is a next page, false otherwise.
     */
    static bool has_next_page(std::string_view body, std::string_view json_next);

    core::config::RestApiConfig cfg_;
    std::string           host_;
    int                   port_      = 443;
    std::string           scheme_;
    std::string           base_path_;
};

} // namespace guss::adapters::rest
