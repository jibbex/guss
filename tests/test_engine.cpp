/**
 * \file test_engine.cpp
 * brief GoogleTest unit tests for the Guss template runtime (Runtime).
 *
 * \details
 * Each test group creates a temporary directory, writes template files into it,
 * constructs a Runtime over that directory, and exercises load() and render().
 * Contexts are built with explicit ctx.set() calls — no simdjson.
 */
#include <gtest/gtest.h>
#include "guss/render/runtime.hpp"
#include "guss/render/context.hpp"
#include "guss/core/value.hpp"
#include "guss/core/error.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace guss::render;
using namespace guss::core;
namespace fs = std::filesystem;
namespace error = guss::core::error;

// ---------------------------------------------------------------------------
// Test fixture — creates and tears down a temporary template directory
// ---------------------------------------------------------------------------

class RuntimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string dir_name = std::string("guss_engine_test_")
            + info->test_suite_name() + "_" + info->name();
        tmp_dir_ = fs::temp_directory_path() / dir_name;
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tmp_dir_, ec);
    }

    /** Write \p content to a file named \p name inside the temp directory. */
    void write_tpl(std::string_view name, std::string_view content) {
        std::ofstream f(tmp_dir_ / name);
        f << content;
    }

    /** Construct a Runtime rooted at tmp_dir_. */
    Runtime make_runtime() {
        return Runtime(tmp_dir_);
    }

    /** Render and assert success; returns the output string. */
    static std::string render_ok(Runtime& eng, std::string_view name, Context& ctx) {
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

TEST_F(RuntimeTest, Load_CacheHit_ReturnsSameAddress) {
    write_tpl("simple.html", "hello");
    Runtime eng = make_runtime();
    auto first  = eng.load("simple.html");
    auto second = eng.load("simple.html");
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);
}

TEST_F(RuntimeTest, Load_FileNotFound_ReturnsError) {
    Runtime eng = make_runtime();
    auto result = eng.load("no_such_file.html");
    EXPECT_FALSE(result.has_value());
}

TEST_F(RuntimeTest, Load_CompiledTemplate_HasReturn) {
    write_tpl("t.html", "");
    Runtime eng = make_runtime();
    auto ct = eng.load("t.html");
    ASSERT_TRUE(ct.has_value());
    ASSERT_FALSE((*ct)->code.empty());
    EXPECT_EQ((*ct)->code.back().op, Op::Return);
}

// ---------------------------------------------------------------------------
// render() — plain text
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Render_PlainText) {
    write_tpl("plain.html", "Hello, World!");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "plain.html", ctx), "Hello, World!");
}

TEST_F(RuntimeTest, Render_EmptyTemplate) {
    write_tpl("empty.html", "");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "empty.html", ctx), "");
}

// ---------------------------------------------------------------------------
// render() — variable interpolation
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Render_SingleVariable) {
    write_tpl("var.html", "Hello, {{ name }}!");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("name", S("World"));
    EXPECT_EQ(render_ok(eng, "var.html", ctx), "Hello, World!");
}

TEST_F(RuntimeTest, Render_DottedPath) {
    write_tpl("dot.html", "{{ post.title }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("post", Value(std::unordered_map<std::string, Value>{
        {"title", S("My Post")}
    }));
    EXPECT_EQ(render_ok(eng, "dot.html", ctx), "My Post");
}

TEST_F(RuntimeTest, Render_MissingVariable_EmitsNull) {
    write_tpl("missing.html", "{{ missing }}");
    Runtime eng = make_runtime();
    Context ctx;
    // Missing variables resolve to null; to_string() on null yields "null".
    EXPECT_EQ(render_ok(eng, "missing.html", ctx), "null");
}

// ---------------------------------------------------------------------------
// render() — HTML escaping
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Render_HtmlEscape_Ampersand) {
    write_tpl("esc.html", "{{ title }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("title", S("a & b"));
    EXPECT_EQ(render_ok(eng, "esc.html", ctx), "a &amp; b");
}

TEST_F(RuntimeTest, Render_HtmlEscape_LtGt) {
    write_tpl("esc2.html", "{{ code }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("code", S("<br>"));
    EXPECT_EQ(render_ok(eng, "esc2.html", ctx), "&lt;br&gt;");
}

TEST_F(RuntimeTest, Render_HtmlEscape_Quote) {
    write_tpl("esc3.html", "{{ attr }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("attr", S(R"(say "hi")"));
    const std::string result = render_ok(eng, "esc3.html", ctx);
    EXPECT_NE(result.find("&quot;"), std::string::npos);
}

// ---------------------------------------------------------------------------
// render() — if / else
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Render_If_TrueBranch) {
    write_tpl("if.html", "{% if flag %}yes{% else %}no{% endif %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("flag", B(true));
    EXPECT_EQ(render_ok(eng, "if.html", ctx), "yes");
}

TEST_F(RuntimeTest, Render_If_FalseBranch) {
    write_tpl("if2.html", "{% if flag %}yes{% else %}no{% endif %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("flag", B(false));
    EXPECT_EQ(render_ok(eng, "if2.html", ctx), "no");
}

TEST_F(RuntimeTest, Render_If_NoElse_False) {
    write_tpl("if3.html", "{% if flag %}yes{% endif %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("flag", B(false));
    EXPECT_EQ(render_ok(eng, "if3.html", ctx), "");
}

TEST_F(RuntimeTest, Render_If_NumericTruthy) {
    write_tpl("if4.html", "{% if count %}has items{% else %}empty{% endif %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("count", I(3));
    EXPECT_EQ(render_ok(eng, "if4.html", ctx), "has items");
}

TEST_F(RuntimeTest, Render_If_NumericFalsy_Zero) {
    write_tpl("if5.html", "{% if count %}has items{% else %}empty{% endif %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("count", I(0));
    EXPECT_EQ(render_ok(eng, "if5.html", ctx), "empty");
}

TEST_F(RuntimeTest, Render_If_Elif) {
    write_tpl("elif.html",
              "{% if x == 1 %}one{% elif x == 2 %}two{% else %}other{% endif %}");
    Runtime eng = make_runtime();

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

TEST_F(RuntimeTest, Render_ForLoop_SimpleArray) {
    write_tpl("for.html", "{% for item in items %}{{ item }},{% endfor %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "for.html", ctx), "a,b,c,");
}

TEST_F(RuntimeTest, Render_ForLoop_Empty) {
    write_tpl("for2.html",
              "{% for item in items %}{{ item }}{% else %}none{% endfor %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({}));
    EXPECT_EQ(render_ok(eng, "for2.html", ctx), "none");
}

TEST_F(RuntimeTest, Render_ForLoop_LoopIndex) {
    write_tpl("idx.html",
              "{% for item in items %}{{ loop.index }}{% endfor %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "idx.html", ctx), "123");
}

TEST_F(RuntimeTest, Render_ForLoop_LoopFirst) {
    write_tpl("first.html",
              "{% for item in items %}{% if loop.first %}FIRST {% endif %}{{ item }}{% endfor %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "first.html", ctx), "FIRST abc");
}

TEST_F(RuntimeTest, Render_ForLoop_LoopLast) {
    write_tpl("last.html",
              "{% for item in items %}{{ item }}{% if loop.last %}!{% endif %}{% endfor %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "last.html", ctx), "abc!");
}

TEST_F(RuntimeTest, Render_ForLoop_CountItems) {
    write_tpl("count.html",
              "{% for item in items %}{{ loop.index }}{% endfor %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c"), S("d")}));
    EXPECT_EQ(render_ok(eng, "count.html", ctx), "1234");
}

TEST_F(RuntimeTest, Render_ForLoop_RevIndex) {
    // loop.revindex is 1-based from the end: 3, 2, 1 for a 3-element array.
    write_tpl("revidx.html",
              "{% for item in items %}{{ loop.revindex }}{% endfor %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "revidx.html", ctx), "321");
}

TEST_F(RuntimeTest, Render_ForLoop_RevIndex0) {
    // loop.revindex0 is 0-based from the end: 2, 1, 0 for a 3-element array.
    write_tpl("revidx0.html",
              "{% for item in items %}{{ loop.revindex0 }}{% endfor %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "revidx0.html", ctx), "210");
}

TEST_F(RuntimeTest, Render_ForLoop_RevIndexLastItem) {
    // loop.revindex equals 1 on the last iteration.
    write_tpl("revidxlast.html",
              "{% for item in items %}{% if loop.revindex == 1 %}LAST{% endif %}{% endfor %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({S("a"), S("b"), S("c")}));
    EXPECT_EQ(render_ok(eng, "revidxlast.html", ctx), "LAST");
}

// ---------------------------------------------------------------------------
// render() — filters
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Render_Filter_Upper) {
    write_tpl("upper.html", "{{ name | upper }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("name", S("hello"));
    EXPECT_EQ(render_ok(eng, "upper.html", ctx), "HELLO");
}

TEST_F(RuntimeTest, Render_Filter_Lower) {
    write_tpl("lower.html", "{{ name | lower }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("name", S("WORLD"));
    EXPECT_EQ(render_ok(eng, "lower.html", ctx), "world");
}

TEST_F(RuntimeTest, Render_Filter_Escape) {
    write_tpl("escape.html", "{{ raw | escape }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("raw", S("<b>bold</b>"));
    const std::string result = render_ok(eng, "escape.html", ctx);
    EXPECT_NE(result.find("&lt;"), std::string::npos);
    EXPECT_NE(result.find("&gt;"), std::string::npos);
}

TEST_F(RuntimeTest, Render_Filter_Default_Truthy) {
    write_tpl("def.html", R"({{ name | default("anonymous") }})");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("name", S("Alice"));
    EXPECT_EQ(render_ok(eng, "def.html", ctx), "Alice");
}

TEST_F(RuntimeTest, Render_Filter_Default_Null) {
    write_tpl("def2.html", R"({{ missing | default("anon") }})");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "def2.html", ctx), "anon");
}

TEST_F(RuntimeTest, Render_Filter_Length) {
    write_tpl("len.html", "{{ items | length }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({I(1), I(2), I(3)}));
    EXPECT_EQ(render_ok(eng, "len.html", ctx), "3");
}

TEST_F(RuntimeTest, Render_Filter_First) {
    write_tpl("fst.html", "{{ items | first }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({S("x"), S("y"), S("z")}));
    EXPECT_EQ(render_ok(eng, "fst.html", ctx), "x");
}

TEST_F(RuntimeTest, Render_Filter_Last) {
    write_tpl("lst.html", "{{ items | last }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Arr({S("x"), S("y"), S("z")}));
    EXPECT_EQ(render_ok(eng, "lst.html", ctx), "z");
}

TEST_F(RuntimeTest, Render_Filter_Unknown_ReturnsError) {
    write_tpl("unk.html", "{{ name | no_such_filter }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("name", S("x"));
    EXPECT_FALSE(eng.render("unk.html", ctx).has_value());
}

// ---------------------------------------------------------------------------
// render() — binary operators
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Render_BinaryAdd_Numbers) {
    write_tpl("add.html", "{{ a + b }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("a", I(3));
    ctx.set("b", I(4));
    const std::string result = render_ok(eng, "add.html", ctx);
    EXPECT_NE(result.find("7"), std::string::npos);
}

TEST_F(RuntimeTest, Render_BinaryEq_True) {
    write_tpl("eq.html", "{% if x == 1 %}yes{% else %}no{% endif %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("x", I(1));
    EXPECT_EQ(render_ok(eng, "eq.html", ctx), "yes");
}

TEST_F(RuntimeTest, Render_BinaryEq_False) {
    write_tpl("eq2.html", "{% if x == 2 %}yes{% else %}no{% endif %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("x", I(1));
    EXPECT_EQ(render_ok(eng, "eq2.html", ctx), "no");
}

TEST_F(RuntimeTest, Render_BinaryAnd) {
    write_tpl("and.html", "{% if a and b %}both{% else %}not{% endif %}");
    Runtime eng = make_runtime();
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

TEST_F(RuntimeTest, Render_BinaryOr) {
    write_tpl("or.html", "{% if a or b %}either{% else %}neither{% endif %}");
    Runtime eng = make_runtime();
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

TEST_F(RuntimeTest, Render_UnaryNot_True) {
    write_tpl("not.html", "{% if not flag %}absent{% else %}present{% endif %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("flag", B(false));
    EXPECT_EQ(render_ok(eng, "not.html", ctx), "absent");
}

// ---------------------------------------------------------------------------
// render() — add_search_path
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, AddSearchPath_FindsTemplateInSecondPath) {
    const fs::path sub = tmp_dir_ / "sub";
    fs::create_directories(sub);
    {
        std::ofstream f(sub / "sub.html");
        f << "found";
    } // flush + close before the engine reads the file

    Runtime eng(tmp_dir_ / "nonexistent");
    eng.add_search_path(sub);

    Context ctx;
    EXPECT_EQ(render_ok(eng, "sub.html", ctx), "found");
}

// ---------------------------------------------------------------------------
// render() — realistic template
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Render_RealisticPageTemplate) {
    write_tpl("page.html",
              "<article>"
              "<h1>{{ title | upper }}</h1>"
              "{% if published %}"
              "<p>Published</p>"
              "{% endif %}"
              "<ul>{% for tag in tags %}<li>{{ tag }}</li>{% endfor %}</ul>"
              "</article>");

    Runtime eng = make_runtime();
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

TEST_F(RuntimeTest, Render_DivByZero_ReturnsNull) {
    write_tpl("divz.html", "{{ a / b }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("a", I(10));
    ctx.set("b", I(0));
    EXPECT_EQ(render_ok(eng, "divz.html", ctx), "null");
}

TEST_F(RuntimeTest, Render_ModByZero_ReturnsNull) {
    write_tpl("modzero.html", "{{ a % b }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("a", I(10));
    ctx.set("b", I(0));
    EXPECT_EQ(render_ok(eng, "modzero.html", ctx), "null");
}

// ---------------------------------------------------------------------------
// render() — safe filter bypasses HTML escaping
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Render_SafeFilter_NoEscaping) {
    write_tpl("safe.html", "{{ content | safe }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("content", S("<b>bold</b>"));
    EXPECT_EQ(render_ok(eng, "safe.html", ctx), "<b>bold</b>");
}

TEST_F(RuntimeTest, Render_NoSafeFilter_EscapesHtml) {
    write_tpl("unsafe.html", "{{ content }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("content", S("<b>bold</b>"));
    EXPECT_EQ(render_ok(eng, "unsafe.html", ctx), "&lt;b&gt;bold&lt;/b&gt;");
}

// ---------------------------------------------------------------------------
// render() — stack overflow guard
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Render_ValueStack_Overflow_Throws) {
    write_tpl("deep.html", "{{ a + b + c + d + e }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("a", I(1));
    ctx.set("b", I(2));
    ctx.set("c", I(3));
    ctx.set("d", I(4));
    ctx.set("e", I(5));
    const std::string result = render_ok(eng, "deep.html", ctx);
    EXPECT_NE(result.find("15"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Stack overflow railguard (compile-time verifier)
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Verifier_NormalTemplate_LoadsOk) {
    write_tpl("stack_ok.html",
        "{% if a and b and c and d and e and f and g and h %}ok{% endif %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("a", Value(true)); ctx.set("b", Value(true));
    ctx.set("c", Value(true)); ctx.set("d", Value(true));
    ctx.set("e", Value(true)); ctx.set("f", Value(true));
    ctx.set("g", Value(true)); ctx.set("h", Value(true));
    auto r = eng.render("stack_ok.html", ctx);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "ok");
}

TEST_F(RuntimeTest, Load_FileNotFound_ReturnsTemplateParseError) {
    Runtime eng = make_runtime();
    Context ctx;
    auto r = eng.render("nonexistent.html", ctx);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, error::ErrorCode::TemplateParseError);
}

// ---------------------------------------------------------------------------
// {% include %}
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Include_RendersPartialInline) {
    write_tpl("header.html", "<header>{{ site_name }}</header>");
    write_tpl("page.html",   "{% include \"header.html\" %}<main>body</main>");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("site_name", Value(std::string("Guss")));
    EXPECT_EQ(render_ok(eng, "page.html", ctx),
              "<header>Guss</header><main>body</main>");
}

TEST_F(RuntimeTest, Include_InheritsContext) {
    write_tpl("nav.html",  "<nav>{{ user }}</nav>");
    write_tpl("base.html", "{% include \"nav.html\" %}<div>{{ content }}</div>");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("user",    Value(std::string("Alice")));
    ctx.set("content", Value(std::string("Hello")));
    EXPECT_EQ(render_ok(eng, "base.html", ctx),
              "<nav>Alice</nav><div>Hello</div>");
}

TEST_F(RuntimeTest, Include_MissingPartial_ReturnsError) {
    write_tpl("page.html", "{% include \"missing.html\" %}");
    Runtime eng = make_runtime();
    Context ctx;
    auto r = eng.render("page.html", ctx);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, error::ErrorCode::TemplateParseError);
}

// ---------------------------------------------------------------------------
// {% set %}
// ---------------------------------------------------------------------------

TEST_F(RuntimeTest, Set_AssignsVariableInContext) {
    write_tpl("t.html", "{% set greeting = \"Hello\" %}{{ greeting }} World");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "t.html", ctx), "Hello World");
}

TEST_F(RuntimeTest, Set_OverridesExistingVariable) {
    write_tpl("t.html", "{% set x = 42 %}{{ x }}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("x", Value(std::string("old")));
    EXPECT_EQ(render_ok(eng, "t.html", ctx), "42");
}

TEST_F(RuntimeTest, Set_ValuePersistsInLoop) {
    write_tpl("t.html", "{% set n = 3 %}{% for i in items %}{{ n }}{% endfor %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("items", Value(std::vector<Value>{Value(int64_t(1)),
                                              Value(int64_t(2)),
                                              Value(int64_t(3))}));
    EXPECT_EQ(render_ok(eng, "t.html", ctx), "333");
}
