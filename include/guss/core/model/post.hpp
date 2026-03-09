#pragma once

#include "./author.hpp"
#include "./taxonomy.hpp"
#include <string>
#include <optional>
#include <vector>
#include <chrono>

namespace guss::model {

enum class PostStatus {
    Draft,
    Published,
    Scheduled
};

inline std::string post_status_to_string(PostStatus status) {
    switch (status) {
        case PostStatus::Published: return "published";
        case PostStatus::Scheduled: return "scheduled";
        case PostStatus::Draft: [[fallthrough]];
        default: return "draft";
    }
}

inline PostStatus post_status_from_string(const std::string& str) {
    if (str == "published") return PostStatus::Published;
    if (str == "scheduled") return PostStatus::Scheduled;
    return PostStatus::Draft;
}

struct Post {
    std::string id;
    std::string title;
    std::string slug;
    std::string source_path;
    std::string content_markdown;
    std::string content_html;
    std::string excerpt;
    PostStatus status = PostStatus::Draft;

    std::optional<std::string> feature_image;
    std::optional<std::string> feature_image_alt;
    std::optional<std::string> meta_title;
    std::optional<std::string> meta_description;
    std::optional<std::string> canonical_url;
    std::optional<std::string> custom_template;

    std::optional<std::chrono::system_clock::time_point> published_at;
    std::optional<std::chrono::system_clock::time_point> updated_at;
    std::chrono::system_clock::time_point created_at;

    std::vector<Author> authors;
    std::vector<Tag> tags;
    std::vector<Category> categories;

    std::string permalink;
    std::string output_path;

    [[nodiscard]] std::string to_json() const {
        auto format_time = [](const std::chrono::system_clock::time_point& tp) -> std::string {
            auto time_t = std::chrono::system_clock::to_time_t(tp);
            std::tm tm = *std::gmtime(&time_t);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
            return buf;
        };

        std::string j;
        j += "{\"id\":\"" + id + "\",";
        j += "\"title\":\"" + title + "\",";
        j += "\"slug\":\"" + slug + "\",";
        j += "\"source_path\":\"" + source_path + "\",";
        j += "\"content\":\"" + content_html + "\",";
        j += "\"content_markdown\":\"" + content_markdown + "\",";
        j += "\"excerpt\":\"" + excerpt + "\",";
        j += "\"status\":\"" + post_status_to_string(status) + "\",";
        j += "\"permalink\":\"" + permalink + "\",";
        j += "\"output_path\":\"" + output_path + "\"";
        if (feature_image) j += ",\"feature_image\":\"" + *feature_image + "\"";
        if (feature_image_alt) j += ",\"feature_image_alt\":\"" + *feature_image_alt + "\"";
        if (meta_title) j += ",\"meta_title\":\"" + *meta_title + "\"";
        if (meta_description) j += ",\"meta_description\":\"" + *meta_description + "\"";
        if (canonical_url) j += ",\"canonical_url\":\"" + *canonical_url + "\"";
        if (custom_template) j += ",\"custom_template\":\"" + *custom_template + "\"";
        j += ",\"created_at\":\"" + format_time(created_at) + "\"";
        if (published_at) j += ",\"published_at\":\"" + format_time(*published_at) + "\"";
        if (updated_at) j += ",\"updated_at\":\"" + format_time(*updated_at) + "\"";

        if (published_at) {
            auto time_t = std::chrono::system_clock::to_time_t(*published_at);
            std::tm tm = *std::gmtime(&time_t);
            j += ",\"year\":" + std::to_string(tm.tm_year + 1900);
            j += ",\"month\":" + std::to_string(tm.tm_mon + 1);
            j += ",\"day\":" + std::to_string(tm.tm_mday);
        }

        j += ",\"authors\":[";
        for (size_t i = 0; i < authors.size(); ++i) {
            if (i > 0) j += ",";
            j += authors[i].to_json();
        }
        j += "]";

        if (!authors.empty()) {
            j += ",\"author\":" + authors[0].to_json();
        }

        j += ",\"tags\":[";
        for (size_t i = 0; i < tags.size(); ++i) {
            if (i > 0) j += ",";
            j += tags[i].to_json();
        }
        j += "]";

        j += ",\"categories\":[";
        for (size_t i = 0; i < categories.size(); ++i) {
            if (i > 0) j += ",";
            j += categories[i].to_json();
        }
        j += "]";

        j += "}";
        return j;
    }
};

} // namespace guss::model
