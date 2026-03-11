#include <gtest/gtest.h>
#include "guss/core/render_item.hpp"

using namespace guss::render;

TEST(RenderItemTest, DefaultConstruction) {
    RenderItem item;
    EXPECT_TRUE(item.output_path.empty());
    EXPECT_TRUE(item.template_name.empty());
    EXPECT_TRUE(item.data.is_null());
}

TEST(RenderItemTest, Construction) {
    std::unordered_map<std::string, Value> m;
    m["slug"] = Value(std::string("hello"));
    RenderItem item{
        std::filesystem::path("hello/index.html"),
        "post.html",
        Value(std::move(m))
    };
    EXPECT_EQ(item.output_path, std::filesystem::path("hello/index.html"));
    EXPECT_EQ(item.template_name, "post.html");
    EXPECT_EQ(item.data["slug"].to_string(), "hello");
}

TEST(CollectionMapTest, EmptyMap) {
    CollectionMap m;
    EXPECT_TRUE(m.empty());
}

TEST(CollectionMapTest, InsertAndLookup) {
    CollectionMap m;
    std::unordered_map<std::string, Value> data;
    data["slug"] = Value(std::string("test-post"));
    m["posts"].push_back(RenderItem{
        std::filesystem::path("test-post/index.html"),
        "post.html",
        Value(std::move(data))
    });
    ASSERT_EQ(m["posts"].size(), 1u);
    EXPECT_EQ(m["posts"][0].template_name, "post.html");
    EXPECT_EQ(m["posts"][0].data["slug"].to_string(), "test-post");
}

TEST(CollectionMapTest, MultipleCollections) {
    CollectionMap m;
    m["posts"].push_back(RenderItem{{}, "post.html", Value{}});
    m["pages"].push_back(RenderItem{{}, "page.html", Value{}});
    m["tags"].push_back(RenderItem{{}, "tag.html", Value{}});
    EXPECT_EQ(m.size(), 3u);
    EXPECT_EQ(m["posts"].size(), 1u);
    EXPECT_EQ(m["tags"].size(), 1u);
}
