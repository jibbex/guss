#include "guss/adapters/markdown/markdown_adapter.hpp"
#include "guss/core/render_item.hpp"
#include "guss/core/value.hpp"

#include <cmark.h>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef GUSS_USE_OPENMP
#include <omp.h>
#endif

namespace {

using namespace guss;

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

std::string mtime_to_iso8601(const std::filesystem::path& path) {
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec) return "";
    auto sys_time = std::chrono::file_clock::to_sys(mtime);
    auto tt = std::chrono::system_clock::to_time_t(
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(sys_time));
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

core::Value yaml_to_value(const YAML::Node& node) {
    if (node.IsScalar()) return core::Value(node.as<std::string>());
    if (node.IsSequence()) {
        std::vector<core::Value> arr;
        arr.reserve(node.size());
        for (const auto& item : node) arr.push_back(yaml_to_value(item));
        return core::Value(std::move(arr));
    }
    if (node.IsMap()) {
        std::unordered_map<std::string, core::Value> m;
        for (const auto& kv : node)
            m[kv.first.as<std::string>()] = yaml_to_value(kv.second);
        return core::Value(std::move(m));
    }
    return core::Value{};
}

/// Single try/catch boundary for the third-party YAML parser.
std::optional<YAML::Node> safe_yaml_load(const std::string& yaml_str,
                                          const std::string& path_for_log) {
    try {
        return YAML::Load(yaml_str);
    } catch (const YAML::Exception& e) {
        spdlog::warn("MarkdownAdapter: YAML error in {}: {}", path_for_log, e.what());
        return std::nullopt;
    }
}

/// Split content into {frontmatter_yaml, body_markdown}.
std::pair<std::string, std::string> split_frontmatter(const std::string& content) {
    if (content.size() < 4 || content.substr(0, 4) != "---\n")
        return {"", content};
    auto end = content.find("\n---\n", 4);
    if (end == std::string::npos) return {"", content};
    return {content.substr(4, end - 4), content.substr(end + 5)};
}

// ---------------------------------------------------------------------------
// parse_md_file — returns raw Value (no RenderItem construction)
// ---------------------------------------------------------------------------

std::optional<core::Value> parse_md_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        spdlog::warn("MarkdownAdapter: cannot open {}", path.string());
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string content = ss.str();

    auto [fm_yaml, body] = split_frontmatter(content);

    std::unordered_map<std::string, core::Value> data;
    if (!fm_yaml.empty()) {
        auto fm_node = safe_yaml_load(fm_yaml, path.string());
        if (fm_node && fm_node->IsMap()) {
            for (const auto& kv : *fm_node)
                data[kv.first.as<std::string>()] = yaml_to_value(kv.second);
        }
    }

    // Slug fallback
    if (!data.count("slug"))
        data["slug"] = core::Value(path.stem().string());

    // published_at fallback
    if (!data.count("published_at") || data.at("published_at").to_string().empty())
        data["published_at"] = core::Value(mtime_to_iso8601(path));

    // Render markdown — cmark allocates with malloc; must free
    char* html_c = cmark_markdown_to_html(body.c_str(), body.size(), CMARK_OPT_DEFAULT);
    data["html"] = core::Value(html_c ? std::string(html_c) : std::string{});
    free(html_c);

    return core::Value(std::move(data));
}

} // anonymous namespace

namespace guss::adapters {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MarkdownAdapter::MarkdownAdapter(const core::config::MarkdownAdapterConfig& cfg,
                                 const core::config::SiteConfig& site_cfg,
                                 const core::config::CollectionCfgMap& collections)
    : ContentAdapter(site_cfg, collections)
    , cfg_(cfg)
{}

// ---------------------------------------------------------------------------
// fetch_all
// ---------------------------------------------------------------------------

core::error::Result<FetchResult> MarkdownAdapter::fetch_all(FetchCallback progress) {
    // Step 1: active collections — intersection of collection_paths and collections_
    std::vector<std::string> active_collections;
    for (const auto& [name, path] : cfg_.collection_paths) {
        if (collections_.count(name))
            active_collections.push_back(name);
    }

    // Step 2: parallel file parse
    std::vector<std::vector<std::optional<core::Value>>>
        parsed(active_collections.size());

#ifdef GUSS_USE_OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (size_t i = 0; i < active_collections.size(); ++i) {
        const std::string&           name = active_collections[i];
        const std::filesystem::path& dir  = cfg_.collection_paths.at(name);

        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || ec) {
            spdlog::warn("MarkdownAdapter: directory does not exist: {}", dir.string());
            continue;
        }

        std::error_code iter_ec;
        if (cfg_.recursive) {
            for (const auto& entry :
                    std::filesystem::recursive_directory_iterator(dir, iter_ec)) {
                if (iter_ec) break;
                std::error_code reg_ec;
                if (entry.is_regular_file(reg_ec) && !reg_ec
                        && entry.path().extension() == ".md")
                    parsed[i].push_back(parse_md_file(entry.path()));
            }
        } else {
            for (const auto& entry :
                    std::filesystem::directory_iterator(dir, iter_ec)) {
                if (iter_ec) break;
                std::error_code reg_ec;
                if (entry.is_regular_file(reg_ec) && !reg_ec
                        && entry.path().extension() == ".md")
                    parsed[i].push_back(parse_md_file(entry.path()));
            }
        }
        if (iter_ec)
            spdlog::warn("MarkdownAdapter: iteration error in {}: {}",
                         dir.string(), iter_ec.message());
    }

    // Step 3: site value
    FetchResult result;
    result.site = build_site_value();

    // Step 4: field_map → enrich → RenderItem
    for (size_t i = 0; i < active_collections.size(); ++i) {
        const std::string& coll_name = active_collections[i];
        const auto& coll_cfg         = collections_.at(coll_name);
        auto fm_it = cfg_.field_maps.find(coll_name);

        std::vector<core::RenderItem> items_vec;
        for (auto& opt : parsed[i]) {
            if (!opt) continue;
            core::Value v = std::move(*opt);

            if (fm_it != cfg_.field_maps.end())
                apply_field_map(v, fm_it->second);

            enrich_item(v, coll_name);

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
            items_vec.push_back(std::move(ri));
        }

        if (!items_vec.empty())
            result.items[coll_name] = std::move(items_vec);
    }

    // Step 4.5: taxonomy synthesis
    // Collect candidates before any insertion (snapshot semantics).
    std::vector<std::string> synthesis_targets;
    for (const auto& [coll_name, cr] : cfg_.cross_references) {
        if (!result.items.count(coll_name) &&
             collections_.count(coll_name) &&
             result.items.count(cr.from))
            synthesis_targets.push_back(coll_name);
    }

    for (const std::string& coll_name : synthesis_targets) {
        const auto& cr       = cfg_.cross_references.at(coll_name);
        const auto& coll_cfg = collections_.at(coll_name);

        // Strip last dot-segment to get the container path.
        // "tags.slug"   -> "tags"   (array of {name,slug} objects)
        // "author.slug" -> "author" (single {name,slug} object)
        const auto dot = cr.via.rfind('.');
        if (dot == std::string::npos) {
            spdlog::warn("MarkdownAdapter: via '{}' has no dot; "
                         "cannot synthesize taxonomy '{}'", cr.via, coll_name);
            continue;
        }
        const std::string container_path = cr.via.substr(0, dot);

        // Walk source items, collect unique taxonomy objects by match_key.
        std::unordered_map<std::string, core::Value> seen;
        std::vector<std::string> order; // first-occurrence insertion order

        for (const auto& src : result.items.at(cr.from)) {
            core::Value container = resolve_path(src.data, container_path);

            auto collect = [&](const core::Value& elem) {
                if (!elem.is_object()) return;
                const std::string key = elem[cr.match_key].to_string();
                if (key.empty() || key == "null") return;
                if (!seen.count(key)) {
                    seen.emplace(key, elem);
                    order.push_back(key);
                }
            };

            if (container.is_array()) {
                for (size_t k = 0; k < container.size(); ++k)
                    collect(container[k]);
            } else if (container.is_object()) {
                collect(container);
            }
        }

        // Enrich and build RenderItems.
        std::vector<core::RenderItem> tax_items;
        for (const std::string& key : order) {
            core::Value v = seen.at(key);
            enrich_item(v, coll_name);
            const std::string op = v["output_path"].to_string();
            if (op.empty() || op == "null") continue;

            if (coll_cfg.archive_template.empty()) {
                spdlog::warn("MarkdownAdapter: taxonomy '{}' has no archive_template; "
                             "skipping synthesized item '{}'", coll_name, key);
                continue;
            }
            core::RenderItem ri;
            ri.data          = std::move(v);
            ri.context_key   = coll_cfg.context_key;
            ri.template_name = coll_cfg.archive_template;
            ri.output_path   = std::filesystem::path(op);
            tax_items.push_back(std::move(ri));
        }

        if (!tax_items.empty())
            result.items[coll_name] = std::move(tax_items);
    }

    // Step 5: cross-references (verbatim from RestCmsAdapter::fetch_all)
    for (const auto& [coll_name, cr] : cfg_.cross_references) {
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
            target_item.extra_context.emplace_back(cr.from,
                                                   core::Value(std::move(related)));
        }
    }

    // Step 6: prev/next links for posts (verbatim from RestCmsAdapter::fetch_all)
    if (auto posts_it = result.items.find("posts"); posts_it != result.items.end()) {
        auto& posts = posts_it->second;
        for (size_t i = 0; i < posts.size(); ++i) {
            if (i > 0)
                posts[i].extra_context.emplace_back("prev_post", posts[i - 1].data);
            if (i + 1 < posts.size())
                posts[i].extra_context.emplace_back("next_post", posts[i + 1].data);
        }
    }

    // Step 7: log + progress
    spdlog::info("MarkdownAdapter: fetched {} collections", active_collections.size());
    for (const auto& name : active_collections) {
        auto it = result.items.find(name);
        spdlog::info("  {}: {} items", name, it != result.items.end() ? it->second.size() : 0u);
    }

    if (progress) progress(1, 1);
    return result;
}

// ---------------------------------------------------------------------------
// ping
// ---------------------------------------------------------------------------

core::error::VoidResult MarkdownAdapter::ping() {
    for (const auto& [name, path] : cfg_.collection_paths) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec)
            return core::error::make_error(core::error::ErrorCode::AdapterNotFound,
                "Content path does not exist",
                path.string());
    }
    return {};
}

} // namespace guss::adapters
