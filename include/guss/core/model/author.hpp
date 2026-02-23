#pragma once

#include <nlohmann/json.hpp>
#include <simdjson.h>
#include <string>
#include <optional>

namespace guss::model {

struct Author {
    std::string id;
    std::string name;
    std::string slug;
    std::optional<std::string> email;
    std::optional<std::string> bio;
    std::optional<std::string> profile_image;
    std::optional<std::string> website;
    std::optional<std::string> twitter;
    std::optional<std::string> facebook;

    [[nodiscard]] nlohmann::json to_json() const {
        nlohmann::json j;
        j["id"] = id;
        j["name"] = name;
        j["slug"] = slug;
        if (email) j["email"] = *email;
        if (bio) j["bio"] = *bio;
        if (profile_image) j["profile_image"] = *profile_image;
        if (website) j["website"] = *website;
        if (twitter) j["twitter"] = *twitter;
        if (facebook) j["facebook"] = *facebook;
        return j;
    }

    static Author from_json(const nlohmann::json& j) {
        Author author;
        author.id = j.value("id", "");
        author.name = j.value("name", "");
        author.slug = j.value("slug", "");
        if (j.contains("email")) author.email = j["email"].get<std::string>();
        if (j.contains("bio")) author.bio = j["bio"].get<std::string>();
        if (j.contains("profile_image")) author.profile_image = j["profile_image"].get<std::string>();
        if (j.contains("website")) author.website = j["website"].get<std::string>();
        if (j.contains("twitter")) author.twitter = j["twitter"].get<std::string>();
        if (j.contains("facebook")) author.facebook = j["facebook"].get<std::string>();
        return author;
    }

    static Author from_json(const simdjson& j) {
        Author author;
        author.id = j["id"].get_string();
        author.name = j["name"].get_string();
        author.slug = j["slug"].get_string();
        if (j.contains("email")) author.email = j["email"].get_string();
        if (j.contains("bio")) author.bio = j["bio"].get_string();
        if (j.contains("profile_image")) author.profile_image = j["profile_image"].get_string();
        if (j.contains("website")) author.website = j["website"].get_string();
        if (j.contains("twitter")) author.twitter = j["twitter"].get_string();
        if (j.contains("facebook")) author.facebook = j["facebook"].get_string();
        return author;
    }
};

} // namespace guss::model
