/**
 * \file test_inheritance.cpp
 * \brief GoogleTest tests for template inheritance (extends / block).
 *
 * \details
 * Tests for full block override execution via the BlockOverrideMap mechanism.
 * Each test uses a unique temporary directory to avoid directory conflicts.
 */
#include <gtest/gtest.h>
#include "guss/render/runtime.hpp"
#include "guss/render/compiler.hpp"
#include "guss/render/context.hpp"
#include "guss/core/error.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace guss::render;
using namespace guss::core;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class InheritanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() /
                   ("guss_inherit_test_" + std::to_string(
                       std::hash<std::string>{}(::testing::UnitTest::GetInstance()
                           ->current_test_info()->name())));
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override {
        fs::remove_all(tmp_dir_);
    }

    void write_tpl(std::string_view name, std::string_view content) {
        std::ofstream f(tmp_dir_ / name);
        f << content;
    }

    Runtime make_runtime() {
        return Runtime(tmp_dir_);
    }

    static std::string render_ok(Runtime& eng, std::string_view name, Context& ctx) {
        auto r = eng.render(name, ctx);
        if (!r.has_value()) {
            ADD_FAILURE() << "render('" << name << "') failed: " << r.error().message;
            return {};
        }
        return std::move(*r);
    }

    fs::path tmp_dir_;
};

// ---------------------------------------------------------------------------
// Basic load — child that extends a parent must not crash
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Load_ChildExtendsParent_NoThrow) {
    write_tpl("base.html",
              "HEADER{% block content %}DEFAULT{% endblock %}FOOTER");
    write_tpl("child.html",
              "{% extends \"base.html\" %}"
              "{% block content %}CHILD{% endblock %}");

    Runtime eng = make_runtime();
    EXPECT_TRUE(eng.load("child.html").has_value());
}

TEST_F(InheritanceTest, Load_ParentIsAlsoCached) {
    write_tpl("base.html", "{% block body %}BASE{% endblock %}");
    write_tpl("child.html",
              "{% extends \"base.html\" %}{% block body %}CHILD{% endblock %}");

    Runtime eng = make_runtime();
    eng.load("child.html");

    // The parent should be reachable without file I/O (cache hit, no error).
    EXPECT_TRUE(eng.load("base.html").has_value());
}

// ---------------------------------------------------------------------------
// Render — base template renders its own default block content
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Render_ParentStandalone) {
    write_tpl("base.html",
              "START{% block content %}DEFAULT{% endblock %}END");
    Runtime eng = make_runtime();

    Context ctx;
    EXPECT_EQ(render_ok(eng, "base.html", ctx), "STARTDEFAULTEND");
}

TEST_F(InheritanceTest, Render_ChildExtendsParent_RendersOverride) {
    write_tpl("base.html",
              "BEFORE{% block main %}MAIN_DEFAULT{% endblock %}AFTER");
    write_tpl("child.html",
              "{% extends \"base.html\" %}"
              "{% block main %}OVERRIDDEN{% endblock %}");

    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("title", Value(std::string("Test")));

    EXPECT_EQ(render_ok(eng, "child.html", ctx), "BEFOREOVERRIDDENAFTER");
}

// ---------------------------------------------------------------------------
// Multi-block child
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Load_MultiBlock_AllBlocksCached) {
    write_tpl("layout.html",
              "{% block header %}H{% endblock %}"
              "{% block body %}B{% endblock %}"
              "{% block footer %}F{% endblock %}");
    write_tpl("page.html",
              "{% extends \"layout.html\" %}"
              "{% block header %}HEADER{% endblock %}"
              "{% block body %}BODY{% endblock %}");

    Runtime eng = make_runtime();
    // Loading the child must succeed even with multiple overridden blocks.
    EXPECT_TRUE(eng.load("page.html").has_value());
}

// ---------------------------------------------------------------------------
// Render — non-block content of base is emitted
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Render_ParentNonBlockContent_Emitted) {
    // A parent with text outside blocks — that text IS emitted because it
    // compiles to EmitText, which the engine handles directly.
    write_tpl("base2.html",
              "<!DOCTYPE html><html>{% block content %}{% endblock %}</html>");
    Runtime eng = make_runtime();
    Context ctx;
    const std::string result = render_ok(eng, "base2.html", ctx);
    // The surrounding HTML should be present.
    EXPECT_NE(result.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(result.find("</html>"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Nested inheritance — three levels must load without crashing
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Load_ThreeLevelChain_NoThrow) {
    write_tpl("root.html",   "{% block a %}ROOT{% endblock %}");
    write_tpl("mid.html",    "{% extends \"root.html\" %}{% block a %}MID{% endblock %}");
    write_tpl("leaf.html",   "{% extends \"mid.html\" %}{% block a %}LEAF{% endblock %}");

    Runtime eng = make_runtime();
    EXPECT_TRUE(eng.load("leaf.html").has_value());
}

// ---------------------------------------------------------------------------
// Context variables are available during render of a child template
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Render_ChildWithContext_VariablesResolved) {
    write_tpl("base3.html", "{{ title }}{% block main %}{% endblock %}");
    write_tpl("child3.html",
              "{% extends \"base3.html\" %}{% block main %}BODY{% endblock %}");

    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("title", Value(std::string("My Site")));

    EXPECT_EQ(render_ok(eng, "child3.html", ctx), "My SiteBODY");
    EXPECT_EQ(render_ok(eng, "base3.html", ctx), "My Site");
}

// ---------------------------------------------------------------------------
// Full inheritance render tests
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Render_ChildOverridesBlock_DefaultReplaced) {
    write_tpl("base.html",
              "BEFORE{% block main %}MAIN_DEFAULT{% endblock %}AFTER");
    write_tpl("child.html",
              "{% extends \"base.html\" %}"
              "{% block main %}OVERRIDDEN{% endblock %}");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "child.html", ctx),
              "BEFOREOVERRIDDENAFTER");
}

TEST_F(InheritanceTest, Render_ChildNoOverride_DefaultRendered) {
    write_tpl("base.html",
              "BEFORE{% block main %}MAIN_DEFAULT{% endblock %}AFTER");
    write_tpl("child.html",
              "{% extends \"base.html\" %}");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "child.html", ctx),
              "BEFOREMAIN_DEFAULTAFTER");
}

TEST_F(InheritanceTest, Render_MultiBlock_PartialOverride) {
    write_tpl("layout.html",
              "{% block header %}H{% endblock %}"
              "{% block body %}B{% endblock %}"
              "{% block footer %}F{% endblock %}");
    write_tpl("page.html",
              "{% extends \"layout.html\" %}"
              "{% block header %}HEADER{% endblock %}"
              "{% block body %}BODY{% endblock %}");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "page.html", ctx), "HEADERBODYF");
}

TEST_F(InheritanceTest, Render_ContextVariablesAvailableInOverride) {
    write_tpl("base.html", "{{ prefix }}{% block title %}Default{% endblock %}");
    write_tpl("child.html",
              "{% extends \"base.html\" %}"
              "{% block title %}{{ the_title }}{% endblock %}");
    Runtime eng = make_runtime();
    Context ctx;
    ctx.set("prefix",    Value(std::string("SITE: ")));
    ctx.set("the_title", Value(std::string("My Page")));
    EXPECT_EQ(render_ok(eng, "child.html", ctx), "SITE: My Page");
}

TEST_F(InheritanceTest, Render_ThreeLevelChain_DeepestWins) {
    write_tpl("root.html",  "{% block a %}ROOT{% endblock %}");
    write_tpl("mid.html",   "{% extends \"root.html\" %}{% block a %}MID{% endblock %}");
    write_tpl("leaf.html",  "{% extends \"mid.html\" %}{% block a %}LEAF{% endblock %}");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "leaf.html", ctx), "LEAF");
}

TEST_F(InheritanceTest, Render_BaseStandaloneStillWorks) {
    write_tpl("base.html",
              "START{% block content %}DEFAULT{% endblock %}END");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "base.html", ctx), "STARTDEFAULTEND");
}

// ---------------------------------------------------------------------------
// super() — renders parent block content at call site
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Super_AppendsParentContent) {
    write_tpl("base.html",  "{% block title %}BASE{% endblock %}");
    write_tpl("child.html",
              "{% extends \"base.html\" %}"
              "{% block title %}{{ super() }}_CHILD{% endblock %}");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "child.html", ctx), "BASE_CHILD");
}

TEST_F(InheritanceTest, Super_ThreeLevelChain) {
    write_tpl("root.html",  "{% block a %}ROOT{% endblock %}");
    write_tpl("mid.html",
              "{% extends \"root.html\" %}"
              "{% block a %}{{ super() }}_MID{% endblock %}");
    write_tpl("leaf.html",
              "{% extends \"mid.html\" %}"
              "{% block a %}{{ super() }}_LEAF{% endblock %}");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "leaf.html", ctx), "ROOT_MID_LEAF");
}

TEST_F(InheritanceTest, Super_OutsideBlock_EmitsEmpty) {
    // super() outside a block override (super_chain is empty) emits empty string
    write_tpl("alone.html", "{{ super() }}END");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "alone.html", ctx), "END");
}

// ---------------------------------------------------------------------------
// Error path — extends with a non-existent parent must return an error
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Load_MissingParent_ReturnsError) {
    write_tpl("orphan.html",
              "{% extends \"nonexistent.html\" %}{% block body %}X{% endblock %}");
    Runtime eng = make_runtime();
    auto r = eng.load("orphan.html");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, guss::core::error::ErrorCode::TemplateParseError);
}

TEST_F(InheritanceTest, Super_WithHtmlContent_NotDoubleEscaped) {
    write_tpl("base.html",
              "{% block content %}<p>Hello</p>{% endblock %}");
    write_tpl("child.html",
              "{% extends \"base.html\" %}"
              "{% block content %}{{ super() }}<em>extra</em>{% endblock %}");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "child.html", ctx), "<p>Hello</p><em>extra</em>");
}

TEST_F(InheritanceTest, Super_WithFilter_AppliesFilter) {
    // Use HTML content in the parent block. super() fetches the raw rendered
    // HTML from the parent; applying | upper transforms the string including
    // the tag characters. The result goes through Op::Emit (auto-escape),
    // so angle brackets are escaped in the final output.  If super() were
    // accidentally double-escaping (going through Emit twice), the filter
    // would receive "&lt;b&gt;base&lt;/b&gt;" and upper() would produce a
    // different string than "&lt;B&gt;BASE&lt;/B&gt;".
    write_tpl("base.html",  "{% block title %}<b>base</b>{% endblock %}");
    write_tpl("child.html",
              "{% extends \"base.html\" %}"
              "{% block title %}{{ super() | upper }}{% endblock %}");
    Runtime eng = make_runtime();
    Context ctx;
    EXPECT_EQ(render_ok(eng, "child.html", ctx), "&lt;B&gt;BASE&lt;/B&gt;");
}
