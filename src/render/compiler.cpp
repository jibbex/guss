/**
 * \file compiler.cpp
 * \brief Bytecode compiler implementation for the Guss template engine.
 */
#include "guss/render/compiler.hpp"

#include <format>
#include <stdexcept>

namespace guss::render {

using namespace lexer;
using namespace ast;

// ---------------------------------------------------------------------------
// op_id — stable integer mapping for operator token types
// ---------------------------------------------------------------------------

static constexpr int32_t op_id(TokenType t) {
    return static_cast<int32_t>(t);
}

// ---------------------------------------------------------------------------
// ends_with_safe_filter — compile-time helper for safe-filter detection
// ---------------------------------------------------------------------------

/**
 * \brief Returns true when the outermost operator in \p expr is a pipe whose
 *        right-hand side is the \c safe filter.
 *
 * \details
 * Used by \c compile_node to decide whether to emit \c Op::EmitRaw (no HTML
 * escaping) instead of \c Op::Emit for a `{{ value | safe }}` expression.
 *
 * \param expr Top-level expression of an \c ExprNode.
 * \retval true  The expression ends with `| safe`.
 * \retval false Otherwise.
 */
static bool ends_with_safe_filter(const ast::Expr& expr) {
    if (const auto* bo = std::get_if<std::unique_ptr<ast::BinaryOp>>(&expr)) {
        if ((*bo)->op == lexer::TokenType::Pipe) {
            if (const auto* f =
                    std::get_if<std::unique_ptr<ast::Filter>>(&(*bo)->right)) {
                // "safe"   — caller has already marked the value as trusted HTML.
                // "escape" — the filter itself performs HTML escaping; emitting
                //            the result through html_escape_into would double-escape.
                return (*f)->name == "safe" || (*f)->name == "escape";
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

CompiledTemplate Compiler::compile(const ast::Template& tpl) {
    out_ = CompiledTemplate{};
    compile_nodes(tpl.nodes);
    emit(Op::Return);
    return std::move(out_);
}

// ---------------------------------------------------------------------------
// Node compilation
// ---------------------------------------------------------------------------

void Compiler::compile_nodes(const std::vector<ast::Node>& nodes) {
    for (const auto& node : nodes) {
        compile_node(node);
    }
}

void Compiler::compile_node(const ast::Node& node) {
    std::visit([this](const auto& ptr) {
        using T = std::decay_t<decltype(*ptr)>;

        if constexpr (std::is_same_v<T, TextNode>) {
            emit(Op::EmitText, static_cast<int32_t>(intern_string(ptr->text)));

        } else if constexpr (std::is_same_v<T, ExprNode>) {
            compile_expr(ptr->expr);
            if (ends_with_safe_filter(ptr->expr)) {
                emit(Op::EmitRaw);
            } else {
                emit(Op::Emit);
            }

        } else if constexpr (std::is_same_v<T, ForNode>) {
            compile_for(*ptr);

        } else if constexpr (std::is_same_v<T, IfNode>) {
            compile_if(*ptr);

        } else if constexpr (std::is_same_v<T, BlockNode>) {
            size_t idx = out_.blocks.size();
            out_.blocks.push_back(ptr->name);
            emit(Op::BlockCall, static_cast<int32_t>(idx));
            compile_nodes(ptr->body);

        } else if constexpr (std::is_same_v<T, ExtendsNode>) {
            // extends directives are handled by the engine at load time;
            // no bytecode is emitted for them.

        } else if constexpr (std::is_same_v<T, IncludeNode>) {
            // IncludeNode is resolved by the engine before compile() is called;
            // no bytecode is emitted for it here.
            (void)ptr;
        }
    }, node);
}

// ---------------------------------------------------------------------------
// Expression compilation
// ---------------------------------------------------------------------------

void Compiler::compile_expr(const ast::Expr& expr) {
    std::visit([this](const auto& ptr) {
        using T = std::decay_t<decltype(*ptr)>;

        if constexpr (std::is_same_v<T, Variable>) {
            emit(Op::Resolve, static_cast<int32_t>(intern_path(ptr->path)));

        } else if constexpr (std::is_same_v<T, StringLit>) {
            emit(Op::Push,
                 static_cast<int32_t>(intern_constant(Value(std::string(ptr->value)))));

        } else if constexpr (std::is_same_v<T, IntLit>) {
            emit(Op::Push,
                 static_cast<int32_t>(intern_constant(Value(ptr->value))));

        } else if constexpr (std::is_same_v<T, FloatLit>) {
            emit(Op::Push,
                 static_cast<int32_t>(intern_constant(Value(ptr->value))));

        } else if constexpr (std::is_same_v<T, BoolLit>) {
            emit(Op::Push,
                 static_cast<int32_t>(intern_constant(Value(ptr->value))));

        } else if constexpr (std::is_same_v<T, Filter>) {
            // A bare Filter node should never appear outside of a Pipe BinaryOp.
            throw std::runtime_error(
                std::format("compile error: bare filter '{}' outside pipe expression",
                            std::string(ptr->name)));

        } else if constexpr (std::is_same_v<T, UnaryOp>) {
            compile_expr(ptr->operand);
            emit(Op::UnaryOp, op_id(ptr->op));

        } else if constexpr (std::is_same_v<T, BinaryOp>) {
            if (ptr->op == TokenType::Pipe) {
                // Right-hand side must be a Filter node.
                const auto* filter_ptr =
                    std::get_if<std::unique_ptr<ast::Filter>>(&ptr->right);
                if (!filter_ptr) {
                    throw std::runtime_error(
                        "compile error: pipe operator right-hand side is not a filter");
                }
                const ast::Filter& filt = **filter_ptr;

                // Compile the input (left-hand side).
                compile_expr(ptr->left);

                // Compile each explicit filter argument.
                for (const auto& arg : filt.args) {
                    compile_expr(arg);
                }

                size_t name_idx  = intern_filter(filt.name);
                size_t arg_count = filt.args.size();
                emit(Op::Filter,
                     static_cast<int32_t>((name_idx << 8) | (arg_count & 0xFF)));

            } else {
                compile_expr(ptr->left);
                compile_expr(ptr->right);
                emit(Op::BinaryOp, op_id(ptr->op));
            }
        }
    }, expr);
}

// ---------------------------------------------------------------------------
// If compilation with back-patching
// ---------------------------------------------------------------------------

void Compiler::compile_if(const ast::IfNode& node) {
    std::vector<size_t> end_jumps;

    for (const auto& branch : node.branches) {
        compile_expr(branch.condition);
        size_t false_jump = emit(Op::JumpIfFalse, 0);
        compile_nodes(branch.body);
        size_t end_jump = emit(Op::Jump, 0);
        end_jumps.push_back(end_jump);
        patch(false_jump,
              static_cast<int32_t>(out_.code.size()) -
                  static_cast<int32_t>(false_jump));
    }

    compile_nodes(node.else_body);

    for (size_t site : end_jumps) {
        patch(site,
              static_cast<int32_t>(out_.code.size()) -
                  static_cast<int32_t>(site));
    }
}

// ---------------------------------------------------------------------------
// For compilation with back-patching
// ---------------------------------------------------------------------------

void Compiler::compile_for(const ast::ForNode& node) {
    // The iterable must be a Variable for bytecode ForBegin encoding.
    const auto* var_ptr =
        std::get_if<std::unique_ptr<ast::Variable>>(&node.iterable);
    if (!var_ptr) {
        throw std::runtime_error(
            "compile error: for-loop iterable must be a variable path");
    }

    size_t iterable_idx = intern_path((*var_ptr)->path);
    size_t var_idx      = intern_path(node.var_name);

    emit(Op::ForBegin,
         static_cast<int32_t>((iterable_idx << 16) | (var_idx & 0xFFFF)));

    size_t loop_top      = out_.code.size();
    size_t exhausted_jump = emit(Op::ForNext, 0);

    compile_nodes(node.body);

    emit(Op::Jump,
         static_cast<int32_t>(loop_top) -
             static_cast<int32_t>(out_.code.size()));

    patch(exhausted_jump,
          static_cast<int32_t>(out_.code.size()) -
              static_cast<int32_t>(exhausted_jump));

    compile_nodes(node.else_body);
    emit(Op::ForEnd);
}

// ---------------------------------------------------------------------------
// Emit helpers
// ---------------------------------------------------------------------------

size_t Compiler::emit(Op op, int32_t operand) {
    size_t idx = out_.code.size();
    out_.code.push_back(Instruction{op, operand});
    // TODO: populate line info from parser token positions when parser exposes line numbers.
    out_.lines.push_back(0);
    return idx;
}

void Compiler::patch(size_t instruction_index, int32_t offset) {
    out_.code[instruction_index].operand = offset;
}

// ---------------------------------------------------------------------------
// Intern helpers
// ---------------------------------------------------------------------------

size_t Compiler::intern_string(std::string_view s) {
    size_t idx = out_.strings.size();
    out_.strings.emplace_back(s);
    return idx;
}

size_t Compiler::intern_path(std::string_view s) {
    size_t idx = out_.paths.size();
    out_.paths.emplace_back(s);
    return idx;
}

size_t Compiler::intern_filter(std::string_view s) {
    size_t idx = out_.filter_names.size();
    out_.filter_names.emplace_back(s);
    return idx;
}

size_t Compiler::intern_constant(Value v) {
    size_t idx = out_.constants.size();
    out_.constants.push_back(std::move(v));
    return idx;
}

} // namespace guss::render
