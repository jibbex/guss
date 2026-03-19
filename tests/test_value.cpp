/**
 * \file test_value.cpp
 * \brief Unit tests for guss::render::Value — including the Map/Array variants
 *        added during the template engine migration.
 */
#include <gtest/gtest.h>
#include "guss/core/value.hpp"

#include <string>
#include <unordered_map>
#include <vector>

using namespace guss::core;

// ---------------------------------------------------------------------------
// Null
// ---------------------------------------------------------------------------

TEST(Value, DefaultConstruct_IsNull) {
    Value v;
    EXPECT_TRUE(v.is_null());
    EXPECT_FALSE(v.is_string());
    EXPECT_FALSE(v.is_number());
    EXPECT_FALSE(v.is_bool());
    EXPECT_FALSE(v.is_array());
    EXPECT_FALSE(v.is_object());
    EXPECT_FALSE(v.is_truthy());
    EXPECT_EQ(v.to_string(), "null");
}

// ---------------------------------------------------------------------------
// Scalar types
// ---------------------------------------------------------------------------

TEST(Value, Bool_True) {
    Value v(true);
    EXPECT_TRUE(v.is_bool());
    EXPECT_TRUE(v.as_bool());
    EXPECT_TRUE(v.is_truthy());
    EXPECT_EQ(v.to_string(), "true");
}

TEST(Value, Bool_False) {
    Value v(false);
    EXPECT_TRUE(v.is_bool());
    EXPECT_FALSE(v.as_bool());
    EXPECT_FALSE(v.is_truthy());
}

TEST(Value, Int64) {
    Value v(int64_t{-42});
    EXPECT_TRUE(v.is_number());
    EXPECT_EQ(v.as_int(), -42);
    EXPECT_FALSE(v.is_truthy() == false); // non-zero → truthy
}

TEST(Value, Int64_Zero_NotTruthy) {
    Value v(int64_t{0});
    EXPECT_FALSE(v.is_truthy());
}

TEST(Value, String_Owned) {
    Value v(std::string("hello"));
    EXPECT_TRUE(v.is_string());
    EXPECT_EQ(v.as_string(), "hello");
    EXPECT_TRUE(v.is_truthy());
}

TEST(Value, String_Empty_NotTruthy) {
    Value v(std::string(""));
    EXPECT_TRUE(v.is_string());
    EXPECT_FALSE(v.is_truthy());
}

TEST(Value, StringView) {
    std::string s = "world";
    Value v{std::string_view(s)};
    EXPECT_TRUE(v.is_string());
    EXPECT_EQ(v.as_string(), "world");
}

// ---------------------------------------------------------------------------
// Map (object) construction and access
// ---------------------------------------------------------------------------

TEST(Value, MapConstructor_IsObject) {
    std::unordered_map<std::string, Value> m;
    m["key"] = Value(std::string("val"));
    Value v(std::move(m));
    EXPECT_TRUE(v.is_object());
    EXPECT_FALSE(v.is_array());
    EXPECT_FALSE(v.is_null());
}

TEST(Value, MapLookup_ExistingKey) {
    std::unordered_map<std::string, Value> m;
    m["name"] = Value(std::string("Alice"));
    Value v(std::move(m));
    Value found = v["name"];
    ASSERT_TRUE(found.is_string());
    EXPECT_EQ(found.as_string(), "Alice");
}

TEST(Value, MapLookup_MissingKey_ReturnsNull) {
    Value v(std::unordered_map<std::string, Value>{});
    EXPECT_TRUE(v["missing"].is_null());
}

TEST(Value, MapSize) {
    std::unordered_map<std::string, Value> m;
    m["a"] = Value(int64_t{1});
    m["b"] = Value(int64_t{2});
    m["c"] = Value(int64_t{3});
    Value v(std::move(m));
    EXPECT_EQ(v.size(), 3u);
}

TEST(Value, MapHas_Present) {
    std::unordered_map<std::string, Value> m;
    m["x"] = Value(true);
    Value v(std::move(m));
    EXPECT_TRUE(v.has("x"));
}

TEST(Value, MapHas_Absent) {
    Value v(std::unordered_map<std::string, Value>{});
    EXPECT_FALSE(v.has("x"));
}

TEST(Value, MapGet_Found) {
    std::unordered_map<std::string, Value> m;
    m["k"] = Value(std::string("v"));
    Value v(std::move(m));
    EXPECT_EQ(v.get("k", Value(std::string("default"))).as_string(), "v");
}

TEST(Value, MapGet_Missing_UsesDefault) {
    Value v(std::unordered_map<std::string, Value>{});
    EXPECT_EQ(v.get("missing", Value(std::string("default"))).as_string(), "default");
}

TEST(Value, MapTruthiness_Empty_IsFalse) {
    Value v(std::unordered_map<std::string, Value>{});
    EXPECT_FALSE(v.is_truthy());
}

TEST(Value, MapTruthiness_NonEmpty_IsTrue) {
    std::unordered_map<std::string, Value> m;
    m["k"] = Value(int64_t{1});
    Value v(std::move(m));
    EXPECT_TRUE(v.is_truthy());
}

// ---------------------------------------------------------------------------
// Array construction and access
// ---------------------------------------------------------------------------

TEST(Value, ArrayConstructor_IsArray) {
    std::vector<Value> arr;
    arr.push_back(Value(int64_t{1}));
    Value v(std::move(arr));
    EXPECT_TRUE(v.is_array());
    EXPECT_FALSE(v.is_object());
    EXPECT_FALSE(v.is_null());
}

TEST(Value, ArrayIndexLookup) {
    std::vector<Value> arr;
    arr.push_back(Value(std::string("first")));
    arr.push_back(Value(std::string("second")));
    Value v(std::move(arr));
    EXPECT_EQ(v[size_t{0}].as_string(), "first");
    EXPECT_EQ(v[size_t{1}].as_string(), "second");
}

TEST(Value, ArrayIndexOOB_ReturnsNull) {
    Value v(std::vector<Value>{Value(int64_t{42})});
    EXPECT_TRUE(v[size_t{99}].is_null());
}

TEST(Value, ArraySize) {
    std::vector<Value> arr = {
        Value(int64_t{1}), Value(int64_t{2}), Value(int64_t{3})
    };
    Value v(std::move(arr));
    EXPECT_EQ(v.size(), 3u);
}

TEST(Value, ArrayTruthiness_Empty_IsFalse) {
    Value v(std::vector<Value>{});
    EXPECT_FALSE(v.is_truthy());
}

TEST(Value, ArrayTruthiness_NonEmpty_IsTrue) {
    Value v(std::vector<Value>{Value(int64_t{1})});
    EXPECT_TRUE(v.is_truthy());
}

// ---------------------------------------------------------------------------
// Copy is O(1) for Map/Array (shared_ptr refcount bump)
// ---------------------------------------------------------------------------

TEST(Value, MapCopy_BothAccessSameData) {
    std::unordered_map<std::string, Value> m;
    m["k"] = Value(std::string("v"));
    Value original(std::move(m));
    Value copy = original;
    EXPECT_EQ(copy["k"].as_string(), "v");
    EXPECT_EQ(original["k"].as_string(), "v");
}

TEST(Value, ArrayCopy_BothAccessSameData) {
    std::vector<Value> arr = {Value(std::string("hello"))};
    Value original(std::move(arr));
    Value copy = original;
    EXPECT_EQ(copy[size_t{0}].as_string(), "hello");
    EXPECT_EQ(original[size_t{0}].as_string(), "hello");
}

// ---------------------------------------------------------------------------
// Nested access
// ---------------------------------------------------------------------------

TEST(Value, NestedMap_ChainedBracketAccess) {
    std::unordered_map<std::string, Value> inner;
    inner["title"] = Value(std::string("Hello"));

    std::unordered_map<std::string, Value> outer;
    outer["post"] = Value(std::move(inner));

    Value v(std::move(outer));
    EXPECT_EQ(v["post"]["title"].as_string(), "Hello");
}

TEST(Value, ArrayOfMaps) {
    std::unordered_map<std::string, Value> tag1;
    tag1["name"] = Value(std::string("cpp"));
    std::unordered_map<std::string, Value> tag2;
    tag2["name"] = Value(std::string("perf"));

    std::vector<Value> tags;
    tags.push_back(Value(std::move(tag1)));
    tags.push_back(Value(std::move(tag2)));
    Value v(std::move(tags));

    EXPECT_EQ(v[size_t{0}]["name"].as_string(), "cpp");
    EXPECT_EQ(v[size_t{1}]["name"].as_string(), "perf");
}
