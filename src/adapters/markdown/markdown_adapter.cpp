#include "guss/adapters/markdown/markdown_adapter.hpp"
#include "guss/core/render_item.hpp"
#include "guss/core/value.hpp"

#include <md4c-html.h>
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

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#elif defined(__APPLE__)
#  include <sys/stat.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#endif

#ifdef GUSS_USE_OPENMP
#include <omp.h>
#endif

namespace {

using namespace guss;

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

/// Returns the file creation (birth) time as ISO-8601, or "" on failure.
/// Falls back to mtime on Linux kernels that don't expose birth time via statx.
std::string creation_time_to_iso8601(const std::filesystem::path& path) {
    char buf[32];
    std::tm tm{};

#if defined(_WIN32)
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    FILETIME ft;
    const BOOL ok = GetFileTime(h, &ft, nullptr, nullptr);
    CloseHandle(h);
    if (!ok) return "";
    ULARGE_INTEGER ull;
    ull.LowPart  = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    // FILETIME: 100-ns intervals since 1601-01-01; subtract to Unix epoch
    time_t tt = static_cast<time_t>((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
    gmtime_s(&tm, &tt);

#elif defined(__APPLE__)
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return "";
    time_t tt = st.st_birthtimespec.tv_sec;
    gmtime_r(&tt, &tm);

#else
    // Linux: statx exposes birth time (stx_btime) on kernels >= 4.11 / glibc >= 2.28.
    // Fall back to mtime if the field is not available.
    struct statx stx{};
    if (statx(AT_FDCWD, path.c_str(), 0, STATX_BTIME, &stx) == 0 &&
            (stx.stx_mask & STATX_BTIME)) {
        time_t tt = static_cast<time_t>(stx.stx_btime.tv_sec);
        gmtime_r(&tt, &tm);
    } else {
        // Fallback: last modification time
        std::error_code ec;
        auto mtime = std::filesystem::last_write_time(path, ec);
        if (ec) return "";
        auto sys_time = std::chrono::file_clock::to_sys(mtime);
        time_t tt = std::chrono::system_clock::to_time_t(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(sys_time));
        gmtime_r(&tt, &tm);
    }
#endif

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
    if (end == std::string::npos) {
        // Also handle files ending with \n--- (no trailing newline)
        if (content.size() >= 8 && content.substr(content.size() - 4) == "\n---")
            return {content.substr(4, content.size() - 8), ""};
        return {"", content};
    }
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

    // published_at fallback: use file creation time so edits don't shift the date
    if (!data.count("published_at") || data.at("published_at").to_string().empty())
        data["published_at"] = core::Value(creation_time_to_iso8601(path));

    // Render markdown with GFM extensions (tables, strikethrough, autolinks)
    std::string html_out;
    auto append_html = [](const MD_CHAR* text, MD_SIZE size, void* userdata) {
        static_cast<std::string*>(userdata)->append(text, size);
    };
    md_html(body.c_str(), static_cast<MD_SIZE>(body.size()), append_html, &html_out,
            MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_PERMISSIVEURLAUTOLINKS, 0);
    data["html"] = core::Value(std::move(html_out));

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
            items_vec.push_back(build_render_item(v, coll_cfg));
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

    // Step 5: cross-references
    apply_cross_references(result, cfg_.cross_references);

    // Step 6: prev/next links for posts
    apply_prev_next(result, "posts");

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
