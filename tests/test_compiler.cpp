/**
 * \file test_compiler.cpp
 * \brief GoogleTest unit tests for the Guss template compiler.
 */
#include <gtest/gtest.h>
#include "guss/render/compiler.hpp"
#include "guss/render/parser.hpp"
#include "guss/render/lexer.hpp"
#include "guss/render/ast.hpp"

#include <string>
#include <string_view>

using namespace guss::render;
using namespace guss::render::lexer;
using namespace guss::render::ast;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Lex, parse, and compile \p src, returning the CompiledTemplate.
 * The source is moved into Template::source before lexing so all
 * string_view fields inside the AST remain valid during compilation.
 */
static CompiledTemplate compile_source(std::string src,
                                       std::string_view name = "<test>") {
    ast::Template tmpl;
    tmpl.source = std::move(src);

    Lexer lex(tmpl.source, name);
    auto tokens = lex.tokenize();

    Parser parser(std::move(tokens), std::string(name));
    ast::Template parsed = parser.parse();

    tmpl.nodes  = std::move(parsed.nodes);
    tmpl.parent = std::move(parsed.parent);
    tmpl.blocks = std::move(parsed.blocks);

    Compiler compiler;
    return compiler.compile(tmpl);
}

/** Return true if \p ct contains at least one instruction with the given op. */
static bool has_op(const CompiledTemplate& ct, Op op) {
    for (const auto& instr : ct.code) {
        if (instr.op == op) return true;
    }
    return false;
}

/** Count instructions with the given op. */
static size_t count_op(const CompiledTemplate& ct, Op op) {
    size_t n = 0;
    for (const auto& instr : ct.code) {
        if (instr.op == op) ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Invariant: code.size() == lines.size()
// ---------------------------------------------------------------------------

TEST(Compiler, Invariant_CodeAndLinesParallel_Empty) {
    auto ct = compile_source("");
    EXPECT_EQ(ct.code.size(), ct.lines.size());
}

TEST(Compiler, Invariant_CodeAndLinesParallel_Text) {
    auto ct = compile_source("hello");
    EXPECT_EQ(ct.code.size(), ct.lines.size());
}

TEST(Compiler, Invariant_CodeAndLinesParallel_Complex) {
    auto ct = compile_source(
        "{% if x %}{{ y }}{% else %}z{% endif %}");
    EXPECT_EQ(ct.code.size(), ct.lines.size());
}

TEST(Compiler, Invariant_CodeAndLinesParallel_ForLoop) {
    auto ct = compile_source(
        "{% for item in items %}{{ item }}{% endfor %}");
    EXPECT_EQ(ct.code.size(), ct.lines.size());
}

// ---------------------------------------------------------------------------
// Empty template
// ---------------------------------------------------------------------------

TEST(Compiler, EmptySource_OnlyReturn) {
    auto ct = compile_source("");
    ASSERT_FALSE(ct.code.empty());
    // Last instruction must be Return.
    EXPECT_EQ(ct.code.back().op, Op::Return);
}

// ---------------------------------------------------------------------------
// TextNode → EmitText
// ---------------------------------------------------------------------------

TEST(Compiler, TextNode_EmitsEmitText) {
    auto ct = compile_source("hello world");
    ASSERT_TRUE(has_op(ct, Op::EmitText));

    // The EmitText operand indexes into strings.
    size_t emit_idx = 0;
    for (size_t i = 0; i < ct.code.size(); ++i) {
        if (ct.code[i].op == Op::EmitText) {
            emit_idx = static_cast<size_t>(ct.code[i].operand);
            break;
        }
    }
    ASSERT_LT(emit_idx, ct.strings.size());
    EXPECT_EQ(ct.strings[emit_idx], "hello world");
}

TEST(Compiler, MultipleTextNodes) {
    auto ct = compile_source("foo{# comment #}bar");
    EXPECT_EQ(count_op(ct, Op::EmitText), 2u);
    ASSERT_EQ(ct.strings.size(), 2u);
    EXPECT_EQ(ct.strings[0], "foo");
    EXPECT_EQ(ct.strings[1], "bar");
}

// ---------------------------------------------------------------------------
// ExprNode ({{ expr }}) → compile_expr + Emit
// ---------------------------------------------------------------------------

TEST(Compiler, ExprNode_Variable_ResolveAndEmit) {
    auto ct = compile_source("{{ name }}");
    EXPECT_TRUE(has_op(ct, Op::Resolve));
    EXPECT_TRUE(has_op(ct, Op::Emit));

    ASSERT_EQ(ct.paths.size(), 1u);
    EXPECT_EQ(ct.paths[0], "name");
}

TEST(Compiler, ExprNode_DottedPath) {
    auto ct = compile_source("{{ post.title }}");
    ASSERT_EQ(ct.paths.size(), 1u);
    EXPECT_EQ(ct.paths[0], "post.title");
}

TEST(Compiler, ExprNode_IntLiteral_PushAndEmit) {
    auto ct = compile_source("{{ 42 }}");
    EXPECT_TRUE(has_op(ct, Op::Push));
    EXPECT_TRUE(has_op(ct, Op::Emit));
    ASSERT_EQ(ct.constants.size(), 1u);
    EXPECT_EQ(ct.constants[0].as_int(), 42);
}

TEST(Compiler, ExprNode_FloatLiteral) {
    auto ct = compile_source("{{ 3.14 }}");
    ASSERT_EQ(ct.constants.size(), 1u);
    EXPECT_DOUBLE_EQ(ct.constants[0].as_double(), 3.14);
}

TEST(Compiler, ExprNode_StringLiteral) {
    auto ct = compile_source(R"({{ "hello" }})");
    ASSERT_EQ(ct.constants.size(), 1u);
    EXPECT_EQ(ct.constants[0].as_string(), "hello");
}

TEST(Compiler, ExprNode_BoolTrue) {
    auto ct = compile_source("{{ true }}");
    ASSERT_EQ(ct.constants.size(), 1u);
    EXPECT_TRUE(ct.constants[0].as_bool());
}

TEST(Compiler, ExprNode_BoolFalse) {
    auto ct = compile_source("{{ false }}");
    ASSERT_EQ(ct.constants.size(), 1u);
    EXPECT_FALSE(ct.constants[0].as_bool());
}

// ---------------------------------------------------------------------------
// UnaryOp
// ---------------------------------------------------------------------------

TEST(Compiler, UnaryNot_EmitsUnaryOp) {
    auto ct = compile_source("{{ not x }}");
    EXPECT_TRUE(has_op(ct, Op::UnaryOp));

    for (const auto& instr : ct.code) {
        if (instr.op == Op::UnaryOp) {
            EXPECT_EQ(instr.operand,
                      static_cast<int32_t>(TokenType::Keyword_Not));
        }
    }
}

TEST(Compiler, UnaryMinus_EmitsUnaryOp) {
    auto ct = compile_source("{{ -5 }}");
    EXPECT_TRUE(has_op(ct, Op::UnaryOp));

    for (const auto& instr : ct.code) {
        if (instr.op == Op::UnaryOp) {
            EXPECT_EQ(instr.operand,
                      static_cast<int32_t>(TokenType::Op_Sub));
        }
    }
}

// ---------------------------------------------------------------------------
// BinaryOp
// ---------------------------------------------------------------------------

TEST(Compiler, BinaryAdd_EmitsBinaryOp) {
    auto ct = compile_source("{{ a + b }}");
    EXPECT_TRUE(has_op(ct, Op::BinaryOp));
    for (const auto& instr : ct.code) {
        if (instr.op == Op::BinaryOp) {
            EXPECT_EQ(instr.operand,
                      static_cast<int32_t>(TokenType::Op_Add));
        }
    }
}

TEST(Compiler, BinaryEq_EmitsBinaryOp) {
    auto ct = compile_source("{{ a == b }}");
    EXPECT_TRUE(has_op(ct, Op::BinaryOp));
    for (const auto& instr : ct.code) {
        if (instr.op == Op::BinaryOp) {
            EXPECT_EQ(instr.operand,
                      static_cast<int32_t>(TokenType::Op_Eq));
        }
    }
}

TEST(Compiler, BinaryAnd_EmitsBinaryOp) {
    auto ct = compile_source("{{ a and b }}");
    EXPECT_TRUE(has_op(ct, Op::BinaryOp));
    for (const auto& instr : ct.code) {
        if (instr.op == Op::BinaryOp) {
            EXPECT_EQ(instr.operand,
                      static_cast<int32_t>(TokenType::Keyword_And));
        }
    }
}

TEST(Compiler, BinaryOr_EmitsBinaryOp) {
    auto ct = compile_source("{{ a or b }}");
    EXPECT_TRUE(has_op(ct, Op::BinaryOp));
    for (const auto& instr : ct.code) {
        if (instr.op == Op::BinaryOp) {
            EXPECT_EQ(instr.operand,
                      static_cast<int32_t>(TokenType::Keyword_Or));
        }
    }
}

// ---------------------------------------------------------------------------
// Filter (pipe)
// ---------------------------------------------------------------------------

TEST(Compiler, Filter_NoArgs_EmitsFilter) {
    auto ct = compile_source("{{ name | upper }}");
    EXPECT_TRUE(has_op(ct, Op::Filter));
    ASSERT_EQ(ct.filter_names.size(), 1u);
    EXPECT_EQ(ct.filter_names[0], "upper");

    for (const auto& instr : ct.code) {
        if (instr.op == Op::Filter) {
            // bits[31:8] hold filter_name_idx (24 bits, no mask needed on read).
            size_t name_idx  = static_cast<size_t>(instr.operand >> 8);
            size_t arg_count = static_cast<size_t>(instr.operand & 0xFF);
            EXPECT_EQ(name_idx, 0u);
            EXPECT_EQ(arg_count, 0u);
        }
    }
}

TEST(Compiler, Filter_WithArgs_CorrectEncoding) {
    auto ct = compile_source("{{ value | truncate(80) }}");
    EXPECT_TRUE(has_op(ct, Op::Filter));
    ASSERT_EQ(ct.filter_names.size(), 1u);
    EXPECT_EQ(ct.filter_names[0], "truncate");

    for (const auto& instr : ct.code) {
        if (instr.op == Op::Filter) {
            size_t arg_count = static_cast<size_t>(instr.operand & 0xFF);
            EXPECT_EQ(arg_count, 1u);
        }
    }
}

TEST(Compiler, FilterChain_TwoFilters) {
    auto ct = compile_source("{{ name | lower | upper }}");
    EXPECT_EQ(count_op(ct, Op::Filter), 2u);
    ASSERT_EQ(ct.filter_names.size(), 2u);
    EXPECT_EQ(ct.filter_names[0], "lower");
    EXPECT_EQ(ct.filter_names[1], "upper");
}

// ---------------------------------------------------------------------------
// If node — jump offsets
// ---------------------------------------------------------------------------

TEST(Compiler, IfNode_Simple_HasJumps) {
    auto ct = compile_source("{% if x %}yes{% endif %}");
    EXPECT_TRUE(has_op(ct, Op::JumpIfFalse));
    EXPECT_TRUE(has_op(ct, Op::Jump));
    EXPECT_EQ(ct.code.size(), ct.lines.size());
}

TEST(Compiler, IfNode_WithElse_HasJumps) {
    auto ct = compile_source("{% if x %}yes{% else %}no{% endif %}");
    EXPECT_TRUE(has_op(ct, Op::JumpIfFalse));
    EXPECT_TRUE(has_op(ct, Op::Jump));
}

TEST(Compiler, IfNode_Elif_MultipleJumps) {
    auto ct = compile_source("{% if a %}A{% elif b %}B{% elif c %}C{% endif %}");
    // Three branches → three JumpIfFalse, three end-Jumps.
    EXPECT_EQ(count_op(ct, Op::JumpIfFalse), 3u);
    EXPECT_EQ(count_op(ct, Op::Jump), 3u);
}

TEST(Compiler, IfNode_JumpIfFalse_OffsetPositive) {
    // The JumpIfFalse offset must be > 0 (jumping forward past the branch body).
    auto ct = compile_source("{% if x %}yes{% endif %}");
    for (const auto& instr : ct.code) {
        if (instr.op == Op::JumpIfFalse) {
            EXPECT_GT(instr.operand, 0);
        }
    }
}

TEST(Compiler, IfNode_Jump_OffsetForwardToEnd) {
    // The unconditional Jump after the branch body must also be positive
    // when there is no else body, pointing past the (empty) else section.
    auto ct = compile_source("{% if x %}yes{% endif %}");
    for (const auto& instr : ct.code) {
        if (instr.op == Op::Jump) {
            EXPECT_GE(instr.operand, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// For node
// ---------------------------------------------------------------------------

TEST(Compiler, ForNode_Basic_HasForOps) {
    auto ct = compile_source("{% for item in items %}{{ item }}{% endfor %}");
    EXPECT_TRUE(has_op(ct, Op::ForBegin));
    EXPECT_TRUE(has_op(ct, Op::ForNext));
    EXPECT_TRUE(has_op(ct, Op::ForEnd));
    EXPECT_TRUE(has_op(ct, Op::Jump));
}

TEST(Compiler, ForNode_ForBegin_CorrectEncoding) {
    auto ct = compile_source("{% for item in items %}{{ item }}{% endfor %}");

    // paths: [0]="items", [1]="item" (iterable first, var second)
    ASSERT_GE(ct.paths.size(), 2u);
    EXPECT_EQ(ct.paths[0], "items");
    EXPECT_EQ(ct.paths[1], "item");

    for (const auto& instr : ct.code) {
        if (instr.op == Op::ForBegin) {
            size_t iterable_idx = static_cast<size_t>(instr.operand >> 16);
            size_t var_idx      = static_cast<size_t>(instr.operand & 0xFFFF);
            EXPECT_EQ(iterable_idx, 0u);
            EXPECT_EQ(var_idx, 1u);
        }
    }
}

TEST(Compiler, ForNode_ForNext_OffsetPositive) {
    // ForNext must jump forward past the loop body when exhausted.
    auto ct = compile_source("{% for x in xs %}{{ x }}{% endfor %}");
    for (const auto& instr : ct.code) {
        if (instr.op == Op::ForNext) {
            EXPECT_GT(instr.operand, 0);
        }
    }
}

TEST(Compiler, ForNode_BackwardJump_Negative) {
    // The unconditional Jump at the end of the loop body must be negative
    // (jumping back to loop_top).
    auto ct = compile_source("{% for x in xs %}{{ x }}{% endfor %}");
    bool found_negative_jump = false;
    for (const auto& instr : ct.code) {
        if (instr.op == Op::Jump && instr.operand < 0) {
            found_negative_jump = true;
        }
    }
    EXPECT_TRUE(found_negative_jump);
}

TEST(Compiler, ForNode_WithElse) {
    auto ct = compile_source(
        "{% for x in items %}{{ x }}{% else %}empty{% endfor %}");
    EXPECT_TRUE(has_op(ct, Op::ForBegin));
    EXPECT_TRUE(has_op(ct, Op::ForEnd));
    // else body text should be in strings
    bool found_empty = false;
    for (const auto& s : ct.strings) {
        if (s == "empty") { found_empty = true; break; }
    }
    EXPECT_TRUE(found_empty);
}

// ---------------------------------------------------------------------------
// Block node
// ---------------------------------------------------------------------------

TEST(Compiler, BlockNode_EmitsBlockCall) {
    auto ct = compile_source("{% block content %}default{% endblock %}");
    EXPECT_TRUE(has_op(ct, Op::BlockCall));
    ASSERT_EQ(ct.blocks.size(), 1u);
    EXPECT_EQ(ct.blocks[0], "content");

    // Operand is now packed: (skip_dist << 16) | (block_idx & 0xFFFF).
    // block_idx == 0; skip_dist is the distance from BlockCall to after BlockEnd.
    for (const auto& instr : ct.code) {
        if (instr.op == Op::BlockCall) {
            const size_t idx  = static_cast<size_t>(instr.operand & 0xFFFF);
            const int    skip = static_cast<int>(
                (static_cast<uint32_t>(instr.operand) >> 16) & 0xFFFF);
            EXPECT_EQ(idx, 0u);    // block index 0 = "content"
            EXPECT_GT(skip, 0);    // skip_dist must be positive
        }
    }
}

TEST(Compiler, BlockNode_MultipleBlocks) {
    auto ct = compile_source(
        "{% block a %}A{% endblock %}{% block b %}B{% endblock %}");
    EXPECT_EQ(count_op(ct, Op::BlockCall), 2u);
    ASSERT_EQ(ct.blocks.size(), 2u);
    EXPECT_EQ(ct.blocks[0], "a");
    EXPECT_EQ(ct.blocks[1], "b");
}

// ---------------------------------------------------------------------------
// Instruction order — relative ordering checks
// ---------------------------------------------------------------------------

TEST(Compiler, InstructionOrder_TextThenExpr) {
    auto ct = compile_source("hello {{ name }}");
    // EmitText must come before Resolve.
    size_t emit_text_pos = SIZE_MAX;
    size_t resolve_pos   = SIZE_MAX;
    for (size_t i = 0; i < ct.code.size(); ++i) {
        if (ct.code[i].op == Op::EmitText  && emit_text_pos == SIZE_MAX)
            emit_text_pos = i;
        if (ct.code[i].op == Op::Resolve && resolve_pos == SIZE_MAX)
            resolve_pos = i;
    }
    EXPECT_LT(emit_text_pos, resolve_pos);
}

TEST(Compiler, InstructionOrder_ResolveBeforeEmit) {
    auto ct = compile_source("{{ name }}");
    size_t resolve_pos = SIZE_MAX;
    size_t emit_pos    = SIZE_MAX;
    for (size_t i = 0; i < ct.code.size(); ++i) {
        if (ct.code[i].op == Op::Resolve && resolve_pos == SIZE_MAX)
            resolve_pos = i;
        if (ct.code[i].op == Op::Emit && emit_pos == SIZE_MAX)
            emit_pos = i;
    }
    EXPECT_LT(resolve_pos, emit_pos);
}

TEST(Compiler, InstructionOrder_FilterAfterInput) {
    // Resolve (input) must appear before Filter.
    auto ct = compile_source("{{ name | upper }}");
    size_t resolve_pos = SIZE_MAX;
    size_t filter_pos  = SIZE_MAX;
    for (size_t i = 0; i < ct.code.size(); ++i) {
        if (ct.code[i].op == Op::Resolve && resolve_pos == SIZE_MAX)
            resolve_pos = i;
        if (ct.code[i].op == Op::Filter && filter_pos == SIZE_MAX)
            filter_pos = i;
    }
    EXPECT_LT(resolve_pos, filter_pos);
}

// ---------------------------------------------------------------------------
// Return instruction
// ---------------------------------------------------------------------------

TEST(Compiler, AlwaysEndsWithReturn) {
    for (const auto* src : {
             "",
             "hello",
             "{{ x }}",
             "{% if x %}y{% endif %}",
             "{% for i in items %}{{ i }}{% endfor %}"
         }) {
        auto ct = compile_source(src);
        ASSERT_FALSE(ct.code.empty());
        EXPECT_EQ(ct.code.back().op, Op::Return) << "src: " << src;
    }
}

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

TEST(Compiler, Error_ForNonVariableIterable) {
    // Parser allows any expr as iterable, but compiler requires Variable.
    // "for x in 42" would have IntLit as iterable — trigger via direct AST.
    ast::Template tmpl;
    tmpl.source = "dummy";

    ast::ForNode fn;
    fn.var_name = "x";
    fn.iterable = std::make_unique<ast::IntLit>(ast::IntLit{42});

    tmpl.nodes.push_back(std::make_unique<ast::ForNode>(std::move(fn)));

    Compiler compiler;
    EXPECT_THROW(compiler.compile(tmpl), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Realistic template
// ---------------------------------------------------------------------------

TEST(Compiler, Realistic_PostTemplate) {
    const std::string src =
        "{% extends \"base.html\" %}\n"
        "{% block content %}\n"
        "<h1>{{ post.title }}</h1>\n"
        "{% for tag in post.tags %}\n"
        "  <span>{{ tag.name | upper }}</span>\n"
        "{% endfor %}\n"
        "{% endblock %}\n";

    CompiledTemplate ct;
    EXPECT_NO_THROW({ ct = compile_source(src, "post.html"); });
    EXPECT_EQ(ct.code.size(), ct.lines.size());
    EXPECT_FALSE(ct.code.empty());
    EXPECT_EQ(ct.code.back().op, Op::Return);

    // Should have at least one block.
    EXPECT_GE(ct.blocks.size(), 1u);
    EXPECT_EQ(ct.blocks[0], "content");

    // Should have filter "upper".
    ASSERT_EQ(ct.filter_names.size(), 1u);
    EXPECT_EQ(ct.filter_names[0], "upper");
}

TEST(Compiler, Nested_IfInsideFor) {
    auto ct = compile_source(
        "{% for x in items %}"
        "{% if x %}{{ x }}{% endif %}"
        "{% endfor %}");
    EXPECT_EQ(ct.code.size(), ct.lines.size());
    EXPECT_TRUE(has_op(ct, Op::ForBegin));
    EXPECT_TRUE(has_op(ct, Op::JumpIfFalse));
    EXPECT_EQ(ct.code.back().op, Op::Return);
}
