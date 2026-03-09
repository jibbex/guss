#pragma once

#include "guss/adapters/adapter.hpp"
#include "guss/core/config.hpp"
#include <filesystem>

namespace guss::adapters {

class MarkdownAdapter final : public ContentAdapter {
public:
    explicit MarkdownAdapter(const config::MarkdownAdapterConfig& cfg);

    error::Result<FetchResult> fetch_all(FetchCallback progress = nullptr) override;
    error::Result<std::vector<model::Post>> fetch_posts(FetchCallback progress = nullptr) override;
    error::Result<std::vector<model::Page>> fetch_pages(FetchCallback progress = nullptr) override;
    error::Result<std::vector<model::Author>> fetch_authors() override;
    error::Result<std::vector<model::Tag>> fetch_tags() override;
    error::Result<std::vector<model::Category>> fetch_categories() override;
    error::Result<std::vector<model::Asset>> fetch_assets() override;

    std::string adapter_name() const override { return "markdown"; }

private:
    config::MarkdownAdapterConfig config_;

    error::Result<model::Post> parse_post_file(const std::filesystem::path& path);
    error::Result<model::Page> parse_page_file(const std::filesystem::path& path);
    error::Result<model::Author> parse_author_file(const std::filesystem::path& path);
};

} // namespace guss::adapters
