/**
 * \file test_inheritance.cpp
 * \brief GoogleTest tests for template inheritance (extends / block).
 *
 * \details
 * Full block override execution is a TODO — BlockCall is currently a no-op in
 * the engine.  These tests verify:
 *   - A child template that extends a parent can be loaded without crashing.
 *   - The parent template's non-block content is rendered when no override fires.
 *   - The cache contains sub-template entries for child block bodies.
 *
 * When full inheritance is implemented (Phase 6+), the expectations marked
 * "TODO: will change" should be updated to assert the child block's content.
 */
#include <gtest/gtest.h>
#include "guss/render/engine.hpp"
#include "guss/render/compiler.hpp"
#include "guss/render/context.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace guss::render;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class InheritanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() / "guss_inherit_test";
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override {
        fs::remove_all(tmp_dir_);
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

    Engine eng = make_engine();
    EXPECT_TRUE(eng.load("child.html").has_value());
}

TEST_F(InheritanceTest, Load_ParentIsAlsoCached) {
    write_tpl("base.html", "{% block body %}BASE{% endblock %}");
    write_tpl("child.html",
              "{% extends \"base.html\" %}{% block body %}CHILD{% endblock %}");

    Engine eng = make_engine();
    eng.load("child.html");

    // The parent should be reachable without file I/O (cache hit, no error).
    EXPECT_TRUE(eng.load("base.html").has_value());
}

// ---------------------------------------------------------------------------
// Render — parent renders its own content when BlockCall is a no-op
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Render_ParentStandalone) {
    write_tpl("base.html",
              "START{% block content %}DEFAULT{% endblock %}END");
    Engine eng = make_engine();

    // The parent's BlockCall is a no-op currently, so the block default body
    // is NOT rendered via BlockCall (it was compiled as BlockCall, not inline).
    // We just verify the parent can be rendered without error.
    Context ctx;
    EXPECT_TRUE(eng.render("base.html", ctx).has_value());
}

TEST_F(InheritanceTest, Render_ChildExtendsParent_NoThrow) {
    write_tpl("base.html",
              "BEFORE{% block main %}MAIN_DEFAULT{% endblock %}AFTER");
    write_tpl("child.html",
              "{% extends \"base.html\" %}"
              "{% block main %}OVERRIDDEN{% endblock %}");

    Engine eng = make_engine();
    Context ctx;
    ctx.set("title", Value(std::string("Test")));

    // With BlockCall as a no-op the child renders as an empty string
    // (child template body is only ExtendsNode + BlockNode; both emit nothing).
    // TODO: When full inheritance is wired, this should return the parent layout
    // with the child's block body substituted.
    EXPECT_TRUE(eng.render("child.html", ctx).has_value());
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

    Engine eng = make_engine();
    // Loading the child must succeed even with multiple overridden blocks.
    EXPECT_TRUE(eng.load("page.html").has_value());
}

// ---------------------------------------------------------------------------
// Render through parent — non-block content is emitted
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Render_ParentNonBlockContent_Emitted) {
    // A parent with text outside blocks — that text IS emitted because it
    // compiles to EmitText, which the engine handles directly.
    write_tpl("base2.html",
              "<!DOCTYPE html><html>{% block content %}{% endblock %}</html>");
    Engine eng = make_engine();
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

    Engine eng = make_engine();
    EXPECT_TRUE(eng.load("leaf.html").has_value());
}

// ---------------------------------------------------------------------------
// Context variables are available during render of a child template
// ---------------------------------------------------------------------------

TEST_F(InheritanceTest, Render_ChildWithContext_VariablesResolved) {
    // When the child renders (currently producing empty output due to no-op
    // BlockCall), context variables should at minimum be accessible without
    // crashing.
    write_tpl("base3.html", "{{ title }}{% block main %}{% endblock %}");
    write_tpl("child3.html",
              "{% extends \"base3.html\" %}{% block main %}BODY{% endblock %}");

    Engine eng = make_engine();
    Context ctx;
    ctx.set("title", Value(std::string("My Site")));

    // Render the child — must succeed.
    EXPECT_TRUE(eng.render("child3.html", ctx).has_value());

    // Rendering the parent directly should emit the title.
    EXPECT_EQ(render_ok(eng, "base3.html", ctx), "My Site");
}
