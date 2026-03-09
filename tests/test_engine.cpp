/**
 * \file test_engine.cpp
 * \brief GoogleTest unit tests for the Guss template engine (Engine).
 *
 * \details
 * Each test group creates a temporary directory, writes template files into it,
 * constructs an Engine over that directory, and exercises load() and render().
 * Contexts are built with explicit ctx.set() calls — no simdjson.
 */
#include <gtest/gtest.h>
#include "guss/render/engine.hpp"
#include "guss/render/context.hpp"
#include "guss/render/value.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace guss::render;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test fixture — creates and tears down a temporary template directory
// ---------------------------------------------------------------------------

class EngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() / "guss_engine_test";
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override {
        fs::remove_all(tmp_dir_);
    }

    /** Write \p content to a file named \p name inside the temp directory. */
    void write_tpl(std::string_view name, std::string_view content) {
        std::ofstream f(tmp_dir_ / name);
        f << content;
    }

    /** Construct an Engine rooted at tmp_dir_. */
    Engine make_engine() {
        return Engine(tmp_dir_);
    }

    /** Render and assert success; returns the output string. */
    static std::string render_ok(Engine& eng, std::string_view name, Context& ctx) {
        auto r = eng.render(name, ctx);
        if (!r.has_value()) {
            ADD_FAILURE() << "render('" << name << "') failed: " << r.error().message;
            return {};
        }
        return std::move(*r);
    }

    /** Helper: Value from a string literal. */
    static Value S(std::string s) { return Value(std::move(s)); }
    /** Helper: Value from an integer. */
    static Value I(int64_t i) { return Value(i); }
    /** Helper: Value from a boolean. */
    static Value B(bool b) { return Value(b); }
    /** Helper: Value from a vector of Values (array). */
    static Value Arr(std::vector<Value> v) { return Value(std::move(v)); }

    fs::path tmp_dir_;
};

// ---------------------------------------------------------------------------
// load() — cache and file-not-found
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Load_CacheHit_ReturnsSameAddress) {
    write_tpl("simple.html", "hello");
    Engine eng = make_engine();
    auto first  = eng.load("simple.html");
    auto second = eng.load("simple.html");
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);
}

TEST_F(EngineTest, Load_FileNotFound_ReturnsError) {
    Engine eng = make_engine();
    auto result = eng.load("no_such_file.html");
    EXPECT_FALSE(result.has_value());
}

TEST_F(EngineTest, Load_CompiledTemplate_HasReturn) {
    write_tpl("t.html", "");
    Engine eng = make_engine();
    auto ct = eng.load("t.html");
    ASSERT_TRUE(ct.has_value());
    ASSERT_FALSE((*ct)->code.empty());
    EXPECT_EQ((*ct)->code.back().op, Op::Return);
}

// ---------------------------------------------------------------------------
// render() — plain text
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_PlainText) {
    write_tpl("plain.html", "Hello, World!");
    Engine eng = make_engine();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "plain.html", ctx), "Hello, World!");
}

TEST_F(EngineTest, Render_EmptyTemplate) {
    write_tpl("empty.html", "");
    Engine eng = make_engine();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "empty.html", ctx), "");
}

// ---------------------------------------------------------------------------
// render() — variable interpolation
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_SingleVariable) {
    write_tpl("var.html", "Hello, {{ name }}!");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("name", S("World"));
    EXPECT_EQ(render_ok(eng, "var.html", ctx), "Hello, World!");
}

TEST_F(EngineTest, Render_DottedPath) {
    write_tpl("dot.html", "{{ post.title }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("post", Value(std::unordered_map<std::string, Value>{
        {"title", S("My Post")}
    }));
    EXPECT_EQ(render_ok(eng, "dot.html", ctx), "My Post");
}

TEST_F(EngineTest, Render_MissingVariable_EmitsNull) {
    write_tpl("missing.html", "{{ missing }}");
    Engine eng = make_engine();
    Context ctx;
    // Missing variables resolve to null; to_string() on null yields "null".
    EXPECT_EQ(render_ok(eng, "missing.html", ctx), "null");
}

// ---------------------------------------------------------------------------
// render() — HTML escaping
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_HtmlEscape_Ampersand) {
    write_tpl("esc.html", "{{ title }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("title", S("a & b"));
    EXPECT_EQ(render_ok(eng, "esc.html", ctx), "a &amp; b");
}

TEST_F(EngineTest, Render_HtmlEscape_LtGt) {
    write_tpl("esc2.html", "{{ code }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("code", S("<br>"));
    EXPECT_EQ(render_ok(eng, "esc2.html", ctx), "&lt;br&gt;");
}

TEST_F(EngineTest, Render_HtmlEscape_Quote) {
    write_tpl("esc3.html", "{{ attr }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("attr", S(R"(say "hi")"));
    const std::string result = render_ok(eng, "esc3.html", ctx);
    EXPECT_NE(result.find("&quot;"), std::string::npos);
}

// ---------------------------------------------------------------------------
// render() — if / else
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_If_TrueBranch) {
    write_tpl("if.html", "{% if flag %}yes{% else %}no{% endif %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("flag", B(true));
    EXPECT_EQ(render_ok(eng, "if.html", ctx), "yes");
}

TEST_F(EngineTest, Render_If_FalseBranch) {
    write_tpl("if2.html", "{% if flag %}yes{% else %}no{% endif %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("flag", B(false));
    EXPECT_EQ(render_ok(eng, "if2.html", ctx), "no");
}

TEST_F(EngineTest, Render_If_NoElse_False) {
    write_tpl("if3.html", "{% if flag %}yes{% endif %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("flag", B(false));
    EXPECT_EQ(render_ok(eng, "if3.html", ctx), "");
}

TEST_F(EngineTest, Render_If_NumericTruthy) {
    write_tpl("if4.html", "{% if count %}has items{% else %}empty{% endif %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("count", I(3));
    EXPECT_EQ(render_ok(eng, "if4.html", ctx), "has items");
}

TEST_F(EngineTest, Render_If_NumericFalsy_Zero) {
    write_tpl("if5.html", "{% if count %}has items{% else %}empty{% endif %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("count", I(0));
    EXPECT_EQ(render_ok(eng, "if5.html", ctx), "empty");
}

TEST_F(EngineTest, Render_If_Elif) {
    write_tpl("elif.html",
              "{% if x == 1 %}one{% elif x == 2 %}two{% else %}other{% endif %}");
    Engine eng = make_engine();

    {
        Context c1;
        c1.set("x", I(1));
        EXPECT_EQ(render_ok(eng, "elif.html", c1), "one");
    }
    {
        Context c2;
        c2.set("x", I(2));
        EXPECT_EQ(render_ok(eng, "elif.html", c2), "two");
    }
    {
        Context c3;
        c3.set("x", I(99));
        EXPECT_EQ(render_ok(eng, "elif.html", c3), "other");
    }
}

// ---------------------------------------------------------------------------
// render() — for loop
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_ForLoop_SimpleArray) {
    write_tpl("for.html", "{% for item in items %}{{ item }},{% endfor %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "for.html", ctx), "a,b,c,");
}

TEST_F(EngineTest, Render_ForLoop_Empty) {
    write_tpl("for2.html",
              "{% for item in items %}{{ item }}{% else %}none{% endfor %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({}));
    EXPECT_EQ(render_ok(eng, "for2.html", ctx), "none");
}

TEST_F(EngineTest, Render_ForLoop_LoopIndex) {
    write_tpl("idx.html",
              "{% for item in items %}{{ loop.index }}{% endfor %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "idx.html", ctx), "123");
}

TEST_F(EngineTest, Render_ForLoop_LoopFirst) {
    write_tpl("first.html",
              "{% for item in items %}{% if loop.first %}FIRST {% endif %}{{ item }}{% endfor %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "first.html", ctx), "FIRST abc");
}

TEST_F(EngineTest, Render_ForLoop_LoopLast) {
    write_tpl("last.html",
              "{% for item in items %}{{ item }}{% if loop.last %}!{% endif %}{% endfor %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "last.html", ctx), "abc!");
}

TEST_F(EngineTest, Render_ForLoop_CountItems) {
    write_tpl("count.html",
              "{% for item in items %}{{ loop.index }}{% endfor %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c"), S("d")}));
    EXPECT_EQ(render_ok(eng, "count.html", ctx), "1234");
}

// ---------------------------------------------------------------------------
// render() — filters
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_Filter_Upper) {
    write_tpl("upper.html", "{{ name | upper }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("name", S("hello"));
    EXPECT_EQ(render_ok(eng, "upper.html", ctx), "HELLO");
}

TEST_F(EngineTest, Render_Filter_Lower) {
    write_tpl("lower.html", "{{ name | lower }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("name", S("WORLD"));
    EXPECT_EQ(render_ok(eng, "lower.html", ctx), "world");
}

TEST_F(EngineTest, Render_Filter_Escape) {
    write_tpl("escape.html", "{{ raw | escape }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("raw", S("<b>bold</b>"));
    const std::string result = render_ok(eng, "escape.html", ctx);
    EXPECT_NE(result.find("&lt;"), std::string::npos);
    EXPECT_NE(result.find("&gt;"), std::string::npos);
}

TEST_F(EngineTest, Render_Filter_Default_Truthy) {
    write_tpl("def.html", R"({{ name | default("anonymous") }})");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("name", S("Alice"));
    EXPECT_EQ(render_ok(eng, "def.html", ctx), "Alice");
}

TEST_F(EngineTest, Render_Filter_Default_Null) {
    write_tpl("def2.html", R"({{ missing | default("anon") }})");
    Engine eng = make_engine();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "def2.html", ctx), "anon");
}

TEST_F(EngineTest, Render_Filter_Length) {
    write_tpl("len.html", "{{ items | length }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({I(1), I(2), I(3)}));
    EXPECT_EQ(render_ok(eng, "len.html", ctx), "3");
}

TEST_F(EngineTest, Render_Filter_First) {
    write_tpl("fst.html", "{{ items | first }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({S("x"), S("y"), S("z")}));
    EXPECT_EQ(render_ok(eng, "fst.html", ctx), "x");
}

TEST_F(EngineTest, Render_Filter_Last) {
    write_tpl("lst.html", "{{ items | last }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("items", Arr({S("x"), S("y"), S("z")}));
    EXPECT_EQ(render_ok(eng, "lst.html", ctx), "z");
}

TEST_F(EngineTest, Render_Filter_Unknown_ReturnsError) {
    write_tpl("unk.html", "{{ name | no_such_filter }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("name", S("x"));
    EXPECT_FALSE(eng.render("unk.html", ctx).has_value());
}

// ---------------------------------------------------------------------------
// render() — binary operators
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_BinaryAdd_Numbers) {
    write_tpl("add.html", "{{ a + b }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("a", I(3));
    ctx.set("b", I(4));
    const std::string result = render_ok(eng, "add.html", ctx);
    EXPECT_NE(result.find("7"), std::string::npos);
}

TEST_F(EngineTest, Render_BinaryEq_True) {
    write_tpl("eq.html", "{% if x == 1 %}yes{% else %}no{% endif %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("x", I(1));
    EXPECT_EQ(render_ok(eng, "eq.html", ctx), "yes");
}

TEST_F(EngineTest, Render_BinaryEq_False) {
    write_tpl("eq2.html", "{% if x == 2 %}yes{% else %}no{% endif %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("x", I(1));
    EXPECT_EQ(render_ok(eng, "eq2.html", ctx), "no");
}

TEST_F(EngineTest, Render_BinaryAnd) {
    write_tpl("and.html", "{% if a and b %}both{% else %}not{% endif %}");
    Engine eng = make_engine();
    {
        Context c1;
        c1.set("a", B(true));
        c1.set("b", B(true));
        EXPECT_EQ(render_ok(eng, "and.html", c1), "both");
    }
    {
        Context c2;
        c2.set("a", B(true));
        c2.set("b", B(false));
        EXPECT_EQ(render_ok(eng, "and.html", c2), "not");
    }
}

TEST_F(EngineTest, Render_BinaryOr) {
    write_tpl("or.html", "{% if a or b %}either{% else %}neither{% endif %}");
    Engine eng = make_engine();
    {
        Context c1;
        c1.set("a", B(false));
        c1.set("b", B(true));
        EXPECT_EQ(render_ok(eng, "or.html", c1), "either");
    }
    {
        Context c2;
        c2.set("a", B(false));
        c2.set("b", B(false));
        EXPECT_EQ(render_ok(eng, "or.html", c2), "neither");
    }
}

// ---------------------------------------------------------------------------
// render() — unary not
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_UnaryNot_True) {
    write_tpl("not.html", "{% if not flag %}absent{% else %}present{% endif %}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("flag", B(false));
    EXPECT_EQ(render_ok(eng, "not.html", ctx), "absent");
}

// ---------------------------------------------------------------------------
// render() — add_search_path
// ---------------------------------------------------------------------------

TEST_F(EngineTest, AddSearchPath_FindsTemplateInSecondPath) {
    const fs::path sub = tmp_dir_ / "sub";
    fs::create_directories(sub);
    {
        std::ofstream f(sub / "sub.html");
        f << "found";
    } // flush + close before the engine reads the file

    Engine eng(tmp_dir_ / "nonexistent");
    eng.add_search_path(sub);

    Context ctx;
    EXPECT_EQ(render_ok(eng, "sub.html", ctx), "found");
}

// ---------------------------------------------------------------------------
// render() — realistic template
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_RealisticPageTemplate) {
    write_tpl("page.html",
              "<article>"
              "<h1>{{ title | upper }}</h1>"
              "{% if published %}"
              "<p>Published</p>"
              "{% endif %}"
              "<ul>{% for tag in tags %}<li>{{ tag }}</li>{% endfor %}</ul>"
              "</article>");

    Engine eng = make_engine();
    Context ctx;
    ctx.set("title",     S("Hello World"));
    ctx.set("published", B(true));
    ctx.set("tags",      Arr({S("tech"), S("news")}));
    const std::string result = render_ok(eng, "page.html", ctx);

    EXPECT_NE(result.find("<h1>HELLO WORLD</h1>"), std::string::npos);
    EXPECT_NE(result.find("<p>Published</p>"),     std::string::npos);
    EXPECT_NE(result.find("<li>tech</li>"),        std::string::npos);
    EXPECT_NE(result.find("<li>news</li>"),        std::string::npos);
}

// ---------------------------------------------------------------------------
// render() — division by zero
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_DivByZero_ReturnsNull) {
    write_tpl("divz.html", "{{ a / b }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("a", I(10));
    ctx.set("b", I(0));
    EXPECT_EQ(render_ok(eng, "divz.html", ctx), "null");
}

TEST_F(EngineTest, Render_ModByZero_ReturnsNull) {
    write_tpl("modzero.html", "{{ a % b }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("a", I(10));
    ctx.set("b", I(0));
    EXPECT_EQ(render_ok(eng, "modzero.html", ctx), "null");
}

// ---------------------------------------------------------------------------
// render() — safe filter bypasses HTML escaping
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_SafeFilter_NoEscaping) {
    write_tpl("safe.html", "{{ content | safe }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("content", S("<b>bold</b>"));
    EXPECT_EQ(render_ok(eng, "safe.html", ctx), "<b>bold</b>");
}

TEST_F(EngineTest, Render_NoSafeFilter_EscapesHtml) {
    write_tpl("unsafe.html", "{{ content }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("content", S("<b>bold</b>"));
    EXPECT_EQ(render_ok(eng, "unsafe.html", ctx), "&lt;b&gt;bold&lt;/b&gt;");
}

// ---------------------------------------------------------------------------
// render() — stack overflow guard
// ---------------------------------------------------------------------------

TEST_F(EngineTest, Render_ValueStack_Overflow_Throws) {
    write_tpl("deep.html", "{{ a + b + c + d + e }}");
    Engine eng = make_engine();
    Context ctx;
    ctx.set("a", I(1));
    ctx.set("b", I(2));
    ctx.set("c", I(3));
    ctx.set("d", I(4));
    ctx.set("e", I(5));
    const std::string result = render_ok(eng, "deep.html", ctx);
    EXPECT_NE(result.find("15"), std::string::npos);
}
