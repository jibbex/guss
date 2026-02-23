#pragma once

#include <nlohmann/json.hpp>
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

    [[nodiscard]] nlohmann::json to_json() const {
        nlohmann::json j;
        j["id"] = id;
        j["source_path"] = source_path;
        j["output_path"] = output_path;
        j["url"] = url;
        j["type"] = asset_type_to_string(type);
        if (alt_text) j["alt_text"] = *alt_text;
        if (title) j["title"] = *title;
        if (width) j["width"] = *width;
        if (height) j["height"] = *height;
        if (file_size) j["file_size"] = *file_size;
        if (mime_type) j["mime_type"] = *mime_type;
        return j;
    }

    static Asset from_json(const nlohmann::json& j) {
        Asset asset;
        asset.id = j.value("id", "");
        asset.source_path = j.value("source_path", "");
        asset.output_path = j.value("output_path", "");
        asset.url = j.value("url", "");
        asset.type = asset_type_from_string(j.value("type", "other"));
        if (j.contains("alt_text")) asset.alt_text = j["alt_text"].get<std::string>();
        if (j.contains("title")) asset.title = j["title"].get<std::string>();
        if (j.contains("width")) asset.width = j["width"].get<size_t>();
        if (j.contains("height")) asset.height = j["height"].get<size_t>();
        if (j.contains("file_size")) asset.file_size = j["file_size"].get<size_t>();
        if (j.contains("mime_type")) asset.mime_type = j["mime_type"].get<std::string>();
        return asset;
    }
};

} // namespace guss::model
