#pragma once

#include "guss/adapters/adapter.hpp"
#include "guss/core/config.hpp"
#include <filesystem>

namespace guss::adapters {

class MarkdownAdapter final : public ContentAdapter {
public:
    MarkdownAdapter(const core::config::MarkdownAdapterConfig& cfg,
                    const core::config::SiteConfig& site_cfg,
                    const core::config::CollectionCfgMap& collections);

    core::error::Result<FetchResult> fetch_all(FetchCallback progress = nullptr) override;
    core::error::VoidResult ping() override;
    std::string adapter_name() const override { return "markdown"; }

private:
    core::config::MarkdownAdapterConfig cfg_;
};

} // namespace guss::adapters
