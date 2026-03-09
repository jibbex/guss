#pragma once

#include <string>
#include <optional>
#include <chrono>

namespace guss::model {

enum class AssetType {
    Image,
    Document,
    Video,
    Audio,
    Other
};

inline std::string asset_type_to_string(AssetType type) {
    switch (type) {
        case AssetType::Image: return "image";
        case AssetType::Document: return "document";
        case AssetType::Video: return "video";
        case AssetType::Audio: return "audio";
        default: return "other";
    }
}

inline AssetType asset_type_from_string(const std::string& str) {
    if (str == "image") return AssetType::Image;
    if (str == "document") return AssetType::Document;
    if (str == "video") return AssetType::Video;
    if (str == "audio") return AssetType::Audio;
    return AssetType::Other;
}

struct Asset {
    std::string id;
    std::string source_path;
    std::string output_path;
    std::string url;
    AssetType type = AssetType::Other;
    std::optional<std::string> alt_text;
    std::optional<std::string> title;
    std::optional<size_t> width;
    std::optional<size_t> height;
    std::optional<size_t> file_size;
    std::optional<std::string> mime_type;

    [[nodiscard]] std::string to_json() const {
        std::string j;
        j += "{\"id\":\"" + id + "\",";
        j += "\"source_path\":\"" + source_path + "\",";
        j += "\"output_path\":\"" + output_path + "\",";
        j += "\"url\":\"" + url + "\",";
        j += "\"type\":\"" + asset_type_to_string(type) + "\"";
        if (alt_text) j += ",\"alt_text\":\"" + *alt_text + "\"";
        if (title) j += ",\"title\":\"" + *title + "\"";
        if (width) j += ",\"width\":" + std::to_string(*width);
        if (height) j += ",\"height\":" + std::to_string(*height);
        if (file_size) j += ",\"file_size\":" + std::to_string(*file_size);
        if (mime_type) j += ",\"mime_type\":\"" + *mime_type + "\"";
        j += "}";
        return j;
    }
};

} // namespace guss::model
