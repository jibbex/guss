/**
 * @file taxonomy.hpp
 * @brief Taxonomy domain models (Tag, Category) for Apex SSG
 */
#pragma once

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

    [[nodiscard]] std::string to_json() const {
        std::string j;
        j += "{\"id\":\"" + id + "\",";
        j += "\"name\":\"" + name + "\",";
        j += "\"slug\":\"" + slug + "\",";
        j += "\"post_count\":" + std::to_string(post_count);
        if (description) j += ",\"description\":\"" + *description + "\"";
        if (feature_image) j += ",\"feature_image\":\"" + *feature_image + "\"";
        j += "}";
        return j;
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

    [[nodiscard]] std::string to_json() const {
        std::string j;
        j += "{\"id\":\"" + id + "\",";
        j += "\"name\":\"" + name + "\",";
        j += "\"slug\":\"" + slug + "\",";
        j += "\"post_count\":" + std::to_string(post_count);
        if (description) j += ",\"description\":\"" + *description + "\"";
        if (feature_image) j += ",\"feature_image\":\"" + *feature_image + "\"";
        if (parent_id) j += ",\"parent_id\":\"" + *parent_id + "\"";
        j += "}";
        return j;
    }
};

} // namespace guss::model
