/**
 * \file lexer.cpp
 * \brief Implementation of the Guss template lexer.
 */
#include "guss/render/lexer.hpp"

#include <cctype>
#include <format>
#include <stdexcept>
#include <string>

namespace guss::render::lexer {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Lexer::Lexer(std::string_view source, std::string_view template_name)
    : source_(source), template_name_(template_name)
{}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

std::vector<Token> Lexer::tokenize() {
    tokens_.clear();
    pos_  = 0;
    line_ = 1;
    col_  = 1;

    while (pos_ < source_.size()) {
        if (match("{#")) {
            skip_comment();
        } else if (match("{{") || match("{%")) {
            // Determine tag type and whether leading-trim is active.
            const bool is_expr  = (source_[pos_ + 1] == '{');
            const std::string_view closing = is_expr ? "}}" : "%}";

            // Check for trim-before marker: {%- or {{-
            const bool trim_before = (pos_ + 2 < source_.size() && source_[pos_ + 2] == '-');

            if (trim_before) {
                trim_trailing_whitespace();
            }

            bool trim_after = false;
            scan_tag(closing, trim_after);

            // If trim_after was set (-%} or -}}), eat whitespace at start
            // of the next text segment. We handle this lazily in scan_text
            // by recording a flag — but the simplest correct approach is to
            // consume whitespace from source_ right now so scan_text picks
            // up clean content.
            if (trim_after) {
                while (pos_ < source_.size() &&
                       (source_[pos_] == ' '  || source_[pos_] == '\t' ||
                        source_[pos_] == '\n'  || source_[pos_] == '\r')) {
                    advance();
                }
            }

        } else {
            scan_text();
        }
    }

    // Emit EOF token.
    tokens_.push_back(make(TokenType::Eof, source_.substr(pos_, 0)));
    return std::move(tokens_);
}

// ---------------------------------------------------------------------------
// scan_text
// ---------------------------------------------------------------------------

void Lexer::scan_text() {
    const size_t   start_pos  = pos_;
    const uint32_t start_line = line_;
    const uint32_t start_col  = col_;

    while (pos_ < source_.size()) {
        // Stop at any opening delimiter.
        if (source_[pos_] == '{' && pos_ + 1 < source_.size()) {
            const char next = source_[pos_ + 1];
            if (next == '{' || next == '%' || next == '#') {
                break;
            }
        }
        advance();
    }

    if (pos_ > start_pos) {
        tokens_.push_back(make_at(TokenType::Text,
                                  source_.substr(start_pos, pos_ - start_pos),
                                  start_line, start_col));
    }
}

// ---------------------------------------------------------------------------
// scan_tag — tokenise inside {{ … }} or {% … %}
// ---------------------------------------------------------------------------

void Lexer::scan_tag(std::string_view closing, bool& trim_after) {
    // Determine open/close token types from the closing delimiter.
    const bool is_expr_tag  = (closing == "}}");
    const TokenType open_tt  = is_expr_tag ? TokenType::ExprOpen  : TokenType::BlockOpen;
    const TokenType close_tt = is_expr_tag ? TokenType::ExprClose : TokenType::BlockClose;

    // Emit the opening delimiter token, then consume the 2-character sequence.
    const uint32_t open_line = line_;
    const uint32_t open_col  = col_;
    advance(2); // consume {{ or {%
    tokens_.push_back(make_at(open_tt, source_.substr(pos_ - 2, 2),
                               open_line, open_col));

    // If the third char was '-', consume it too (trim-before marker already
    // used by caller; we just need to skip past it).
    if (pos_ < source_.size() && source_[pos_] == '-') {
        advance();
    }

    trim_after = false;

    while (pos_ < source_.size()) {
        // --- skip whitespace ---
        while (pos_ < source_.size() &&
               (source_[pos_] == ' ' || source_[pos_] == '\t' ||
                source_[pos_] == '\n' || source_[pos_] == '\r')) {
            advance();
        }

        if (pos_ >= source_.size()) {
            error(std::format("unterminated tag, expected '{}'",
                              std::string(closing)));
        }

        // --- check for closing delimiter (with optional trim marker) ---
        // Pattern: [-]}} or [-]%}
        if (source_[pos_] == '-' &&
            pos_ + 1 < source_.size() &&
            source_.substr(pos_ + 1, closing.size()) == closing) {
            trim_after = true;
            advance();           // consume '-'
            const uint32_t cl_line = line_;
            const uint32_t cl_col  = col_;
            advance(closing.size()); // consume }} or %}
            tokens_.push_back(make_at(close_tt,
                                       source_.substr(pos_ - closing.size(), closing.size()),
                                       cl_line, cl_col));
            return;
        }

        if (match(closing)) {
            const uint32_t cl_line = line_;
            const uint32_t cl_col  = col_;
            advance(closing.size());
            tokens_.push_back(make_at(close_tt,
                                       source_.substr(pos_ - closing.size(), closing.size()),
                                       cl_line, cl_col));
            return;
        }

        // --- dispatch on current character ---
        const char c = source_[pos_];

        if (c == '"' || c == '\'') {
            scan_string(c);
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            scan_number();
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            // Identifier or keyword
            const size_t   id_start  = pos_;
            const uint32_t id_line   = line_;
            const uint32_t id_col    = col_;
            while (pos_ < source_.size() &&
                   (std::isalnum(static_cast<unsigned char>(source_[pos_])) ||
                    source_[pos_] == '_')) {
                advance();
            }
            const std::string_view word = source_.substr(id_start, pos_ - id_start);
            tokens_.push_back(make_at(keyword_or_identifier(word), word,
                                      id_line, id_col));
        } else if (c == '=') {
            if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
                tokens_.push_back(make(TokenType::Op_Eq, source_.substr(pos_, 2)));
                advance(2);
            } else {
                error(std::format("unexpected character '{}'", c));
            }
        } else if (c == '!') {
            if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
                tokens_.push_back(make(TokenType::Op_Ne, source_.substr(pos_, 2)));
                advance(2);
            } else {
                error(std::format("unexpected character '{}'", c));
            }
        } else if (c == '<') {
            if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
                tokens_.push_back(make(TokenType::Op_Le, source_.substr(pos_, 2)));
                advance(2);
            } else {
                tokens_.push_back(make(TokenType::Op_Lt, source_.substr(pos_, 1)));
                advance();
            }
        } else if (c == '>') {
            if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
                tokens_.push_back(make(TokenType::Op_Ge, source_.substr(pos_, 2)));
                advance(2);
            } else {
                tokens_.push_back(make(TokenType::Op_Gt, source_.substr(pos_, 1)));
                advance();
            }
        } else if (c == '.') {
            tokens_.push_back(make(TokenType::Dot, source_.substr(pos_, 1)));
            advance();
        } else if (c == '|') {
            tokens_.push_back(make(TokenType::Pipe, source_.substr(pos_, 1)));
            advance();
        } else if (c == '(') {
            tokens_.push_back(make(TokenType::LParen, source_.substr(pos_, 1)));
            advance();
        } else if (c == ')') {
            tokens_.push_back(make(TokenType::RParen, source_.substr(pos_, 1)));
            advance();
        } else if (c == ',') {
            tokens_.push_back(make(TokenType::Comma, source_.substr(pos_, 1)));
            advance();
        } else if (c == '+') {
            tokens_.push_back(make(TokenType::Op_Add, source_.substr(pos_, 1)));
            advance();
        } else if (c == '-') {
            tokens_.push_back(make(TokenType::Op_Sub, source_.substr(pos_, 1)));
            advance();
        } else if (c == '*') {
            tokens_.push_back(make(TokenType::Op_Mul, source_.substr(pos_, 1)));
            advance();
        } else if (c == '/') {
            tokens_.push_back(make(TokenType::Op_Div, source_.substr(pos_, 1)));
            advance();
        } else if (c == '%') {
            // Could be the start of %} — check before emitting Op_Mod.
            if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '}') {
                // This is the block closing delimiter; fall through to the
                // closing-delimiter check at the top of the loop.
                // We should not reach here because match(closing) is checked
                // before the dispatch; if we do, it is an error.
                error(std::format("unexpected character '{}' inside template tag", c));
            } else {
                tokens_.push_back(make(TokenType::Op_Mod, source_.substr(pos_, 1)));
                advance();
            }
        } else {
            error(std::format("unexpected character '{}' inside template tag", c));
        }
    }

    // Reached end of source without finding closing delimiter.
    error(std::format("unterminated tag, expected '{}'", std::string(closing)));
}

// ---------------------------------------------------------------------------
// scan_string
// ---------------------------------------------------------------------------

void Lexer::scan_string(char quote) {
    // Capture position of the opening quote before advancing past it.
    const uint32_t tok_line = line_;
    const uint32_t tok_col  = col_;

    advance(); // consume opening quote

    const size_t content_start = pos_;

    while (pos_ < source_.size() && source_[pos_] != quote) {
        if (source_[pos_] == '\n') {
            error("unterminated string literal — newline before closing quote");
        }
        if (source_[pos_] == '\\' && pos_ + 1 < source_.size()) {
            // Simple escape: consume both the backslash and the escaped char.
            advance(2);
        } else {
            advance();
        }
    }

    if (pos_ >= source_.size()) {
        error("unterminated string literal");
    }

    const std::string_view content = source_.substr(content_start,
                                                     pos_ - content_start);
    advance(); // consume closing quote

    tokens_.push_back(make_at(TokenType::StringLiteral, content,
                               tok_line, tok_col));
}

// ---------------------------------------------------------------------------
// scan_number
// ---------------------------------------------------------------------------

void Lexer::scan_number() {
    const size_t   num_start = pos_;
    const uint32_t num_line  = line_;
    const uint32_t num_col   = col_;

    while (pos_ < source_.size() &&
           std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
        advance();
    }

    bool is_float = false;
    if (pos_ < source_.size() && source_[pos_] == '.') {
        // Make sure it's actually a fractional part, not a member accessor.
        if (pos_ + 1 < source_.size() &&
            std::isdigit(static_cast<unsigned char>(source_[pos_ + 1]))) {
            is_float = true;
            advance(); // consume '.'
            while (pos_ < source_.size() &&
                   std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
                advance();
            }
        }
    }

    // Optional exponent.
    if (pos_ < source_.size() &&
        (source_[pos_] == 'e' || source_[pos_] == 'E')) {
        is_float = true;
        advance();
        if (pos_ < source_.size() &&
            (source_[pos_] == '+' || source_[pos_] == '-')) {
            advance();
        }
        if (pos_ >= source_.size() ||
            !std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
            error("malformed numeric exponent");
        }
        while (pos_ < source_.size() &&
               std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
            advance();
        }
    }

    const std::string_view raw = source_.substr(num_start, pos_ - num_start);
    tokens_.push_back(make_at(is_float ? TokenType::FloatLiteral
                                       : TokenType::IntLiteral,
                               raw, num_line, num_col));
}

// ---------------------------------------------------------------------------
// skip_comment — {# … #}
// ---------------------------------------------------------------------------

void Lexer::skip_comment() {
    advance(2); // consume {#
    while (pos_ < source_.size()) {
        if (match("#}")) {
            advance(2); // consume #}
            return;
        }
        advance();
    }
    error("unterminated comment block, expected '#}'");
}

// ---------------------------------------------------------------------------
// Whitespace trimming helpers
// ---------------------------------------------------------------------------

void Lexer::trim_trailing_whitespace() {
    // Find the last Text token and strip its trailing whitespace.
    for (auto it = tokens_.rbegin(); it != tokens_.rend(); ++it) {
        if (it->type == TokenType::Text) {
            std::string_view v = it->value;
            // Strip trailing whitespace characters.
            size_t end = v.size();
            while (end > 0 &&
                   (v[end - 1] == ' '  || v[end - 1] == '\t' ||
                    v[end - 1] == '\n' || v[end - 1] == '\r')) {
                --end;
            }
            if (end == 0) {
                // The text token became empty — remove it entirely.
                tokens_.erase((it + 1).base());
            } else {
                it->value = v.substr(0, end);
            }
            return;
        }
        // If we hit a non-Text token we've passed out of the preceding text
        // segment; nothing to trim.
        break;
    }
}

// ---------------------------------------------------------------------------
// Low-level primitives
// ---------------------------------------------------------------------------

Token Lexer::make(TokenType t, std::string_view v) const {
    return Token{t, v, line_, col_};
}

Token Lexer::make_at(TokenType t, std::string_view v,
                      uint32_t line, uint32_t col) const {
    return Token{t, v, line, col};
}

void Lexer::advance(size_t n) {
    for (size_t i = 0; i < n && pos_ < source_.size(); ++i) {
        if (source_[pos_] == '\n') {
            ++line_;
            col_ = 1;
        } else {
            ++col_;
        }
        ++pos_;
    }
}

char Lexer::peek(size_t offset) const {
    const size_t idx = pos_ + offset;
    if (idx >= source_.size()) {
        return '\0';
    }
    return source_[idx];
}

bool Lexer::match(std::string_view s) const {
    if (pos_ + s.size() > source_.size()) {
        return false;
    }
    return source_.substr(pos_, s.size()) == s;
}

TokenType Lexer::keyword_or_identifier(std::string_view s) const {
    if (s == "for")      return TokenType::Keyword_For;
    if (s == "endfor")   return TokenType::Keyword_EndFor;
    if (s == "if")       return TokenType::Keyword_If;
    if (s == "elif")     return TokenType::Keyword_Elif;
    if (s == "else")     return TokenType::Keyword_Else;
    if (s == "endif")    return TokenType::Keyword_EndIf;
    if (s == "block")    return TokenType::Keyword_Block;
    if (s == "endblock") return TokenType::Keyword_EndBlock;
    if (s == "extends")  return TokenType::Keyword_Extends;
    if (s == "include")  return TokenType::Keyword_Include;
    if (s == "in")       return TokenType::Keyword_In;
    if (s == "not")      return TokenType::Keyword_Not;
    if (s == "and")      return TokenType::Keyword_And;
    if (s == "or")       return TokenType::Keyword_Or;
    if (s == "true")     return TokenType::Keyword_True;
    if (s == "false")    return TokenType::Keyword_False;
    return TokenType::Identifier;
}

void Lexer::error(const std::string& msg) const {
    throw std::runtime_error(
        std::format("{}:{}:{}: {}", template_name_, line_, col_, msg));
}

} // namespace guss::render::lexer
