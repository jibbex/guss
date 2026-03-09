/**
 * \file parser.hpp
 * \brief Recursive-descent parser for the Guss template engine.
 *
 * \details
 * Accepts the flat token vector produced by \c Lexer::tokenize() and builds
 * the in-memory AST defined in \c ast.hpp.  The resulting \c ast::Template
 * is transient — it is consumed by the compiler/renderer and then discarded.
 *
 * Error messages follow the format:
 * \code
 * parse error in 'post.html' at line 42: unexpected token '}', expected 'endfor'
 * \endcode
 */
#pragma once
#include "guss/render/ast.hpp"
#include "guss/render/lexer.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace guss::render {

/**
 * \brief Recursive-descent parser that converts a token stream into an AST.
 *
 * \details
 * Construct a Parser with the token vector from \c Lexer::tokenize() and
 * a template name used in error messages.  Call \c parse() once to obtain
 * a fully built \c ast::Template.
 *
 * The parser sets \c ast::Template::parent when an \c extends directive is
 * found, populates \c ast::Template::nodes with top-level statement nodes,
 * and fills \c ast::Template::blocks with pointers to all \c BlockNode
 * instances for fast inheritance resolution.
 *
 * \note The \c ast::Template::source field is \b not set by the parser; it
 *       is the responsibility of the caller (template loader / engine) to
 *       populate that field so that all \c string_view fields inside the AST
 *       remain valid.
 */
class Parser {
public:
    /**
     * \brief Construct a parser over the given token stream.
     *
     * \param tokens        Output of \c Lexer::tokenize().
     * \param template_name Name used in parse-error messages.
     */
    explicit Parser(std::vector<lexer::Token> tokens,
                    std::string               template_name);

    /**
     * \brief Parse the full token stream and return the completed AST.
     *
     * \return A fully populated \c ast::Template.
     *
     * \throws std::runtime_error on any syntax error, with a message of the
     *         form:
     *         \code
     *         parse error in 'post.html' at line 42: <reason>
     *         \endcode
     */
    ast::Template parse();

private:
    std::vector<lexer::Token> tokens_;
    size_t                    pos_ = 0;
    std::string               template_name_;

    // -------------------------------------------------------------------------
    // Statement parsing
    // -------------------------------------------------------------------------

    /// Parse the next top-level or nested node from the token stream.
    ast::Node        parse_node();

    /// Dispatch on the keyword immediately following a BlockOpen.
    ast::Node        parse_block_tag();

    /// Parse a \c {% for var in expr %} … {% endfor %} node.
    ast::ForNode     parse_for();

    /// Parse a \c {% if expr %} … {% endif %} node (with optional elif/else).
    ast::IfNode      parse_if();

    /// Parse a \c {% block name %} … {% endblock %} node.
    ast::BlockNode   parse_block();

    /// Parse an \c {% extends "template" %} directive.
    ast::ExtendsNode parse_extends();

    /// Parse an \c {% include "template" %} directive.
    ast::IncludeNode parse_include();

    // -------------------------------------------------------------------------
    // Body helpers
    // -------------------------------------------------------------------------

    /**
     * \brief Parse a sequence of nodes until a block-close tag is encountered.
     *
     * \param stop_keywords  Token types (keywords inside a block tag) that
     *                       terminate the body.  The terminating BlockOpen and
     *                       keyword token are \b consumed; the caller is
     *                       responsible for consuming BlockClose if required.
     * \param found_keyword  Populated with whichever stop keyword was seen.
     * \return Vector of parsed nodes up to (but not including) the terminator.
     */
    std::vector<ast::Node> parse_body(
        const std::vector<lexer::TokenType>& stop_keywords,
        lexer::TokenType&                    found_keyword);

    // -------------------------------------------------------------------------
    // Expression parsing — precedence levels (low → high)
    // -------------------------------------------------------------------------

    /// Parse a full expression (entry point).
    ast::Expr parse_expr();

    /// Level 1 — logical \c or (left-associative).
    ast::Expr parse_or();

    /// Level 2 — logical \c and (left-associative).
    ast::Expr parse_and();

    /// Level 3 — logical \c not (prefix unary).
    ast::Expr parse_not();

    /// Level 4 — comparison operators (non-chaining).
    ast::Expr parse_comparison();

    /// Level 5 — additive \c + / \c - (left-associative).
    ast::Expr parse_additive();

    /// Level 6 — multiplicative \c * / \c / / \c % (left-associative).
    ast::Expr parse_multiplicative();

    /// Level 7 — unary \c - (prefix).
    ast::Expr parse_unary();

    /// Level 8 — primary: variable, literal, or parenthesised expression.
    ast::Expr parse_primary();

    /**
     * \brief Level 9 — filter chain: \c expr | filter_name(args…).
     *
     * \param base  The already-parsed left-hand expression.
     * \return \p base wrapped in zero or more \c ast::Filter nodes.
     */
    ast::Expr parse_filter_chain(ast::Expr base);

    // -------------------------------------------------------------------------
    // Token-stream helpers
    // -------------------------------------------------------------------------

    /**
     * \brief Assert the current token has the expected type and advance.
     *
     * \throws std::runtime_error if the current token does not match.
     */
    lexer::Token              consume(lexer::TokenType expected);

    /**
     * \brief Non-consuming lookahead.
     *
     * \param offset  0 = current token, 1 = next token, …
     * \return Reference to the token at \c pos_ + \p offset (clamped to Eof).
     */
    const lexer::Token&       peek(size_t offset = 0) const;

    /// Return \c true if the token at \c pos_ + \p offset has type \p t.
    bool                      check(lexer::TokenType t, size_t offset = 0) const;

    /// Return \c true when \c pos_ is at or past the Eof token.
    bool                      at_end() const;

    /// Build and throw a \c std::runtime_error with location prefix.
    [[noreturn]] void         error(std::string_view message) const;

    /**
     * \brief Helper: build a dotted-path \c string_view spanning two tokens.
     *
     * \details
     * Both \p first and \p last are identifier tokens whose \c value fields
     * are views into the same underlying source buffer.  This function
     * constructs a \c string_view from the start of \p first to the end of
     * \p last by pointer arithmetic — valid because they share the same
     * contiguous source allocation.
     *
     * \param first The first token in the path (e.g. \c foo in \c foo.bar.baz).
     * \param last  The last token in the path (e.g. \c baz in \c foo.bar.baz).
     * \retval string_view A view spanning from the start of \p first to the end of \p last.
     */
    static std::string_view span_tokens(const lexer::Token& first,
                                        const lexer::Token& last);
};

} // namespace guss::render
