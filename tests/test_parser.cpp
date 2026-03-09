/**
 * \file test_parser.cpp
 * \brief GoogleTest unit tests for the Guss template parser.
 */
#include <gtest/gtest.h>
#include "guss/render/parser.hpp"
#include "guss/render/lexer.hpp"
#include "guss/render/ast.hpp"

#include <string>
#include <string_view>
#include <vector>

using namespace guss::render;
using namespace guss::render::lexer;
using namespace guss::render::ast;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Lex \p src and run the parser, returning the completed ast::Template.
 *
 * The source is moved into Template::source BEFORE lexing so that all
 * string_view fields inside the AST point into the final stable buffer and
 * remain valid for the entire lifetime of the returned Template.
 */
static ast::Template parse_template(std::string src,
                                    std::string_view name = "<test>") {
    ast::Template tmpl;
    tmpl.source = std::move(src);

    // Lex from the stable buffer owned by tmpl.source.
    Lexer lex(tmpl.source, name);
    auto tokens = lex.tokenize();

    // Parse: the parser produces its own Template (sans source).
    Parser parser(std::move(tokens), std::string(name));
    ast::Template parsed = parser.parse();

    // Merge the parsed tree into tmpl (which owns the source).
    tmpl.nodes  = std::move(parsed.nodes);
    tmpl.parent = std::move(parsed.parent);
    tmpl.blocks = std::move(parsed.blocks);

    return tmpl;
}

// ---------------------------------------------------------------------------
// Empty template
// ---------------------------------------------------------------------------

TEST(Parser, EmptySource) {
    auto tmpl = parse_template("");
    EXPECT_TRUE(tmpl.nodes.empty());
    EXPECT_FALSE(tmpl.parent.has_value());
    EXPECT_TRUE(tmpl.blocks.empty());
}

// ---------------------------------------------------------------------------
// Plain text node
// ---------------------------------------------------------------------------

TEST(Parser, PlainText) {
    auto tmpl = parse_template("hello world");
    ASSERT_EQ(tmpl.nodes.size(), 1u);

    const auto* tn = std::get_if<std::unique_ptr<TextNode>>(&tmpl.nodes[0]);
    ASSERT_NE(tn, nullptr);
    EXPECT_EQ((*tn)->text, "hello world");
}

TEST(Parser, MultipleTextSegments) {
    // Two text segments separated by a comment (comment produces no tokens).
    auto tmpl = parse_template("before{# ignored #}after");
    ASSERT_EQ(tmpl.nodes.size(), 2u);

    const auto* t0 = std::get_if<std::unique_ptr<TextNode>>(&tmpl.nodes[0]);
    const auto* t1 = std::get_if<std::unique_ptr<TextNode>>(&tmpl.nodes[1]);
    ASSERT_NE(t0, nullptr);
    ASSERT_NE(t1, nullptr);
    EXPECT_EQ((*t0)->text, "before");
    EXPECT_EQ((*t1)->text, "after");
}

// ---------------------------------------------------------------------------
// Expression nodes  {{ expr }}
// ---------------------------------------------------------------------------

TEST(Parser, SimpleExprNode_Variable) {
    auto tmpl = parse_template("{{ name }}");
    ASSERT_EQ(tmpl.nodes.size(), 1u);

    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);

    const auto* var = std::get_if<std::unique_ptr<Variable>>(&(*en)->expr);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ((*var)->path, "name");
}

TEST(Parser, DottedPathVariable) {
    auto tmpl = parse_template("{{ post.title }}");
    ASSERT_EQ(tmpl.nodes.size(), 1u);

    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* var = std::get_if<std::unique_ptr<Variable>>(&(*en)->expr);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ((*var)->path, "post.title");
}

TEST(Parser, DeepDottedPathVariable) {
    auto tmpl = parse_template("{{ a.b.c.d }}");
    ASSERT_EQ(tmpl.nodes.size(), 1u);

    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* var = std::get_if<std::unique_ptr<Variable>>(&(*en)->expr);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ((*var)->path, "a.b.c.d");
}

TEST(Parser, IntLiteralExpr) {
    auto tmpl = parse_template("{{ 42 }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* il = std::get_if<std::unique_ptr<IntLit>>(&(*en)->expr);
    ASSERT_NE(il, nullptr);
    EXPECT_EQ((*il)->value, 42);
}

TEST(Parser, FloatLiteralExpr) {
    auto tmpl = parse_template("{{ 3.14 }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* fl = std::get_if<std::unique_ptr<FloatLit>>(&(*en)->expr);
    ASSERT_NE(fl, nullptr);
    EXPECT_DOUBLE_EQ((*fl)->value, 3.14);
}

TEST(Parser, StringLiteralExpr) {
    auto tmpl = parse_template(R"({{ "hello" }})");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* sl = std::get_if<std::unique_ptr<StringLit>>(&(*en)->expr);
    ASSERT_NE(sl, nullptr);
    EXPECT_EQ((*sl)->value, "hello");
}

TEST(Parser, BoolLiteralTrue) {
    auto tmpl = parse_template("{{ true }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bl = std::get_if<std::unique_ptr<BoolLit>>(&(*en)->expr);
    ASSERT_NE(bl, nullptr);
    EXPECT_TRUE((*bl)->value);
}

TEST(Parser, BoolLiteralFalse) {
    auto tmpl = parse_template("{{ false }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bl = std::get_if<std::unique_ptr<BoolLit>>(&(*en)->expr);
    ASSERT_NE(bl, nullptr);
    EXPECT_FALSE((*bl)->value);
}

// ---------------------------------------------------------------------------
// Filter chain
// ---------------------------------------------------------------------------

TEST(Parser, SimpleFilter_NoArgs) {
    // {{ name | upper }}
    // Expected: BinaryOp(Pipe, Variable{name}, Filter{upper, []})
    auto tmpl = parse_template("{{ name | upper }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);

    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Pipe);

    // Left should be Variable{name}
    const auto* var = std::get_if<std::unique_ptr<Variable>>(&(*bop)->left);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ((*var)->path, "name");

    // Right should be Filter{upper, []}
    const auto* filt = std::get_if<std::unique_ptr<Filter>>(&(*bop)->right);
    ASSERT_NE(filt, nullptr);
    EXPECT_EQ((*filt)->name, "upper");
    EXPECT_TRUE((*filt)->args.empty());
}

TEST(Parser, FilterWithArgs) {
    // {{ value | truncate(80) }}
    auto tmpl = parse_template("{{ value | truncate(80) }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);

    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Pipe);

    const auto* filt = std::get_if<std::unique_ptr<Filter>>(&(*bop)->right);
    ASSERT_NE(filt, nullptr);
    EXPECT_EQ((*filt)->name, "truncate");
    ASSERT_EQ((*filt)->args.size(), 1u);

    const auto* arg_il = std::get_if<std::unique_ptr<IntLit>>(&(*filt)->args[0]);
    ASSERT_NE(arg_il, nullptr);
    EXPECT_EQ((*arg_il)->value, 80);
}

TEST(Parser, FilterChain_TwoFilters) {
    // {{ name | lower | upper }}
    // Tree: BinaryOp(Pipe,
    //          BinaryOp(Pipe, Variable{name}, Filter{lower, []}),
    //          Filter{upper, []})
    auto tmpl = parse_template("{{ name | lower | upper }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);

    const auto* outer = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ((*outer)->op, TokenType::Pipe);

    // Outer right: Filter{upper}
    const auto* filt_upper =
        std::get_if<std::unique_ptr<Filter>>(&(*outer)->right);
    ASSERT_NE(filt_upper, nullptr);
    EXPECT_EQ((*filt_upper)->name, "upper");

    // Outer left: BinaryOp(Pipe, Variable{name}, Filter{lower})
    const auto* inner =
        std::get_if<std::unique_ptr<BinaryOp>>(&(*outer)->left);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ((*inner)->op, TokenType::Pipe);

    const auto* var = std::get_if<std::unique_ptr<Variable>>(&(*inner)->left);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ((*var)->path, "name");

    const auto* filt_lower =
        std::get_if<std::unique_ptr<Filter>>(&(*inner)->right);
    ASSERT_NE(filt_lower, nullptr);
    EXPECT_EQ((*filt_lower)->name, "lower");
}

// ---------------------------------------------------------------------------
// Unary operators
// ---------------------------------------------------------------------------

TEST(Parser, UnaryNot) {
    auto tmpl = parse_template("{{ not x }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* uop = std::get_if<std::unique_ptr<UnaryOp>>(&(*en)->expr);
    ASSERT_NE(uop, nullptr);
    EXPECT_EQ((*uop)->op, TokenType::Keyword_Not);
}

TEST(Parser, UnaryMinus) {
    auto tmpl = parse_template("{{ -42 }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* uop = std::get_if<std::unique_ptr<UnaryOp>>(&(*en)->expr);
    ASSERT_NE(uop, nullptr);
    EXPECT_EQ((*uop)->op, TokenType::Op_Sub);
}

// ---------------------------------------------------------------------------
// Binary operators
// ---------------------------------------------------------------------------

TEST(Parser, BinaryAdd) {
    auto tmpl = parse_template("{{ a + b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Op_Add);
}

TEST(Parser, BinaryEq) {
    auto tmpl = parse_template("{{ a == b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Op_Eq);
}

TEST(Parser, BinaryNe) {
    auto tmpl = parse_template("{{ a != b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Op_Ne);
}

TEST(Parser, BinaryLt) {
    auto tmpl = parse_template("{{ a < b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Op_Lt);
}

TEST(Parser, BinaryGt) {
    auto tmpl = parse_template("{{ a > b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Op_Gt);
}

TEST(Parser, BinaryLe) {
    auto tmpl = parse_template("{{ a <= b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Op_Le);
}

TEST(Parser, BinaryGe) {
    auto tmpl = parse_template("{{ a >= b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Op_Ge);
}

TEST(Parser, BinaryAnd) {
    auto tmpl = parse_template("{{ a and b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Keyword_And);
}

TEST(Parser, BinaryOr) {
    auto tmpl = parse_template("{{ a or b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Keyword_Or);
}

TEST(Parser, BinaryMul) {
    auto tmpl = parse_template("{{ a * b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Op_Mul);
}

TEST(Parser, BinaryDiv) {
    auto tmpl = parse_template("{{ a / b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Op_Div);
}

TEST(Parser, BinaryMod) {
    auto tmpl = parse_template("{{ a % b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Op_Mod);
}

// ---------------------------------------------------------------------------
// Expression precedence tests
// ---------------------------------------------------------------------------

// "not a or b and c"  →  "(not a) or (b and c)"
// Tree:  BinaryOp(Or,
//            UnaryOp(Not, Variable{a}),
//            BinaryOp(And, Variable{b}, Variable{c}))
TEST(Parser, Precedence_NotOrAnd) {
    auto tmpl = parse_template("{{ not a or b and c }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);

    // Top-level: Or
    const auto* or_op = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(or_op, nullptr);
    EXPECT_EQ((*or_op)->op, TokenType::Keyword_Or);

    // Left of Or: UnaryOp(Not, a)
    const auto* not_op = std::get_if<std::unique_ptr<UnaryOp>>(&(*or_op)->left);
    ASSERT_NE(not_op, nullptr);
    EXPECT_EQ((*not_op)->op, TokenType::Keyword_Not);

    const auto* var_a = std::get_if<std::unique_ptr<Variable>>(&(*not_op)->operand);
    ASSERT_NE(var_a, nullptr);
    EXPECT_EQ((*var_a)->path, "a");

    // Right of Or: BinaryOp(And, b, c)
    const auto* and_op =
        std::get_if<std::unique_ptr<BinaryOp>>(&(*or_op)->right);
    ASSERT_NE(and_op, nullptr);
    EXPECT_EQ((*and_op)->op, TokenType::Keyword_And);

    const auto* var_b = std::get_if<std::unique_ptr<Variable>>(&(*and_op)->left);
    ASSERT_NE(var_b, nullptr);
    EXPECT_EQ((*var_b)->path, "b");

    const auto* var_c = std::get_if<std::unique_ptr<Variable>>(&(*and_op)->right);
    ASSERT_NE(var_c, nullptr);
    EXPECT_EQ((*var_c)->path, "c");
}

// "a + b * c"  →  "a + (b * c)"
TEST(Parser, Precedence_AddMul) {
    auto tmpl = parse_template("{{ a + b * c }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);

    // Top-level: Add
    const auto* add_op = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(add_op, nullptr);
    EXPECT_EQ((*add_op)->op, TokenType::Op_Add);

    // Left: Variable{a}
    const auto* var_a = std::get_if<std::unique_ptr<Variable>>(&(*add_op)->left);
    ASSERT_NE(var_a, nullptr);
    EXPECT_EQ((*var_a)->path, "a");

    // Right: BinaryOp(Mul, b, c)
    const auto* mul_op =
        std::get_if<std::unique_ptr<BinaryOp>>(&(*add_op)->right);
    ASSERT_NE(mul_op, nullptr);
    EXPECT_EQ((*mul_op)->op, TokenType::Op_Mul);
}

// "a - b - c"  →  "(a - b) - c"  (left-associative)
TEST(Parser, Precedence_LeftAssociativeSub) {
    auto tmpl = parse_template("{{ a - b - c }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);

    // Top-level: Sub
    const auto* outer = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ((*outer)->op, TokenType::Op_Sub);

    // Right: Variable{c}
    const auto* var_c = std::get_if<std::unique_ptr<Variable>>(&(*outer)->right);
    ASSERT_NE(var_c, nullptr);
    EXPECT_EQ((*var_c)->path, "c");

    // Left: BinaryOp(Sub, a, b)
    const auto* inner = std::get_if<std::unique_ptr<BinaryOp>>(&(*outer)->left);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ((*inner)->op, TokenType::Op_Sub);
}

// "a or b or c"  →  "(a or b) or c"  (left-associative)
TEST(Parser, Precedence_LeftAssociativeOr) {
    auto tmpl = parse_template("{{ a or b or c }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);

    const auto* outer = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ((*outer)->op, TokenType::Keyword_Or);

    // Right should be Variable{c}
    const auto* var_c = std::get_if<std::unique_ptr<Variable>>(&(*outer)->right);
    ASSERT_NE(var_c, nullptr);
    EXPECT_EQ((*var_c)->path, "c");

    // Left: BinaryOp(Or, a, b)
    const auto* inner = std::get_if<std::unique_ptr<BinaryOp>>(&(*outer)->left);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ((*inner)->op, TokenType::Keyword_Or);
}

// Comparison is non-chaining: "a < b < c" — after parsing "a < b" the parser
// returns without consuming the second "<", so the outer expression is just "a < b"
// and the extra "< c" tokens remain unprocessed at ExprClose level.
// This test confirms that "{{ a < b }}" parses as BinaryOp(Lt, a, b).
TEST(Parser, ComparisonNonChaining) {
    auto tmpl = parse_template("{{ a < b }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* bop = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(bop, nullptr);
    EXPECT_EQ((*bop)->op, TokenType::Op_Lt);
}

// Parenthesised expression overrides precedence.
// "(a + b) * c"  top-level must be Mul
TEST(Parser, Parentheses_OverridePrecedence) {
    auto tmpl = parse_template("{{ (a + b) * c }}");
    const auto* en = std::get_if<std::unique_ptr<ExprNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    const auto* mul_op = std::get_if<std::unique_ptr<BinaryOp>>(&(*en)->expr);
    ASSERT_NE(mul_op, nullptr);
    EXPECT_EQ((*mul_op)->op, TokenType::Op_Mul);

    // Left: BinaryOp(Add, a, b)
    const auto* add_op =
        std::get_if<std::unique_ptr<BinaryOp>>(&(*mul_op)->left);
    ASSERT_NE(add_op, nullptr);
    EXPECT_EQ((*add_op)->op, TokenType::Op_Add);
}

// ---------------------------------------------------------------------------
// For node
// ---------------------------------------------------------------------------

TEST(Parser, ForNode_Basic) {
    auto tmpl = parse_template(
        "{% for item in items %}{{ item }}{% endfor %}");
    ASSERT_EQ(tmpl.nodes.size(), 1u);

    const auto* fn = std::get_if<std::unique_ptr<ForNode>>(&tmpl.nodes[0]);
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ((*fn)->var_name, "item");
    EXPECT_EQ((*fn)->body.size(), 1u);
    EXPECT_TRUE((*fn)->else_body.empty());

    // Iterable: Variable{items}
    const auto* iter =
        std::get_if<std::unique_ptr<Variable>>(&(*fn)->iterable);
    ASSERT_NE(iter, nullptr);
    EXPECT_EQ((*iter)->path, "items");
}

TEST(Parser, ForNode_WithBody) {
    auto tmpl = parse_template(
        "{% for x in list %}<li>{{ x }}</li>{% endfor %}");
    const auto* fn = std::get_if<std::unique_ptr<ForNode>>(&tmpl.nodes[0]);
    ASSERT_NE(fn, nullptr);
    // Body has 3 nodes: Text("<li>"), ExprNode(x), Text("</li>")
    EXPECT_EQ((*fn)->body.size(), 3u);
}

TEST(Parser, ForNode_WithElse) {
    auto tmpl = parse_template(
        "{% for x in items %}{{ x }}{% else %}empty{% endfor %}");
    const auto* fn = std::get_if<std::unique_ptr<ForNode>>(&tmpl.nodes[0]);
    ASSERT_NE(fn, nullptr);

    EXPECT_EQ((*fn)->body.size(), 1u);
    ASSERT_EQ((*fn)->else_body.size(), 1u);

    const auto* et = std::get_if<std::unique_ptr<TextNode>>(
        &(*fn)->else_body[0]);
    ASSERT_NE(et, nullptr);
    EXPECT_EQ((*et)->text, "empty");
}

TEST(Parser, ForNode_DottedIterable) {
    auto tmpl = parse_template(
        "{% for tag in post.tags %}{{ tag.name }}{% endfor %}");
    const auto* fn = std::get_if<std::unique_ptr<ForNode>>(&tmpl.nodes[0]);
    ASSERT_NE(fn, nullptr);

    const auto* iter =
        std::get_if<std::unique_ptr<Variable>>(&(*fn)->iterable);
    ASSERT_NE(iter, nullptr);
    EXPECT_EQ((*iter)->path, "post.tags");
}

// ---------------------------------------------------------------------------
// If node
// ---------------------------------------------------------------------------

TEST(Parser, IfNode_Simple) {
    auto tmpl = parse_template("{% if x %}yes{% endif %}");
    ASSERT_EQ(tmpl.nodes.size(), 1u);

    const auto* in = std::get_if<std::unique_ptr<IfNode>>(&tmpl.nodes[0]);
    ASSERT_NE(in, nullptr);
    ASSERT_EQ((*in)->branches.size(), 1u);
    EXPECT_EQ((*in)->branches[0].body.size(), 1u);
    EXPECT_TRUE((*in)->else_body.empty());
}

TEST(Parser, IfNode_WithElse) {
    auto tmpl = parse_template("{% if x %}yes{% else %}no{% endif %}");
    const auto* in = std::get_if<std::unique_ptr<IfNode>>(&tmpl.nodes[0]);
    ASSERT_NE(in, nullptr);

    ASSERT_EQ((*in)->branches.size(), 1u);
    ASSERT_EQ((*in)->else_body.size(), 1u);
}

TEST(Parser, IfNode_WithElif) {
    auto tmpl = parse_template(
        "{% if a %}A{% elif b %}B{% elif c %}C{% endif %}");
    const auto* in = std::get_if<std::unique_ptr<IfNode>>(&tmpl.nodes[0]);
    ASSERT_NE(in, nullptr);

    ASSERT_EQ((*in)->branches.size(), 3u);
    EXPECT_TRUE((*in)->else_body.empty());
}

TEST(Parser, IfNode_ElifAndElse) {
    auto tmpl = parse_template(
        "{% if a %}A{% elif b %}B{% else %}C{% endif %}");
    const auto* in = std::get_if<std::unique_ptr<IfNode>>(&tmpl.nodes[0]);
    ASSERT_NE(in, nullptr);

    ASSERT_EQ((*in)->branches.size(), 2u);
    ASSERT_EQ((*in)->else_body.size(), 1u);

    const auto* ct = std::get_if<std::unique_ptr<TextNode>>(
        &(*in)->else_body[0]);
    ASSERT_NE(ct, nullptr);
    EXPECT_EQ((*ct)->text, "C");
}

TEST(Parser, IfNode_ConditionIsExpression) {
    auto tmpl = parse_template("{% if x == 1 %}ok{% endif %}");
    const auto* in = std::get_if<std::unique_ptr<IfNode>>(&tmpl.nodes[0]);
    ASSERT_NE(in, nullptr);

    ASSERT_EQ((*in)->branches.size(), 1u);
    const auto* cond =
        std::get_if<std::unique_ptr<BinaryOp>>(&(*in)->branches[0].condition);
    ASSERT_NE(cond, nullptr);
    EXPECT_EQ((*cond)->op, TokenType::Op_Eq);
}

// ---------------------------------------------------------------------------
// Block node
// ---------------------------------------------------------------------------

TEST(Parser, BlockNode_Empty) {
    auto tmpl = parse_template("{% block content %}{% endblock %}");
    ASSERT_EQ(tmpl.nodes.size(), 1u);

    const auto* bn = std::get_if<std::unique_ptr<BlockNode>>(&tmpl.nodes[0]);
    ASSERT_NE(bn, nullptr);
    EXPECT_EQ((*bn)->name, "content");
    EXPECT_TRUE((*bn)->body.empty());
}

TEST(Parser, BlockNode_WithBody) {
    auto tmpl = parse_template("{% block title %}My Title{% endblock %}");
    const auto* bn = std::get_if<std::unique_ptr<BlockNode>>(&tmpl.nodes[0]);
    ASSERT_NE(bn, nullptr);
    EXPECT_EQ((*bn)->name, "title");
    ASSERT_EQ((*bn)->body.size(), 1u);

    const auto* tn = std::get_if<std::unique_ptr<TextNode>>(
        &(*bn)->body[0]);
    ASSERT_NE(tn, nullptr);
    EXPECT_EQ((*tn)->text, "My Title");
}

TEST(Parser, BlockNode_InBlocksMap) {
    auto tmpl = parse_template("{% block header %}H{% endblock %}");
    ASSERT_EQ(tmpl.blocks.count("header"), 1u);

    BlockNode* ptr = tmpl.blocks.at("header");
    EXPECT_EQ(ptr->name, "header");
}

TEST(Parser, BlocksMap_MultipleBlocks) {
    auto tmpl = parse_template(
        "{% block a %}A{% endblock %}{% block b %}B{% endblock %}");
    EXPECT_EQ(tmpl.blocks.count("a"), 1u);
    EXPECT_EQ(tmpl.blocks.count("b"), 1u);
}

// ---------------------------------------------------------------------------
// Extends node
// ---------------------------------------------------------------------------

TEST(Parser, ExtendsNode_SetsParent) {
    auto tmpl = parse_template(R"({% extends "base.html" %})");
    ASSERT_TRUE(tmpl.parent.has_value());
    EXPECT_EQ(tmpl.parent.value(), "base.html");

    ASSERT_EQ(tmpl.nodes.size(), 1u);
    const auto* en =
        std::get_if<std::unique_ptr<ExtendsNode>>(&tmpl.nodes[0]);
    ASSERT_NE(en, nullptr);
    EXPECT_EQ((*en)->parent_template, "base.html");
}

TEST(Parser, ExtendsNode_FirstNodeOnly) {
    // extends before blocks is fine.
    auto tmpl = parse_template(
        R"({% extends "base.html" %}{% block content %}X{% endblock %})");
    ASSERT_TRUE(tmpl.parent.has_value());
    EXPECT_EQ(tmpl.nodes.size(), 2u);
}

TEST(Parser, ExtendsNode_MustBeFirst_Error) {
    // A text node then extends should fail.
    EXPECT_THROW(
        parse_template(R"(text{% extends "base.html" %})"),
        std::runtime_error);
}

TEST(Parser, ExtendsNode_AfterWhitespaceText_IsAllowed) {
    // Pure whitespace text before extends is treated as TextNode, which is
    // allowed before extends.
    EXPECT_NO_THROW(parse_template(R"(   {% extends "base.html" %})"));
}

// ---------------------------------------------------------------------------
// Include node
// ---------------------------------------------------------------------------

TEST(Parser, IncludeNode) {
    auto tmpl = parse_template(R"({% include "partial.html" %})");
    ASSERT_EQ(tmpl.nodes.size(), 1u);

    const auto* in =
        std::get_if<std::unique_ptr<IncludeNode>>(&tmpl.nodes[0]);
    ASSERT_NE(in, nullptr);
    EXPECT_EQ((*in)->template_name, "partial.html");
}

// ---------------------------------------------------------------------------
// Nested structures
// ---------------------------------------------------------------------------

TEST(Parser, NestedForInIf) {
    auto tmpl = parse_template(
        "{% if x %}"
        "{% for i in items %}{{ i }}{% endfor %}"
        "{% endif %}");

    const auto* in = std::get_if<std::unique_ptr<IfNode>>(&tmpl.nodes[0]);
    ASSERT_NE(in, nullptr);
    ASSERT_EQ((*in)->branches.size(), 1u);
    ASSERT_EQ((*in)->branches[0].body.size(), 1u);

    const auto* fn = std::get_if<std::unique_ptr<ForNode>>(
        &(*in)->branches[0].body[0]);
    ASSERT_NE(fn, nullptr);
}

TEST(Parser, BlockContainsForLoop) {
    auto tmpl = parse_template(
        "{% block items %}"
        "{% for x in xs %}{{ x }}{% endfor %}"
        "{% endblock %}");

    const auto* bn = std::get_if<std::unique_ptr<BlockNode>>(&tmpl.nodes[0]);
    ASSERT_NE(bn, nullptr);
    ASSERT_EQ((*bn)->body.size(), 1u);

    const auto* fn =
        std::get_if<std::unique_ptr<ForNode>>(&(*bn)->body[0]);
    ASSERT_NE(fn, nullptr);
}

TEST(Parser, BlocksMap_NestedBlock) {
    // Nested blocks should also be collected into the blocks map.
    auto tmpl = parse_template(
        "{% block outer %}"
        "{% block inner %}X{% endblock %}"
        "{% endblock %}");

    EXPECT_EQ(tmpl.blocks.count("outer"), 1u);
    EXPECT_EQ(tmpl.blocks.count("inner"), 1u);
}

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

TEST(Parser, Error_UnmatchedEndFor) {
    EXPECT_THROW(parse_template("{% endfor %}"), std::runtime_error);
}

TEST(Parser, Error_UnmatchedEndIf) {
    EXPECT_THROW(parse_template("{% endif %}"), std::runtime_error);
}

TEST(Parser, Error_UnmatchedEndBlock) {
    EXPECT_THROW(parse_template("{% endblock %}"), std::runtime_error);
}

TEST(Parser, Error_UnclosedFor) {
    EXPECT_THROW(parse_template("{% for x in items %}{{ x }}"),
                 std::runtime_error);
}

TEST(Parser, Error_UnclosedIf) {
    EXPECT_THROW(parse_template("{% if x %}yes"),
                 std::runtime_error);
}

TEST(Parser, Error_UnclosedBlock) {
    EXPECT_THROW(parse_template("{% block foo %}content"),
                 std::runtime_error);
}

TEST(Parser, Error_ExtendsNotFirst) {
    EXPECT_THROW(
        parse_template("{{ x }}{% extends \"base.html\" %}"),
        std::runtime_error);
}

TEST(Parser, Error_ContainsTemplateName) {
    try {
        parse_template("{% endfor %}", "my_template.html");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        const std::string msg(e.what());
        EXPECT_NE(msg.find("my_template.html"), std::string::npos)
            << "Error message: " << msg;
    }
}

TEST(Parser, Error_ContainsLineNumber) {
    try {
        parse_template("line1\nline2\n{% endfor %}", "t.html");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        const std::string msg(e.what());
        // Should contain line 3.
        EXPECT_NE(msg.find("3"), std::string::npos)
            << "Error message: " << msg;
    }
}

TEST(Parser, Error_MissingExprClose) {
    // {{ x without closing }} — lexer will throw, not parser, but ensure
    // the overall pipeline throws.
    EXPECT_THROW(parse_template("{{ x"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Realistic template
// ---------------------------------------------------------------------------

TEST(Parser, RealisticTemplate) {
    const std::string src =
        "{% extends \"base.html\" %}\n"
        "{% block content %}\n"
        "<h1>{{ post.title }}</h1>\n"
        "{% for tag in post.tags %}\n"
        "  <span>{{ tag.name | upper }}</span>\n"
        "{% endfor %}\n"
        "{% endblock %}\n";

    EXPECT_NO_THROW({
        auto tmpl = parse_template(src, "post.html");
        EXPECT_TRUE(tmpl.parent.has_value());
        EXPECT_EQ(tmpl.parent.value(), "base.html");
        EXPECT_EQ(tmpl.blocks.count("content"), 1u);
    });
}

// ---------------------------------------------------------------------------
// Error message format check
// ---------------------------------------------------------------------------

TEST(Parser, ErrorFormat_ParseErrorPrefix) {
    try {
        parse_template("{% endfor %}", "post.html");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        const std::string msg(e.what());
        // Must start with "parse error in 'post.html'"
        EXPECT_NE(msg.find("parse error in"), std::string::npos)
            << "Error message: " << msg;
        EXPECT_NE(msg.find("post.html"), std::string::npos)
            << "Error message: " << msg;
    }
}
