#pragma once

#include "guss/adapters/adapter.hpp"
#include "guss/core/config.hpp"
#include <filesystem>

namespace guss::adapters {

class MarkdownAdapter final : public ContentAdapter {
public:
    explicit MarkdownAdapter(const config::MarkdownAdapterConfig& cfg);

    error::Result<FetchResult> fetch_all(FetchCallback progress = nullptr) override;
    error::Result<std::vector<domain::Post>> fetch_posts(FetchCallback progress = nullptr) override;
    error::Result<std::vector<domain::Page>> fetch_pages(FetchCallback progress = nullptr) override;
    error::Result<std::vector<domain::Author>> fetch_authors() override;
    error::Result<std::vector<domain::Tag>> fetch_tags() override;
    error::Result<std::vector<domain::Category>> fetch_categories() override;
    error::Result<std::vector<domain::Asset>> fetch_assets() override;

    std::string adapter_name() const override { return "markdown"; }

private:
    config::MarkdownAdapterConfig config_;

    error::Result<domain::Post> parse_post_file(const std::filesystem::path& path);
    error::Result<domain::Page> parse_page_file(const std::filesystem::path& path);
    error::Result<domain::Author> parse_author_file(const std::filesystem::path& path);
};

} // namespace guss::adapters
