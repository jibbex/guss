/**
 * @file test_markdown_adapter.cpp
 * @brief Integration tests for the config-driven MarkdownAdapter.
 */
#include <gtest/gtest.h>
#include "guss/adapters/markdown/markdown_adapter.hpp"
#include "guss/core/config.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace guss;
using namespace guss::core;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class MarkdownAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string dir_name = std::string("guss_test_md_")
            + info->test_suite_name() + "_" + info->name();
        test_dir_ = fs::temp_directory_path() / dir_name;
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(test_dir_, ec);
    }

    void write_md(const fs::path& dir,
                  const std::string& filename,
                  const std::string& frontmatter,
                  const std::string& body = "Hello world.")
    {
        fs::create_directories(dir);
        std::ofstream f(dir / filename);
        f << "---\n" << frontmatter << "\n---\n" << body;
    }

    config::SiteConfig make_site() {
        config::SiteConfig s;
        s.title = "Test Site";
        s.url   = "https://example.com";
        return s;
    }

    config::CollectionCfgMap make_collection(
        const std::string& name,
        const std::string& permalink = "/{slug}/",
        const std::string& item_tpl  = "item.html")
    {
        config::CollectionCfgMap m;
        config::CollectionConfig cc;
        cc.item_template = item_tpl;
        cc.permalink     = permalink;
        cc.context_key   = name;
        m[name] = cc;
        return m;
    }

    fs::path test_dir_;
};

// ---------------------------------------------------------------------------
// MultipleCollections
// ---------------------------------------------------------------------------

TEST_F(MarkdownAdapterTest, MultipleCollections) {
    fs::path posts_dir = test_dir_ / "posts";
    fs::path pages_dir = test_dir_ / "pages";
    write_md(posts_dir, "hello.md", "slug: hello");
    write_md(pages_dir, "about.md", "slug: about");

    config::MarkdownAdapterConfig cfg;
    cfg.collection_paths["posts"] = posts_dir;
    cfg.collection_paths["pages"] = pages_dir;

    config::CollectionCfgMap colls;
    config::CollectionConfig cc;
    cc.item_template = "item.html";
    cc.permalink     = "/{slug}/";
    colls["posts"] = cc;
    colls["pages"] = cc;

    adapters::MarkdownAdapter adapter(cfg, make_site(), colls);
    auto result = adapter.fetch_all();
    ASSERT_TRUE(result.has_value()) << result.error().message;

    EXPECT_EQ(result->items.at("posts").size(), 1u);
    EXPECT_EQ(result->items.at("pages").size(), 1u);
}

// ---------------------------------------------------------------------------
// FieldMapApplied
// ---------------------------------------------------------------------------

TEST_F(MarkdownAdapterTest, FieldMapApplied) {
    fs::path posts_dir = test_dir_ / "posts";
    write_md(posts_dir, "post.md", "slug: my-post\nmeta_author: Alice");

    config::MarkdownAdapterConfig cfg;
    cfg.collection_paths["posts"] = posts_dir;
    cfg.field_maps["posts"]["author"] = "meta_author";

    adapters::MarkdownAdapter adapter(cfg, make_site(), make_collection("posts"));
    auto result = adapter.fetch_all();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->items.at("posts").size(), 1u);

    EXPECT_EQ(result->items.at("posts")[0].data["author"].to_string(), "Alice");
}

// ---------------------------------------------------------------------------
// CrossReferenceWired
// ---------------------------------------------------------------------------

TEST_F(MarkdownAdapterTest, CrossReferenceWired) {
    // Tags populated from physical .md files (collection_paths entry present).
    // This exercises Step 5 (cross-reference wiring), NOT Step 4.5 (synthesis).
    // See TaxonomySynthesis for the synthesis path.
    fs::path posts_dir = test_dir_ / "posts";
    fs::path tags_dir  = test_dir_ / "tags";

    write_md(posts_dir, "post.md", "slug: my-post\ntags:\n  - slug: cpp");
    write_md(tags_dir,  "cpp.md",  "slug: cpp");

    config::MarkdownAdapterConfig cfg;
    cfg.collection_paths["posts"] = posts_dir;
    cfg.collection_paths["tags"]  = tags_dir;

    config::CrossRefConfig cr;
    cr.from      = "posts";
    cr.via       = "tags.slug";
    cr.match_key = "slug";
    cfg.cross_references["tags"] = cr;

    config::CollectionCfgMap colls;
    config::CollectionConfig post_cc, tag_cc;
    post_cc.item_template = "post.html";
    post_cc.permalink     = "/{slug}/";
    tag_cc.item_template  = "tag.html";
    tag_cc.permalink      = "/tag/{slug}/";
    colls["posts"] = post_cc;
    colls["tags"]  = tag_cc;

    adapters::MarkdownAdapter adapter(cfg, make_site(), colls);
    auto result = adapter.fetch_all();
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const auto& tags = result->items.at("tags");
    ASSERT_EQ(tags.size(), 1u);

    bool found = false;
    for (const auto& [key, val] : tags[0].extra_context) {
        if (key == "posts") {
            found = true;
            EXPECT_GT(val.size(), 0u);
        }
    }
    EXPECT_TRUE(found) << "expected 'posts' key in tags[0].extra_context";
}

// ---------------------------------------------------------------------------
// EnrichItemFields
// ---------------------------------------------------------------------------

TEST_F(MarkdownAdapterTest, EnrichItemFields) {
    fs::path posts_dir = test_dir_ / "posts";
    write_md(posts_dir, "my-post.md",
        "slug: my-post\npublished_at: 2024-06-15T10:00:00Z");

    config::MarkdownAdapterConfig cfg;
    cfg.collection_paths["posts"] = posts_dir;

    adapters::MarkdownAdapter adapter(cfg, make_site(),
        make_collection("posts", "/{year}/{month}/{slug}/"));
    auto result = adapter.fetch_all();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->items.at("posts").size(), 1u);

    const auto& item = result->items.at("posts")[0];
    EXPECT_EQ(item.data["year"].to_string(),  "2024");
    EXPECT_EQ(item.data["month"].to_string(), "06");
    EXPECT_EQ(item.data["day"].to_string(),   "15");

    std::string op = item.data["output_path"].to_string();
    EXPECT_FALSE(op.empty());
    EXPECT_NE(op, "null");
    EXPECT_FALSE(item.output_path.empty());
}

// ---------------------------------------------------------------------------
// MissingCollectionPathSkipped
// ---------------------------------------------------------------------------

TEST_F(MarkdownAdapterTest, MissingCollectionPathSkipped) {
    fs::path real_dir    = test_dir_ / "posts";
    fs::path missing_dir = test_dir_ / "nonexistent";
    write_md(real_dir, "hello.md", "slug: hello");

    config::MarkdownAdapterConfig cfg;
    cfg.collection_paths["posts"]   = real_dir;
    cfg.collection_paths["missing"] = missing_dir;

    config::CollectionCfgMap colls;
    config::CollectionConfig cc;
    cc.item_template = "item.html";
    cc.permalink     = "/{slug}/";
    colls["posts"]   = cc;
    colls["missing"] = cc;

    adapters::MarkdownAdapter adapter(cfg, make_site(), colls);
    auto result = adapter.fetch_all();
    ASSERT_TRUE(result.has_value()) << result.error().message;

    EXPECT_EQ(result->items.at("posts").size(), 1u);
    EXPECT_EQ(result->items.count("missing"), 0u);
}

// ---------------------------------------------------------------------------
// SlugFallback
// ---------------------------------------------------------------------------

TEST_F(MarkdownAdapterTest, SlugFallback) {
    fs::path posts_dir = test_dir_ / "posts";
    write_md(posts_dir, "hello-world.md", "title: Hello World");

    config::MarkdownAdapterConfig cfg;
    cfg.collection_paths["posts"] = posts_dir;

    adapters::MarkdownAdapter adapter(cfg, make_site(), make_collection("posts"));
    auto result = adapter.fetch_all();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->items.at("posts").size(), 1u);

    EXPECT_EQ(result->items.at("posts")[0].data["slug"].to_string(), "hello-world");
}

// ---------------------------------------------------------------------------
// PublishedAtFallback
// ---------------------------------------------------------------------------

TEST_F(MarkdownAdapterTest, PublishedAtFallback) {
    fs::path posts_dir = test_dir_ / "posts";
    write_md(posts_dir, "post.md", "slug: my-post");

    config::MarkdownAdapterConfig cfg;
    cfg.collection_paths["posts"] = posts_dir;

    adapters::MarkdownAdapter adapter(cfg, make_site(), make_collection("posts"));
    auto result = adapter.fetch_all();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->items.at("posts").size(), 1u);

    std::string pub = result->items.at("posts")[0].data["published_at"].to_string();
    EXPECT_FALSE(pub.empty());
    EXPECT_NE(pub, "null");
}

// ---------------------------------------------------------------------------
// TaxonomySynthesis
// ---------------------------------------------------------------------------

TEST_F(MarkdownAdapterTest, TaxonomySynthesis) {
    fs::path posts_dir = test_dir_ / "posts";

    // Post 1: tagged cpp only
    write_md(posts_dir, "post1.md",
        "slug: post1\n"
        "tags:\n"
        "  - name: \"C++\"\n"
        "    slug: cpp");

    // Post 2: tagged cpp AND java
    write_md(posts_dir, "post2.md",
        "slug: post2\n"
        "tags:\n"
        "  - name: \"C++\"\n"
        "    slug: cpp\n"
        "  - name: Java\n"
        "    slug: java");

    config::MarkdownAdapterConfig cfg;
    cfg.collection_paths["posts"] = posts_dir;
    // NOTE: no "tags" in collection_paths — tags must be synthesized

    config::CrossRefConfig cr;
    cr.from      = "posts";
    cr.via       = "tags.slug";
    cr.match_key = "slug";
    cfg.cross_references["tags"] = cr;

    config::CollectionCfgMap colls;
    config::CollectionConfig post_cc;
    post_cc.item_template = "post.html";
    post_cc.permalink     = "/{slug}/";
    colls["posts"] = post_cc;

    config::CollectionConfig tag_cc;
    tag_cc.archive_template = "tag.html";
    tag_cc.permalink        = "/tag/{slug}/";
    tag_cc.context_key      = "tag";
    colls["tags"] = tag_cc;

    adapters::MarkdownAdapter adapter(cfg, make_site(), colls);
    auto result = adapter.fetch_all();
    ASSERT_TRUE(result.has_value()) << result.error().message;

    // Tags must be synthesized
    ASSERT_TRUE(result->items.count("tags")) << "tags collection not synthesized";
    const auto& tags = result->items.at("tags");
    ASSERT_EQ(tags.size(), 2u) << "expected 2 unique tags (cpp, java)";

    // Find cpp and java items (order is not guaranteed)
    const core::RenderItem* cpp_item  = nullptr;
    const core::RenderItem* java_item = nullptr;
    for (const auto& item : tags) {
        std::string slug = item.data["slug"].to_string();
        if (slug == "cpp")  cpp_item  = &item;
        if (slug == "java") java_item = &item;
    }
    ASSERT_NE(cpp_item,  nullptr) << "cpp tag not found";
    ASSERT_NE(java_item, nullptr) << "java tag not found";

    // Both items use archive_template, not item_template
    EXPECT_EQ(cpp_item->template_name,  "tag.html");
    EXPECT_EQ(java_item->template_name, "tag.html");

    // output_path is set
    EXPECT_EQ(cpp_item->output_path.generic_string(),  "tag/cpp/index.html");
    EXPECT_EQ(java_item->output_path.generic_string(), "tag/java/index.html");

    // Cross-reference: cpp has 2 posts, java has 1 post
    auto find_posts = [](const core::RenderItem& item) -> const core::Value* {
        for (const auto& [key, val] : item.extra_context)
            if (key == "posts") return &val;
        return nullptr;
    };

    const core::Value* cpp_posts  = find_posts(*cpp_item);
    const core::Value* java_posts = find_posts(*java_item);
    ASSERT_NE(cpp_posts,  nullptr) << "cpp tag missing 'posts' in extra_context";
    ASSERT_NE(java_posts, nullptr) << "java tag missing 'posts' in extra_context";
    EXPECT_EQ(cpp_posts->size(),  2u) << "cpp tag should have 2 posts";
    EXPECT_EQ(java_posts->size(), 1u) << "java tag should have 1 post";
}

// ---------------------------------------------------------------------------
// ExplicitEmptySlugIsPreserved
// ---------------------------------------------------------------------------

TEST_F(MarkdownAdapterTest, ExplicitEmptySlugIsPreserved) {
    // An explicit slug: "" in frontmatter must not be overwritten by the stem fallback.
    // With permalink /{slug}/, empty slug → // → permalink_to_path → index.html (root).
    write_md(test_dir_ / "pages", "index.md",
             "slug: \"\"\ntitle: Home\ncustom_template: landing.html\n");

    config::MarkdownAdapterConfig cfg;
    cfg.collection_paths["pages"] = test_dir_ / "pages";

    config::CollectionCfgMap cols;
    config::CollectionConfig cc;
    cc.item_template = "docs.html";
    cc.permalink     = "/{slug}/";
    cc.context_key   = "page";
    cols["pages"]    = cc;

    adapters::MarkdownAdapter adapter(cfg, make_site(), cols);
    auto result = adapter.fetch_all(nullptr);
    ASSERT_TRUE(result.has_value());

    const auto& pages = result->items.at("pages");
    ASSERT_EQ(pages.size(), 1u);

    // The slug field must be empty string (not overwritten with "index")
    EXPECT_EQ(pages[0].data["slug"].to_string(), "");

    // output_path must be "index.html" (root), not "index/index.html"
    EXPECT_EQ(pages[0].output_path, std::filesystem::path("index.html"));
}

// ---------------------------------------------------------------------------
// TaxonomyNotSynthesizedIfPhysicalCollectionExists
// ---------------------------------------------------------------------------

TEST_F(MarkdownAdapterTest, TaxonomyNotSynthesizedIfPhysicalCollectionExists) {
    fs::path posts_dir = test_dir_ / "posts";
    fs::path tags_dir  = test_dir_ / "tags";

    write_md(posts_dir, "post1.md",
        "slug: post1\n"
        "tags:\n"
        "  - name: \"C++\"\n"
        "    slug: cpp");

    // Physical tag file — deliberately different slug to prove synthesis was suppressed
    write_md(tags_dir, "physical-tag.md", "slug: physical-tag\nname: Physical Tag");

    config::MarkdownAdapterConfig cfg;
    cfg.collection_paths["posts"] = posts_dir;
    cfg.collection_paths["tags"]  = tags_dir;  // physical collection exists

    config::CrossRefConfig cr;
    cr.from      = "posts";
    cr.via       = "tags.slug";
    cr.match_key = "slug";
    cfg.cross_references["tags"] = cr;

    config::CollectionCfgMap colls;
    config::CollectionConfig post_cc;
    post_cc.item_template = "post.html";
    post_cc.permalink     = "/{slug}/";
    colls["posts"] = post_cc;

    config::CollectionConfig tag_cc;
    tag_cc.archive_template = "tag.html";
    tag_cc.permalink        = "/tag/{slug}/";
    tag_cc.context_key      = "tag";
    colls["tags"] = tag_cc;

    adapters::MarkdownAdapter adapter(cfg, make_site(), colls);
    auto result = adapter.fetch_all();
    ASSERT_TRUE(result.has_value()) << result.error().message;

    // Physical collection wins — exactly one item from the .md file
    ASSERT_TRUE(result->items.count("tags")) << "tags collection missing";
    const auto& tags = result->items.at("tags");
    ASSERT_EQ(tags.size(), 1u) << "expected exactly 1 tag from physical file, not synthesized items";
    EXPECT_EQ(tags[0].data["slug"].to_string(), "physical-tag");

    // Cross-reference still runs on the physical collection. No post carries slug 'physical-tag',
    // so the related vector is empty. The emplace_back is unconditional once the guard passes
    // (target_val non-empty and non-null), so 'posts' is always inserted even with zero matches.
    bool posts_key_found = false;
    for (const auto& [key, val] : tags[0].extra_context) {
        if (key == "posts") {
            posts_key_found = true;
            EXPECT_EQ(val.size(), 0u) << "no post matches physical-tag slug";
        }
    }
    ASSERT_TRUE(posts_key_found) << "cross-reference step should still add 'posts' key to physical tag";
}
