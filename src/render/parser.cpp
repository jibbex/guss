/**
 * \file parser.cpp
 * \brief Recursive-descent parser implementation for the Guss template engine.
 */
#include "guss/render/parser.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <format>
#include <functional>
#include <stdexcept>
#include <string>

namespace guss::render {

using namespace lexer;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Parser::Parser(std::vector<Token> tokens, std::string template_name)
    : tokens_(std::move(tokens))
    , template_name_(std::move(template_name))
{}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

ast::Template Parser::parse() {
    ast::Template tmpl;

    while (!at_end()) {
        if (peek().type == TokenType::Eof) {
            break;
        }

        ast::Node node = parse_node();

        // Validate ExtendsNode placement: must be the first non-text node.
        if (std::holds_alternative<std::unique_ptr<ast::ExtendsNode>>(node)) {
            const auto& ext =
                std::get<std::unique_ptr<ast::ExtendsNode>>(node);

            for (const auto& existing : tmpl.nodes) {
                bool is_whitespace_text = false;
                if (const auto* tp = std::get_if<std::unique_ptr<ast::TextNode>>(&existing)) {
                    std::string_view txt = (*tp)->text;
                    is_whitespace_text = std::all_of(txt.begin(), txt.end(),
                        [](unsigned char c) { return std::isspace(c); });
                }
                if (!is_whitespace_text) {
                    error("'extends' must be the first statement in a template");
                }
            }

            tmpl.parent = ext->parent_template;
        }

        tmpl.nodes.push_back(std::move(node));
    }

    // Populate the blocks map by walking the full node tree.
    std::function<void(std::vector<ast::Node>&)> collect_blocks =
        [&](std::vector<ast::Node>& nodes) {
            for (auto& n : nodes) {
                if (auto* bp =
                        std::get_if<std::unique_ptr<ast::BlockNode>>(&n)) {
                    tmpl.blocks[(*bp)->name] = bp->get();
                    collect_blocks((*bp)->body);
                } else if (auto* fp =
                               std::get_if<std::unique_ptr<ast::ForNode>>(
                                   &n)) {
                    collect_blocks((*fp)->body);
                    collect_blocks((*fp)->else_body);
                } else if (auto* ip =
                               std::get_if<std::unique_ptr<ast::IfNode>>(
                                   &n)) {
                    for (auto& branch : (*ip)->branches) {
                        collect_blocks(branch.body);
                    }
                    collect_blocks((*ip)->else_body);
                }
            }
        };

    collect_blocks(tmpl.nodes);

    return tmpl;
}

// ---------------------------------------------------------------------------
// Statement parsing
// ---------------------------------------------------------------------------

ast::Node Parser::parse_node() {
    const Token& tok = peek();

    if (tok.type == TokenType::Text) {
        std::string_view text = tok.value;
        consume(TokenType::Text);
        return std::make_unique<ast::TextNode>(ast::TextNode{text});
    }

    if (tok.type == TokenType::ExprOpen) {
        consume(TokenType::ExprOpen);
        ast::Expr expr = parse_expr();
        consume(TokenType::ExprClose);
        return std::make_unique<ast::ExprNode>(ast::ExprNode{std::move(expr)});
    }

    if (tok.type == TokenType::BlockOpen) {
        return parse_block_tag();
    }

    error(std::format("unexpected token '{}'", std::string(tok.value)));
}

ast::Node Parser::parse_block_tag() {
    consume(TokenType::BlockOpen);

    const Token& kw = peek();

    switch (kw.type) {
    case TokenType::Keyword_For: {
        ast::ForNode fn = parse_for();
        return std::make_unique<ast::ForNode>(std::move(fn));
    }
    case TokenType::Keyword_If: {
        ast::IfNode in = parse_if();
        return std::make_unique<ast::IfNode>(std::move(in));
    }
    case TokenType::Keyword_Block: {
        ast::BlockNode bn = parse_block();
        return std::make_unique<ast::BlockNode>(std::move(bn));
    }
    case TokenType::Keyword_Extends: {
        ast::ExtendsNode en = parse_extends();
        return std::make_unique<ast::ExtendsNode>(std::move(en));
    }
    case TokenType::Keyword_Include: {
        ast::IncludeNode in = parse_include();
        return std::make_unique<ast::IncludeNode>(std::move(in));
    }
    case TokenType::Keyword_Set: {
        consume(TokenType::Keyword_Set);

        const Token& var_tok = peek();
        if (var_tok.type != TokenType::Identifier) {
            error(std::format("expected variable name after 'set', got '{}'",
                              std::string(var_tok.value)));
        }
        std::string var_name(var_tok.value);
        consume(TokenType::Identifier);

        const Token& eq_tok = peek();
        if (eq_tok.type != TokenType::Op_Assign) {
            error("expected '=' after variable name in {% set %}");
        }
        consume(TokenType::Op_Assign);

        ast::Expr val = parse_expr();
        consume(TokenType::BlockClose);

        ast::SetNode sn;
        sn.var_name = std::move(var_name);
        sn.value    = std::move(val);
        return std::make_unique<ast::SetNode>(std::move(sn));
    }
    case TokenType::Keyword_EndFor:
        error("unexpected 'endfor' without matching 'for'");
    case TokenType::Keyword_EndIf:
        error("unexpected 'endif' without matching 'if'");
    case TokenType::Keyword_EndBlock:
        error("unexpected 'endblock' without matching 'block'");
    default:
        error(std::format("unknown block tag keyword '{}'",
                          std::string(kw.value)));
    }
}

// ---------------------------------------------------------------------------
// parse_body — collects nodes until one of the stop keywords is encountered
// ---------------------------------------------------------------------------

std::vector<ast::Node> Parser::parse_body(
    const std::vector<TokenType>& stop_keywords,
    TokenType&                    found_keyword)
{
    std::vector<ast::Node> body;

    while (true) {
        if (at_end()) {
            error("unexpected end of template — missing closing tag");
        }

        // Look ahead: is the next thing a BlockOpen followed by a stop keyword?
        if (peek().type == TokenType::BlockOpen) {
            const Token& next = peek(1);
            for (auto stop : stop_keywords) {
                if (next.type == stop) {
                    found_keyword = stop;
                    // Consume BlockOpen and the keyword; caller handles
                    // whatever follows (BlockClose, expr for elif, etc.).
                    consume(TokenType::BlockOpen);
                    consume(stop);
                    return body;
                }
            }
        }

        body.push_back(parse_node());
    }
}

// ---------------------------------------------------------------------------
// parse_for
// ---------------------------------------------------------------------------

ast::ForNode Parser::parse_for() {
    consume(TokenType::Keyword_For);

    // Loop variable.
    const Token& var_tok = peek();
    consume(TokenType::Identifier);
    std::string var_name(var_tok.value);

    consume(TokenType::Keyword_In);

    ast::Expr iterable = parse_expr();

    consume(TokenType::BlockClose);

    // Body ends at {% endfor %} or {% else %}.
    TokenType found{};
    std::vector<ast::Node> body = parse_body(
        {TokenType::Keyword_EndFor, TokenType::Keyword_Else},
        found);

    std::vector<ast::Node> else_body;

    if (found == TokenType::Keyword_Else) {
        // parse_body consumed BlockOpen + Keyword_Else; consume BlockClose.
        consume(TokenType::BlockClose);

        TokenType found2{};
        else_body = parse_body({TokenType::Keyword_EndFor}, found2);
        // Consume BlockClose for endfor.
        consume(TokenType::BlockClose);
    } else {
        // found == Keyword_EndFor — consume its BlockClose.
        consume(TokenType::BlockClose);
    }

    return ast::ForNode{
        std::move(var_name),
        std::move(iterable),
        std::move(body),
        std::move(else_body)
    };
}

// ---------------------------------------------------------------------------
// parse_if
// ---------------------------------------------------------------------------

ast::IfNode Parser::parse_if() {
    consume(TokenType::Keyword_If);

    ast::IfNode ifnode;

    // First if-branch.
    {
        ast::Expr cond = parse_expr();
        consume(TokenType::BlockClose);

        TokenType found{};
        std::vector<ast::Node> branch_body = parse_body(
            {TokenType::Keyword_EndIf,
             TokenType::Keyword_Elif,
             TokenType::Keyword_Else},
            found);

        ifnode.branches.push_back(
            ast::IfNode::Branch{std::move(cond), std::move(branch_body)});

        // Consume any number of elif branches.
        while (found == TokenType::Keyword_Elif) {
            ast::Expr elif_cond = parse_expr();
            consume(TokenType::BlockClose);

            TokenType found2{};
            std::vector<ast::Node> elif_body = parse_body(
                {TokenType::Keyword_EndIf,
                 TokenType::Keyword_Elif,
                 TokenType::Keyword_Else},
                found2);

            ifnode.branches.push_back(
                ast::IfNode::Branch{std::move(elif_cond),
                                    std::move(elif_body)});
            found = found2;
        }

        if (found == TokenType::Keyword_Else) {
            // parse_body consumed BlockOpen + Keyword_Else; consume BlockClose.
            consume(TokenType::BlockClose);

            TokenType found3{};
            ifnode.else_body = parse_body({TokenType::Keyword_EndIf}, found3);
            // Consume BlockClose for endif.
            consume(TokenType::BlockClose);
        } else {
            // found == Keyword_EndIf — consume its BlockClose.
            consume(TokenType::BlockClose);
        }
    }

    return ifnode;
}

// ---------------------------------------------------------------------------
// parse_block
// ---------------------------------------------------------------------------

ast::BlockNode Parser::parse_block() {
    consume(TokenType::Keyword_Block);

    const Token& name_tok = peek();
    consume(TokenType::Identifier);
    std::string name(name_tok.value);

    consume(TokenType::BlockClose);

    TokenType found{};
    std::vector<ast::Node> body = parse_body({TokenType::Keyword_EndBlock},
                                             found);
    consume(TokenType::BlockClose);

    return ast::BlockNode{std::move(name), std::move(body)};
}

// ---------------------------------------------------------------------------
// parse_extends
// ---------------------------------------------------------------------------

ast::ExtendsNode Parser::parse_extends() {
    consume(TokenType::Keyword_Extends);

    const Token& str_tok = peek();
    consume(TokenType::StringLiteral);
    std::string parent(str_tok.value);

    consume(TokenType::BlockClose);

    return ast::ExtendsNode{std::move(parent)};
}

// ---------------------------------------------------------------------------
// parse_include
// ---------------------------------------------------------------------------

ast::IncludeNode Parser::parse_include() {
    consume(TokenType::Keyword_Include);

    const Token& str_tok = peek();
    consume(TokenType::StringLiteral);
    std::string tname(str_tok.value);

    consume(TokenType::BlockClose);

    return ast::IncludeNode{std::move(tname)};
}

// ---------------------------------------------------------------------------
// Expression parsing
// ---------------------------------------------------------------------------

ast::Expr Parser::parse_expr() {
    return parse_or();
}

ast::Expr Parser::parse_or() {
    ast::Expr left = parse_and();

    while (check(TokenType::Keyword_Or)) {
        consume(TokenType::Keyword_Or);
        ast::Expr right = parse_and();
        left = std::make_unique<ast::BinaryOp>(
            ast::BinaryOp{TokenType::Keyword_Or,
                          std::move(left),
                          std::move(right)});
    }

    return left;
}

ast::Expr Parser::parse_and() {
    ast::Expr left = parse_not();

    while (check(TokenType::Keyword_And)) {
        consume(TokenType::Keyword_And);
        ast::Expr right = parse_not();
        left = std::make_unique<ast::BinaryOp>(
            ast::BinaryOp{TokenType::Keyword_And,
                          std::move(left),
                          std::move(right)});
    }

    return left;
}

ast::Expr Parser::parse_not() {
    if (check(TokenType::Keyword_Not)) {
        consume(TokenType::Keyword_Not);
        ast::Expr operand = parse_not(); // right-recursive for prefix chains
        return std::make_unique<ast::UnaryOp>(
            ast::UnaryOp{TokenType::Keyword_Not, std::move(operand)});
    }
    return parse_comparison();
}

ast::Expr Parser::parse_comparison() {
    ast::Expr left = parse_additive();

    // Non-chaining: consume at most one comparison operator.
    const TokenType t = peek().type;
    if (t == TokenType::Op_Eq || t == TokenType::Op_Ne ||
        t == TokenType::Op_Lt || t == TokenType::Op_Gt ||
        t == TokenType::Op_Le || t == TokenType::Op_Ge)
    {
        consume(t);
        ast::Expr right = parse_additive();
        left = std::make_unique<ast::BinaryOp>(
            ast::BinaryOp{t, std::move(left), std::move(right)});
    }

    return left;
}

ast::Expr Parser::parse_additive() {
    ast::Expr left = parse_multiplicative();

    while (check(TokenType::Op_Add) || check(TokenType::Op_Sub)) {
        const TokenType op = peek().type;
        consume(op);
        ast::Expr right = parse_multiplicative();
        left = std::make_unique<ast::BinaryOp>(
            ast::BinaryOp{op, std::move(left), std::move(right)});
    }

    return left;
}

ast::Expr Parser::parse_multiplicative() {
    ast::Expr left = parse_unary();

    while (check(TokenType::Op_Mul) ||
           check(TokenType::Op_Div) ||
           check(TokenType::Op_Mod))
    {
        const TokenType op = peek().type;
        consume(op);
        ast::Expr right = parse_unary();
        left = std::make_unique<ast::BinaryOp>(
            ast::BinaryOp{op, std::move(left), std::move(right)});
    }

    return left;
}

ast::Expr Parser::parse_unary() {
    if (check(TokenType::Op_Sub)) {
        consume(TokenType::Op_Sub);
        ast::Expr operand = parse_unary();
        return std::make_unique<ast::UnaryOp>(
            ast::UnaryOp{TokenType::Op_Sub, std::move(operand)});
    }
    return parse_primary();
}

ast::Expr Parser::parse_primary() {
    const Token& tok = peek();

    switch (tok.type) {
    case TokenType::Keyword_True: {
        consume(TokenType::Keyword_True);
        return parse_filter_chain(
            std::make_unique<ast::BoolLit>(ast::BoolLit{true}));
    }

    case TokenType::Keyword_False: {
        consume(TokenType::Keyword_False);
        return parse_filter_chain(
            std::make_unique<ast::BoolLit>(ast::BoolLit{false}));
    }

    case TokenType::StringLiteral: {
        std::string_view sv = tok.value;
        consume(TokenType::StringLiteral);
        return parse_filter_chain(
            std::make_unique<ast::StringLit>(ast::StringLit{sv}));
    }

    case TokenType::IntLiteral: {
        std::string_view sv = tok.value;
        consume(TokenType::IntLiteral);
        int64_t val = 0;
        std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return parse_filter_chain(
            std::make_unique<ast::IntLit>(ast::IntLit{val}));
    }

    case TokenType::FloatLiteral: {
        std::string_view sv = tok.value;
        consume(TokenType::FloatLiteral);
        // std::from_chars for double requires <charconv> with full float support;
        // use stod for portability.
        double val = std::stod(std::string(sv));
        return parse_filter_chain(
            std::make_unique<ast::FloatLit>(ast::FloatLit{val}));
    }

    case TokenType::Identifier: {
        // Save the token's position in the vector (stable reference).
        size_t first_idx = pos_;
        consume(TokenType::Identifier);

        // Accumulate dot-separated path segments.
        size_t last_idx = first_idx; // index of the last identifier consumed
        while (check(TokenType::Dot) && check(TokenType::Identifier, 1)) {
            consume(TokenType::Dot);
            last_idx = pos_;
            consume(TokenType::Identifier);
        }

        std::string_view path =
            span_tokens(tokens_[first_idx], tokens_[last_idx]);
        return parse_filter_chain(
            std::make_unique<ast::Variable>(ast::Variable{path}));
    }

    case TokenType::Keyword_Super: {
        consume(TokenType::Keyword_Super);
        consume(TokenType::LParen);
        consume(TokenType::RParen);
        return parse_filter_chain(std::make_unique<ast::SuperNode>());
    }

    case TokenType::LParen: {
        consume(TokenType::LParen);
        ast::Expr inner = parse_expr();
        consume(TokenType::RParen);
        return parse_filter_chain(std::move(inner));
    }

    default:
        error(std::format("expected expression, got '{}'",
                          std::string(tok.value)));
    }
}

ast::Expr Parser::parse_filter_chain(ast::Expr base) {
    while (check(TokenType::Pipe)) {
        consume(TokenType::Pipe);

        // Get filter name before consuming.
        const Token& name_tok = peek();
        std::string_view filter_name = name_tok.value;
        consume(TokenType::Identifier);

        std::vector<ast::Expr> args;

        // Optional argument list: filter_name(arg, arg, …)
        if (check(TokenType::LParen)) {
            consume(TokenType::LParen);
            if (!check(TokenType::RParen)) {
                args.push_back(parse_expr());
                while (check(TokenType::Comma)) {
                    consume(TokenType::Comma);
                    args.push_back(parse_expr());
                }
            }
            consume(TokenType::RParen);
        }

        // Represent filter application as BinaryOp(Pipe, base, Filter{name, args}).
        // ast::Filter holds only the specification; BinaryOp provides the connection
        // to the input expression.
        auto filter_node = std::make_unique<ast::Filter>(
            ast::Filter{filter_name, std::move(args)});
        base = std::make_unique<ast::BinaryOp>(
            ast::BinaryOp{TokenType::Pipe,
                          std::move(base),
                          std::move(filter_node)});
    }

    return base;
}

// ---------------------------------------------------------------------------
// Token-stream helpers
// ---------------------------------------------------------------------------

Token Parser::consume(TokenType expected) {
    auto tt_name = [](TokenType t) -> std::string {
        switch (t) {
        case TokenType::Text:              return "text";
        case TokenType::ExprOpen:          return "'{{'";
        case TokenType::ExprClose:         return "'}}'";
        case TokenType::BlockOpen:         return "'{%'";
        case TokenType::BlockClose:        return "'%}'";
        case TokenType::Identifier:        return "identifier";
        case TokenType::Dot:               return "'.'";
        case TokenType::Pipe:              return "'|'";
        case TokenType::StringLiteral:     return "string";
        case TokenType::IntLiteral:        return "integer";
        case TokenType::FloatLiteral:      return "float";
        case TokenType::Keyword_For:       return "'for'";
        case TokenType::Keyword_EndFor:    return "'endfor'";
        case TokenType::Keyword_If:        return "'if'";
        case TokenType::Keyword_Elif:      return "'elif'";
        case TokenType::Keyword_Else:      return "'else'";
        case TokenType::Keyword_EndIf:     return "'endif'";
        case TokenType::Keyword_Block:     return "'block'";
        case TokenType::Keyword_EndBlock:  return "'endblock'";
        case TokenType::Keyword_Extends:   return "'extends'";
        case TokenType::Keyword_Include:   return "'include'";
        case TokenType::Keyword_In:        return "'in'";
        case TokenType::Keyword_Not:       return "'not'";
        case TokenType::Keyword_And:       return "'and'";
        case TokenType::Keyword_Or:        return "'or'";
        case TokenType::Keyword_True:      return "'true'";
        case TokenType::Keyword_False:     return "'false'";
        case TokenType::Keyword_Set:       return "'set'";
        case TokenType::Keyword_Super:     return "'super'";
        case TokenType::Op_Assign:         return "'='";
        case TokenType::Op_Eq:             return "'=='";
        case TokenType::Op_Ne:             return "'!='";
        case TokenType::Op_Lt:             return "'<'";
        case TokenType::Op_Gt:             return "'>'";
        case TokenType::Op_Le:             return "'<='";
        case TokenType::Op_Ge:             return "'>='";
        case TokenType::Op_Add:            return "'+'";
        case TokenType::Op_Sub:            return "'-'";
        case TokenType::Op_Mul:            return "'*'";
        case TokenType::Op_Div:            return "'/'";
        case TokenType::Op_Mod:            return "'%'";
        case TokenType::LParen:            return "'('";
        case TokenType::RParen:            return "')'";
        case TokenType::Comma:             return "','";
        case TokenType::Eof:               return "end-of-file";
        }
        return "unknown";
    };

    if (at_end() && expected != TokenType::Eof) {
        error(std::format("unexpected end of input, expected {}",
                          tt_name(expected)));
    }

    if (peek().type != expected) {
        const Token& got = peek();
        error(std::format("unexpected token {}, expected {}",
                          got.value.empty()
                              ? tt_name(got.type)
                              : std::format("'{}'", std::string(got.value)),
                          tt_name(expected)));
    }

    return tokens_[pos_++];
}

const Token& Parser::peek(size_t offset) const {
    const size_t idx = pos_ + offset;
    if (idx >= tokens_.size()) {
        return tokens_.back(); // Eof
    }
    return tokens_[idx];
}

bool Parser::check(TokenType t, size_t offset) const {
    return peek(offset).type == t;
}

bool Parser::at_end() const {
    return pos_ >= tokens_.size() ||
           tokens_[pos_].type == TokenType::Eof;
}

void Parser::error(std::string_view message) const {
    const Token& tok =
        (pos_ < tokens_.size()) ? tokens_[pos_] : tokens_.back();
    throw std::runtime_error(
        std::format("parse error in '{}' at line {}: {}",
                    template_name_, tok.line, std::string(message)));
}

std::string_view Parser::span_tokens(const Token& first,
                                     const Token& last) {
    const char* begin = first.value.data();
    const char* end   = last.value.data() + last.value.size();
    return std::string_view(begin, static_cast<size_t>(end - begin));
}

} // namespace guss::render
