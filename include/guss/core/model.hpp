/*!
 * \file            model.hpp
 * \brief           In-memory content model for a site (posts, pages, authors, taxonomies, assets, menus, redirects and site meta).
 *
 * \details         Plain-value C++ types that represent a website's content payload.
 *                  Design goals:
 *                      - Simple, POD-like value types that are easy to store in STL containers.
 *                      - Explicit optional fields for values that may be missing (std::optional).
 *                      - A small ContentBody variant to represent either HTML or Markdown content.
 *                      - JSON extensibility points using nlohmann::json for provider-specific or opaque metadata.
 *
 *                  The header follows a Doxygen-friendly style so that the documentation can be
 *                  generated alongside the code and be available to integrators of this model.
 *
 * \author          Manfred Michaelis
 * \date            22.02.2026
 */
#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace guss {

/**
 * \brief           HTML body container.
 *
 * \details         Simple wrapper that holds HTML content for a page or post.
 */
struct HtmlBody {
    std::string html; ///< HTML source
};

/**
 * \brief           Markdown body container.
 *
 * \details         Simple wrapper that holds Markdown content for a page or post.
 */
struct MarkdownBody {
    std::string markdown; ///< Markdown source
};

/**
 * \brief           Variant type for content bodies.
 *
 * \details         A post or page body can be either HTML or Markdown. This alias hides the underlying
 *                  std::variant type and makes the intent explicit.
 */
using ContentBody = std::variant<HtmlBody, MarkdownBody>;

/**
 * \brief           Return true if the ContentBody contains Markdown.
 *
 * \param[in] body  Reference to the ContentBody.
 * \retval          true when the held alternative is MarkdownBody.
 * \retval          false otherwise.
 */
[[nodiscard]] inline bool is_markdown(const ContentBody& body) {
    return std::holds_alternative<MarkdownBody>(body);
}

/**
 * \brief           Get a non-owning view of the currently-active body text.
 *
 * \details         Returns a std::string_view referencing the internal string inside the active variant.
 *                  The caller MUST ensure the returned view does not outlive the ContentBody or any
 *                  mutation that may change the active alternative.
 *
 * \param[in] body  Reference to the ContentBody.
 * \retval          std::string_view referencing the active string (html or markdown).
 */
[[nodiscard]] inline std::string_view body_text(const ContentBody& body) {
    return std::visit([](const auto& b) -> std::string_view {
        if constexpr (std::is_same_v<std::decay_t<decltype(b)>, HtmlBody>)
            return b.html;
        else
            return b.markdown;
    }, body);
}

/**
 * \brief           Alias for a system clock time point.
 *
 * \details         Used for created_at, published_at and updated_at timestamps.
 */
using TimePoint = std::chrono::system_clock::time_point;

/* Forward declarations for main domain types so references can be used in other structs. */
struct Post;
struct Page;
struct Author;
struct Taxonomy;
struct Asset;
struct Menu;
struct Redirect;
struct SiteMeta;
struct SiteContent;

/**
 * \brief           Lightweight reference to an author.
 *
 * \details         Used inside Post::authors to avoid embedding the whole Author object.
 */
struct AuthorRef {
    std::string id;     ///< Unique identifier for the author (provider-specific)
    std::string slug;   ///< URL slug for the author
    std::string name;   ///< Display name
};

/**
 * \brief           Classification of a Taxonomy.
 *
 * \details         Tag: general tag, Category: hierarchical category, Custom: provider-defined custom taxonomy.
 */
enum class TaxonomyKind { Tag, Category, Custom };

/**
 * \brief           Lightweight reference to a taxonomy (tag/category/custom).
 *
 * \details         Used inside Post::taxonomies to avoid embedding the whole Taxonomy object.
 */
struct TaxonomyRef {
    std::string id;                         ///< Unique identifier for the taxonomy
    std::string slug;                       ///< URL slug
    std::string name;                       ///< Display name
    TaxonomyKind kind = TaxonomyKind::Tag;  ///< Kind (defaults to Tag)
};

/**
 * \brief           SEO-related metadata.
 *
 * \details         All fields are optional and should be omitted when not provided by the source.
 */
struct SeoMeta {
    std::optional<std::string> meta_title;       ///< Optional meta title override
    std::optional<std::string> meta_description; ///< Optional meta description
    std::optional<std::string> canonical_url;    ///< Optional canonical URL
    std::optional<std::string> og_image;         ///< Open Graph image URL
    std::optional<std::string> og_title;         ///< Open Graph title
    std::optional<std::string> og_description;   ///< Open Graph description
    std::optional<std::string> robots;           ///< Robots directive (e.g. "noindex")
};

/**
 * \brief           Represents an uploaded/attached asset (image/file).
 *
 * \details         Many fields are optional and describe additional metadata for the asset.
 */
struct Asset {
    std::string id;                             ///< Asset identifier
    std::string url;                            ///< Publicly-accessible URL
    std::optional<std::string> title;           ///< Optional title
    std::optional<std::string> alt;             ///< Optional alt text (images)
    std::optional<std::string> mime_type;       ///< Optional MIME type
    std::optional<uint32_t> width;              ///< Optional image width in pixels
    std::optional<uint32_t> height;             ///< Optional image height in pixels
    std::optional<uint64_t> file_size;          ///< Optional file size in bytes
};

/**
 * \brief  Status of a post in the publishing workflow.
 *
 * \details Published: visible content; Draft: not published; Scheduled: to be published at published_at.
 */
enum class PostStatus { Published, Draft, Scheduled };

/**
 * \brief           Main blog post representation.
 *
 * \details         Contains the content body (HTML or Markdown), metadata, authors, taxonomies,
 *                  optional featured image, SEO data and an extensible `extra` JSON field for
 *                  provider-specific data.
 */
struct Post {
    std::string id;                                 ///< Unique post identifier
    std::string slug;                               ///< URL slug
    std::string title;                              ///< Title
    ContentBody body;                               ///< Content body (HTML or Markdown)
    std::optional<std::string> excerpt;             ///< Optional short excerpt
    PostStatus status = PostStatus::Published;      ///< Publishing status (defaults to Published)
    std::optional<TimePoint> published_at;          ///< Optional publish time
    std::optional<TimePoint> updated_at;            ///< Optional last-updated time
    TimePoint created_at;                           ///< Required creation time
    std::vector<AuthorRef> authors;                 ///< Authors
    std::vector<TaxonomyRef> taxonomies;            ///< Tags / categories / custom taxonomies
    std::optional<Asset> featured_image;            ///< Optional featured image
    SeoMeta seo;                                    ///< SEO metadata
    std::unique_ptr<nlohmann::json> extra;          ///< Provider-specific extra JSON data
    std::optional<uint32_t> reading_time_minutes;   ///< Optional precomputed reading time
    std::optional<std::string> permalink;           ///< Optional absolute permalink override
};

/**
 * \brief           Page representation (static page).
 *
 * \details         Similar to Post but with page-specific fields like template_name. Pages typically
 *                  do not contain authors or taxonomies in this model.
 */
struct Page {
    std::string id;                                 ///< Unique page identifier
    std::string slug;                               ///< URL slug
    std::string title;                              ///< Title
    ContentBody body;                               ///< Content body (HTML or Markdown)
    std::optional<std::string> template_name;       ///< Optional template selection hint
    std::optional<TimePoint> published_at;          ///< Optional publish time
    std::optional<TimePoint> updated_at;            ///< Optional last-updated time
    TimePoint created_at;                           ///< Required creation time
    SeoMeta seo;                                    ///< SEO metadata
    std::unique_ptr<nlohmann::json> extra;          ///< Provider-specific extra JSON data
    std::optional<std::string> permalink;           ///< Optional absolute permalink override
};

/**
 * \brief           Author representation (full).
 *
 * \details         Contains optional biography, avatar and external links.
 */
struct Author {
    std::string id;                                 ///< Author identifier
    std::string slug;                               ///< URL slug
    std::string name;                               ///< Display name
    std::optional<std::string> bio;                 ///< Short biography
    std::optional<Asset> avatar;                    ///< Optional avatar asset
    std::optional<std::string> website;             ///< Optional website URL
    std::optional<std::string> location;            ///< Optional location string
    std::unique_ptr<nlohmann::json> extra;          ///< Provider-specific extra JSON data
};

/**
 * \brief           Taxonomy (tag/category/custom) full representation.
 *
 * \details         May include a parent id for hierarchical taxonomies and extra provider-specific
 *                  data via the extra JSON field.
 */
struct Taxonomy {
    std::string id;                                 ///< Taxonomy identifier
    std::string slug;                               ///< URL slug
    std::string name;                               ///< Display name
    std::optional<std::string> description;         ///< Optional description
    TaxonomyKind kind = TaxonomyKind::Tag;          ///< Kind (defaults to Tag)
    std::optional<std::string> parent;              ///< Optional parent taxonomy id
    std::unique_ptr<nlohmann::json> extra;          ///< Provider-specific extra JSON data
};

/**
 * \brief  Menu item for navigational menus.
 *
 * \details MenuItem is recursive and can contain child items. Use is_external to indicate
 *          items that should be rendered with target="_blank" or similar.
 */
struct MenuItem {
    std::string label;                              ///< Visible label
    std::string url;                                ///< Target URL
    std::vector<MenuItem> children;                 ///< Child menu items
    bool is_external = false;                       ///< Whether the link is external (default false)
};

/**
 * \brief  Named menu container.
 */
struct Menu {
    std::string id;                                 ///< Menu identifier
    std::string name;                               ///< Human-readable name
    std::vector<MenuItem> items;                    ///< Top-level menu items
};

/**
 * \brief  Redirect entry (from -> to).
 *
 * \details The status field uses HTTP redirect status codes (301 or 302).
 */
enum class RedirectStatus : uint16_t { Permanent = 301, Temporary = 302 };

/**
 * \brief  Redirect record.
 */
struct Redirect {
    std::string from;                                   ///< Source path or URL
    std::string to;                                     ///< Target path or URL
    RedirectStatus status = RedirectStatus::Permanent;  ///< Redirect status (default 301)
};

/**
 * \brief  Global site metadata.
 *
 * \details Contains site-level information used for rendering and feed generation.
 */
struct SiteMeta {
    std::string title;                              ///< Site title (required)
    std::optional<std::string> description;         ///< Optional site description;
    std::string url;                                ///< Canonical site URL (required)
    std::optional<std::string> language;            ///< Optional language (e.g. "en-US");
    std::optional<Asset> logo;                      ///< Optional site logo
    std::optional<Asset> icon;                      ///< Optional site icon
    std::vector<Menu> menus;                        ///< Site menus
    std::unique_ptr<nlohmann::json> extra;          ///< Provider-specific extra JSON data
};

/**
 * \brief  Complete site content payload.
 *
 * \details Aggregates meta and all content collections. Useful for export/import and
 *          full-snapshot operations.
 */
struct SiteContent {
    SiteMeta meta;                                  ///< Site metadata
    std::vector<Post> posts;                        ///< All posts
    std::vector<Page> pages;                        ///< All pages
    std::vector<Author> authors;                    ///< All authors
    std::vector<Taxonomy> taxonomies;               ///< All taxonomies
    std::vector<Asset> assets;                      ///< All assets
    std::vector<Redirect> redirects;                ///< All redirects
};

/**
 * \brief  Options to control fetching subsets of content.
 *
 * \details - limit/offset for pagination,
 *          - updated_since to request incremental updates,
 *          - status to filter posts by PostStatus,
 *          - include_drafts to include draft items when permitted.
 */
struct FetchOptions {
    std::optional<size_t> limit;                    ///< Optional maximum number of items
    std::optional<size_t> offset;                   ///< Optional offset for pagination
    std::optional<TimePoint> updated_since;         ///< Optional incremental update filter
    std::optional<PostStatus> status;               ///< Optional post status filter
    bool include_drafts = false;                    ///< Whether drafts should be included (default false)
};

/* --------------------------------------------------------------------------
 * JSON serialization helpers
 *
 * The following free functions are declared to allow nlohmann::json to serialize
 * the core types. Implementations should produce a clear and stable JSON shape,
 * including provider-specific `extra` fields verbatim.
 *
 * Each function fills the supplied nlohmann::json reference `j` with the JSON
 * representation of the provided object.
 * -------------------------------------------------------------------------- */

/**
 * \brief           Serialize a Post into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  p    Post to serialize.
 */
void to_json(nlohmann::json& j, const Post& p);

/**
 * \brief           Serialize a Page into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  p    Page to serialize.
 */
void to_json(nlohmann::json& j, const Page& p);

/**
 * \brief           Serialize an Author into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  a    Author to serialize.
 */
void to_json(nlohmann::json& j, const Author& a);

/**
 * \brief           Serialize an AuthorRef into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  a    AuthorRef to serialize.
 */
void to_json(nlohmann::json& j, const AuthorRef& a);

/**
 * \brief           Serialize a Taxonomy into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  t    Taxonomy to serialize.
 */
void to_json(nlohmann::json& j, const Taxonomy& t);

/**
 * \brief           Serialize a TaxonomyRef into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  t    TaxonomyRef to serialize.
 */
void to_json(nlohmann::json& j, const TaxonomyRef& t);

/**
 * \brief           Serialize an Asset into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  a    Asset to serialize.
 */
void to_json(nlohmann::json& j, const Asset& a);

/**
 * \brief           Serialize a Menu into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  m    Menu to serialize.
 */
void to_json(nlohmann::json& j, const Menu& m);

/**
 * \brief           Serialize a MenuItem into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  m    MenuItem to serialize.
 */
void to_json(nlohmann::json& j, const MenuItem& m);

/**
 * \brief           Serialize SiteMeta into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  s    SiteMeta to serialize.
 */
void to_json(nlohmann::json& j, const SiteMeta& s);

/**
 * \brief           Serialize SEO metadata into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  s    SeoMeta to serialize.
 */
void to_json(nlohmann::json& j, const SeoMeta& s);

/**
 * \brief           Serialize a Redirect into JSON.
 *
 * \param[out] j    JSON output to populate.
 * \param[in]  r    Redirect to serialize.
 */
void to_json(nlohmann::json& j, const Redirect& r);

} // namespace guss