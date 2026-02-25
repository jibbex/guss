/**
 * @file test_ghost_json.cpp
 * @brief Unit tests for Ghost CMS JSON parsing.
 */
#include <gtest/gtest.h>
#include "guss/adapters/ghost/json_parser.hpp"

using namespace guss::adapters::ghost;
using namespace guss::model;

class GhostJsonTest : public ::testing::Test {
protected:
    // Sample Ghost API responses
    static constexpr std::string_view authors_response = R"({
        "authors": [
            {
                "id": "1",
                "name": "John Doe",
                "slug": "john-doe",
                "bio": "A writer",
                "profile_image": "https://example.com/john.jpg",
                "website": "https://johndoe.com",
                "twitter": "@johndoe"
            },
            {
                "id": "2",
                "name": "Jane Smith",
                "slug": "jane-smith",
                "email": "jane@example.com"
            }
        ]
    })";

    static constexpr std::string_view tags_response = R"({
        "tags": [
            {
                "id": "tag1",
                "name": "Technology",
                "slug": "tech",
                "description": "Tech articles"
            },
            {
                "id": "tag2",
                "name": "News",
                "slug": "news"
            }
        ]
    })";

    static constexpr std::string_view posts_response = R"({
        "posts": [
            {
                "id": "post1",
                "title": "Hello World",
                "slug": "hello-world",
                "html": "<p>Content here</p>",
                "excerpt": "A brief excerpt",
                "status": "published",
                "published_at": "2024-03-15T10:30:00.000Z",
                "created_at": "2024-03-10T08:00:00.000Z",
                "feature_image": "https://example.com/image.jpg",
                "authors": [
                    {
                        "id": "1",
                        "name": "John Doe",
                        "slug": "john-doe"
                    }
                ],
                "tags": [
                    {
                        "id": "tag1",
                        "name": "Technology",
                        "slug": "tech"
                    }
                ]
            }
        ],
        "meta": {
            "pagination": {
                "page": 1,
                "limit": 15,
                "pages": 1,
                "total": 1,
                "next": null,
                "prev": null
            }
        }
    })";

    static constexpr std::string_view posts_paginated_response = R"({
        "posts": [
            {
                "id": "post2",
                "title": "Second Post",
                "slug": "second-post",
                "html": "<p>More content</p>",
                "status": "published",
                "created_at": "2024-03-20T12:00:00.000Z"
            }
        ],
        "meta": {
            "pagination": {
                "page": 1,
                "limit": 1,
                "pages": 2,
                "total": 2,
                "next": 2,
                "prev": null
            }
        }
    })";

    static constexpr std::string_view pages_response = R"({
        "pages": [
            {
                "id": "page1",
                "title": "About Us",
                "slug": "about",
                "html": "<p>About page content</p>",
                "status": "published",
                "published_at": "2024-01-01T00:00:00.000Z",
                "created_at": "2024-01-01T00:00:00.000Z"
            }
        ],
        "meta": {
            "pagination": {
                "page": 1,
                "pages": 1,
                "next": null
            }
        }
    })";
};

TEST_F(GhostJsonTest, ParsesAuthorsResponse) {
    auto result = parse_authors_response(authors_response);
    ASSERT_TRUE(result.has_value());

    const auto& authors = *result;
    ASSERT_EQ(authors.size(), 2);

    EXPECT_EQ(authors[0].id, "1");
    EXPECT_EQ(authors[0].name, "John Doe");
    EXPECT_EQ(authors[0].slug, "john-doe");
    EXPECT_EQ(authors[0].bio, "A writer");
    EXPECT_EQ(authors[0].profile_image, "https://example.com/john.jpg");
    EXPECT_EQ(authors[0].twitter, "@johndoe");

    EXPECT_EQ(authors[1].id, "2");
    EXPECT_EQ(authors[1].email, "jane@example.com");
}

TEST_F(GhostJsonTest, ParsesTagsResponse) {
    auto result = parse_tags_response(tags_response);
    ASSERT_TRUE(result.has_value());

    const auto& tags = *result;
    ASSERT_EQ(tags.size(), 2);

    EXPECT_EQ(tags[0].id, "tag1");
    EXPECT_EQ(tags[0].name, "Technology");
    EXPECT_EQ(tags[0].slug, "tech");
    EXPECT_EQ(tags[0].description, "Tech articles");

    EXPECT_EQ(tags[1].id, "tag2");
    EXPECT_FALSE(tags[1].description.has_value());
}

TEST_F(GhostJsonTest, ParsesPostsResponse) {
    auto result = parse_posts_response(posts_response);
    ASSERT_TRUE(result.has_value());

    const auto& [posts, has_more] = *result;
    ASSERT_EQ(posts.size(), 1);
    EXPECT_FALSE(has_more);

    const auto& post = posts[0];
    EXPECT_EQ(post.id, "post1");
    EXPECT_EQ(post.title, "Hello World");
    EXPECT_EQ(post.slug, "hello-world");
    EXPECT_EQ(post.content_html, "<p>Content here</p>");
    EXPECT_EQ(post.excerpt, "A brief excerpt");
    EXPECT_EQ(post.status, PostStatus::Published);
    EXPECT_EQ(post.feature_image, "https://example.com/image.jpg");

    ASSERT_EQ(post.authors.size(), 1);
    EXPECT_EQ(post.authors[0].name, "John Doe");

    ASSERT_EQ(post.tags.size(), 1);
    EXPECT_EQ(post.tags[0].name, "Technology");
}

TEST_F(GhostJsonTest, ParsesPostsWithPagination) {
    auto result = parse_posts_response(posts_paginated_response);
    ASSERT_TRUE(result.has_value());

    const auto& [posts, has_more] = *result;
    EXPECT_TRUE(has_more);
    EXPECT_EQ(posts.size(), 1);
}

TEST_F(GhostJsonTest, ParsesPagesResponse) {
    auto result = parse_pages_response(pages_response);
    ASSERT_TRUE(result.has_value());

    const auto& [pages, has_more] = *result;
    ASSERT_EQ(pages.size(), 1);
    EXPECT_FALSE(has_more);

    const auto& page = pages[0];
    EXPECT_EQ(page.id, "page1");
    EXPECT_EQ(page.title, "About Us");
    EXPECT_EQ(page.slug, "about");
    EXPECT_EQ(page.status, PageStatus::Published);
}

TEST_F(GhostJsonTest, HandlesInvalidJson) {
    auto result = parse_authors_response("not valid json");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, guss::error::ErrorCode::AdapterParseError);
}

TEST_F(GhostJsonTest, HandlesMissingField) {
    std::string_view bad_response = R"({"not_authors": []})";
    auto result = parse_authors_response(bad_response);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, guss::error::ErrorCode::AdapterParseError);
}

TEST_F(GhostJsonTest, ParsesTimestamp) {
    auto result = parse_timestamp("2024-03-15T10:30:00.000Z");
    ASSERT_TRUE(result.has_value());

    auto time_t = std::chrono::system_clock::to_time_t(*result);
    std::tm tm = *std::gmtime(&time_t);

    EXPECT_EQ(tm.tm_year + 1900, 2024);
    EXPECT_EQ(tm.tm_mon + 1, 3);
    EXPECT_EQ(tm.tm_mday, 15);
}

TEST_F(GhostJsonTest, HandlesNullTimestamp) {
    auto result = parse_timestamp("");
    EXPECT_FALSE(result.has_value());
}

TEST_F(GhostJsonTest, HandlesNullFields) {
    std::string_view response = R"({
        "authors": [
            {
                "id": "1",
                "name": "Test",
                "slug": "test",
                "bio": null,
                "website": null
            }
        ]
    })";

    auto result = parse_authors_response(response);
    ASSERT_TRUE(result.has_value());

    const auto& author = (*result)[0];
    EXPECT_FALSE(author.bio.has_value());
    EXPECT_FALSE(author.website.has_value());
}
