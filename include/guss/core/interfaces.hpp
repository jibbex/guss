/*!
 * \file                interfaces.hpp
 * \brief               Core adapter, templating and build interfaces for the Guss static site tool.
 *
 * \details             Defines the error/result type, the CmsAdapter interface for fetching content
 *                      from external CMS providers, shared in-memory site data, render/templating
 *                      contexts, the pluggable TemplateEngine interface, and the Builder interface
 *                      for producing site output. These are minimal, value-oriented interfaces
 *                      intended to be implemented by concrete adapters, engines and builders.
 *
 * \author              Manfred Michaelis
 * \date                22.02.2026
 */
#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "guss/core/model.h"

namespace guss {

/**
 * \struct              Error
 * \brief               Represents an operation failure.
 *
 * \details             Simple, serializable error container used as the error alternative of Result.
 *                      Contains a human-readable message and optional context which can be used to
 *                      store path, endpoint or other debugging information.
 */
struct Error {
    std::string message; ///< Short human-readable error message
    std::string context; ///< Optional context (file, URL, operation)

    /**
     * \brief           Construct an Error with a message.
     * \param msg       Error message
     */
    Error(std::string msg) : message(std::move(msg)) {}

    /**
     * \brief           Construct an Error with a message and context.
     * \param msg       Error message
     * \param ctx       Context string
     */
    Error(std::string msg, std::string ctx)
        : message(std::move(msg)), context(std::move(ctx)) {}
};

/**
 * \brief               Generic result type for operations that can fail.
 * \details             Uses std::expected<T, Error> where Error is the error type used across the API.
 *
 * \tparam T            Success value type.
 */
template <typename T>
using Result = std::expected<T, Error>;

/**
 * \class               CmsAdapter
 * \brief               Abstract interface for fetching content from a CMS / content provider.
 *
 * \details             Concrete adapters implement this interface to provide posts, pages, taxonomies,
 *                      assets and other site content. Implementations should avoid throwing and instead
 *                      return Result<T> with an Error on failure.
 */
class CmsAdapter {
public:
    virtual ~CmsAdapter() = default;

    /**
     * \brief           Machine name of the adapter (single word).
     * \retval          std::string_view A string view pointing to a static name.
     */
    [[nodiscard]] virtual std::string_view name() const = 0;

    /**
     * \brief           Human-friendly display name of the adapter.
     * \retval          std::string_view A string view with the display name.
     */
    [[nodiscard]] virtual std::string_view display_name() const = 0;

    /**
     * \brief           Lightweight health check / connectivity test.
     * \details         Implementations should perform a minimal request to verify connectivity
     *                  and credentials. Failures should return an Error with details.
     *
     * \retval          Result<bool> where true indicates reachable and ready.
     */
    [[nodiscard]] virtual Result<bool> ping() = 0;

    /**
     * \brief           Fetch posts using the provided FetchOptions.
     * \param[in] opts  Fetch options (pagination, status filter, etc.)
     * \retval          Result<std::vector<Post>> containing a vector of Post on success or Error on failure.
     */
    [[nodiscard]] virtual Result<std::vector<Post>> fetch_posts(const FetchOptions& opts) = 0;

    /**
     * \brief           Fetch a single post by its slug.
     * \param[in] slug  Post slug
     * \retval          Result<std::optional<Post>> containing optional Post (std::nullopt when not found).
     */
    [[nodiscard]] virtual Result<std::optional<Post>> fetch_post_by_slug(std::string_view slug) = 0;

    /**
     * \brief           Fetch pages using the provided FetchOptions.
     * \param[in] opts  Fetch options
     * \retval          Result<std::vector<Page>> with vector of Page
     */
    [[nodiscard]] virtual Result<std::vector<Page>> fetch_pages(const FetchOptions& opts) = 0;

    /**
     * \brief           Fetch global site metadata.
     * \retval          Result<SiteMeta> with SiteMeta on success.
     */
    [[nodiscard]] virtual Result<SiteMeta> fetch_site_meta() = 0;

    /**
     * \brief           Fetch all taxonomies (tags/categories/custom).
     * \retval          Result<std::vector<Taxonomy>> with vector of Taxonomy
     */
    [[nodiscard]] virtual Result<std::vector<Taxonomy>> fetch_taxonomies() = 0;

    /**
     * \brief           Fetch all authors.
     * \retval          Result<std::vector<Author>> with vector of Author
     */
    [[nodiscard]] virtual Result<std::vector<Author>> fetch_authors() = 0;

    /**
     * \brief           Fetch all assets (images/files).
     * \retval          Result<std::vector<Asset>> with vector of Asset
     */
    [[nodiscard]] virtual Result<std::vector<Asset>> fetch_assets() = 0;

    /**
     * \brief           Fetch menus configured in the site.
     * \retval          Result<std::vector<Menu>> with vector of Menu
     */
    [[nodiscard]] virtual Result<std::vector<Menu>> fetch_menus() = 0;

    /**
     * \brief           Fetch redirects configured in the site.
     * \retval          Result<std::vector<Redirect>> with vector of Redirect
     */
    [[nodiscard]] virtual Result<std::vector<Redirect>> fetch_redirects() = 0;

    /**
     * \brief               Fetch provider-specific custom endpoint returning JSON.
     * \param[in] endpoint  Custom endpoint path or identifier
     * \retval              Result<nlohmann::json> with JSON returned by the provider
     */
    [[nodiscard]] virtual Result<nlohmann::json> fetch_custom(std::string_view endpoint) = 0;

    /**
     * \brief           Convenience method to fetch everything and assemble a SiteContent object.
     * \details         Default implementation calls the individual fetch_* methods and combines
     *                  results into a SiteContent. Implementations may override for efficiency,
     *                  streaming or to provide provider-specific optimizations.
     *
     * \param[in] opts  FetchOptions forwarded to underlying fetch calls (when applicable).
     * \retval          Result<SiteContent> with a fully-populated SiteContent or Error on failure.
     */
    [[nodiscard]] virtual Result<SiteContent> fetch_all(const FetchOptions& opts);
};

/**
 * \struct              SharedSiteData
 * \brief               Immutable, shared representation of pre-serialized site data used by renderers.
 *
 * \details             Holds JSON objects for the site, posts and other collections. The intention
 *                      is to build this structure once from fetched SiteContent and then share a
 *                      single allocation across multiple render invocations (pages) via shared_ptr.
 */
struct SharedSiteData {
    nlohmann::json site;        ///< Serialized SiteMeta
    nlohmann::json posts;       ///< JSON array of posts
    nlohmann::json taxonomies;  ///< JSON array of taxonomies
    nlohmann::json authors;     ///< JSON array of authors
    nlohmann::json menus;       ///< JSON array of menus
    nlohmann::json extra;       ///< Additional provider-specific shared data

    /**
     * \brief           Build SharedSiteData from a SiteContent snapshot.
     * \details         Serializes the SiteContent into JSON (including `extra` fields) and
     *                  returns a fully-populated SharedSiteData. This function is expected to
     *                  be called once after fetching; the resulting object is then referenced
     *                  by many RenderContext instances without copying.
     *
     * \param[in] content       SiteContent to convert
     * \param[in] extra_data    Additional JSON to include in SharedSiteData::extra
     * \retval                  SharedSiteData constructed from the provided content
     */
    static SharedSiteData from_site_content(
        const SiteContent& content,
        const nlohmann::json& extra_data
    );
};

/**
 * \struct              PaginationContext
 * \brief               Pagination metadata used when rendering index pages.
 */
struct PaginationContext {
    size_t current_page;
    size_t total_pages;
    size_t total_items;
    size_t items_per_page;
    bool has_prev;
    bool has_next;
    std::optional<std::string> prev_url;
    std::optional<std::string> next_url;
};

/**
 * \brief               Serialize PaginationContext to JSON for template engines.
 *
 * \param[out] j        JSON output
 * \param[in]  p        PaginationContext to serialize
 */
void to_json(nlohmann::json& j, const PaginationContext& p);

/**
 * \struct              RenderContext
 * \brief               Per-render invocation context passed to template engines.
 *
 * \details             Contains the specific item being rendered (content), optional pagination
 *                      information (for index pages) and a shared pointer to SharedSiteData which
 *                      is not copied but referenced. The method to_template_data() flattens these
 *                      fields into a single JSON object suitable for template rendering libraries
 *                      such as inja.
 */
struct RenderContext {
    nlohmann::json content;                                 ///< Current item's JSON representation
    std::optional<PaginationContext> pagination;            ///< Optional pagination data
    std::shared_ptr<const SharedSiteData> shared;           ///< Shared site data (no copies)

    /**
     * \brief           Flatten the render context into a single JSON object for templates.
     *
     * \details         Merges content, shared data and pagination into a single object that
     *                  templating engines can consume directly. Implementations should ensure
     *                  that keys do not conflict or document the conflict resolution strategy.
     *
     * \retval          nlohmann::json object representing the combined template data.
     */
    [[nodiscard]] nlohmann::json to_template_data() const;
};

/**
 * \class               TemplateEngine
 * \brief               Abstract interface for pluggable templating engines.
 *
 * \details             Implementations must load templates from a theme directory and render
 *                      named templates using a RenderContext. Engines are expected to be
 *                      re-usable across many render calls.
 */
class TemplateEngine {
public:
    virtual ~TemplateEngine() = default;

    /**
     * \brief           Machine name of the template engine implementation.
     * \retval          std::string_view with the engine name.
     */
    [[nodiscard]] virtual std::string_view name() const = 0;

    /**
     * \brief           Load templates from a theme directory.
     *
     * \param[in] theme_dir Filesystem path to the theme/templates directory.
     * \retval              Result<void> indicating success or Error on failure.
     */
    [[nodiscard]] virtual Result<void> load_templates(const std::filesystem::path& theme_dir) = 0;

    /**
     * \brief           Render a named template using the provided RenderContext.
     *
     * \param[in] template_name Name of the template to render.
     * \param[in] ctx           RenderContext containing content, shared data and pagination.
     * \retval                  Result<std::string> containing the rendered string on success.
     */
    [[nodiscard]] virtual Result<std::string> render(std::string_view template_name, const RenderContext& ctx) = 0;

    /**
     * \brief           Check whether a template exists in the currently-loaded set.
     * \param[in] template_name Template name to query
     * \retval                  true when the template is present
     * \retval                  false otherwise
     */
    [[nodiscard]] virtual bool has_template(std::string_view template_name) const = 0;

    /**
     * \brief           List all available template names.
     * \retval          std::vector<std::string> of template names.
     */
    [[nodiscard]] virtual std::vector<std::string> list_templates() const = 0;
};

/**
 * \struct              BuildOutput
 * \brief               Result of rendering a single page.
 *
 * \details             Contains the output path, the rendered content, the HTTP status that should
 *                      be applied (commonly 200) and an optional redirect target.
 */
struct BuildOutput {
    std::string path;                         ///< Output path (relative to output_dir)
    std::string content;                      ///< Rendered content
    uint16_t status = 200;                    ///< HTTP status for the output
    std::optional<std::string> redirect_to;   ///< Optional redirect target (if set)
};

/**
 * \struct              BuildReport
 * \brief               Summary of a build operation.
 *
 * \details             Reports counts of built and skipped pages, any errors encountered,
 *                      overall duration in milliseconds and the output directory used.
 */
struct BuildReport {
    size_t pages_built = 0;
    size_t pages_skipped = 0;
    std::vector<std::string> errors;
    uint64_t duration_ms = 0;
    std::string output_dir;
};

/**
 * \brief               Type of progress callback used by builders.
 *
 * \param current       Number of items processed so far.
 * \param total         Total number of items to process.
 * \param msg           Short message describing the current step.
 */
using ProgressCallback = std::function<void(size_t, size_t, std::string_view)>;

/**
 * \class               Builder
 * \brief               Abstract interface for producing site output from content and templates.
 *
 * \details             Implementations coordinate the CmsAdapter, TemplateEngine and filesystem
 *                      I/O to produce a set of BuildOutput items and write them to disk.
 */
class Builder {
public:
    virtual ~Builder() = default;

    /**
     * \brief           Perform a full build of the site into output_dir.
     *
     * \param[in] output_dir    Destination directory for build artifacts.
     * \param[in] progress      Optional callback invoked with progress updates.
     * \retval                  Result containing a BuildReport on success.
     */
    [[nodiscard]] virtual Result<BuildReport> build_full(
        const std::filesystem::path& output_dir,
        ProgressCallback progress = nullptr
    ) = 0;

    /**
     * \brief           Perform an incremental build (only changed/dirty pages).
     *
     * \param[in] output_dir    Destination directory for build artifacts.
     * \param[in] progress      Optional progress callback.
     * \retval                  Result with BuildReport on success.
     */
    [[nodiscard]] virtual Result<BuildReport> build_incremental(
        const std::filesystem::path& output_dir,
        ProgressCallback progress = nullptr
    ) = 0;

    /**
     * \brief           Build a single page identified by slug.
     *
     * \param[in] slug          Page/post slug
     * \param[in] output_dir    Destination directory where the output should be written
     * \retval                  Result with optional BuildOutput.
     * \retval                  std::nullopt may indicate the page was skipped.
     */
    [[nodiscard]] virtual Result<std::optional<BuildOutput>> build_page(
        std::string_view slug,
        const std::filesystem::path& output_dir
    ) = 0;
};

/**
 * \enum                TriggerKind
 * \brief               Types of events that can trigger a rebuild.
 */
enum class TriggerKind { FileChanged, Webhook, PollInterval, Manual };

/**
 * \struct              RebuildTrigger
 * \brief               Details of a rebuild trigger event.
 */
struct RebuildTrigger {
    TriggerKind kind;     ///< Trigger type
    std::string detail;   ///< Optional detail (file path, webhook id, interval description)
};

} // namespace guss