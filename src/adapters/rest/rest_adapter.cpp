/**
 * \file rest_adapter.cpp
 * \brief Generic REST CMS adapter implementation.
 */
#include "guss/adapters/rest/rest_adapter.hpp"
#include "guss/adapters/adapter.hpp"
#include "guss/core/permalink.hpp"
#include "guss/core/value.hpp"

#include <httplib.h>
#include <spdlog/spdlog.h>
#include <simdjson.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#ifdef GUSS_USE_OPENMP
#include <omp.h>
#endif

namespace guss::adapters::rest {

namespace {

/**
 * \brief Convert a simdjson::ondemand::value to a render::Value recursively.
 *
 * \details
 * This function handles all JSON types (object, array, string, number, boolean, null and missing) and converts
 * them into the corresponding render::Value representation. Objects are converted to render::Value maps, arrays
 * to render::Value arrays, strings to render::Value strings, numbers to render::Value numbers (int64, uint64,
 * or double depending on the type), booleans to render::Value booleans, and null/missing values to
 * render::Value null.
 *
 * \param[in] val The simdjson::ondemand::value to convert.
 * \retval The corresponding render::Value representation of the input simdjson value.
 */
render::Value from_simdjson(simdjson::ondemand::value val) {
    using namespace render;
    switch (val.type()) {
        case simdjson::ondemand::json_type::object: {
            std::unordered_map<std::string, Value> m;
            for (auto field : val.get_object()) {
                std::string key(field.unescaped_key().value());
                m[key] = from_simdjson(field.value());
            }
            return Value(std::move(m));
        }
        case simdjson::ondemand::json_type::array: {
            std::vector<Value> arr;
            for (auto elem : val.get_array()) {
                arr.push_back(from_simdjson(elem.value()));
            }
            return Value(std::move(arr));
        }
        case simdjson::ondemand::json_type::string:
            return Value(std::string(val.get_string().value()));
        case simdjson::ondemand::json_type::number: {
            auto nt_result = val.get_number_type();
            if (nt_result.error()) return Value(val.get_double().value());
            auto nt = nt_result.value();
            if (nt == simdjson::ondemand::number_type::signed_integer)
                return Value(val.get_int64().value());
            if (nt == simdjson::ondemand::number_type::unsigned_integer)
                return Value(val.get_uint64().value());
            return Value(val.get_double().value());
        }
        case simdjson::ondemand::json_type::boolean:
            return Value(val.get_bool().value());
        case simdjson::ondemand::json_type::null:
        default:
            return Value{};
    }
}


struct UrlParts {
    std::string scheme;
    std::string host;
    int         port = 443;
    std::string base_path;
};

UrlParts parse_url(const std::string& url) {
    UrlParts parts;
    std::regex url_regex(R"(^(https?)://([^/:]+)(?::(\d+))?(.*)$)");
    std::smatch match;
    if (std::regex_match(url, match, url_regex)) {
        parts.scheme = match[1].str();
        parts.host   = match[2].str();
        if (match[3].matched) {
            parts.port = std::stoi(match[3].str());
        } else {
            parts.port = (parts.scheme == "http") ? 80 : 443;
        }
        parts.base_path = match[4].str();
        if (!parts.base_path.empty() && parts.base_path.back() == '/')
            parts.base_path.pop_back();
    }
    return parts;
}

// Build query string from an EndpointParams map.
std::string build_query(const config::EndpointParams& params) {
    std::string q;
    for (const auto& [k, v] : params) {
        q += (q.empty() ? "?" : "&");
        q += k + "=" + v;
    }
    return q;
}

/// Extract items from a JSON object under response_key.
/// No assumption about pagination structure — purely generic.
error::Result<std::vector<render::Value>>
parse_keyed_array(std::string_view body, std::string_view response_key) {
    simdjson::ondemand::parser parser;
    auto padded = simdjson::padded_string(body.data(), body.size());
    auto doc = parser.iterate(padded);
    if (doc.error())
        return error::make_error(error::ErrorCode::ContentParseError,
                                 "simdjson parse error",
                                 std::string(body.substr(0, 120)));

    simdjson::ondemand::object root;
    if (doc.get_object().get(root))
        return error::make_error(error::ErrorCode::ContentParseError,
                                 "expected JSON object at root", "");

    simdjson::ondemand::value arr_val;
    if (root[response_key].get(arr_val))
        return error::make_error(error::ErrorCode::ContentParseError,
                                 "missing array key", std::string(response_key));

    simdjson::ondemand::array arr;
    if (arr_val.get_array().get(arr))
        return error::make_error(error::ErrorCode::ContentParseError,
                                 "expected array under key", std::string(response_key));

    std::vector<render::Value> values;
    for (auto elem : arr) {
        simdjson::ondemand::value v;
        if (!elem.get(v))
            values.push_back(from_simdjson(v));
    }
    return values;
}

/// Parse a root-array JSON response (no wrapping object).
error::Result<std::vector<render::Value>>
parse_root_array(std::string_view body) {
    simdjson::ondemand::parser parser;
    auto padded = simdjson::padded_string(body.data(), body.size());
    auto doc = parser.iterate(padded);
    if (doc.error())
        return error::make_error(error::ErrorCode::ContentParseError,
                                 "simdjson parse error",
                                 std::string(body.substr(0, 120)));

    simdjson::ondemand::array arr;
    if (doc.get_array().get(arr))
        return error::make_error(error::ErrorCode::ContentParseError,
                                 "expected root JSON array", "");

    std::vector<render::Value> values;
    for (auto elem : arr) {
        simdjson::ondemand::value v;
        if (!elem.get(v))
            values.push_back(from_simdjson(v));
    }
    return values;
}

std::string lowercase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Parse the full JSON body into a render::Value (used for body-inspection strategies).
render::Value parse_body_value(std::string_view body) {
    simdjson::ondemand::parser sj_parser;
    auto padded = simdjson::padded_string(body.data(), body.size());
    auto doc    = sj_parser.iterate(padded);
    if (doc.error()) return render::Value{};
    simdjson::ondemand::value rv;
    if (doc.get_value().get(rv)) return render::Value{};
    return from_simdjson(rv);
}

/// Strip scheme://host:port from an absolute URL, returning the path+query.
/// Returns the string unchanged if it already starts with '/'.
std::string url_to_path(const std::string& url) {
    static const std::regex abs_re(R"(^https?://[^/]+(/.*)?$)");
    std::smatch m;
    if (std::regex_match(url, m, abs_re))
        return m[1].matched ? m[1].str() : "/";
    if (!url.empty() && url[0] == '/')
        return url;
    return "/" + url;
}

/// Parse RFC 5988 Link header and return the server path of the rel="next" URL.
std::optional<std::string> parse_link_next(const std::string& link_header) {
    static const std::regex re(R"(<([^>]+)>\s*;\s*rel\s*=\s*"next")",
                               std::regex::icase);
    std::smatch m;
    if (!std::regex_search(link_header, m, re)) return std::nullopt;
    return url_to_path(m[1].str());
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

RestCmsAdapter::RestCmsAdapter(const config::RestApiConfig&    cfg,
                               const config::SiteConfig&       site_cfg,
                               const config::CollectionCfgMap& collections)
    : ContentAdapter(site_cfg, collections)
    , cfg_(cfg)
{
    auto parts = parse_url(cfg_.base_url);
    scheme_    = parts.scheme;
    host_      = parts.host;
    port_      = parts.port;
    base_path_ = parts.base_path;
}

// ---------------------------------------------------------------------------
// HttpResponse::header
// ---------------------------------------------------------------------------

std::string RestCmsAdapter::HttpResponse::header(std::string_view name) const {
    std::string key = lowercase(std::string(name));
    auto it = headers.find(key);
    return it != headers.end() ? it->second : std::string{};
}

// ---------------------------------------------------------------------------
// do_get (shared core)
// ---------------------------------------------------------------------------

error::Result<RestCmsAdapter::HttpResponse>
RestCmsAdapter::do_get(const std::string& full_path_with_auth) const {
    // Auth injection
    std::string full_path = full_path_with_auth;
    if (cfg_.auth.type == config::AuthConfig::Type::ApiKey &&
        !cfg_.auth.param.empty() && !cfg_.auth.value.empty())
    {
        full_path += (full_path.find('?') == std::string::npos ? "?" : "&");
        full_path += cfg_.auth.param + "=" + cfg_.auth.value;
    }

    spdlog::debug("RestCmsAdapter GET: {}", full_path);

    auto configure = [&](auto& client) {
        client.set_connection_timeout(std::chrono::milliseconds(cfg_.timeout_ms));
        client.set_read_timeout(std::chrono::milliseconds(cfg_.timeout_ms));
        if (cfg_.auth.type == config::AuthConfig::Type::Basic)
            client.set_basic_auth(cfg_.auth.username, cfg_.auth.password);
        if (cfg_.auth.type == config::AuthConfig::Type::Bearer)
            client.set_bearer_token_auth(cfg_.auth.value);
    };

    httplib::Result res;
    if (scheme_ == "http") {
        httplib::Client client(host_, port_);
        configure(client);
        res = client.Get(full_path);
    } else {
        httplib::SSLClient client(host_, port_);
        configure(client);
        res = client.Get(full_path);
    }

    if (auto err = get_error(res)) return *err;

    HttpResponse hr;
    hr.body = res->body;
    for (const auto& [k, v] : res->headers)
        hr.headers[lowercase(k)] = v;
    return hr;
}

// ---------------------------------------------------------------------------
// http_get
// ---------------------------------------------------------------------------

error::Result<RestCmsAdapter::HttpResponse>
RestCmsAdapter::http_get(const std::string& path) const {
    return do_get(base_path_ + path);
}

// ---------------------------------------------------------------------------
// http_get_raw_path
// ---------------------------------------------------------------------------

error::Result<RestCmsAdapter::HttpResponse>
RestCmsAdapter::http_get_raw_path(const std::string& path) const {
    return do_get(path);
}

// ---------------------------------------------------------------------------
// has_next_page  (json_next strategy)
// ---------------------------------------------------------------------------

bool RestCmsAdapter::has_next_page(std::string_view body, std::string_view json_next) {
    simdjson::ondemand::parser parser;
    auto padded = simdjson::padded_string(body.data(), body.size());
    auto doc    = parser.iterate(padded);
    if (doc.error()) return false;

    simdjson::ondemand::value root_val;
    if (doc.get_value().get(root_val)) return false;

    render::Value root     = from_simdjson(root_val);
    render::Value sentinel = resolve_path(root, json_next);
    return !sentinel.is_null();
}

// ---------------------------------------------------------------------------
// fetch_endpoint
// ---------------------------------------------------------------------------

error::Result<std::vector<render::Value>>
RestCmsAdapter::fetch_endpoint(const std::string&            collection_name,
                               const config::EndpointConfig& ep) const
{
    const config::PaginationConfig& pag =
        ep.pagination ? *ep.pagination : cfg_.pagination;

    std::vector<render::Value> all_values;

    // Pagination state variables — only the active strategy uses its variables.
    int         page              = 1;
    int         total_pages       = 0;   // set on first response for header-based strategies
    std::string cursor_token;            // cursor token for json_cursor strategy
    std::optional<std::string> link_next_path;      // link_header strategy
    std::optional<std::string> json_body_next_path; // json_next_url strategy
    bool        has_more          = true;

    while (has_more) {
        // ---- Section A: Build request -----------------------------------
        error::Result<HttpResponse> resp_r;

        if (pag.link_header && link_next_path.has_value()) {
            resp_r = http_get_raw_path(*link_next_path);
        } else if (pag.json_next_url.has_value() && json_body_next_path.has_value()) {
            resp_r = http_get_raw_path(*json_body_next_path);
        } else {
            std::string path = "/" + ep.path;
            path += build_query(ep.params);
            char sep = path.find('?') == std::string::npos ? '?' : '&';

            if (pag.limit_param.has_value()) {
                path += sep;
                path += *pag.limit_param + "=" + std::to_string(pag.limit);
                sep = '&';
            }

            if (pag.json_cursor.has_value() && !cursor_token.empty()) {
                path += sep;
                path += pag.cursor_param.value_or("cursor") + "=" + cursor_token;
            } else if (pag.offset_param.has_value()) {
                path += sep;
                path += *pag.offset_param + "=" + std::to_string((page - 1) * pag.limit);
            } else if (pag.page_param.has_value()) {
                path += sep;
                path += *pag.page_param + "=" + std::to_string(page);
            }

            resp_r = http_get(path);
        }

        // ---- HTTP error handling ----------------------------------------
        if (!resp_r) {
            if (pag.optimistic_fetching) break;  // 404/network error → stop silently
            return std::unexpected(resp_r.error());
        }
        const HttpResponse& resp = *resp_r;

        // ---- Section B: Parse items -------------------------------------
        std::vector<render::Value> page_values;
        if (ep.response_key.empty()) {
            auto vals_r = parse_root_array(resp.body);
            if (!vals_r) {
                if (pag.optimistic_fetching) break;
                return std::unexpected(vals_r.error());
            }
            page_values = std::move(*vals_r);
        } else {
            auto vals_r = parse_keyed_array(resp.body, ep.response_key);
            if (!vals_r) {
                if (pag.optimistic_fetching) break;
                return std::unexpected(vals_r.error());
            }
            page_values = std::move(*vals_r);
        }

        // optimistic_fetching: an empty page signals end-of-data — do not append.
        if (pag.optimistic_fetching && page_values.empty()) {
            has_more = false;
            break;
        }

        all_values.insert(all_values.end(),
                          std::make_move_iterator(page_values.begin()),
                          std::make_move_iterator(page_values.end()));

        // ---- Section C: Pagination strategy (evaluated in priority order)
        if (pag.total_pages_header.has_value()) {
            // Strategy 1: total pages declared in a response header (e.g. X-WP-TotalPages).
            if (total_pages == 0) {
                const std::string hval = resp.header(*pag.total_pages_header);
                if (!hval.empty()) {
                    try { total_pages = std::stoi(hval); } catch (...) {}
                }
                if (total_pages <= 0) total_pages = 1;
            }
            has_more = page < total_pages;
        } else if (pag.total_count_header.has_value()) {
            // Strategy 2: total item count in a header; derive page count from limit.
            if (total_pages == 0) {
                const std::string hval = resp.header(*pag.total_count_header);
                if (!hval.empty()) {
                    try {
                        int total_count = std::stoi(hval);
                        int lim         = pag.limit > 0 ? pag.limit : 1;
                        total_pages     = (total_count + lim - 1) / lim;
                    } catch (...) {}
                }
                if (total_pages <= 0) total_pages = 1;
            }
            has_more = page < total_pages;
        } else if (pag.link_header) {
            // Strategy 3: follow the URL extracted from Link: <url>; rel="next" header.
            link_next_path = parse_link_next(resp.header("link"));
            has_more = link_next_path.has_value();
        } else if (pag.json_cursor.has_value()) {
            // Strategy 4: body contains the next cursor token at a dot-path.
            render::Value root       = parse_body_value(resp.body);
            render::Value cursor_val = resolve_path(root, *pag.json_cursor);
            cursor_token = cursor_val.is_null() ? std::string{} : cursor_val.to_string();
            has_more = !cursor_token.empty();
        } else if (pag.json_next_url.has_value()) {
            // Strategy 5: body contains the full URL of the next page at a dot-path.
            render::Value root    = parse_body_value(resp.body);
            render::Value url_val = resolve_path(root, *pag.json_next_url);
            std::string next_str  = url_val.is_null() ? std::string{} : url_val.to_string();
            if (!next_str.empty()) {
                json_body_next_path = url_to_path(next_str);
                has_more = true;
            } else {
                json_body_next_path = std::nullopt;
                has_more = false;
            }
        } else if (pag.json_next.has_value()) {
            // Strategy 6: dot-path non-null sentinel; increment page counter (Ghost-style).
            has_more = has_next_page(resp.body, *pag.json_next);
        } else if (pag.optimistic_fetching) {
            // Strategy 7: blindly fetch page N+1; empty page or error signals end-of-data.
            has_more = true;
        } else {
            // No pagination configured — single page only.
            has_more = false;
        }

        ++page;
    }

    return all_values;
}

// ---------------------------------------------------------------------------
// fetch_all
// ---------------------------------------------------------------------------

error::Result<FetchResult> RestCmsAdapter::fetch_all(FetchCallback progress) {
    // Collect collections that have both an endpoint and a collection config.
    std::vector<std::string> active_collections;
    for (const auto& [name, ep] : cfg_.endpoints) {
        if (collections_.count(name))
            active_collections.push_back(name);
    }

    // Fetch each collection (parallelise if OpenMP is available).
    std::vector<error::Result<std::vector<render::Value>>> fetched(active_collections.size());

#ifdef GUSS_USE_OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (size_t i = 0; i < active_collections.size(); ++i) {
        const std::string& name = active_collections[i];
        fetched[i] = fetch_endpoint(name, cfg_.endpoints.at(name));
    }

    FetchResult result;
    result.site = build_site_value();

    // Check errors and build initial RenderItem vectors.
    for (size_t i = 0; i < active_collections.size(); ++i) {
        if (!fetched[i]) return std::unexpected(fetched[i].error());
    }

    // Apply field maps, enrich, build RenderItems.
    for (size_t i = 0; i < active_collections.size(); ++i) {
        const std::string& coll_name = active_collections[i];
        auto& values                 = *fetched[i];
        const auto& coll_cfg         = collections_.at(coll_name);

        // Apply field map if configured
        auto fm_it = cfg_.field_maps.find(coll_name);

        auto& items_vec = result.items[coll_name];
        items_vec.reserve(values.size());

        for (auto& v : values) {
            if (fm_it != cfg_.field_maps.end())
                apply_field_map(v, fm_it->second);

            enrich_item(v, coll_name);

            std::string op  = v["output_path"].to_string();
            std::string tpl = coll_cfg.item_template;

            // Per-item template override (custom_template field)
            auto custom = v["custom_template"];
            if (!custom.is_null() && !custom.to_string().empty())
                tpl = custom.to_string();

            render::RenderItem ri;
            ri.data        = v;
            ri.context_key = coll_cfg.context_key;

            if (!op.empty() && op != "null" && !tpl.empty()) {
                ri.output_path   = std::filesystem::path(op);
                ri.template_name = tpl;
            }

            items_vec.push_back(std::move(ri));
        }
    }

    // Build cross-references.
    for (const auto& [coll_name, cr] : cfg_.cross_references) {
        auto target_it = result.items.find(coll_name);
        if (target_it == result.items.end()) continue;

        auto source_it = result.items.find(cr.from);
        if (source_it == result.items.end()) continue;

        for (auto& target_item : target_it->second) {
            const std::string target_val = target_item.data[cr.match_key].to_string();
            if (target_val.empty() || target_val == "null") continue;

            std::vector<render::Value> related;
            for (const auto& src : source_it->second) {
                render::Value via_val = resolve_path(src.data, cr.via);
                if (via_val.is_array()) {
                    for (size_t k = 0; k < via_val.size(); ++k) {
                        const render::Value& elem = via_val[k];
                        const std::string cmp = elem.is_object()
                            ? elem[cr.match_key].to_string()
                            : elem.to_string();
                        if (cmp == target_val) {
                            related.push_back(src.data);
                            break;
                        }
                    }
                } else {
                    if (via_val.to_string() == target_val)
                        related.push_back(src.data);
                }
            }

            target_item.extra_context.emplace_back(cr.from,
                                                   render::Value(std::move(related)));
        }
    }

    // Add prev/next links to the posts collection.
    if (auto posts_it = result.items.find("posts"); posts_it != result.items.end()) {
        auto& posts = posts_it->second;
        for (size_t i = 0; i < posts.size(); ++i) {
            if (i > 0) {
                posts[i].extra_context.emplace_back("prev_post", posts[i - 1].data);
            }
            if (i + 1 < posts.size()) {
                posts[i].extra_context.emplace_back("next_post", posts[i + 1].data);
            }
        }
    }

    spdlog::info("RestCmsAdapter: fetched {} collections", active_collections.size());
    for (const auto& name : active_collections) {
        spdlog::info("  {}: {} items", name, result.items[name].size());
    }

    if (progress) progress(1, 1);
    return result;
}

// ---------------------------------------------------------------------------
// ping
// ---------------------------------------------------------------------------

error::VoidResult RestCmsAdapter::ping() {
    // Hit the first configured endpoint with limit=1 as a connectivity check.
    if (cfg_.endpoints.empty()) {
        return error::make_error(error::ErrorCode::AdapterNotFound,
                                 "No endpoints configured", cfg_.base_url);
    }

    const auto& [name, ep] = *cfg_.endpoints.begin();
    const config::PaginationConfig& pag = ep.pagination ? *ep.pagination : cfg_.pagination;

    std::string path = "/" + ep.path;
    path += build_query(ep.params);
    char sep = path.find('?') == std::string::npos ? '?' : '&';
    if (pag.limit_param.has_value()) {
        path += sep;
        path += *pag.limit_param + "=1";
        sep = '&';
    }
    if (pag.page_param.has_value()) {
        path += sep;
        path += *pag.page_param + "=1";
    }

    auto res = http_get(path);
    if (!res) return std::unexpected(res.error());
    return {};
}

} // namespace guss::adapters::rest
