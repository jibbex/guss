/**
 * \file test_pipeline_prepare.cpp
 * \brief Unit tests for pipeline prepare-phase data structures and config-driven logic.
 *
 * \details
 * The pipeline's phase_prepare is a private method and cannot be called directly.
 * These tests exercise the underlying public interfaces: CollectionMap grouping,
 * CollectionConfig pagination arithmetic, and FetchResult construction. Together
 * they document the contracts the pipeline relies on.
 */
#include <gtest/gtest.h>
#include "guss/adapters/adapter.hpp"
#include "guss/core/config.hpp"
#include "guss/core/render_item.hpp"
#include "guss/core/permalink.hpp"
#include <filesystem>
#include <unordered_map>

using namespace guss;

// ---------- CollectionMap grouping ----------

TEST(CollectionMapGroupingTest, GroupsByCollectionName) {
    core::CollectionMap m;
    for (int i = 0; i < 3; ++i) {
        std::unordered_map<std::string, core::Value> d;
        d["slug"] = core::Value(std::string("post-") + std::to_string(i));
        m["posts"].push_back(core::RenderItem{
            std::filesystem::path("post-" + std::to_string(i) + "/index.html"),
            "post.html",
            core::Value(std::move(d))
        });
    }
    EXPECT_EQ(m["posts"].size(), 3u);
    EXPECT_EQ(m["posts"][0].template_name, "post.html");
    EXPECT_EQ(m["posts"][0].data["slug"].to_string(), "post-0");
}

TEST(CollectionMapGroupingTest, MissingCollectionKeyReturnsEmptyVector) {
    core::CollectionMap m;
    // Accessing a non-existent key via [] creates an empty entry.
    EXPECT_TRUE(m["nonexistent"].empty());
}

TEST(CollectionMapGroupingTest, MultipleCollectionTypes) {
    core::CollectionMap m;
    m["posts"].push_back(core::RenderItem{
        std::filesystem::path("p/index.html"), "post.html",
        core::Value(std::unordered_map<std::string, core::Value>{})});
    m["tags"].push_back(core::RenderItem{
        std::filesystem::path("t/index.html"), "tag.html",
        core::Value(std::unordered_map<std::string, core::Value>{})});
    EXPECT_EQ(m.size(), 2u);
    EXPECT_EQ(m["posts"].size(), 1u);
    EXPECT_EQ(m["tags"].size(), 1u);
}

// ---------- CollectionConfig pagination logic ----------

TEST(CollectionConfigPaginationTest, PaginateZeroMeansSinglePage) {
    core::config::CollectionConfig cfg;
    cfg.paginate = 0;
    // paginate == 0 means single archive page (no pagination).
    EXPECT_EQ(cfg.paginate, 0);
    int total_items = 25;
    int per_page = cfg.paginate > 0 ? cfg.paginate : total_items;
    int total_pages = (total_items + per_page - 1) / per_page;
    EXPECT_EQ(total_pages, 1);
}

TEST(CollectionConfigPaginationTest, PaginateComputesCorrectPageCount) {
    core::config::CollectionConfig cfg;
    cfg.paginate = 10;
    int total_items = 25;
    int total_pages = (total_items + cfg.paginate - 1) / cfg.paginate;
    EXPECT_EQ(total_pages, 3);  // ceil(25/10) = 3
}

TEST(CollectionConfigPaginationTest, PaginateExactDivisionNoExtraPage) {
    core::config::CollectionConfig cfg;
    cfg.paginate = 10;
    int total_items = 20;
    int total_pages = (total_items + cfg.paginate - 1) / cfg.paginate;
    EXPECT_EQ(total_pages, 2);  // 20/10 = 2 exactly
}

// ---------- FetchResult structure ----------

TEST(FetchResultTest, DefaultConstruction) {
    adapters::FetchResult fr;
    EXPECT_TRUE(fr.items.empty());
    EXPECT_TRUE(fr.site.is_null());
}

TEST(FetchResultTest, SiteValueAccessible) {
    adapters::FetchResult fr;
    std::unordered_map<std::string, core::Value> m;
    m["title"] = core::Value(std::string("My Site"));
    fr.site = core::Value(std::move(m));
    EXPECT_EQ(fr.site["title"].to_string(), "My Site");
}

TEST(FetchResultTest, MissingCollectionKeySkipped) {
    adapters::FetchResult fr;
    fr.items["posts"].push_back(core::RenderItem{
        std::filesystem::path("p/index.html"), "post.html",
        core::Value(std::unordered_map<std::string, core::Value>{})});

    // A collection key present in items but absent from collections config
    // should be silently skipped by the pipeline.
    // Verify the key exists in items but not in an empty config map.
    core::config::CollectionCfgMap cfg;
    EXPECT_EQ(fr.items.count("posts"), 1u);
    EXPECT_EQ(cfg.count("posts"), 0u);
    // Pipeline would skip "posts" since it is not in cfg -- no crash, no output.
}
