/**
 * @file adapter.cpp
 * @brief ContentAdapter base class implementation.
 *
 * Provides shared helpers used by all concrete adapters:
 * build_site_value, resolve_path, apply_field_map, enrich_item.
 */
#include "guss/adapters/adapter.hpp"
#include "guss/core/permalink.hpp"

#include <httplib.h>
#include <string>
#include <string_view>
#include <charconv>

namespace guss::adapters {

// ---------------------------------------------------------------------------
// get_error
// ---------------------------------------------------------------------------

std::optional<std::unexpected<core::error::Error>> get_error(const httplib::Result& res) {
    if (!res) {
        return core::error::make_error(
            core::error::ErrorCode::AdapterConnectionFailed,
            "HTTP request failed",
            httplib::to_string(res.error())
        );
    }

    switch (res->status) {
        case 400:
            return core::error::make_error(
                core::error::ErrorCode::AdapterBadRequest, "Bad request", "HTTP 400");
        case 401: [[fallthrough]]
        case 403:
            return core::error::make_error(
                core::error::ErrorCode::AdapterAuthFailed,
                "Authentication failed",
                "HTTP " + std::to_string(res->status));
        case 404:
            return core::error::make_error(
                core::error::ErrorCode::AdapterNotFound, "Not found", "HTTP 404");
        case 429:
            return core::error::make_error(
                core::error::ErrorCode::AdapterRateLimited, "Rate limited", "HTTP 429");
        case 500:
            return core::error::make_error(
                core::error::ErrorCode::AdapterServerError, "Server error", "HTTP 500");
        default:
            if (res->status >= 400) {
                return core::error::make_error(
                    core::error::ErrorCode::AdapterFetchFailed,
                    "HTTP error",
                    "HTTP " + std::to_string(res->status));
            }
    }

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// build_site_value
// ---------------------------------------------------------------------------

core::Value ContentAdapter::build_site_value() const {
    std::unordered_map<std::string, core::Value> m;
    m["title"]       = core::Value(site_cfg_.title);
    m["description"] = core::Value(site_cfg_.description);
    m["url"]         = core::Value(site_cfg_.url);
    m["language"]    = core::Value(site_cfg_.language);
    if (site_cfg_.logo)        m["logo"]        = core::Value(*site_cfg_.logo);
    if (site_cfg_.icon)        m["icon"]        = core::Value(*site_cfg_.icon);
    if (site_cfg_.cover_image) m["cover_image"] = core::Value(*site_cfg_.cover_image);
    if (site_cfg_.twitter)     m["twitter"]     = core::Value(*site_cfg_.twitter);
    if (site_cfg_.facebook)    m["facebook"]    = core::Value(*site_cfg_.facebook);

    if (!site_cfg_.navigation.empty()) {
        std::unordered_map<std::string, core::Value> nav_map;
        for (const auto& [nav_name, items] : site_cfg_.navigation) {
            std::vector<core::Value> arr;
            arr.reserve(items.size());
            for (const auto& ni : items) {
                std::unordered_map<std::string, core::Value> entry;
                entry["label"]    = core::Value(ni.label);
                entry["url"]      = core::Value(ni.url);
                entry["external"] = core::Value(ni.external);
                arr.push_back(core::Value(std::move(entry)));
            }
            nav_map[nav_name] = core::Value(std::move(arr));
        }
        m["navigation"] = core::Value(std::move(nav_map));
    }

    return core::Value(std::move(m));
}

// ---------------------------------------------------------------------------
// resolve_path
// ---------------------------------------------------------------------------

core::Value ContentAdapter::resolve_path(const core::Value& v, std::string_view path) {
    core::Value current = v;
    std::string_view remaining = path;

    while (!remaining.empty()) {
        auto dot = remaining.find('.');
        std::string_view segment = (dot == std::string_view::npos)
            ? remaining
            : remaining.substr(0, dot);
        remaining = (dot == std::string_view::npos)
            ? std::string_view{}
            : remaining.substr(dot + 1);

        // Try numeric index first
        size_t idx = 0;
        auto [ptr, ec] = std::from_chars(segment.data(), segment.data() + segment.size(), idx);
        if (ec == std::errc{} && ptr == segment.data() + segment.size()) {
            current = current[idx];
        } else if (current.is_array()) {
            // Array field projection: collect `segment` from every element that
            // has it. Supports paths like "tags.slug" → ["tech","news"].
            std::vector<core::Value> projected;
            for (size_t i = 0; i < current.size(); ++i) {
                core::Value f = current[i][segment];
                if (!f.is_null()) projected.push_back(std::move(f));
            }
            current = core::Value(std::move(projected));
        } else {
            current = current[segment];
        }

        if (current.is_null()) return current;
    }

    return current;
}

// ---------------------------------------------------------------------------
// apply_field_map
// ---------------------------------------------------------------------------

void ContentAdapter::apply_field_map(
    core::Value& item,
    const std::unordered_map<std::string, std::string>& field_map) {
    for (const auto& [target, source_path] : field_map) {
        item.set(target, resolve_path(item, source_path));
    }
}

// ---------------------------------------------------------------------------
// enrich_item
// ---------------------------------------------------------------------------

void ContentAdapter::enrich_item(core::Value& item, const std::string& collection_name) const {
    if (!item.is_object()) return;

    // Extract year/month/day from published_at if present and long enough
    std::string published_at = item["published_at"].to_string();
    if (published_at != "null" && published_at.size() >= 10) {
        item.set("year",  core::Value(std::string(published_at.substr(0, 4))));
        item.set("month", core::Value(std::string(published_at.substr(5, 2))));
        item.set("day",   core::Value(std::string(published_at.substr(8, 2))));
    }

    // Compute permalink and output_path from collection config
    auto cfg_it = collections_.find(collection_name);
    if (cfg_it == collections_.end() || cfg_it->second.permalink.empty()) return;

    const std::string permalink =
        core::PermalinkGenerator::expand(cfg_it->second.permalink, item);
    const std::filesystem::path output_path =
        core::PermalinkGenerator::permalink_to_path(permalink);

    item.set("permalink",   core::Value(std::string(permalink)));
    item.set("output_path", core::Value(output_path.generic_string()));
}

// ---------------------------------------------------------------------------
// build_render_item
// ---------------------------------------------------------------------------

core::RenderItem ContentAdapter::build_render_item(
    const core::Value& v,
    const core::config::CollectionConfig& coll_cfg)
{
    std::string op  = v["output_path"].to_string();
    std::string tpl = coll_cfg.item_template;

    auto custom = v["custom_template"];
    if (!custom.is_null() && !custom.to_string().empty())
        tpl = custom.to_string();

    core::RenderItem ri;
    ri.data        = v;
    ri.context_key = coll_cfg.context_key;

    if (!op.empty() && op != "null" && !tpl.empty()) {
        ri.output_path   = std::filesystem::path(op);
        ri.template_name = tpl;
    }
    return ri;
}

// ---------------------------------------------------------------------------
// apply_cross_references
// ---------------------------------------------------------------------------

void ContentAdapter::apply_cross_references(
    FetchResult& result,
    const core::config::CrossRefCfgMap& cross_refs) const
{
    for (const auto& [coll_name, cr] : cross_refs) {
        auto target_it = result.items.find(coll_name);
        if (target_it == result.items.end()) continue;

        auto source_it = result.items.find(cr.from);
        if (source_it == result.items.end()) continue;

        for (auto& target_item : target_it->second) {
            const std::string target_val = target_item.data[cr.match_key].to_string();
            if (target_val.empty() || target_val == "null") continue;

            std::vector<core::Value> related;
            for (const auto& src : source_it->second) {
                core::Value via_val = resolve_path(src.data, cr.via);
                if (via_val.is_array()) {
                    for (size_t k = 0; k < via_val.size(); ++k) {
                        const core::Value& elem = via_val[k];
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
            target_item.extra_context.emplace_back(cr.from, core::Value(std::move(related)));
        }
    }
}

// ---------------------------------------------------------------------------
// apply_prev_next
// ---------------------------------------------------------------------------

void ContentAdapter::apply_prev_next(FetchResult& result, const std::string& collection_name) {
    auto it = result.items.find(collection_name);
    if (it == result.items.end()) return;

    auto& items = it->second;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0)
            items[i].extra_context.emplace_back("prev_post", items[i - 1].data);
        if (i + 1 < items.size())
            items[i].extra_context.emplace_back("next_post", items[i + 1].data);
    }
}

} // namespace guss::adapters