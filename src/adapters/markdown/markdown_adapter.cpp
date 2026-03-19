#include "guss/adapters/markdown/markdown_adapter.hpp"
#include "guss/core/permalink.hpp"
#include "guss/core/render_item.hpp"
#include "guss/core/value.hpp"
#include <chrono>
#include <cmark.h>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

#ifdef GUSS_USE_OPENMP
#include <omp.h>
#endif

namespace guss::adapters {

namespace {

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

struct ParsedDate { std::string year, month, day; };
ParsedDate parse_date(std::string_view iso) {
    if (iso.size() < 10) return {};
    return {
        std::string(iso.substr(0, 4)),
        std::string(iso.substr(5, 2)),
        std::string(iso.substr(8, 2))
    };
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

/// Parse YAML content, returning nullopt and logging on failure.
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

// Split content into {frontmatter_yaml, body_markdown}.
// Frontmatter is delimited by "---" lines.
std::pair<std::string, std::string> split_frontmatter(const std::string& content) {
    if (content.size() < 4 || content.substr(0, 4) != "---\n")
        return {"", content};
    auto end = content.find("\n---\n", 4);
    if (end == std::string::npos) return {"", content};
    return {content.substr(4, end - 4), content.substr(end + 5)};
}

std::optional<core::RenderItem> process_file(
    const std::filesystem::path& path,
    const core::config::CollectionConfig& coll_cfg)
{
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

    // slug fallback
    if (!data.count("slug") || data.at("slug").to_string().empty())
        data["slug"] = core::Value(path.stem().string());

    // published_at fallback
    std::string published_at_str;
    if (auto it = data.find("published_at"); it != data.end())
        published_at_str = it->second.to_string();
    if (published_at_str.empty()) {
        published_at_str = mtime_to_iso8601(path);
        data["published_at"] = core::Value(published_at_str);
    }

    // year/month/day
    auto date = parse_date(published_at_str);
    data["year"]  = core::Value(std::move(date.year));
    data["month"] = core::Value(std::move(date.month));
    data["day"]   = core::Value(std::move(date.day));

    // Render markdown with cmark
    {
        char* html_c = cmark_markdown_to_html(body.c_str(), body.size(), CMARK_OPT_DEFAULT);
        data["html"] = core::Value(html_c ? std::string(html_c) : std::string{});
        free(html_c);
    }

    // Compute output path
    core::Value item_val(std::move(data));
    const std::string permalink = core::PermalinkGenerator::expand(coll_cfg.permalink, item_val);
    auto output_path = core::PermalinkGenerator::permalink_to_path(permalink);

    return core::RenderItem{std::move(output_path), coll_cfg.item_template, std::move(item_val)};
}

} // anonymous namespace

MarkdownAdapter::MarkdownAdapter(const core::config::MarkdownAdapterConfig& cfg,
                                 const core::config::SiteConfig& site_cfg,
                                 const core::config::CollectionCfgMap& collections)
    : ContentAdapter(site_cfg, collections)
    , config_(cfg)
{}

core::error::Result<FetchResult> MarkdownAdapter::fetch_all(FetchCallback /*progress*/) {
    FetchResult result;

    // Collect .md files
    std::vector<std::filesystem::path> md_files;
    {
        std::error_code ec;
        if (!std::filesystem::exists(config_.content_path, ec) || ec) {
            spdlog::warn("MarkdownAdapter: content_path does not exist: {}",
                         config_.content_path.string());
            return result;
        }
        std::error_code iter_ec;
        for (const auto& entry :
                std::filesystem::recursive_directory_iterator(config_.content_path, iter_ec)) {
            if (iter_ec) break;
            std::error_code ec3;
            if (entry.is_regular_file(ec3) && !ec3 && entry.path().extension() == ".md")
                md_files.push_back(entry.path());
        }
        if (iter_ec) spdlog::warn("MarkdownAdapter: directory iteration error: {}", iter_ec.message());
    }

    if (md_files.empty()) return result;

    // Pick collection: prefer "posts", otherwise first entry with item_template
    std::string collection_name;
    const core::config::CollectionConfig* coll_cfg = nullptr;
    if (auto it = collections_.find("posts");
            it != collections_.end() && !it->second.item_template.empty()) {
        collection_name = "posts";
        coll_cfg = &it->second;
    } else {
        for (const auto& [name, cfg] : collections_) {
            if (!cfg.item_template.empty()) {
                collection_name = name;
                coll_cfg = &cfg;
                break;
            }
        }
    }

    if (!coll_cfg) {
        spdlog::info("MarkdownAdapter: no collection with item_template configured; skipping");
        return result;
    }

    // Process files in parallel
    std::vector<std::optional<core::RenderItem>> processed(md_files.size());
#ifdef GUSS_USE_OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (size_t i = 0; i < md_files.size(); ++i)
        processed[i] = process_file(md_files[i], *coll_cfg);

    auto& items_vec = result.items[collection_name];
    for (auto& opt : processed)
        if (opt) items_vec.push_back(std::move(*opt));

    spdlog::info("MarkdownAdapter: {} items from {}",
                 items_vec.size(), config_.content_path.string());

    // Populate site Value from SiteConfig via base class helper
    result.site = build_site_value();

    return result;
}

core::error::VoidResult MarkdownAdapter::ping() {
    std::error_code ec;
    if (!std::filesystem::exists(config_.content_path, ec) || ec)
        return core::error::make_error(core::error::ErrorCode::AdapterNotFound,
            "Content path does not exist", config_.content_path.string());
    return {};
}

} // namespace guss::adapters
