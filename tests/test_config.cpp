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
        // Create temporary directory for test files
        test_dir_ = fs::temp_directory_path() / "guss_test_config";
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        // Clean up
        fs::remove_all(test_dir_);
    }

    void write_config(const std::string& content) {
        auto path = test_dir_ / "guss.yaml";
        std::ofstream file(path);
        file << content;
    }

    fs::path test_dir_;
};

TEST_F(ConfigTest, ParsesGhostAdapter) {
    write_config(R"(
site:
  title: "Test Site"
  description: "A test"
  url: "https://example.com"

source:
  type: ghost
  api_url: "https://demo.ghost.io"
  content_api_key: "abc123"
  timeout_ms: 5000
)");

    auto path = test_dir_ / "guss.yaml";
    const std::string path_str = path.string();
    guss::config::Config config(path_str);

    EXPECT_EQ(config.site().title, "Test Site");
    EXPECT_EQ(config.site().url, "https://example.com");

    ASSERT_TRUE(std::holds_alternative<guss::config::GhostAdapterConfig>(config.adapter()));
    const auto& ghost = std::get<guss::config::GhostAdapterConfig>(config.adapter());
    EXPECT_EQ(ghost.api_url, "https://demo.ghost.io");
    EXPECT_EQ(ghost.content_api_key, "abc123");
    EXPECT_EQ(ghost.timeout_ms, 5000);
}

TEST_F(ConfigTest, ParsesPermalinks) {
    write_config(R"(
site:
  title: "Test"
  url: "https://example.com"

permalinks:
  post: "/blog/{slug}/"
  page: "/p/{slug}/"
  tag: "/tags/{slug}/"
)");

    auto path = test_dir_ / "guss.yaml";
    const std::string path_str = path.string();
    guss::config::Config config(path_str);

    EXPECT_EQ(config.permalinks().post_pattern, "/blog/{slug}/");
    EXPECT_EQ(config.permalinks().page_pattern, "/p/{slug}/");
    EXPECT_EQ(config.permalinks().tag_pattern, "/tags/{slug}/");
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
    EXPECT_EQ(config.permalinks().post_pattern, "/{year}/{month}/{slug}/");
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

TEST_F(ConfigTest, ParsesTemplateConfig) {
    write_config(R"(
site:
  title: "Test"
  url: "https://example.com"

templates:
  templates_dir: "./theme"
  default_post_template: "single.html"
  index_template: "home.html"
)");

    auto path = test_dir_ / "guss.yaml";
    const std::string path_str = path.string();
    guss::config::Config config(path_str);

    EXPECT_EQ(config.templates().templates_dir, "./theme");
    EXPECT_EQ(config.templates().default_post_template, "single.html");
    EXPECT_EQ(config.templates().index_template, "home.html");
}
