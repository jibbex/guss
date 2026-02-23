#pragma once

#include "./author.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <vector>
#include <chrono>

namespace guss::model {

enum class PageStatus {
    Draft,
    Published
};

inline std::string page_status_to_string(const PageStatus status) {
    if (status == PageStatus::Published) return "published";
    return "draft";
}

inline PageStatus page_status_from_string(const std::string& str) {
    if (str == "published") return PageStatus::Published;
    return PageStatus::Draft;
}

struct Page {
    std::string id;
    std::string title;
    std::string slug;
    std::string source_path;
    std::string content_markdown;
    std::string content_html;
    PageStatus status = PageStatus::Draft;

    std::optional<std::string> feature_image;
    std::optional<std::string> feature_image_alt;
    std::optional<std::string> meta_title;
    std::optional<std::string> meta_description;
    std::optional<std::string> canonical_url;
    std::optional<std::string> custom_template;
    std::optional<std::string> parent_slug;

    std::optional<std::chrono::system_clock::time_point> published_at;
    std::optional<std::chrono::system_clock::time_point> updated_at;
    std::chrono::system_clock::time_point created_at;

    std::vector<Author> authors;

    std::string permalink;
    std::string output_path;
    int menu_order = 0;
    bool show_in_menu = false;

    [[nodiscard]] nlohmann::json to_json() const {
        nlohmann::json j;
        j["id"] = id;
        j["title"] = title;
        j["slug"] = slug;
        j["source_path"] = source_path;
        j["content"] = content_html;
        j["content_markdown"] = content_markdown;
        j["status"] = page_status_to_string(status);
        j["permalink"] = permalink;
        j["output_path"] = output_path;
        j["menu_order"] = menu_order;
        j["show_in_menu"] = show_in_menu;

        if (feature_image) j["feature_image"] = *feature_image;
        if (feature_image_alt) j["feature_image_alt"] = *feature_image_alt;
        if (meta_title) j["meta_title"] = *meta_title;
        if (meta_description) j["meta_description"] = *meta_description;
        if (canonical_url) j["canonical_url"] = *canonical_url;
        if (custom_template) j["custom_template"] = *custom_template;
        if (parent_slug) j["parent_slug"] = *parent_slug;

        auto format_time = [](const std::chrono::system_clock::time_point& tp) -> std::string {
            auto time_t = std::chrono::system_clock::to_time_t(tp);
            std::tm tm = *std::gmtime(&time_t);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
            return buf;
        };

        j["created_at"] = format_time(created_at);
        if (published_at) j["published_at"] = format_time(*published_at);
        if (updated_at) j["updated_at"] = format_time(*updated_at);

        nlohmann::json authors_json = nlohmann::json::array();
        for (const auto& author : authors) {
            authors_json.push_back(author.to_json());
        }
        j["authors"] = authors_json;

        if (!authors.empty()) {
            j["author"] = authors[0].to_json();
        }

        return j;
    }
};

} // namespace guss::model
