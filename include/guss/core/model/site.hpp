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
#include <nlohmann/json.hpp>
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

    [[nodiscard]] nlohmann::json to_json() const {
        nlohmann::json j;
        j["title"] = title;
        j["description"] = description;
        j["url"] = url;
        j["language"] = language;
        if (logo) j["logo"] = *logo;
        if (icon) j["icon"] = *icon;
        if (cover_image) j["cover_image"] = *cover_image;
        if (twitter) j["twitter"] = *twitter;
        if (facebook) j["facebook"] = *facebook;
        return j;
    }
};

struct MenuItem {
    std::string label;
    std::string url;
    bool is_current = false;
    std::vector<MenuItem> children;

    [[nodiscard]] nlohmann::json to_json() const {
        nlohmann::json j;
        j["label"] = label;
        j["url"] = url;
        j["is_current"] = is_current;
        if (!children.empty()) {
            nlohmann::json children_json = nlohmann::json::array();
            for (const auto& child : children) {
                children_json.push_back(child.to_json());
            }
            j["children"] = children_json;
        }
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

    [[nodiscard]] nlohmann::json to_json() const {
        nlohmann::json j;

        j["site"] = metadata.to_json();

        nlohmann::json posts_json = nlohmann::json::array();
        for (const auto& post : posts) {
            if (post.status == PostStatus::Published) {
                posts_json.push_back(post.to_json());
            }
        }
        j["posts"] = posts_json;

        nlohmann::json pages_json = nlohmann::json::array();
        for (const auto& page : pages) {
            if (page.status == PageStatus::Published) {
                pages_json.push_back(page.to_json());
            }
        }
        j["pages"] = pages_json;

        nlohmann::json authors_json = nlohmann::json::array();
        for (const auto& author : authors) {
            authors_json.push_back(author.to_json());
        }
        j["authors"] = authors_json;

        nlohmann::json tags_json = nlohmann::json::array();
        for (const auto& tag : tags) {
            tags_json.push_back(tag.to_json());
        }
        j["tags"] = tags_json;

        nlohmann::json cats_json = nlohmann::json::array();
        for (const auto& cat : categories) {
            cats_json.push_back(cat.to_json());
        }
        j["categories"] = cats_json;

        nlohmann::json nav_json = nlohmann::json::array();
        for (const auto& item : navigation) {
            nav_json.push_back(item.to_json());
        }
        j["navigation"] = nav_json;

        return j;
    }
};

} // namespace guss::model
