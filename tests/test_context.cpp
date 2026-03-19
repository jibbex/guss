/**
 * \file test_context.cpp
 * \brief Unit tests for guss::render::Context — scope resolution, dotted-path
 *        navigation, parent-child chaining, and PMR stack-arena allocation.
 */
#include <gtest/gtest.h>
#include "guss/render/context.hpp"
#include "guss/core/value.hpp"

#include <array>
#include <memory_resource>
#include <string>
#include <unordered_map>
#include <vector>

using namespace guss::core;
using guss::render::Context;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Value make_map(std::initializer_list<std::pair<const char*, Value>> pairs) {
    std::unordered_map<std::string, Value> m;
    for (auto& [k, v] : pairs) m[k] = v;
    return Value(std::move(m));
}

// ---------------------------------------------------------------------------
// Basic set / resolve
// ---------------------------------------------------------------------------

TEST(Context, DefaultConstruct_SetResolve) {
    Context ctx;
    ctx.set("name", Value(std::string("Alice")));
    Value v = ctx.resolve("name");
    ASSERT_TRUE(v.is_string());
    EXPECT_EQ(v.as_string(), "Alice");
}

TEST(Context, Resolve_MissingKey_ReturnsNull) {
    Context ctx;
    EXPECT_TRUE(ctx.resolve("no_such_key").is_null());
}

TEST(Context, Set_Overwrites_ExistingKey) {
    Context ctx;
    ctx.set("x", Value(int64_t{1}));
    ctx.set("x", Value(int64_t{2}));
    EXPECT_EQ(ctx.resolve("x").as_int(), 2);
}

TEST(Context, Set_MultipleKeys) {
    Context ctx;
    ctx.set("a", Value(std::string("alpha")));
    ctx.set("b", Value(int64_t{42}));
    ctx.set("c", Value(true));
    EXPECT_EQ(ctx.resolve("a").as_string(), "alpha");
    EXPECT_EQ(ctx.resolve("b").as_int(), 42);
    EXPECT_TRUE(ctx.resolve("c").as_bool());
}

// ---------------------------------------------------------------------------
// Flat dotted keys — "loop.index" stored verbatim, not split on '.'
// ---------------------------------------------------------------------------

TEST(Context, FlatDottedKey_ExactMatch) {
    Context ctx;
    ctx.set("loop.index", Value(int64_t{3}));
    Value v = ctx.resolve("loop.index");
    ASSERT_FALSE(v.is_null());
    EXPECT_EQ(v.as_int(), 3);
}

TEST(Context, FlatDottedKey_MultipleLoopVars) {
    Context ctx;
    ctx.set("loop.index",  Value(int64_t{5}));
    ctx.set("loop.index0", Value(int64_t{4}));
    ctx.set("loop.first",  Value(false));
    ctx.set("loop.last",   Value(true));
    ctx.set("loop.length", Value(int64_t{10}));

    EXPECT_EQ(ctx.resolve("loop.index").as_int(),   5);
    EXPECT_EQ(ctx.resolve("loop.index0").as_int(),  4);
    EXPECT_FALSE(ctx.resolve("loop.first").as_bool());
    EXPECT_TRUE(ctx.resolve("loop.last").as_bool());
    EXPECT_EQ(ctx.resolve("loop.length").as_int(), 10);
}

TEST(Context, FlatDottedKey_PrefixKeyAbsent_StillResolves) {
    // "loop" is not set; only "loop.index" is. Resolution must still work.
    Context ctx;
    ctx.set("loop.index", Value(int64_t{1}));
    EXPECT_FALSE(ctx.resolve("loop.index").is_null());
    EXPECT_TRUE(ctx.resolve("loop").is_null());
}

// ---------------------------------------------------------------------------
// Dotted-path navigation into a locally-set Value (object)
// ---------------------------------------------------------------------------

TEST(Context, DottedPath_IntoValueMap_TwoLevels) {
    Context ctx;
    ctx.set("post", make_map({
        {"title", Value(std::string("My Post"))},
        {"slug",  Value(std::string("my-post"))},
    }));

    EXPECT_EQ(ctx.resolve("post.title").as_string(), "My Post");
    EXPECT_EQ(ctx.resolve("post.slug").as_string(),  "my-post");
}

TEST(Context, DottedPath_IntoValueMap_ThreeLevels) {
    Value author = make_map({{"name", Value(std::string("Alice"))}});
    Value post   = make_map({{"author", author}});

    Context ctx;
    ctx.set("post", post);

    EXPECT_EQ(ctx.resolve("post.author.name").as_string(), "Alice");
}

TEST(Context, DottedPath_MissingField_ReturnsNull) {
    Context ctx;
    ctx.set("post", Value(std::unordered_map<std::string, Value>{}));
    EXPECT_TRUE(ctx.resolve("post.title").is_null());
}

TEST(Context, DottedPath_MissingTopLevel_ReturnsNull) {
    Context ctx;
    EXPECT_TRUE(ctx.resolve("post.title").is_null());
}

TEST(Context, DottedPath_ExactMatchWinsOverNavigation) {
    // Both "post.title" (flat) and "post" (object with "title") are set.
    // Exact match for "post.title" takes priority.
    Context ctx;
    ctx.set("post.title", Value(std::string("flat")));
    ctx.set("post", make_map({{"title", Value(std::string("nav"))}}));

    EXPECT_EQ(ctx.resolve("post.title").as_string(), "flat");
    EXPECT_EQ(ctx.resolve("post")["title"].as_string(), "nav");
}

// ---------------------------------------------------------------------------
// Parent-child scope chaining
// ---------------------------------------------------------------------------

TEST(Context, Child_InheritsParentLocals) {
    Context parent;
    parent.set("site", Value(std::string("MySite")));

    Context child = parent.child();
    EXPECT_EQ(child.resolve("site").as_string(), "MySite");
}

TEST(Context, Child_ShadowsParentVariable) {
    Context parent;
    parent.set("x", Value(int64_t{1}));

    Context child = parent.child();
    child.set("x", Value(int64_t{99}));

    EXPECT_EQ(child.resolve("x").as_int(),  99);
    EXPECT_EQ(parent.resolve("x").as_int(),  1);
}

TEST(Context, Child_OwnVariable_DoesNotLeakToParent) {
    Context parent;
    Context child = parent.child();
    child.set("loop_var", Value(std::string("item")));

    EXPECT_TRUE(parent.resolve("loop_var").is_null());
    EXPECT_EQ(child.resolve("loop_var").as_string(), "item");
}

TEST(Context, GrandChild_ResolvesAcrossThreeGenerations) {
    Context grandparent;
    grandparent.set("global", Value(std::string("gp")));

    Context parent = grandparent.child();
    parent.set("mid", Value(std::string("p")));

    Context child = parent.child();
    child.set("local", Value(std::string("c")));

    EXPECT_EQ(child.resolve("global").as_string(), "gp");
    EXPECT_EQ(child.resolve("mid").as_string(),    "p");
    EXPECT_EQ(child.resolve("local").as_string(),  "c");
    EXPECT_TRUE(parent.resolve("local").is_null());
}

// ---------------------------------------------------------------------------
// PMR constructor — stack arena
// ---------------------------------------------------------------------------

TEST(Context, PmrConstructor_WorksCorrectly) {
    alignas(std::max_align_t) std::array<std::byte, 8192> buf;
    std::pmr::monotonic_buffer_resource mbr(buf.data(), buf.size(),
        std::pmr::new_delete_resource());

    Context ctx(&mbr);
    ctx.set("site",        Value(std::string("MySite")));
    ctx.set("post",        make_map({{"title", Value(std::string("Hello"))}}));
    ctx.set("loop.index",  Value(int64_t{1}));
    ctx.set("loop.first",  Value(true));

    EXPECT_EQ(ctx.resolve("site").as_string(),         "MySite");
    EXPECT_EQ(ctx.resolve("post.title").as_string(),   "Hello");
    EXPECT_EQ(ctx.resolve("loop.index").as_int(),      1);
    EXPECT_TRUE(ctx.resolve("loop.first").as_bool());
}

TEST(Context, PmrConstructor_StackBufferSufficient_ForTypicalTemplate) {
    // null_memory_resource as upstream: any overflow throws std::bad_alloc.
    // 8 KB must be enough for ~10 top-level variables + loop metadata.
    alignas(std::max_align_t) std::array<std::byte, 8192> buf;
    std::pmr::monotonic_buffer_resource mbr(buf.data(), buf.size(),
        std::pmr::null_memory_resource());

    ASSERT_NO_THROW({
        Context ctx(&mbr);
        ctx.set("site",        Value(std::string("My Blog")));
        ctx.set("post",        make_map({
            {"title",   Value(std::string("Hello World"))},
            {"slug",    Value(std::string("hello-world"))},
            {"content", Value(std::string("<p>Content</p>"))},
        }));
        ctx.set("tags",        Value(std::vector<Value>{}));
        ctx.set("pagination",  Value(std::unordered_map<std::string, Value>{}));
        ctx.set("loop.index",  Value(int64_t{1}));
        ctx.set("loop.index0", Value(int64_t{0}));
        ctx.set("loop.first",  Value(true));
        ctx.set("loop.last",   Value(false));
        ctx.set("loop.length", Value(int64_t{5}));

        EXPECT_EQ(ctx.resolve("post.title").as_string(), "Hello World");
        EXPECT_EQ(ctx.resolve("loop.index").as_int(), 1);
    });
}

TEST(Context, PmrChild_SharesMemoryResource) {
    // Both parent and child allocate from the same 8 KB buffer.
    // If child() silently fell back to the default (heap) allocator, the
    // null_memory_resource upstream on the parent would not protect us —
    // but the child's own variables would escape the arena.  The correct
    // behaviour is that child() propagates the parent's resource.
    alignas(std::max_align_t) std::array<std::byte, 8192> buf;
    std::pmr::monotonic_buffer_resource mbr(buf.data(), buf.size(),
        std::pmr::null_memory_resource());

    ASSERT_NO_THROW({
        Context parent(&mbr);
        parent.set("site", Value(std::string("MySite")));

        Context child = parent.child();
        child.set("post", make_map({{"title", Value(std::string("Post"))}}));

        EXPECT_EQ(child.resolve("site").as_string(),       "MySite");
        EXPECT_EQ(child.resolve("post.title").as_string(), "Post");
    });
}
