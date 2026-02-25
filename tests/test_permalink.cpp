/**
 * @file test_permalink.cpp
 * @brief Unit tests for permalink generation.
 */
#include <gtest/gtest.h>
#include "guss/core/permalink.hpp"
#include <chrono>

using namespace guss;

class PermalinkTest : public ::testing::Test {
protected:
    config::PermalinkConfig default_config() {
        config::PermalinkConfig cfg;
        cfg.post_pattern = "/{year}/{month}/{slug}/";
        cfg.page_pattern = "/{slug}/";
        cfg.tag_pattern = "/tag/{slug}/";
        cfg.category_pattern = "/category/{slug}/";
        cfg.author_pattern = "/author/{slug}/";
        return cfg;
    }

    model::Post make_post(const std::string& slug, const std::string& id = "123") {
        model::Post post;
        post.id = id;
        post.slug = slug;
        post.title = "Test Post";

        // Create a date: 2024-03-15
        std::tm tm = {};
        tm.tm_year = 124; // 2024 - 1900
        tm.tm_mon = 2;    // March (0-indexed)
        tm.tm_mday = 15;
        #ifdef _WIN32
        post.published_at = std::chrono::system_clock::from_time_t(_mkgmtime(&tm));
        #else
        post.published_at = std::chrono::system_clock::from_time_t(timegm(&tm));
        #endif

        return post;
    }
};

TEST_F(PermalinkTest, GeneratesPostPermalink) {
    PermalinkGenerator gen(default_config());
    auto post = make_post("hello-world");

    auto permalink = gen.for_post(post);
    EXPECT_EQ(permalink, "/2024/03/hello-world/");
}

TEST_F(PermalinkTest, GeneratesPagePermalink) {
    PermalinkGenerator gen(default_config());

    model::Page page;
    page.id = "456";
    page.slug = "about";

    auto permalink = gen.for_page(page);
    EXPECT_EQ(permalink, "/about/");
}

TEST_F(PermalinkTest, GeneratesTagPermalink) {
    PermalinkGenerator gen(default_config());

    model::Tag tag;
    tag.id = "789";
    tag.slug = "tech";
    tag.name = "Technology";

    auto permalink = gen.for_tag(tag);
    EXPECT_EQ(permalink, "/tag/tech/");
}

TEST_F(PermalinkTest, GeneratesAuthorPermalink) {
    PermalinkGenerator gen(default_config());

    model::Author author;
    author.id = "abc";
    author.slug = "john-doe";
    author.name = "John Doe";

    auto permalink = gen.for_author(author);
    EXPECT_EQ(permalink, "/author/john-doe/");
}

TEST_F(PermalinkTest, ConvertsPermalinkToPath) {
    auto path = PermalinkGenerator::permalink_to_path("/2024/03/hello-world/");
    EXPECT_EQ(path.string(), "2024/03/hello-world/index.html");
}

TEST_F(PermalinkTest, ConvertsRootPermalinkToPath) {
    auto path = PermalinkGenerator::permalink_to_path("/");
    EXPECT_EQ(path.string(), "index.html");
}

TEST_F(PermalinkTest, ConvertsPermalinkWithoutTrailingSlash) {
    auto path = PermalinkGenerator::permalink_to_path("/about");
    EXPECT_EQ(path.string(), "about/index.html");
}

TEST_F(PermalinkTest, CustomPatternWithId) {
    config::PermalinkConfig cfg;
    cfg.post_pattern = "/posts/{id}/{slug}/";

    PermalinkGenerator gen(cfg);
    auto post = make_post("hello-world", "post-123");

    auto permalink = gen.for_post(post);
    EXPECT_EQ(permalink, "/posts/post-123/hello-world/");
}

TEST_F(PermalinkTest, PatternWithDayToken) {
    config::PermalinkConfig cfg;
    cfg.post_pattern = "/{year}/{month}/{day}/{slug}/";

    PermalinkGenerator gen(cfg);
    auto post = make_post("hello-world");

    auto permalink = gen.for_post(post);
    EXPECT_EQ(permalink, "/2024/03/15/hello-world/");
}

TEST_F(PermalinkTest, SlugOnly) {
    config::PermalinkConfig cfg;
    cfg.post_pattern = "/{slug}/";

    PermalinkGenerator gen(cfg);
    auto post = make_post("simple-post");

    auto permalink = gen.for_post(post);
    EXPECT_EQ(permalink, "/simple-post/");
}

TEST_F(PermalinkTest, HandlesSpecialCharactersInSlug) {
    PermalinkGenerator gen(default_config());
    auto post = make_post("hello-world-2024");

    auto permalink = gen.for_post(post);
    EXPECT_EQ(permalink, "/2024/03/hello-world-2024/");
}

TEST_F(PermalinkTest, NormalizesDoubleSlashes) {
    config::PermalinkConfig cfg;
    cfg.post_pattern = "//blog//{slug}//";

    PermalinkGenerator gen(cfg);
    auto post = make_post("test");

    auto permalink = gen.for_post(post);
    EXPECT_EQ(permalink, "/blog/test/");
}
