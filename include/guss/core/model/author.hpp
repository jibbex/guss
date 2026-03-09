#pragma once

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

    [[nodiscard]] std::string to_json() const {
        std::string j;
        j += "{\"id\":\"" + id + "\",";
        j += "\"name\":\"" + name + "\",";
        j += "\"slug\":\"" + slug + "\"";
        if (email) j += ",\"email\":\"" + *email + "\"";
        if (bio) j += ",\"bio\":\"" + *bio + "\"";
        if (profile_image) j += ",\"profile_image\":\"" + *profile_image + "\"";
        if (website) j += ",\"website\":\"" + *website + "\"";
        if (twitter) j += ",\"twitter\":\"" + *twitter + "\"";
        if (facebook) j += ",\"facebook\":\"" + *facebook + "\"";
        j += "}";
        return j;
    }
};

} // namespace guss::model
