#pragma once

#include "guss/adapters/adapter.hpp"
#include "guss/core/config.hpp"
#include <string_view>

namespace guss::adapters {

class WordPressAdapter final : public ContentAdapter {
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

    error::Result<std::string> api_request(const std::string& endpoint);
    domain::Post json_to_post(std::string_view json);
    domain::Page json_to_page(std::string_view json);
    domain::Author json_to_author(std::string_view json);
    domain::Tag json_to_tag(std::string_view json);
    domain::Category json_to_category(std::string_view json);
};

} // namespace guss::adapters
