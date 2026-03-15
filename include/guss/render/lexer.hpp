/**
 * \file lexer.hpp
 * \brief Lexer for template parsing in the Guss rendering engine.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace guss::render::lexer {

/**
 * \brief Enumeration of token types for the template lexer.
 *
 * \details
 * This enum defines the various token types that the lexer can produce when parsing template strings.
 * It includes tokens for raw text, expression delimiters, block delimiters, comments, identifiers,
 * operators, keywords, and punctuation. The lexer will emit these tokens to be consumed by the parser
 * for building the abstract syntax tree (AST) of the template.
 */
enum class TokenType {
    Text,               ///< Raw HTML/text passthrough
    ExprOpen,           ///< {{
    ExprClose,          ///< }}
    BlockOpen,          ///< {%
    BlockClose,         ///< %}
    Identifier,         ///< post, title, loop.index
    Dot,                ///< .
    Pipe,               ///< |
    StringLiteral,      ///< "hello"
    IntLiteral,         ///< 42
    FloatLiteral,       ///< 3.14
    Keyword_For,        ///< for
    Keyword_EndFor,     ///< endfor
    Keyword_If,         ///< if
    Keyword_Elif,       ///< elif
    Keyword_Else,       ///< else
    Keyword_EndIf,      ///< endif
    Keyword_Block,      ///< block
    Keyword_EndBlock,   ///< endblock
    Keyword_Extends,    ///< extends
    Keyword_Include,    ///< include
    Keyword_In,         ///< in
    Keyword_Not,        ///< not
    Keyword_And,        ///< and
    Keyword_Or,         ///< or
    Keyword_True,       ///< true
    Keyword_False,      ///< false
    Keyword_Set,        ///< set
    Keyword_Super,      ///< super
    Op_Assign,          ///< =
    Op_Eq,              ///< ==
    Op_Ne,              ///< !=
    Op_Lt,              ///< <
    Op_Gt,              ///< >
    Op_Le,              ///< <=
    Op_Ge,              ///< >=
    Op_Add,             ///< +
    Op_Sub,             ///< -
    Op_Mul,             ///< *
    Op_Div,             ///< /
    Op_Mod,             ///< %
    LParen,             ///< (
    RParen,             ///< )
    Comma,              ///< ,
    Eof,                ///< End of input
};

// ---------------------------------------------------------------------------
// Token
// ---------------------------------------------------------------------------

/**
 * \brief A single lexer token produced from a template source string.
 *
 * \details
 * All \c string_view fields are non-owning views into the original source
 * buffer passed to \c Lexer.  The source buffer must outlive any \c Token
 * (and the \c std::vector returned by \c Lexer::tokenize()).
 *
 * For \c StringLiteral tokens the \c value view covers the content between
 * the surrounding quote characters (the quotes themselves are excluded).
 */
struct Token {
    TokenType        type;   ///< Category of this token.
    std::string_view value;  ///< Slice of the original source — zero allocation.
    uint32_t         line;   ///< 1-based source line number.
    uint32_t         col;    ///< 1-based source column number.
};

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

/**
 * \brief Single-pass template lexer that converts a source string into a flat
 *        vector of \c Token values.
 *
 * \details
 * The lexer operates in two alternating modes:
 *
 * **Text mode** — characters are accumulated verbatim until a Jinja2 opening
 * delimiter (\c \{\{ , \c \{% , or \c \{# ) is encountered.  A single
 * \c Text token covering the entire span is emitted (empty spans are silently
 * dropped).
 *
 * **Tag mode** — after entering a delimiter, whitespace is skipped and
 * identifiers, operators, and literals are tokenised until the matching
 * closing delimiter (\c \}\} , \c %\} , or \c #\} ) is consumed.
 *
 * Whitespace trimming modifiers (\c {%- and \c -%\} ) strip leading/trailing
 * whitespace from the adjacent text token in the same way as Jinja2.
 *
 * Comments (\c \{# … #\} ) are consumed entirely; no tokens are emitted.
 *
 * \note The \c source passed to the constructor must remain alive for the
 *       entire lifetime of the returned token vector, because all
 *       \c string_view fields inside tokens point directly into \c source.
 */
class Lexer {
public:
    /**
     * \brief Construct a Lexer over the given source view.
     *
     * \param source  A non-owning view of the full template text.
     *                Must outlive the \c Lexer and the tokens it produces.
     * \param template_name  Optional name used in error messages
     *                       (format: \c name:line:col ).
     */
    explicit Lexer(std::string_view source,
                   std::string_view template_name = "<template>");

    /**
     * \brief Tokenize the entire source in one pass.
     *
     * \return A flat vector of \c Token values.  All \c string_view fields
     *         inside tokens point into the source passed at construction time.
     *         The final token is always \c TokenType::Eof.
     *
     * \throws std::runtime_error with message \c "name:line:col: <reason>"
     *         on any lexical error (unterminated string, unknown character
     *         inside a tag, unterminated delimiter, etc.).
     */
    [[nodiscard]] std::vector<Token> tokenize();

private:
    std::string_view   source_;
    std::string_view   template_name_;
    size_t             pos_  = 0;
    uint32_t           line_ = 1;
    uint32_t           col_  = 1;
    /// \note Only valid during a call to \c tokenize(); cleared on each entry.
    std::vector<Token> tokens_;

    // --- scanning helpers ---------------------------------------------------

    /**
     * Accumulate raw text until \c \{\{ , \c \{% , or \c \{# .
     * Emits a single \c Text token; empty spans are dropped.
     */
    void scan_text();

    /**
     * Tokenize inside an expression (\c \{\{ ) or block (\c \{% ) delimiter.
     * \param closing    The two-character closing delimiter (\c "}}" or \c "%}").
     * \param trim_after Set to \c true when the closing delimiter ends with
     *                   \c - (i.e. \c -%\} ), so the caller can trim the next text token.
     */
    void scan_tag(std::string_view closing, bool& trim_after);

    /**
     * Scan a quoted string literal (single or double quotes).
     * Emits a \c StringLiteral token whose value excludes the quote chars.
     */
    void scan_string(char quote);

    /// Scan an integer or floating-point numeric literal.
    void scan_number();

    /// Consume a comment block (\c \{# … #\} ) entirely.  No token emitted.
    void skip_comment();

    // --- low-level primitives -----------------------------------------------

    /// Construct a token anchored at the current position.
    [[nodiscard]] Token make(TokenType t, std::string_view v) const;

    /// Construct a token anchored at an explicit line/column.
    [[nodiscard]] Token make_at(TokenType t, std::string_view v,
                                uint32_t line, uint32_t col) const;

    /// Advance \p n characters, updating line/col tracking.
    void advance(size_t n = 1);

    /**
     * Peek at the character \p offset positions ahead of \c pos_.
     * Returns \c '\\0' if out of bounds.
     */
    [[nodiscard]] char peek(size_t offset = 0) const;

    /// Return \c true if \p s matches exactly at the current position.
    [[nodiscard]] bool match(std::string_view s) const;

    /// Map a keyword string to its \c TokenType, or \c Identifier if unknown.
    [[nodiscard]] TokenType keyword_or_identifier(std::string_view s) const;

    /// Build and throw a \c std::runtime_error with location prefix.
    [[noreturn]] void error(const std::string& msg) const;

    /// Strip trailing whitespace/newline characters from the last \c Text token.
    void trim_trailing_whitespace();
};

} // namespace guss::render::lexer