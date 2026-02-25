#pragma once

#include "guss/adapters/adapter.hpp"
#include "guss/core/config.hpp"

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

    /**
     * @brief Make an HTTP GET request to the Ghost Content API.
     * @param endpoint API endpoint path (e.g., "/ghost/api/content/posts/")
     * @return Response body as string or error.
     */
    error::Result<std::string> api_get(const std::string& endpoint) const;
};

} // namespace guss::adapters
