/**
 * @file site.hpp
 * @brief Site aggregate domain model for Apex SSG
 */
#pragma once

#include "./post.hpp"
#include "./page.hpp"
#include "./author.hpp"
#include "./taxonomy.hpp"
#include "./asset.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace guss::model {

struct SiteMetadata {
    std::string title;
    std::string description;
    std::string url;
    std::string language = "en";
    std::optional<std::string> logo;
    std::optional<std::string> icon;
    std::optional<std::string> cover_image;
    std::optional<std::string> twitter;
    std::optional<std::string> facebook;

    [[nodiscard]] std::string to_json() const {
        std::string j;
        j += "{\"title\":\"" + title + "\",";
        j += "\"description\":\"" + description + "\",";
        j += "\"url\":\"" + url + "\",";
        j += "\"language\":\"" + language + "\"";
        if (logo) j += ",\"logo\":\"" + *logo + "\"";
        if (icon) j += ",\"icon\":\"" + *icon + "\"";
        if (cover_image) j += ",\"cover_image\":\"" + *cover_image + "\"";
        if (twitter) j += ",\"twitter\":\"" + *twitter + "\"";
        if (facebook) j += ",\"facebook\":\"" + *facebook + "\"";
        j += "}";
        return j;
    }
};

struct MenuItem {
    std::string label;
    std::string url;
    bool is_current = false;
    std::vector<MenuItem> children;

    [[nodiscard]] std::string to_json() const {
        std::string j;
        j += "{\"label\":\"" + label + "\",";
        j += "\"url\":\"" + url + "\",";
        j += "\"is_current\":" + std::string(is_current ? "true" : "false");
        if (!children.empty()) {
            j += ",\"children\":[";
            for (size_t i = 0; i < children.size(); ++i) {
                if (i > 0) j += ",";
                j += children[i].to_json();
            }
            j += "]";
        }
        j += "}";
        return j;
    }
};

struct SiteData {
    SiteMetadata metadata;

    std::vector<Post> posts;
    std::vector<Page> pages;
    std::vector<Author> authors;
    std::vector<Tag> tags;
    std::vector<Category> categories;
    std::vector<Asset> assets;
    std::vector<MenuItem> navigation;

    std::unordered_map<std::string, size_t> posts_by_slug;
    std::unordered_map<std::string, size_t> pages_by_slug;
    std::unordered_map<std::string, size_t> authors_by_slug;
    std::unordered_map<std::string, size_t> tags_by_slug;
    std::unordered_map<std::string, size_t> categories_by_slug;

    void build_indices() {
        posts_by_slug.clear();
        for (size_t i = 0; i < posts.size(); ++i) {
            posts_by_slug[posts[i].slug] = i;
        }

        pages_by_slug.clear();
        for (size_t i = 0; i < pages.size(); ++i) {
            pages_by_slug[pages[i].slug] = i;
        }

        authors_by_slug.clear();
        for (size_t i = 0; i < authors.size(); ++i) {
            authors_by_slug[authors[i].slug] = i;
        }

        tags_by_slug.clear();
        for (size_t i = 0; i < tags.size(); ++i) {
            tags_by_slug[tags[i].slug] = i;
        }

        categories_by_slug.clear();
        for (size_t i = 0; i < categories.size(); ++i) {
            categories_by_slug[categories[i].slug] = i;
        }
    }

    [[nodiscard]] std::vector<const Post*> get_published_posts() const {
        std::vector<const Post*> result;
        for (const auto& post : posts) {
            if (post.status == PostStatus::Published) {
                result.push_back(&post);
            }
        }
        std::sort(result.begin(), result.end(), [](const Post* a, const Post* b) {
            auto a_time = a->published_at.value_or(a->created_at);
            auto b_time = b->published_at.value_or(b->created_at);
            return a_time > b_time;
        });
        return result;
    }

    [[nodiscard]] std::vector<const Page*> get_published_pages() const {
        std::vector<const Page*> result;
        for (const auto& page : pages) {
            if (page.status == PageStatus::Published) {
                result.push_back(&page);
            }
        }
        return result;
    }

    [[nodiscard]] std::string to_json() const {
        std::string j;
        j += "{\"site\":" + metadata.to_json();

        j += ",\"posts\":[";
        bool first = true;
        for (const auto& post : posts) {
            if (post.status == PostStatus::Published) {
                if (!first) j += ",";
                j += post.to_json();
                first = false;
            }
        }
        j += "]";

        j += ",\"pages\":[";
        first = true;
        for (const auto& page : pages) {
            if (page.status == PageStatus::Published) {
                if (!first) j += ",";
                j += page.to_json();
                first = false;
            }
        }
        j += "]";

        j += ",\"authors\":[";
        for (size_t i = 0; i < authors.size(); ++i) {
            if (i > 0) j += ",";
            j += authors[i].to_json();
        }
        j += "]";

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

        j += ",\"navigation\":[";
        for (size_t i = 0; i < navigation.size(); ++i) {
            if (i > 0) j += ",";
            j += navigation[i].to_json();
        }
        j += "]";

        j += "}";
        return j;
    }
};

} // namespace guss::model
