/**
 * \file ast.hpp
 * \brief AST node types for the Guss template renderer.
 * \details
 * This header defines the in-memory representation of a parsed Guss template as an abstract syntax tree (AST).
 * The AST consists of expression nodes (for computations and variable references) and statement nodes
 * (for template structure and control flow).  String data is stored as non-owning `std::string_view` references
 * into the original template source, which must be kept alive for the lifetime of the AST.
 */
#pragma once
#include "guss/render/lexer.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

/**
 * \defgroup guss_render_ast AST — Abstract Syntax Tree
 * \ingroup guss_render
 * \brief Types that represent a parsed Guss template as an in-memory tree.
 * \details
 * The AST is split into two layers:\n
 * **Expression layer** (`Expr` and its node types)\n
 * Represents values and computations that appear inside `{{ }}` delimiters:
 * literals, variable references, filter chains, and unary/binary operators.\n
 * **Statement / node layer** (`Node` and its node types)\n
 * Represents the structural elements of a template: raw text, expression
 * output, control flow (`for`, `if`), block/extends inheritance, and
 * `include` directives.\n
 * Ownership is expressed entirely through `std::unique_ptr` inside the
 * `std::variant` aliases `Expr` and `Node`.  String data is borrowed via
 * `std::string_view` and must be kept alive by the owning `Template::source`
 * field for the lifetime of the tree.
 */

namespace guss::render::ast {

/// \addtogroup guss_render_ast
/// \{

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

struct Variable;
struct StringLit;
struct IntLit;
struct FloatLit;
struct BoolLit;
struct Filter;
struct UnaryOp;
struct BinaryOp;
struct SuperNode;

// ---------------------------------------------------------------------------
// Expression variant
// ---------------------------------------------------------------------------

/**
 * \brief Owning sum-type for every kind of expression node.
 * \details
 * Each alternative is stored as a `std::unique_ptr` so that recursive
 * structures (`UnaryOp`, `BinaryOp`, `Filter`) do not require the contained
 * types to be complete at the point of the `variant` definition.
 * \code{.cpp}
 * std::visit(overloaded{
 *     [](const std::unique_ptr<Variable>& v) { ... },
 *     [](const std::unique_ptr<IntLit>&   i) { ... },
 *     // ...
 * }, expr);
 * \endcode
 */
using Expr = std::variant<
    std::unique_ptr<Variable>,
    std::unique_ptr<StringLit>,
    std::unique_ptr<IntLit>,
    std::unique_ptr<FloatLit>,
    std::unique_ptr<BoolLit>,
    std::unique_ptr<Filter>,
    std::unique_ptr<UnaryOp>,
    std::unique_ptr<BinaryOp>,
    std::unique_ptr<SuperNode>
>;

// ---------------------------------------------------------------------------
// Expression node types
// ---------------------------------------------------------------------------

/**
 * \brief A dot-separated variable path resolved against the render context.
 * \details
 * The `path` string is a non-owning view into `Template::source`.
 * Attribute traversal (e.g. `"post.title"`) is performed by the renderer at
 * evaluation time.
 */
struct Variable {
    std::string_view path;              ///< Dot-separated lookup path, e.g. `"post.title"`.
};

/**
 * \brief A string literal value parsed from the template source.
 * \details
 * `value` is a non-owning view into `Template::source`; it does **not**
 * include the surrounding quote characters.
 */
struct StringLit {
    std::string_view value;             ///< Unquoted string content.
};

/**
 * \brief A 64-bit signed integer literal.
 */
struct IntLit {
    int64_t value;                      ///< Parsed integer value.
};

/**
 * \brief A double-precision floating-point literal.
 */
struct FloatLit {
    double value;                       ///< Parsed floating-point value.
};

/**
 * \brief A boolean literal (`true` or `false`).
 */
struct BoolLit {
    bool value;                         ///< Parsed boolean value.
};

/**
 * \brief A filter application node, e.g. `value | upper` or `value | truncate(80)`.
 * \details
 * Filters form a chain in the expression tree: the left-hand side of a pipe
 * operator becomes an implicit first argument supplied by the renderer, while
 * any explicit arguments are stored in `args`.  The `name` view points into
 * `Template::source`.
 */
struct Filter {
    std::string_view name;              ///< Filter identifier, e.g. `"upper"`, `"truncate"`.
    std::vector<Expr> args;             ///< Zero or more explicit filter arguments.
};

/**
 * \brief A unary prefix operation applied to a single operand expression.
 * \details
 * Common unary operators are logical negation (`not`) and arithmetic negation
 * (`-`).  The concrete operator is identified by `op`.
 */
struct UnaryOp {
    lexer::TokenType op;                ///< The operator token (e.g. `TokenType::Keyword_Not`).
    Expr operand;                       ///< The single operand expression.
};

/**
 * \brief A binary infix operation applied to two operand expressions.
 * \details
 * Covers arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`,
 * `>`, `<=`, `>=`), logical (`and`, `or`), and membership (`in`, `not in`)
 * operators.
 */
struct BinaryOp {
    lexer::TokenType op;                ///< The operator token.
    Expr left;                          ///< Left-hand operand.
    Expr right;                         ///< Right-hand operand.
};

/**
 * \brief A `{{ super() }}` call expression that renders the parent block's content.
 *
 * \details
 * Valid only inside a block override in a child template.  At render time the
 * executor walks the `super_chain` to find the immediate parent block body and
 * renders it, then pushes the resulting string onto the value stack so that the
 * surrounding expression can concatenate or emit it.
 */
struct SuperNode {};

// ---------------------------------------------------------------------------
// Template node forward declarations
// ---------------------------------------------------------------------------

struct TextNode;
struct ExprNode;
struct ForNode;
struct IfNode;
struct BlockNode;
struct ExtendsNode;
struct IncludeNode;
struct SetNode;

// ---------------------------------------------------------------------------
// Node variant
// ---------------------------------------------------------------------------

/**
 * \brief Owning sum-type for every kind of template statement node.
 * \details
 * Mirrors the design of `Expr`: each alternative is heap-allocated via
 * `std::unique_ptr` to allow recursive containment (e.g. `ForNode::body`
 * holds a `std::vector<Node>`).
 */
using Node = std::variant<
    std::unique_ptr<TextNode>,
    std::unique_ptr<ExprNode>,
    std::unique_ptr<ForNode>,
    std::unique_ptr<IfNode>,
    std::unique_ptr<BlockNode>,
    std::unique_ptr<ExtendsNode>,
    std::unique_ptr<IncludeNode>,
    std::unique_ptr<SetNode>
>;

// ---------------------------------------------------------------------------
// Template node types
// ---------------------------------------------------------------------------

/**
 * \brief A raw text segment that is emitted verbatim to the output stream.
 * \details
 * `text` is a non-owning view into `Template::source` covering everything
 * between two template tags (or between a tag and the start/end of the file).
 */
struct TextNode {
    std::string_view text;              ///< Raw template text, emitted verbatim.
};

/**
 * \brief An expression output node corresponding to a `{{ expr }}` tag.
 * \details
 * The renderer evaluates `expr` against the current context and writes the
 * result — after applying any configured auto-escaping — to the output stream.
 */
struct ExprNode {
    Expr expr; ///< The expression to evaluate and render.
};

/**
 * \brief A `{% for var in iterable %}…{% endfor %}` loop node.
 * \details
 * During rendering, `iterable` is evaluated and each element is bound to
 * `var_name` in a child context before rendering each node in `body`.
 * If `iterable` is empty (or evaluates to a falsy sequence), `else_body` is
 * rendered instead.
 */
struct ForNode {
    std::string var_name;               ///< Loop variable name, e.g. `"item"`.
    Expr iterable;                      ///< Expression that yields the sequence to iterate.
    std::vector<Node> body;             ///< Nodes rendered for each iteration.
    std::vector<Node> else_body;        ///< Nodes rendered when the iterable is empty.
};

/**
 * \brief A multi-branch conditional node (`{% if %}…{% elif %}…{% else %}`).
 * \details
 * Branches are evaluated in order; the body of the first branch whose
 * `condition` is truthy is rendered.  If no branch matches, `else_body` is
 * rendered (may be empty).
 */
struct IfNode {
    /**
     * \brief A single `if` / `elif` branch with its condition and body.
     */
    struct Branch {
        Expr condition;                 ///< Guard expression for this branch.
        std::vector<Node> body;         ///< Nodes rendered when the condition is true.
    };

    std::vector<Branch> branches;       ///< Ordered list of `if`/`elif` branches.
    std::vector<Node>   else_body;      ///< Fallback nodes for the `{% else %}` clause.
};

/**
 * \brief A named block that child templates can override via `{% block name %}`.
 *
 * \details
 * When a child template extends this template, the renderer replaces `body`
 * with the child's block of the same name (if provided).  Blocks are
 * registered by name in `Template::blocks` for O(1) lookup during inheritance
 * resolution.
 */
struct BlockNode {
    std::string name;                   ///< Unique block identifier within the template.
    std::vector<Node> body;             ///< Default content rendered when not overridden.
};

/**
 * \brief An `{% extends "parent.html" %}` inheritance directive.
 *
 * \details
 * Must appear as the first node in a template.  The renderer loads the parent
 * template and merges the child's named blocks into it before rendering.
 * Only one `ExtendsNode` is permitted per template.
 */
struct ExtendsNode {
    std::string parent_template;        ///< Name/path of the parent template to extend.
};

/**
 * \brief An `{% include "partial.html" %}` inclusion directive.
 *
 * \details
 * The renderer loads and renders the named template in the current context and
 * inserts its output at this position in the output stream.
 */
struct IncludeNode {
    std::string template_name;          ///< Name/path of the template to include.
};

/**
 * \brief A `{% set varname = expr %}` assignment node.
 * \details Sets a variable in the render context at the point of execution.
 */
struct SetNode {
    std::string var_name;  ///< Variable name to assign.
    Expr        value;     ///< Expression to evaluate and assign.
};

// ---------------------------------------------------------------------------
// Top-level template descriptor
// ---------------------------------------------------------------------------

/**
 * \brief The fully parsed representation of a single template file.
 *
 * \details
 * A `Template` object owns the complete source text and the AST derived from
 * it.  All `std::string_view` fields inside the tree borrow from `source`, so
 * the `Template` instance must remain alive for the lifetime of any renderer
 * that operates on it.
 *
 * Lifetime contract:
 * - `source` owns the raw bytes.
 * - `nodes` owns the top-level AST nodes (and transitively all child nodes).
 * - `blocks` holds raw, non-owning pointers into `BlockNode` instances inside
 *   `nodes`; these pointers are stable because nodes are heap-allocated via
 *   `std::unique_ptr`.
 */
struct Template {
    std::string source;                 ///< Owns the raw template source text.
    std::optional<std::string> parent;  ///< Non-empty when this template extends another.
    std::vector<Node> nodes;            ///< Top-level AST node sequence.

    /**
     * \brief Index of named blocks for fast inheritance resolution.
     *
     * \details
     * Populated by the parser immediately after building `nodes`.  Each
     * pointer refers to a `BlockNode` inside `nodes` and remains valid for
     * the lifetime of this `Template` object.
     */
    std::unordered_map<std::string, BlockNode*> blocks;
};

/// \} // end of guss_render_ast group

} // namespace guss::render::ast