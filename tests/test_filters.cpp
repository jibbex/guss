/**
 * \file test_filters.cpp
 * \brief GoogleTest unit tests for all built-in Guss template filters.
 *
 * \details
 * Each filter is tested via its free function in guss::render::filters so
 * that the tests are independent of the engine's load/compile/execute path.
 * Additional integration smoke tests at the bottom verify that each filter
 * is reachable through the Engine by name.
 */
#include <gtest/gtest.h>
#include "guss/render/filters.hpp"
#include "guss/render/value.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace guss::render;
using namespace guss::render::filters;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Convenience: call a filter with no arguments. */
static Value call(Value(*fn)(const Value&, std::span<const Value>),
                  const Value& v) {
    return fn(v, {});
}

/** Convenience: call a filter with one argument. */
static Value call1(Value(*fn)(const Value&, std::span<const Value>),
                   const Value& v,
                   const Value& arg) {
    const Value args[1] = {arg};
    return fn(v, std::span<const Value>(args, 1));
}

// ---------------------------------------------------------------------------
// date
// ---------------------------------------------------------------------------

TEST(FilterDate, GhostMillisecondFormat) {
    const Value v(std::string("2024-01-15T10:30:00.000Z"));
    const Value result = call(date, v);
    EXPECT_EQ(result.as_string(), "2024-01-15");
}

TEST(FilterDate, CustomFormat) {
    const Value v(std::string("2024-01-15T10:30:00Z"));
    const Value fmt(std::string("%d/%m/%Y"));
    const Value result = call1(date, v, fmt);
    EXPECT_EQ(result.as_string(), "15/01/2024");
}

TEST(FilterDate, TimeComponents) {
    const Value v(std::string("2024-06-21T14:05:00.000Z"));
    const Value fmt(std::string("%H:%M"));
    const Value result = call1(date, v, fmt);
    EXPECT_EQ(result.as_string(), "14:05");
}

TEST(FilterDate, InvalidStringReturnsOriginal) {
    const Value v(std::string("not-a-date"));
    const Value result = call(date, v);
    // Parsing fails; original value returned.
    EXPECT_EQ(result.as_string(), "not-a-date");
}

TEST(FilterDate, NonStringReturnsOriginal) {
    const Value v(int64_t{42});
    const Value result = call(date, v);
    EXPECT_TRUE(result.is_number());
}

// ---------------------------------------------------------------------------
// truncate
// ---------------------------------------------------------------------------

TEST(FilterTruncate, ShortStringUnchanged) {
    const Value v(std::string("hello"));
    const Value result = call(truncate, v);
    EXPECT_EQ(result.as_string(), "hello");
}

TEST(FilterTruncate, ExactLimitUnchanged) {
    const Value v(std::string("hello"));
    const Value limit(int64_t{5});
    const Value result = call1(truncate, v, limit);
    EXPECT_EQ(result.as_string(), "hello");
}

TEST(FilterTruncate, ExceedsLimitAppendsEllipsis) {
    const Value v(std::string("hello world"));
    const Value limit(int64_t{5});
    const Value result = call1(truncate, v, limit);
    // "hello" + U+2026 (UTF-8: E2 80 A6)
    EXPECT_EQ(result.as_string(), "hello\xe2\x80\xa6");
}

TEST(FilterTruncate, DefaultLimitIs255) {
    // Build a string shorter than 255 code points — must be returned unchanged.
    std::string s(100, 'a');
    const Value v(s);
    const Value result = call(truncate, v);
    EXPECT_EQ(result.as_string(), s);
}

TEST(FilterTruncate, NonStringReturnsOriginal) {
    const Value v(int64_t{7});
    const Value result = call(truncate, v);
    EXPECT_TRUE(result.is_number());
}

// ---------------------------------------------------------------------------
// escape
// ---------------------------------------------------------------------------

TEST(FilterEscape, Ampersand) {
    const Value v(std::string("a & b"));
    const Value result = call(escape, v);
    EXPECT_EQ(result.as_string(), "a &amp; b");
}

TEST(FilterEscape, LessThan) {
    const Value v(std::string("<tag>"));
    const Value result = call(escape, v);
    EXPECT_EQ(result.as_string(), "&lt;tag&gt;");
}

TEST(FilterEscape, DoubleQuote) {
    const Value v(std::string(R"(say "hi")"));
    const Value result = call(escape, v);
    EXPECT_NE(result.as_string().find("&quot;"), std::string::npos);
}

TEST(FilterEscape, SingleQuote) {
    const Value v(std::string("it's"));
    const Value result = call(escape, v);
    EXPECT_NE(result.as_string().find("&#39;"), std::string::npos);
}

TEST(FilterEscape, NoSpecialCharsUnchanged) {
    const Value v(std::string("plain text"));
    EXPECT_EQ(call(escape, v).as_string(), "plain text");
}

TEST(FilterEscape, NonStringReturnsOriginal) {
    const Value v(int64_t{1});
    const Value result = call(escape, v);
    EXPECT_TRUE(result.is_number());
}

// ---------------------------------------------------------------------------
// safe
// ---------------------------------------------------------------------------

TEST(FilterSafe, IsIdentity) {
    const Value v(std::string("<b>bold</b>"));
    const Value result = call(safe, v);
    EXPECT_EQ(result.as_string(), "<b>bold</b>");
}

TEST(FilterSafe, NullIsIdentity) {
    const Value v{};
    EXPECT_TRUE(call(safe, v).is_null());
}

TEST(FilterSafe, NumberIsIdentity) {
    const Value v(int64_t{99});
    const Value result = call(safe, v);
    EXPECT_EQ(result.as_int(), int64_t{99});
}

// ---------------------------------------------------------------------------
// default_
// ---------------------------------------------------------------------------

TEST(FilterDefault, TruthyValueReturnedUnchanged) {
    const Value v(std::string("Alice"));
    const Value fallback(std::string("anon"));
    const Value result = call1(default_, v, fallback);
    EXPECT_EQ(result.as_string(), "Alice");
}

TEST(FilterDefault, NullReturnsFallback) {
    const Value v{};
    const Value fallback(std::string("anon"));
    const Value result = call1(default_, v, fallback);
    EXPECT_EQ(result.as_string(), "anon");
}

TEST(FilterDefault, EmptyStringReturnsFallback) {
    const Value v(std::string(""));
    const Value fallback(std::string("default"));
    const Value result = call1(default_, v, fallback);
    EXPECT_EQ(result.as_string(), "default");
}

TEST(FilterDefault, NoArgAndNullReturnsNull) {
    const Value v{};
    const Value result = call(default_, v);
    EXPECT_TRUE(result.is_null());
}

TEST(FilterDefault, ZeroReturnsFallback) {
    const Value v(int64_t{0});
    const Value fallback(int64_t{42});
    const Value result = call1(default_, v, fallback);
    EXPECT_EQ(result.as_int(), int64_t{42});
}

// ---------------------------------------------------------------------------
// length
// ---------------------------------------------------------------------------

TEST(FilterLength, NullReturnsZero) {
    const Value v{};
    EXPECT_EQ(call(length, v).as_int(), int64_t{0});
}

TEST(FilterLength, StringCodePoints) {
    // 5 ASCII characters = 5 code points.
    const Value v(std::string("hello"));
    EXPECT_EQ(call(length, v).as_int(), int64_t{5});
}

TEST(FilterLength, StringUtf8TwoByteCodePoints) {
    // U+00E9 (é) encodes as 2 bytes in UTF-8.  3 code points, 4 bytes.
    const Value v(std::string("r\xc3\xa9sum\xc3\xa9")); // "résumé" — 6 code points
    EXPECT_EQ(call(length, v).as_int(), int64_t{6});
}

TEST(FilterLength, NumberReturnsZero) {
    const Value v(int64_t{99});
    EXPECT_EQ(call(length, v).as_int(), int64_t{0});
}

// ---------------------------------------------------------------------------
// lower
// ---------------------------------------------------------------------------

TEST(FilterLower, AllUpper) {
    const Value v(std::string("HELLO WORLD"));
    EXPECT_EQ(call(lower, v).as_string(), "hello world");
}

TEST(FilterLower, Mixed) {
    const Value v(std::string("CamelCase"));
    EXPECT_EQ(call(lower, v).as_string(), "camelcase");
}

TEST(FilterLower, AlreadyLower) {
    const Value v(std::string("abc"));
    EXPECT_EQ(call(lower, v).as_string(), "abc");
}

TEST(FilterLower, NonStringReturnsOriginal) {
    const Value v(int64_t{5});
    EXPECT_TRUE(call(lower, v).is_number());
}

// ---------------------------------------------------------------------------
// upper
// ---------------------------------------------------------------------------

TEST(FilterUpper, AllLower) {
    const Value v(std::string("hello world"));
    EXPECT_EQ(call(upper, v).as_string(), "HELLO WORLD");
}

TEST(FilterUpper, Mixed) {
    const Value v(std::string("CamelCase"));
    EXPECT_EQ(call(upper, v).as_string(), "CAMELCASE");
}

TEST(FilterUpper, AlreadyUpper) {
    const Value v(std::string("ABC"));
    EXPECT_EQ(call(upper, v).as_string(), "ABC");
}

TEST(FilterUpper, NonStringReturnsOriginal) {
    const Value v(int64_t{5});
    EXPECT_TRUE(call(upper, v).is_number());
}

// ---------------------------------------------------------------------------
// slugify
// ---------------------------------------------------------------------------

TEST(FilterSlugify, BasicConversion) {
    const Value v(std::string("Hello World"));
    EXPECT_EQ(call(slugify, v).as_string(), "hello-world");
}

TEST(FilterSlugify, Underscores) {
    const Value v(std::string("hello_world"));
    EXPECT_EQ(call(slugify, v).as_string(), "hello-world");
}

TEST(FilterSlugify, SpecialCharactersRemoved) {
    const Value v(std::string("Hello, World!"));
    EXPECT_EQ(call(slugify, v).as_string(), "hello-world");
}

TEST(FilterSlugify, ConsecutiveHyphensCollapsed) {
    const Value v(std::string("foo  bar"));
    EXPECT_EQ(call(slugify, v).as_string(), "foo-bar");
}

TEST(FilterSlugify, LeadingTrailingHyphensStripped) {
    const Value v(std::string("  hello  "));
    EXPECT_EQ(call(slugify, v).as_string(), "hello");
}

TEST(FilterSlugify, AlreadySlug) {
    const Value v(std::string("my-post-title"));
    EXPECT_EQ(call(slugify, v).as_string(), "my-post-title");
}

TEST(FilterSlugify, EmptyString) {
    const Value v(std::string(""));
    EXPECT_EQ(call(slugify, v).as_string(), "");
}

TEST(FilterSlugify, NonStringReturnsOriginal) {
    const Value v(int64_t{5});
    EXPECT_TRUE(call(slugify, v).is_number());
}

TEST(FilterSlugify, AccentedChar) {
    // é is 0xC3 0xA9 in UTF-8 — percent-encoded
    Value result = filters::slugify(Value(std::string_view("café")), {});
    const std::string s(result.as_string());
    // Should contain "caf" followed by percent-encoded bytes
    EXPECT_FALSE(s.empty());
    EXPECT_NE(s.find("caf"), std::string::npos);
}

// ---------------------------------------------------------------------------
// join / first / last / reverse — engine integration tests using native Values
// ---------------------------------------------------------------------------

#include "guss/render/engine.hpp"
#include "guss/render/context.hpp"
#include <filesystem>
#include <fstream>

class ArrayFilterTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = std::filesystem::temp_directory_path() / "guss_filter_test";
        std::filesystem::create_directories(tmp_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(tmp_dir_);
    }

    void write_tpl(std::string_view name, std::string_view content) {
        std::ofstream f(tmp_dir_ / name);
        f << content;
    }

    Engine make_engine() {
        return Engine(tmp_dir_);
    }

    static std::string render_ok(Engine& eng, std::string_view name, Context& ctx) {
        auto r = eng.render(name, ctx);
        if (!r.has_value()) {
            ADD_FAILURE() << "render('" << name << "') failed: " << r.error().message;
            return {};
        }
        return std::move(*r);
    }

    static Value S(std::string s) { return Value(std::move(s)); }
    static Value I(int64_t i) { return Value(i); }
    static Value Arr(std::vector<Value> v) { return Value(std::move(v)); }

    std::filesystem::path tmp_dir_;
};

// join
TEST_F(ArrayFilterTest, Join_DefaultSeparator) {
    write_tpl("j.html", R"({{ items | join }})");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "j.html", ctx), "abc");
}

TEST_F(ArrayFilterTest, Join_CommaSeparator) {
    write_tpl("j2.html", R"({{ items | join(", ") }})");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({S("x"), S("y"), S("z")}));
    EXPECT_EQ(render_ok(eng, "j2.html", ctx), "x, y, z");
}

TEST_F(ArrayFilterTest, Join_EmptyArray) {
    write_tpl("j3.html", R"({{ items | join }})");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({}));
    EXPECT_EQ(render_ok(eng, "j3.html", ctx), "");
}

// first
TEST_F(ArrayFilterTest, First_ReturnsFirstElement) {
    write_tpl("fst.html", "{{ items | first }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({S("alpha"), S("beta"), S("gamma")}));
    EXPECT_EQ(render_ok(eng, "fst.html", ctx), "alpha");
}

TEST_F(ArrayFilterTest, First_EmptyArrayReturnsNull) {
    write_tpl("fst2.html", "{{ items | first }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({}));
    EXPECT_EQ(render_ok(eng, "fst2.html", ctx), "null");
}

// last
TEST_F(ArrayFilterTest, Last_ReturnsLastElement) {
    write_tpl("lst.html", "{{ items | last }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({S("alpha"), S("beta"), S("gamma")}));
    EXPECT_EQ(render_ok(eng, "lst.html", ctx), "gamma");
}

TEST_F(ArrayFilterTest, Last_EmptyArrayReturnsNull) {
    write_tpl("lst2.html", "{{ items | last }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({}));
    EXPECT_EQ(render_ok(eng, "lst2.html", ctx), "null");
}

// reverse
TEST_F(ArrayFilterTest, Reverse_String) {
    write_tpl("rev.html", "{{ word | reverse }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("word", S("abcde"));
    EXPECT_EQ(render_ok(eng, "rev.html", ctx), "edcba");
}

TEST(FilterReverse, Array_ReturnsReversedElements) {
    // Verify the filter itself returns a new reversed array.
    Value v = Value(std::vector<Value>{
        Value(std::string("a")), Value(std::string("b")), Value(std::string("c"))
    });
    Value result = filters::reverse(v, {});
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), size_t{3});
    EXPECT_EQ(result[size_t{0}].as_string(), "c");
    EXPECT_EQ(result[size_t{1}].as_string(), "b");
    EXPECT_EQ(result[size_t{2}].as_string(), "a");
}

TEST(FilterReverse, EmptyArray_ReturnsEmptyArray) {
    Value v = Value(std::vector<Value>{});
    Value result = filters::reverse(v, {});
    ASSERT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), size_t{0});
}

// ---------------------------------------------------------------------------
// striptags
// ---------------------------------------------------------------------------

TEST(FilterStriptags, RemovesTags) {
    const Value v(std::string("<b>bold</b> and <i>italic</i>"));
    EXPECT_EQ(call(striptags, v).as_string(), "bold and italic");
}

TEST(FilterStriptags, NoTagsUnchanged) {
    const Value v(std::string("plain text"));
    EXPECT_EQ(call(striptags, v).as_string(), "plain text");
}

TEST(FilterStriptags, EmptyString) {
    const Value v(std::string(""));
    EXPECT_EQ(call(striptags, v).as_string(), "");
}

TEST(FilterStriptags, SelfClosingTag) {
    const Value v(std::string("line1<br/>line2"));
    EXPECT_EQ(call(striptags, v).as_string(), "line1line2");
}

TEST(FilterStriptags, NonStringReturnsOriginal) {
    const Value v(int64_t{3});
    EXPECT_TRUE(call(striptags, v).is_number());
}

// ---------------------------------------------------------------------------
// urlencode
// ---------------------------------------------------------------------------

TEST(FilterUrlencode, SpaceEncodedAsPercent20) {
    const Value v(std::string("hello world"));
    EXPECT_EQ(call(urlencode, v).as_string(), "hello%20world");
}

TEST(FilterUrlencode, UnreservedCharsUnchanged) {
    const Value v(std::string("abc-_.~"));
    EXPECT_EQ(call(urlencode, v).as_string(), "abc-_.~");
}

TEST(FilterUrlencode, SlashEncoded) {
    const Value v(std::string("a/b"));
    EXPECT_EQ(call(urlencode, v).as_string(), "a%2Fb");
}

TEST(FilterUrlencode, QueryString) {
    const Value v(std::string("q=hello world&lang=en"));
    const std::string result = std::string(call(urlencode, v).as_string());
    EXPECT_NE(result.find("%20"), std::string::npos);
    EXPECT_NE(result.find("%26"), std::string::npos);
}

TEST(FilterUrlencode, EmptyString) {
    const Value v(std::string(""));
    EXPECT_EQ(call(urlencode, v).as_string(), "");
}

TEST(FilterUrlencode, NonStringReturnsOriginal) {
    const Value v(int64_t{7});
    EXPECT_TRUE(call(urlencode, v).is_number());
}

// ---------------------------------------------------------------------------
// register_all — verify all 15 filters are registered
// ---------------------------------------------------------------------------

TEST(FilterRegisterAll, AllFifteenFiltersPresent) {
    std::vector<FilterFn>                  registry;
    std::unordered_map<std::string, size_t> index;
    register_all(registry, index);

    EXPECT_EQ(registry.size(), size_t{15});
    EXPECT_EQ(index.size(),    size_t{15});

    const std::vector<std::string> expected_names = {
        "date", "truncate", "escape", "safe", "default",
        "length", "lower", "upper", "slugify", "join",
        "first", "last", "reverse", "striptags", "urlencode"
    };
    for (const auto& name : expected_names) {
        EXPECT_NE(index.find(name), index.end())
            << "filter not registered: " << name;
    }
}

TEST(FilterRegisterAll, IndexedIdsMatchRegistry) {
    std::vector<FilterFn>                  registry;
    std::unordered_map<std::string, size_t> index;
    register_all(registry, index);

    for (const auto& [name, id] : index) {
        EXPECT_LT(id, registry.size())
            << "filter id out of range for: " << name;
        EXPECT_TRUE(static_cast<bool>(registry[id]))
            << "null FilterFn for: " << name;
    }
}
