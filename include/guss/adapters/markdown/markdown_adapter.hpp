#pragma once

#include "guss/adapters/adapter.hpp"
#include "guss/core/config.hpp"
#include <filesystem>

namespace guss::adapters {

class MarkdownAdapter final : public ContentAdapter {
public:
    MarkdownAdapter(const config::MarkdownAdapterConfig& cfg,
                    const config::SiteConfig& site_cfg,
                    const config::CollectionCfgMap& collections);

    error::Result<FetchResult> fetch_all(FetchCallback progress = nullptr) override;
    error::VoidResult ping() override;
    std::string adapter_name() const override { return "markdown"; }

private:
    config::MarkdownAdapterConfig config_;
};

} // namespace guss::adapters
