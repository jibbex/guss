/**
 * @file test_permalink.cpp
 * @brief Unit tests for permalink generation.
 */
#include <gtest/gtest.h>
#include "guss/core/permalink.hpp"
#include "guss/core/value.hpp"

TEST(PermalinkExpandTest, ExpandsSlugToken) {
    std::unordered_map<std::string, guss::core::Value> m;
    m["slug"] = guss::core::Value(std::string("my-post"));
    guss::core::Value v(std::move(m));
    EXPECT_EQ(guss::core::PermalinkGenerator::expand("/{slug}/", v), "/my-post/");
}

TEST(PermalinkExpandTest, ExpandsMultipleTokens) {
    std::unordered_map<std::string, guss::core::Value> m;
    m["slug"]  = guss::core::Value(std::string("hello"));
    m["year"]  = guss::core::Value(std::string("2024"));
    m["month"] = guss::core::Value(std::string("03"));
    guss::core::Value v(std::move(m));
    EXPECT_EQ(guss::core::PermalinkGenerator::expand("/{year}/{month}/{slug}/", v), "/2024/03/hello/");
}

TEST(PermalinkExpandTest, MissingTokenProducesEmptySegment) {
    std::unordered_map<std::string, guss::core::Value> m;
    m["slug"] = guss::core::Value(std::string("test"));
    guss::core::Value v(std::move(m));
    // {year} missing -- produces empty string for that token, no crash
    EXPECT_EQ(guss::core::PermalinkGenerator::expand("/{year}/{slug}/", v), "//test/");
}

TEST(PermalinkExpandTest, NoTokensPassthrough) {
    guss::core::Value v;
    EXPECT_EQ(guss::core::PermalinkGenerator::expand("/static/page/", v), "/static/page/");
}

TEST(PermalinkExpandTest, PermalinkToPathStripsLeadingSlash) {
    auto p = guss::core::PermalinkGenerator::permalink_to_path("/2024/03/hello/");
    EXPECT_EQ(p, std::filesystem::path("2024/03/hello/index.html"));
}

TEST(PermalinkExpandTest, PermalinkToPathEmptyInput) {
    auto p = guss::core::PermalinkGenerator::permalink_to_path("/");
    EXPECT_EQ(p, std::filesystem::path("index.html"));
}
