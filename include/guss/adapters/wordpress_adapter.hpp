#pragma once

#include "guss/adapters/adapter.hpp"
#include "guss/core/config.hpp"

namespace guss::adapters {

class WordPressAdapter : public ContentAdapter {
public:
    explicit WordPressAdapter(const config::WordPressAdapterConfig& cfg);

    error::Result<FetchResult> fetch_all(FetchCallback progress = nullptr) override;
    error::Result<std::vector<domain::Post>> fetch_posts(FetchCallback progress = nullptr) override;
    error::Result<std::vector<domain::Page>> fetch_pages(FetchCallback progress = nullptr) override;
    error::Result<std::vector<domain::Author>> fetch_authors() override;
    error::Result<std::vector<domain::Tag>> fetch_tags() override;
    error::Result<std::vector<domain::Category>> fetch_categories() override;
    error::Result<std::vector<domain::Asset>> fetch_assets() override;

    std::string adapter_name() const override { return "wordpress"; }

private:
    config::WordPressAdapterConfig config_;

    error::Result<nlohmann::json> api_request(const std::string& endpoint);
    domain::Post json_to_post(const nlohmann::json& j);
    domain::Page json_to_page(const nlohmann::json& j);
    domain::Author json_to_author(const nlohmann::json& j);
    domain::Tag json_to_tag(const nlohmann::json& j);
    domain::Category json_to_category(const nlohmann::json& j);
};

} // namespace guss::adapters
