/**
* @file ghost_adapter.hpp
 * @brief Ghost CMS adapter for Apex SSG
 */
#pragma once

#include "./adapter.hpp"
#include "../core/config.hpp"

namespace guss::adapters {

class GhostAdapter : public ContentAdapter {
public:
    explicit GhostAdapter(const config::GhostAdapterConfig& cfg);

    error::Result<FetchResult> fetch_all(FetchCallback progress = nullptr) override;
    error::Result<std::vector<model::Post>> fetch_posts(FetchCallback progress = nullptr) override;
    error::Result<std::vector<model::Page>> fetch_pages(FetchCallback progress = nullptr) override;
    error::Result<std::vector<model::Author>> fetch_authors() override;
    error::Result<std::vector<model::Tag>> fetch_tags() override;
    error::Result<std::vector<model::Category>> fetch_categories() override;
    error::Result<std::vector<model::Asset>> fetch_assets() override;

    std::string adapter_name() const override { return "ghost"; }

private:
    config::GhostAdapterConfig config_;

    error::Result<nlohmann::json> api_request(const std::string& endpoint);
    model::Post json_to_post(const nlohmann::json& j);
    model::Page json_to_page(const nlohmann::json& j);
    model::Author json_to_author(const nlohmann::json& j);
    model::Tag json_to_tag(const nlohmann::json& j);
};

} // namespace guss::adapters
