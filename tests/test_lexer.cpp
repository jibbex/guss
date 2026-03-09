/**
 * \file test_lexer.cpp
 * \brief GoogleTest unit tests for the Guss template lexer.
 */
#include <gtest/gtest.h>
#include "guss/render/lexer.hpp"

#include <string>
#include <string_view>
#include <vector>

using namespace guss::render::lexer;

// ---------------------------------------------------------------------------
// Helper: collect token types from a source string
// ---------------------------------------------------------------------------
static std::vector<TokenType> types(const std::string& src,
                                    std::string_view name = "<test>") {
    Lexer lex(src, name);
    auto tokens = lex.tokenize();
    std::vector<TokenType> result;
    result.reserve(tokens.size());
    for (const auto& t : tokens) {
        result.push_back(t.type);
    }
    return result;
}

// Tokenise and return the full token list (excluding Eof).
static std::vector<Token> tokenize(const std::string& src,
                                   std::string_view name = "<test>") {
    Lexer lex(src, name);
    auto tokens = lex.tokenize();
    if (!tokens.empty() && tokens.back().type == TokenType::Eof) {
        tokens.pop_back();
    }
    return tokens;
}

// ---------------------------------------------------------------------------
// Empty / trivial inputs
// ---------------------------------------------------------------------------

TEST(Lexer, EmptySource) {
    Lexer lex("", "<test>");
    auto tokens = lex.tokenize();
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::Eof);
}

TEST(Lexer, PurePlainText) {
    auto toks = tokenize("hello world");
    ASSERT_EQ(toks.size(), 1u);
    EXPECT_EQ(toks[0].type, TokenType::Text);
    EXPECT_EQ(toks[0].value, "hello world");
    EXPECT_EQ(toks[0].line, 1u);
    EXPECT_EQ(toks[0].col,  1u);
}

TEST(Lexer, PurePlainTextMultiline) {
    auto toks = tokenize("line1\nline2\n");
    ASSERT_EQ(toks.size(), 1u);
    EXPECT_EQ(toks[0].value, "line1\nline2\n");
}

// ---------------------------------------------------------------------------
// Expression tags  {{ … }}
// ---------------------------------------------------------------------------

TEST(Lexer, SimpleExpressionTag) {
    auto toks = tokenize("{{ name }}");
    // ExprOpen, Identifier("name"), ExprClose
    ASSERT_EQ(toks.size(), 3u);
    EXPECT_EQ(toks[0].type, TokenType::ExprOpen);
    EXPECT_EQ(toks[1].type, TokenType::Identifier);
    EXPECT_EQ(toks[1].value, "name");
    EXPECT_EQ(toks[2].type, TokenType::ExprClose);
}

TEST(Lexer, ExpressionTagDelimiters) {
    // Make sure ExprOpen / ExprClose appear around a simple expr.
    Lexer lex("{{x}}", "<test>");
    auto toks = lex.tokenize();
    // Expected: ExprOpen, Identifier, ExprClose, Eof
    ASSERT_GE(toks.size(), 3u);
    EXPECT_EQ(toks[0].type, TokenType::ExprOpen);
    EXPECT_EQ(toks[1].type, TokenType::Identifier);
    EXPECT_EQ(toks[1].value, "x");
    EXPECT_EQ(toks[2].type, TokenType::ExprClose);
}

TEST(Lexer, ExpressionWithDot) {
    const std::string src = "{{ post.title }}";
    auto toks = tokenize(src);
    // ExprOpen, Identifier(post), Dot, Identifier(title), ExprClose
    ASSERT_EQ(toks.size(), 5u);
    EXPECT_EQ(toks[0].type, TokenType::ExprOpen);
    EXPECT_EQ(toks[1].type, TokenType::Identifier);
    EXPECT_EQ(toks[1].value, "post");
    EXPECT_EQ(toks[2].type, TokenType::Dot);
    EXPECT_EQ(toks[3].type, TokenType::Identifier);
    EXPECT_EQ(toks[3].value, "title");
    EXPECT_EQ(toks[4].type, TokenType::ExprClose);
}

TEST(Lexer, ExpressionWithPipe) {
    const std::string src = "{{ value | upper }}";
    auto toks = tokenize(src);
    // ExprOpen, Identifier, Pipe, Identifier, ExprClose
    ASSERT_EQ(toks.size(), 5u);
    EXPECT_EQ(toks[1].type, TokenType::Identifier);
    EXPECT_EQ(toks[1].value, "value");
    EXPECT_EQ(toks[2].type, TokenType::Pipe);
    EXPECT_EQ(toks[3].type, TokenType::Identifier);
    EXPECT_EQ(toks[3].value, "upper");
    EXPECT_EQ(toks[4].type, TokenType::ExprClose);
}

// ---------------------------------------------------------------------------
// Block tags  {% … %}
// ---------------------------------------------------------------------------

TEST(Lexer, SimpleBlockTag) {
    auto toks = tokenize("{% if x %}");
    // BlockOpen, Keyword_If, Identifier(x), BlockClose
    ASSERT_EQ(toks.size(), 4u);
    EXPECT_EQ(toks[0].type, TokenType::BlockOpen);
    EXPECT_EQ(toks[1].type, TokenType::Keyword_If);
    EXPECT_EQ(toks[2].type, TokenType::Identifier);
    EXPECT_EQ(toks[2].value, "x");
    EXPECT_EQ(toks[3].type, TokenType::BlockClose);
}

TEST(Lexer, BlockTagEmitsBlockClose) {
    Lexer lex("{% endif %}", "<test>");
    auto toks = lex.tokenize();
    // BlockOpen, Keyword_EndIf, BlockClose, Eof
    ASSERT_GE(toks.size(), 3u);
    EXPECT_EQ(toks[0].type, TokenType::BlockOpen);
    EXPECT_EQ(toks[1].type, TokenType::Keyword_EndIf);
    EXPECT_EQ(toks[2].type, TokenType::BlockClose);
}

TEST(Lexer, ForBlockTag) {
    const std::string src = "{% for item in items %}";
    auto toks = tokenize(src);
    // BlockOpen, Keyword_For, Identifier(item), Keyword_In, Identifier(items)
    ASSERT_GE(toks.size(), 4u);
    EXPECT_EQ(toks[0].type, TokenType::BlockOpen);
    EXPECT_EQ(toks[1].type, TokenType::Keyword_For);
    EXPECT_EQ(toks[2].type, TokenType::Identifier);
    EXPECT_EQ(toks[2].value, "item");
    EXPECT_EQ(toks[3].type, TokenType::Keyword_In);
    EXPECT_EQ(toks[4].type, TokenType::Identifier);
    EXPECT_EQ(toks[4].value, "items");
}

// ---------------------------------------------------------------------------
// Keyword round-trip
// ---------------------------------------------------------------------------

TEST(Lexer, AllKeywordsRecognised) {
    const std::vector<std::pair<std::string, TokenType>> cases = {
        { "{% for %}",      TokenType::Keyword_For      },
        { "{% endfor %}",   TokenType::Keyword_EndFor   },
        { "{% if %}",       TokenType::Keyword_If       },
        { "{% elif %}",     TokenType::Keyword_Elif     },
        { "{% else %}",     TokenType::Keyword_Else     },
        { "{% endif %}",    TokenType::Keyword_EndIf    },
        { "{% block %}",    TokenType::Keyword_Block    },
        { "{% endblock %}", TokenType::Keyword_EndBlock },
        { "{% extends %}",  TokenType::Keyword_Extends  },
        { "{% include %}",  TokenType::Keyword_Include  },
        { "{% in %}",       TokenType::Keyword_In       },
        { "{% not %}",      TokenType::Keyword_Not      },
        { "{% and %}",      TokenType::Keyword_And      },
        { "{% or %}",       TokenType::Keyword_Or       },
    };

    for (const auto& [src, expected_type] : cases) {
        Lexer lex(src, "<test>");
        auto toks = lex.tokenize();
        // toks: BlockOpen, <keyword>, BlockClose, Eof
        ASSERT_GE(toks.size(), 2u) << "source: " << src;
        EXPECT_EQ(toks[1].type, expected_type) << "source: " << src;
    }
}

TEST(Lexer, UnknownWordIsIdentifier) {
    auto toks = tokenize("{{ myVar }}");
    ASSERT_GE(toks.size(), 1u);
    EXPECT_EQ(toks[1].type, TokenType::Identifier);
    EXPECT_EQ(toks[1].value, "myVar");
}

// ---------------------------------------------------------------------------
// String literals
// ---------------------------------------------------------------------------

TEST(Lexer, DoubleQuotedString) {
    auto toks = tokenize(R"({{ "hello" }})");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type,  TokenType::StringLiteral);
    EXPECT_EQ(toks[1].value, "hello");   // quotes excluded
}

TEST(Lexer, SingleQuotedString) {
    auto toks = tokenize("{{ 'world' }}");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type,  TokenType::StringLiteral);
    EXPECT_EQ(toks[1].value, "world");
}

TEST(Lexer, StringLiteralViewPointsIntoSource) {
    // The string_view inside the token must point into the original source.
    const std::string src = R"({{ "abc" }})";
    Lexer lex(src, "<test>");
    auto toks = lex.tokenize();
    const Token& str_tok = toks[1];
    ASSERT_EQ(str_tok.type, TokenType::StringLiteral);
    // Verify the pointer arithmetic: value data must be within src's buffer.
    EXPECT_GE(str_tok.value.data(), src.data());
    EXPECT_LE(str_tok.value.data() + str_tok.value.size(),
              src.data() + src.size());
}

TEST(Lexer, EmptyDoubleQuotedString) {
    auto toks = tokenize(R"({{ "" }})");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type,  TokenType::StringLiteral);
    EXPECT_EQ(toks[1].value, "");
}

TEST(Lexer, StringWithEscapeSequence) {
    // A backslash-escaped quote inside a double-quoted string should not
    // terminate the string early.
    const std::string src = R"({{ "say \"hi\"" }})";
    auto toks = tokenize(src);
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type, TokenType::StringLiteral);
    // The raw content between the outer quotes, including escape sequences:
    EXPECT_EQ(toks[1].value, R"(say \"hi\")");
}

// ---------------------------------------------------------------------------
// Numeric literals
// ---------------------------------------------------------------------------

TEST(Lexer, IntegerLiteral) {
    auto toks = tokenize("{{ 42 }}");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type,  TokenType::IntLiteral);
    EXPECT_EQ(toks[1].value, "42");
}

TEST(Lexer, FloatLiteral) {
    auto toks = tokenize("{{ 3.14 }}");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type,  TokenType::FloatLiteral);
    EXPECT_EQ(toks[1].value, "3.14");
}

TEST(Lexer, FloatWithExponent) {
    auto toks = tokenize("{{ 1.5e10 }}");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type, TokenType::FloatLiteral);
    EXPECT_EQ(toks[1].value, "1.5e10");
}

TEST(Lexer, FloatWithNegativeExponent) {
    auto toks = tokenize("{{ 2.0E-3 }}");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type, TokenType::FloatLiteral);
    EXPECT_EQ(toks[1].value, "2.0E-3");
}

TEST(Lexer, ZeroLiteral) {
    auto toks = tokenize("{{ 0 }}");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type,  TokenType::IntLiteral);
    EXPECT_EQ(toks[1].value, "0");
}

// ---------------------------------------------------------------------------
// Comparison operators
// ---------------------------------------------------------------------------

TEST(Lexer, ComparisonOperators) {
    const std::vector<std::pair<std::string, TokenType>> cases = {
        { "{{ a == b }}", TokenType::Op_Eq },
        { "{{ a != b }}", TokenType::Op_Ne },
        { "{{ a < b }}",  TokenType::Op_Lt },
        { "{{ a > b }}",  TokenType::Op_Gt },
        { "{{ a <= b }}", TokenType::Op_Le },
        { "{{ a >= b }}", TokenType::Op_Ge },
    };
    for (const auto& [src, expected] : cases) {
        auto toks = tokenize(src);
        ASSERT_GE(toks.size(), 3u) << "source: " << src;
        EXPECT_EQ(toks[2].type, expected) << "source: " << src;
    }
}

// ---------------------------------------------------------------------------
// Punctuation
// ---------------------------------------------------------------------------

TEST(Lexer, Parens) {
    auto toks = tokenize("{{ f(x) }}");
    // ExprOpen, Identifier(f), LParen, Identifier(x), RParen
    ASSERT_GE(toks.size(), 5u);
    EXPECT_EQ(toks[2].type, TokenType::LParen);
    EXPECT_EQ(toks[4].type, TokenType::RParen);
}

TEST(Lexer, Comma) {
    auto toks = tokenize("{{ f(a, b) }}");
    // ExprOpen, Identifier(f), LParen, Identifier(a), Comma, Identifier(b), RParen
    ASSERT_GE(toks.size(), 6u);
    EXPECT_EQ(toks[4].type, TokenType::Comma);
}

// ---------------------------------------------------------------------------
// Comments  {# … #}
// ---------------------------------------------------------------------------

TEST(Lexer, CommentProducesNoTokens) {
    auto toks = tokenize("{# this is a comment #}");
    // Only Eof should remain (pop_back removes it, so toks is empty).
    EXPECT_TRUE(toks.empty());
}

TEST(Lexer, CommentBetweenText) {
    const std::string src = "before{# ignored #}after";
    auto toks = tokenize(src);
    // Text("before"), Text("after")
    ASSERT_EQ(toks.size(), 2u);
    EXPECT_EQ(toks[0].type,  TokenType::Text);
    EXPECT_EQ(toks[0].value, "before");
    EXPECT_EQ(toks[1].type,  TokenType::Text);
    EXPECT_EQ(toks[1].value, "after");
}

TEST(Lexer, MultilineComment) {
    auto toks = tokenize("{#\n  multiline\n  comment\n#}hello");
    ASSERT_EQ(toks.size(), 1u);
    EXPECT_EQ(toks[0].value, "hello");
}

// ---------------------------------------------------------------------------
// Mixed text and tags
// ---------------------------------------------------------------------------

TEST(Lexer, TextAroundExpression) {
    const std::string src = "Hello, {{ name }}!";
    auto toks = tokenize(src);
    // Text("Hello, "), ExprOpen, Identifier(name), ExprClose, Text("!")
    ASSERT_EQ(toks.size(), 5u);
    EXPECT_EQ(toks[0].type,  TokenType::Text);
    EXPECT_EQ(toks[0].value, "Hello, ");
    EXPECT_EQ(toks[1].type,  TokenType::ExprOpen);
    EXPECT_EQ(toks[2].type,  TokenType::Identifier);
    EXPECT_EQ(toks[3].type,  TokenType::ExprClose);
    EXPECT_EQ(toks[4].type,  TokenType::Text);
    EXPECT_EQ(toks[4].value, "!");
}

TEST(Lexer, MultipleTagsInSequence) {
    auto toks = tokenize("{% if a %}{{ b }}{% endif %}");
    // BlockOpen Keyword_If Identifier BlockClose
    // ExprOpen  Identifier ExprClose
    // BlockOpen Keyword_EndIf BlockClose
    auto t = types("{% if a %}{{ b }}{% endif %}");
    const std::vector<TokenType> expected = {
        TokenType::BlockOpen, TokenType::Keyword_If, TokenType::Identifier,
        TokenType::BlockClose,
        TokenType::ExprOpen,  TokenType::Identifier, TokenType::ExprClose,
        TokenType::BlockOpen, TokenType::Keyword_EndIf, TokenType::BlockClose,
        TokenType::Eof,
    };
    EXPECT_EQ(t, expected);
}

// ---------------------------------------------------------------------------
// Whitespace trimming
// ---------------------------------------------------------------------------

TEST(Lexer, TrimBefore_BlockTag) {
    // {%- trims whitespace/newlines before the tag from the preceding text.
    auto toks = tokenize("  \n  {%- if x %}");
    // After trimming the leading text, it should be empty and not emitted.
    // Remaining: BlockOpen, Keyword_If, Identifier, BlockClose
    for (const auto& tok : toks) {
        EXPECT_NE(tok.type, TokenType::Text)
            << "No Text token expected after trim-before; got value='"
            << tok.value << "'";
    }
}

TEST(Lexer, TrimBefore_PreservesNonWhitespace) {
    // Only trailing whitespace of the preceding text is stripped.
    const std::string src = "abc  {%- if x %}";
    auto toks = tokenize(src);
    // Text should be "abc" (trailing spaces removed).
    ASSERT_GE(toks.size(), 1u);
    EXPECT_EQ(toks[0].type,  TokenType::Text);
    EXPECT_EQ(toks[0].value, "abc");
}

TEST(Lexer, TrimAfter_BlockTag) {
    // -%} trims whitespace/newlines after the closing delimiter.
    auto toks = tokenize("{% if x -%}  \n  rest");
    // Text("rest") — leading whitespace consumed.
    // Find the text token at the end.
    const Token* text_tok = nullptr;
    for (const auto& tok : toks) {
        if (tok.type == TokenType::Text) {
            text_tok = &tok;
        }
    }
    ASSERT_NE(text_tok, nullptr) << "Expected a Text token after trimmed tag";
    EXPECT_EQ(text_tok->value, "rest");
}

TEST(Lexer, TrimBoth) {
    // {%- … -%} trims on both sides.
    auto toks = tokenize("  \t{%- block name -%}  \n  content");
    // No text before the block tag; text after is "content".
    // Find first Text token (if any) before BlockOpen.
    bool found_text_before = false;
    for (const auto& tok : toks) {
        if (tok.type == TokenType::BlockOpen) break;
        if (tok.type == TokenType::Text) { found_text_before = true; break; }
    }
    EXPECT_FALSE(found_text_before);

    const Token* last_text = nullptr;
    for (const auto& tok : toks) {
        if (tok.type == TokenType::Text) last_text = &tok;
    }
    ASSERT_NE(last_text, nullptr);
    EXPECT_EQ(last_text->value, "content");
}

TEST(Lexer, NoTrimWithoutDash) {
    // Without dashes, whitespace is preserved.
    const std::string src = "  \n  {% if x %}  \n  rest";
    auto toks = tokenize(src);
    ASSERT_GE(toks.size(), 1u);
    EXPECT_EQ(toks[0].type, TokenType::Text);
    EXPECT_EQ(toks[0].value, "  \n  ");
}

// ---------------------------------------------------------------------------
// Line and column tracking
// ---------------------------------------------------------------------------

TEST(Lexer, LineTracking) {
    auto toks = tokenize("line1\nline2\n{{ var }}");
    // Text token on line 1; ExprOpen on line 3.
    const auto& expr_open = toks[1]; // after the Text token
    EXPECT_EQ(expr_open.type, TokenType::ExprOpen);
    EXPECT_EQ(expr_open.line, 3u);
}

TEST(Lexer, ColTracking) {
    auto toks = tokenize("abc{{ x }}");
    // ExprOpen at col 4.
    const auto& expr_open = toks[1];
    EXPECT_EQ(expr_open.type, TokenType::ExprOpen);
    EXPECT_EQ(expr_open.line, 1u);
    EXPECT_EQ(expr_open.col,  4u);
}

// ---------------------------------------------------------------------------
// Error cases — must throw std::runtime_error with location info
// ---------------------------------------------------------------------------

TEST(Lexer, ErrorUnterminatedExprTag) {
    EXPECT_THROW({
        Lexer lex("{{ unclosed", "tmpl.html");
        lex.tokenize();
    }, std::runtime_error);
}

TEST(Lexer, ErrorUnterminatedBlockTag) {
    EXPECT_THROW({
        Lexer lex("{% unclosed", "tmpl.html");
        lex.tokenize();
    }, std::runtime_error);
}

TEST(Lexer, ErrorUnterminatedComment) {
    EXPECT_THROW({
        Lexer lex("{# unclosed comment", "tmpl.html");
        lex.tokenize();
    }, std::runtime_error);
}

TEST(Lexer, ErrorUnterminatedStringDouble) {
    EXPECT_THROW({
        Lexer lex("{{ \"unclosed }}", "tmpl.html");
        lex.tokenize();
    }, std::runtime_error);
}

TEST(Lexer, ErrorUnterminatedStringSingle) {
    EXPECT_THROW({
        Lexer lex("{{ 'unclosed }}", "tmpl.html");
        lex.tokenize();
    }, std::runtime_error);
}

TEST(Lexer, ErrorMessageContainsTemplateName) {
    try {
        Lexer lex("{{ unclosed", "my_template.html");
        lex.tokenize();
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        const std::string msg(e.what());
        EXPECT_NE(msg.find("my_template.html"), std::string::npos)
            << "Error message: " << msg;
    }
}

TEST(Lexer, ErrorMessageContainsLineAndCol) {
    try {
        Lexer lex("line1\nline2\n{{ unclosed", "t.html");
        lex.tokenize();
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        const std::string msg(e.what());
        // Should contain "3:" for line 3.
        EXPECT_NE(msg.find("3:"), std::string::npos)
            << "Error message: " << msg;
    }
}

TEST(Lexer, ErrorUnexpectedCharInTag) {
    EXPECT_THROW({
        Lexer lex("{{ @ }}", "<test>");
        lex.tokenize();
    }, std::runtime_error);
}

// ---------------------------------------------------------------------------
// string_view zero-allocation guarantee
// ---------------------------------------------------------------------------

TEST(Lexer, TextTokenViewPointsIntoSource) {
    const std::string src = "hello {{ name }} world";
    Lexer lex(src, "<test>");
    auto toks = lex.tokenize();

    for (const auto& tok : toks) {
        if (tok.type == TokenType::Text || tok.type == TokenType::Identifier) {
            EXPECT_GE(tok.value.data(), src.data())
                << "Token view starts before source buffer";
            EXPECT_LE(tok.value.data() + tok.value.size(),
                      src.data() + src.size())
                << "Token view ends after source buffer";
        }
    }
}

// ---------------------------------------------------------------------------
// ExprClose and BlockClose tokens are emitted
// ---------------------------------------------------------------------------

TEST(Lexer, ExprCloseEmitted) {
    Lexer lex("{{ x }}", "<test>");
    auto toks = lex.tokenize();
    // ExprOpen, Identifier, ExprClose, Eof
    ASSERT_EQ(toks.size(), 4u);
    EXPECT_EQ(toks[2].type, TokenType::ExprClose);
}

TEST(Lexer, BlockCloseEmitted) {
    Lexer lex("{% x %}", "<test>");
    auto toks = lex.tokenize();
    // BlockOpen, Identifier, BlockClose, Eof
    ASSERT_EQ(toks.size(), 4u);
    EXPECT_EQ(toks[2].type, TokenType::BlockClose);
}

// ---------------------------------------------------------------------------
// Realistic template snippet
// ---------------------------------------------------------------------------

TEST(Lexer, RealisticTemplate) {
    const std::string src =
        "<!DOCTYPE html>\n"
        "{% extends \"base.html\" %}\n"
        "{% block content %}\n"
        "<h1>{{ post.title }}</h1>\n"
        "{% for tag in post.tags %}\n"
        "  <span>{{ tag.name | upper }}</span>\n"
        "{% endfor %}\n"
        "{% endblock %}\n";

    Lexer lex(src, "index.html");
    // Should tokenise without throwing.
    EXPECT_NO_THROW({
        auto toks = lex.tokenize();
        EXPECT_GT(toks.size(), 5u);
        EXPECT_EQ(toks.back().type, TokenType::Eof);
    });
}

// ---------------------------------------------------------------------------
// Identifier with digits (not as first character)
// ---------------------------------------------------------------------------

TEST(Lexer, IdentifierWithDigits) {
    auto toks = tokenize("{{ my_var2 }}");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type,  TokenType::Identifier);
    EXPECT_EQ(toks[1].value, "my_var2");
}

TEST(Lexer, IdentifierUnderscoreStart) {
    auto toks = tokenize("{{ _private }}");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type,  TokenType::Identifier);
    EXPECT_EQ(toks[1].value, "_private");
}

// ---------------------------------------------------------------------------
// Logical keyword operators inside expressions
// ---------------------------------------------------------------------------

TEST(Lexer, LogicalOperatorsInExpr) {
    auto toks = tokenize("{{ a and b or not c }}");
    // ExprOpen, a, and, b, or, not, c, ExprClose
    ASSERT_GE(toks.size(), 7u);
    EXPECT_EQ(toks[2].type, TokenType::Keyword_And);
    EXPECT_EQ(toks[4].type, TokenType::Keyword_Or);
    EXPECT_EQ(toks[5].type, TokenType::Keyword_Not);
}

// ---------------------------------------------------------------------------
// Boolean literals
// ---------------------------------------------------------------------------

TEST(Lexer, BooleanLiterals) {
    const std::vector<std::pair<std::string, TokenType>> cases = {
        { "{{ true }}",  TokenType::Keyword_True  },
        { "{{ false }}", TokenType::Keyword_False },
    };
    for (const auto& [src, expected] : cases) {
        auto toks = tokenize(src);
        ASSERT_GE(toks.size(), 2u) << "source: " << src;
        EXPECT_EQ(toks[1].type, expected) << "source: " << src;
    }
}

// ---------------------------------------------------------------------------
// Arithmetic operators
// ---------------------------------------------------------------------------

TEST(Lexer, ArithmeticOperators) {
    const std::vector<std::pair<std::string, TokenType>> cases = {
        { "{{ a + b }}", TokenType::Op_Add },
        { "{{ a - b }}", TokenType::Op_Sub },
        { "{{ a * b }}", TokenType::Op_Mul },
        { "{{ a / b }}", TokenType::Op_Div },
    };
    for (const auto& [src, expected] : cases) {
        auto toks = tokenize(src);
        ASSERT_GE(toks.size(), 3u) << "source: " << src;
        EXPECT_EQ(toks[2].type, expected) << "source: " << src;
    }
}

TEST(Lexer, UnaryMinusEmitsOpSub) {
    // A lone '-' before an identifier should emit Op_Sub (unary minus is a
    // parser concern; the lexer always emits Op_Sub for '-').
    auto toks = tokenize("{{ -x }}");
    // ExprOpen, Op_Sub, Identifier(x), ExprClose
    ASSERT_GE(toks.size(), 3u);
    EXPECT_EQ(toks[1].type, TokenType::Op_Sub);
    EXPECT_EQ(toks[2].type, TokenType::Identifier);
}

TEST(Lexer, UnsignedIntegerLiteral) {
    // Numbers are unsigned; sign is handled by the parser via Op_Sub.
    auto toks = tokenize("{{ 99 }}");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type,  TokenType::IntLiteral);
    EXPECT_EQ(toks[1].value, "99");
}

// ---------------------------------------------------------------------------
// Column tracking: reset after newline
// ---------------------------------------------------------------------------

TEST(Lexer, ColResetsAfterNewline) {
    // Expression starts at column 1 of line 2.
    auto toks = tokenize("\n{{ x }}");
    ASSERT_GE(toks.size(), 2u);
    EXPECT_EQ(toks[1].type, TokenType::ExprOpen);
    EXPECT_EQ(toks[1].line, 2u);
    EXPECT_EQ(toks[1].col,  1u);
}

// ---------------------------------------------------------------------------
// Trim-before on expression tag
// ---------------------------------------------------------------------------

TEST(Lexer, TrimBefore_ExprTag) {
    auto toks = tokenize("  \n  {{- name }}");
    for (const auto& tok : toks) {
        EXPECT_NE(tok.type, TokenType::Text)
            << "No Text token expected after trim-before on expr tag; got value='"
            << tok.value << "'";
    }
}
