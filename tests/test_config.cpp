/**
 * @file test_config.cpp
 * @brief Unit tests for configuration system.
 */
#include <gtest/gtest.h>
#include "guss/core/config.hpp"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string dir_name = std::string("guss_test_config_")
            + info->test_suite_name() + "_" + info->name();
        test_dir_ = fs::temp_directory_path() / dir_name;
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(test_dir_, ec);
    }

    void write_config(const std::string& content) {
        auto path = test_dir_ / "guss.yaml";
        std::ofstream file(path);
        file << content;
    }

    fs::path test_dir_;
};

TEST_F(ConfigTest, ParsesRestApiAdapter) {
    write_config(R"(
site:
  title: "Test Site"
  description: "A test"
  url: "https://example.com"

source:
  type: rest_api
  base_url: "https://demo.ghost.io"
  timeout_ms: 5000
  auth:
    type: api_key
    param: key
    value: "abc123"
  pagination:
    page_param: page
    limit_param: limit
    limit: 15
    json_next: "meta.pagination.next"
  endpoints:
    posts:
      path: "ghost/api/content/posts/"
      response_key: "posts"
      params:
        include: "authors,tags"
)");

    auto path = test_dir_ / "guss.yaml";
    const std::string path_str = path.string();
    guss::config::Config config(path_str);

    EXPECT_EQ(config.site().title, "Test Site");
    EXPECT_EQ(config.site().url, "https://example.com");

    ASSERT_TRUE(std::holds_alternative<guss::config::RestApiConfig>(config.adapter()));
    const auto& rest = std::get<guss::config::RestApiConfig>(config.adapter());
    EXPECT_EQ(rest.base_url, "https://demo.ghost.io");
    EXPECT_EQ(rest.timeout_ms, 5000);
    EXPECT_EQ(rest.auth.type, guss::config::AuthConfig::Type::ApiKey);
    EXPECT_EQ(rest.auth.param, "key");
    EXPECT_EQ(rest.auth.value, "abc123");
    EXPECT_EQ(rest.pagination.json_next, "meta.pagination.next");
    ASSERT_TRUE(rest.endpoints.count("posts"));
    EXPECT_EQ(rest.endpoints.at("posts").path, "ghost/api/content/posts/");
    EXPECT_EQ(rest.endpoints.at("posts").response_key, "posts");
}

TEST_F(ConfigTest, ParsesCollectionPermalinks) {
    write_config(R"(
site:
  title: "Test"
  url: "https://example.com"

collections:
  posts:
    item_template: "post.html"
    permalink: "/blog/{slug}/"
  pages:
    item_template: "page.html"
    permalink: "/p/{slug}/"
  tags:
    archive_template: "tag.html"
    permalink: "/tags/{slug}/"
)");

    auto path = test_dir_ / "guss.yaml";
    const std::string path_str = path.string();
    guss::config::Config config(path_str);

    ASSERT_TRUE(config.collections().count("posts"));
    EXPECT_EQ(config.collections().at("posts").permalink, "/blog/{slug}/");
    ASSERT_TRUE(config.collections().count("pages"));
    EXPECT_EQ(config.collections().at("pages").permalink, "/p/{slug}/");
    ASSERT_TRUE(config.collections().count("tags"));
    EXPECT_EQ(config.collections().at("tags").permalink, "/tags/{slug}/");
}

TEST_F(ConfigTest, ParsesOutputConfig) {
    write_config(R"(
site:
  title: "Test"
  url: "https://example.com"

output:
  output_dir: "./public"
  generate_sitemap: false
  generate_rss: true
  minify_html: true
)");

    auto path = test_dir_ / "guss.yaml";
    const std::string path_str = path.string();
    guss::config::Config config(path_str);

    EXPECT_EQ(config.output().output_dir, "./public");
    EXPECT_FALSE(config.output().generate_sitemap);
    EXPECT_TRUE(config.output().generate_rss);
    EXPECT_TRUE(config.output().minify_html);
}

TEST_F(ConfigTest, UsesDefaults) {
    write_config(R"(
site:
  title: "Minimal"
  url: "https://example.com"
)");

    auto path = test_dir_ / "guss.yaml";
    const std::string path_str = path.string();
    guss::config::Config config(path_str);

    // Check defaults
    EXPECT_EQ(config.site().language, "en");
    EXPECT_EQ(config.output().output_dir, "./dist");
    EXPECT_TRUE(config.output().generate_sitemap);
    // No source section defaults to MarkdownAdapterConfig
    ASSERT_TRUE(std::holds_alternative<guss::config::MarkdownAdapterConfig>(config.adapter()));
}

TEST_F(ConfigTest, ParsesMarkdownAdapter) {
    write_config(R"(
site:
  title: "Test"
  url: "https://example.com"

source:
  type: markdown
  content_path: "./posts"
  pages_path: "./pages"
  recursive: false
)");

    auto path = test_dir_ / "guss.yaml";
    const std::string path_str = path.string();
    guss::config::Config config(path_str);

    ASSERT_TRUE(std::holds_alternative<guss::config::MarkdownAdapterConfig>(config.adapter()));
    const auto& md = std::get<guss::config::MarkdownAdapterConfig>(config.adapter());
    EXPECT_EQ(md.content_path, "./posts");
    EXPECT_EQ(md.pages_path, "./pages");
    EXPECT_FALSE(md.recursive);
}

TEST_F(ConfigTest, ParsesCollectionContextKey) {
    write_config(R"(
site:
  title: "Test"
  url: "https://example.com"

collections:
  posts:
    item_template: "post.html"
    archive_template: "index.html"
    permalink: "/{year}/{month}/{slug}/"
    context_key: "post"
  tags:
    archive_template: "tag.html"
    permalink: "/tag/{slug}/"
)");

    auto path = test_dir_ / "guss.yaml";
    const std::string path_str = path.string();
    guss::config::Config config(path_str);

    ASSERT_TRUE(config.collections().count("posts"));
    EXPECT_EQ(config.collections().at("posts").context_key, "post");
    // Default context_key is "item" when not specified
    ASSERT_TRUE(config.collections().count("tags"));
    EXPECT_EQ(config.collections().at("tags").context_key, "item");
}

TEST(CollectionCfgTest, ParsesPostsCollection) {
    const std::string yaml = R"(
site:
  title: "Test"
  url: "https://example.com"
collections:
  posts:
    item_template: "post.html"
    archive_template: "index.html"
    permalink: "/{year}/{month}/{slug}/"
    paginate: 10
)";
    auto tmp = std::filesystem::temp_directory_path() / "guss_test_collections.yaml";
    std::ofstream(tmp) << yaml;
    guss::config::Config cfg(tmp.string());
    ASSERT_TRUE(cfg.collections().contains("posts"));
    const auto& posts = cfg.collections().at("posts");
    EXPECT_EQ(posts.item_template, "post.html");
    EXPECT_EQ(posts.archive_template, "index.html");
    EXPECT_EQ(posts.permalink, "/{year}/{month}/{slug}/");
    EXPECT_EQ(posts.paginate, 10);
    std::filesystem::remove(tmp);
}

TEST(CollectionCfgTest, MissingCollectionKeySkipped) {
    const std::string yaml = R"(
site:
  title: "Test"
  url: "https://example.com"
collections:
  tags:
    archive_template: "tag.html"
    permalink: "/tag/{slug}/"
)";
    auto tmp = std::filesystem::temp_directory_path() / "guss_test_coll2.yaml";
    std::ofstream(tmp) << yaml;
    guss::config::Config cfg(tmp.string());
    EXPECT_FALSE(cfg.collections().contains("posts"));
    EXPECT_TRUE(cfg.collections().contains("tags"));
    std::filesystem::remove(tmp);
}

TEST(CollectionCfgTest, PaginateDefaultsToZero) {
    const std::string yaml = R"(
site:
  title: "Test"
  url: "https://example.com"
collections:
  tags:
    archive_template: "tag.html"
    permalink: "/tag/{slug}/"
)";
    auto tmp = std::filesystem::temp_directory_path() / "guss_test_coll3.yaml";
    std::ofstream(tmp) << yaml;
    guss::config::Config cfg(tmp.string());
    EXPECT_EQ(cfg.collections().at("tags").paginate, 0);
    std::filesystem::remove(tmp);
}
