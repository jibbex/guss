/**
 * @file taxonomy.hpp
 * @brief Taxonomy domain models (Tag, Category) for Apex SSG
 */
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <optional>

namespace guss::model {

struct Tag {
    std::string id;
    std::string name;
    std::string slug;
    std::optional<std::string> description;
    std::optional<std::string> feature_image;
    size_t post_count = 0;

    [[nodiscard]] nlohmann::json to_json() const {
        nlohmann::json j;
        j["id"] = id;
        j["name"] = name;
        j["slug"] = slug;
        j["post_count"] = post_count;
        if (description) j["description"] = *description;
        if (feature_image) j["feature_image"] = *feature_image;
        return j;
    }

    static Tag from_json(const nlohmann::json& j) {
        Tag tag;
        tag.id = j.value("id", "");
        tag.name = j.value("name", "");
        tag.slug = j.value("slug", "");
        tag.post_count = j.value("post_count", 0);
        if (j.contains("description")) tag.description = j["description"].get<std::string>();
        if (j.contains("feature_image")) tag.feature_image = j["feature_image"].get<std::string>();
        return tag;
    }
};

struct Category {
    std::string id;
    std::string name;
    std::string slug;
    std::optional<std::string> description;
    std::optional<std::string> feature_image;
    std::optional<std::string> parent_id;
    size_t post_count = 0;

    [[nodiscard]] nlohmann::json to_json() const {
        nlohmann::json j;
        j["id"] = id;
        j["name"] = name;
        j["slug"] = slug;
        j["post_count"] = post_count;
        if (description) j["description"] = *description;
        if (feature_image) j["feature_image"] = *feature_image;
        if (parent_id) j["parent_id"] = *parent_id;
        return j;
    }

    static Category from_json(const nlohmann::json& j) {
        Category cat;
        cat.id = j.value("id", "");
        cat.name = j.value("name", "");
        cat.slug = j.value("slug", "");
        cat.post_count = j.value("post_count", 0);
        if (j.contains("description")) cat.description = j["description"].get<std::string>();
        if (j.contains("feature_image")) cat.feature_image = j["feature_image"].get<std::string>();
        if (j.contains("parent_id")) cat.parent_id = j["parent_id"].get<std::string>();
        return cat;
    }
};

} // namespace guss::model
