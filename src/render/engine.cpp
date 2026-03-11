/**
 * \file engine.cpp
 * \brief Template engine implementation: bytecode executor, filter registry, and cache.
 */
#include "guss/render/engine.hpp"
#include "guss/render/filters.hpp"
#include "guss/render/lexer.hpp"
#include "guss/render/parser.hpp"
#include "guss/render/detail/html.hpp"

#include "guss/core/error.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <fstream>
#include <format>
#include <sstream>
#include <stdexcept>

namespace guss::render {

// ---------------------------------------------------------------------------
// eval_binary — file-local helper
// ---------------------------------------------------------------------------

/** \brief Evaluate a binary operator given the op_id (cast of lexer::TokenType). */
static Value eval_binary(int32_t op_id, const Value& lhs, const Value& rhs) {
    using TT = lexer::TokenType;
    const auto tt = static_cast<TT>(op_id);

    // Arithmetic, promote to double if either side is floating-point.
    if (tt == TT::Op_Add || tt == TT::Op_Sub ||
        tt == TT::Op_Mul || tt == TT::Op_Div || tt == TT::Op_Mod) {

        if ((lhs.is_string() || rhs.is_string()) && tt == TT::Op_Add) {
            // String concatenation, coerce both operands (Jinja2 semantics).
            return Value(std::string(lhs.to_string()) + rhs.to_string());
        }

        if (lhs.is_number() && rhs.is_number()) {
            const double l = lhs.as_double();
            const double r = rhs.as_double();
            switch (tt) {
                case TT::Op_Add: return Value(l + r);
                case TT::Op_Sub: return Value(l - r);
                case TT::Op_Mul: return Value(l * r);
                case TT::Op_Div:
                    if (r == 0.0) return Value{};
                    return Value(l / r);
                case TT::Op_Mod: {
                    const int64_t ri = rhs.as_int();
                    if (ri == 0) return Value{};
                    return Value(lhs.as_int() % ri);
                }
                default: break;
            }
        }
        return Value{}; // Unsupported operand types.
    }

    // Comparison.
    if (tt == TT::Op_Eq || tt == TT::Op_Ne ||
        tt == TT::Op_Lt || tt == TT::Op_Gt ||
        tt == TT::Op_Le || tt == TT::Op_Ge) {

        if (lhs.is_number() && rhs.is_number()) {
            const double l = lhs.as_double();
            const double r = rhs.as_double();
            switch (tt) {
                case TT::Op_Eq: return Value(l == r);
                case TT::Op_Ne: return Value(l != r);
                case TT::Op_Lt: return Value(l <  r);
                case TT::Op_Gt: return Value(l >  r);
                case TT::Op_Le: return Value(l <= r);
                case TT::Op_Ge: return Value(l >= r);
                default: break;
            }
        }

        if (lhs.is_string() && rhs.is_string()) {
            const auto l = lhs.as_string();
            const auto r = rhs.as_string();
            switch (tt) {
                case TT::Op_Eq: return Value(l == r);
                case TT::Op_Ne: return Value(l != r);
                case TT::Op_Lt: return Value(l <  r);
                case TT::Op_Gt: return Value(l >  r);
                case TT::Op_Le: return Value(l <= r);
                case TT::Op_Ge: return Value(l >= r);
                default: break;
            }
        }

        if (lhs.is_bool() && rhs.is_bool()) {
            switch (tt) {
                case TT::Op_Eq: return Value(lhs.as_bool() == rhs.as_bool());
                case TT::Op_Ne: return Value(lhs.as_bool() != rhs.as_bool());
                default: break;
            }
        }

        if (lhs.is_null() && rhs.is_null()) {
            return Value(tt == TT::Op_Eq);
        }
        if (lhs.is_null() || rhs.is_null()) {
            return Value(tt == TT::Op_Ne);
        }

        return Value(false);
    }

    // Logical — both sides already evaluated (stack machine, no short-circuit).
    if (tt == TT::Keyword_And) {
        return Value(lhs.is_truthy() && rhs.is_truthy());
    }
    if (tt == TT::Keyword_Or) {
        return Value(lhs.is_truthy() || rhs.is_truthy());
    }

    return Value{};
}

// ---------------------------------------------------------------------------
// eval_unary — file-local helper
// ---------------------------------------------------------------------------

/** \brief Evaluate a unary operator given the op_id (cast of lexer::TokenType). */
static Value eval_unary(int32_t op_id, const Value& v) {
    using TT = lexer::TokenType;
    const auto tt = static_cast<TT>(op_id);

    if (tt == TT::Keyword_Not) {
        return Value(!v.is_truthy());
    }
    if (tt == TT::Op_Sub) {
        if (v.is_number()) {
            return Value(-v.as_double());
        }
    }
    return Value{};
}

// ---------------------------------------------------------------------------
// Engine — constructor
// ---------------------------------------------------------------------------

Engine::Engine(std::filesystem::path theme_dir) {
    search_paths_.push_back(std::move(theme_dir));
    register_builtin_filters();
}

// ---------------------------------------------------------------------------
// Engine::add_search_path
// ---------------------------------------------------------------------------

void Engine::add_search_path(const std::filesystem::path& dir) {
    search_paths_.push_back(dir);
}

// ---------------------------------------------------------------------------
// Engine::resolve_path
// ---------------------------------------------------------------------------

std::filesystem::path Engine::resolve_path(std::string_view name) const {
    for (const auto& base : search_paths_) {
        auto candidate = base / name;
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error(
        std::format("engine: template not found in any search path: '{}'",
                    std::string(name)));
}

// ---------------------------------------------------------------------------
// Engine::resolve_filter_id
// ---------------------------------------------------------------------------

size_t Engine::resolve_filter_id(const std::string& name) const {
    auto it = filter_index_.find(name);
    if (it == filter_index_.end()) {
        throw std::runtime_error(
            std::format("engine: unknown filter '{}'", name));
    }
    return it->second;
}

// ---------------------------------------------------------------------------
// Engine::resolve_filter_ids
// ---------------------------------------------------------------------------

void Engine::resolve_filter_ids(CompiledTemplate& tpl) {
    for (Instruction& instr : tpl.code) {
        if (instr.op != Op::Filter) {
            continue;
        }
        const size_t name_idx  = static_cast<size_t>(instr.operand >> 8);
        const size_t arg_count = static_cast<size_t>(instr.operand & 0xFF);
        const size_t filter_id = resolve_filter_id(tpl.filter_names[name_idx]);
        instr.operand = static_cast<int32_t>((filter_id << 8) | (arg_count & 0xFF));
    }
}

// ---------------------------------------------------------------------------
// Engine::load
// ---------------------------------------------------------------------------

error::Result<const CompiledTemplate*> Engine::load(std::string_view name) {
    const std::string key(name);

    // Cache hit; return immediately.
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return &it->second;
    }

    // All lexing, parsing, and compilation happens at load time (startup), not
    // on the hot render path.  Wrap the entire block in a single catch so that
    // exceptions from third-party code (lexer, parser, compiler) are converted
    // to Result at this one boundary.
    try {
        const auto path = resolve_path(name);
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file) {
            throw std::runtime_error(
                std::format("engine: cannot open template file '{}'", path.string()));
        }

        // Read source into the AST immediately so that string_views created by
        // the lexer and parser are stable for the lifetime of parsed_ast.
        // Do NOT move or reassign parsed_ast.source after this point.
        ast::Template parsed_ast;
        {
            std::ostringstream buf;
            buf << file.rdbuf();
            parsed_ast.source = buf.str();
        }

        lexer::Lexer lex(parsed_ast.source, name);
        auto tokens = lex.tokenize();

        Parser parser(std::move(tokens), std::string(name));
        auto temp_ast = parser.parse();
        parsed_ast.nodes  = std::move(temp_ast.nodes);
        parsed_ast.parent = std::move(temp_ast.parent);
        parsed_ast.blocks = std::move(temp_ast.blocks);

        Compiler compiler;
        CompiledTemplate compiled = compiler.compile(parsed_ast);
        resolve_filter_ids(compiled);

        if (parsed_ast.parent) {
            auto parent_result = load(*parsed_ast.parent);
            if (!parent_result) return parent_result;

            for (const auto& [block_name, block_ptr] : parsed_ast.blocks) {
                (void)block_ptr;
                const std::string block_key =
                    std::string(*parsed_ast.parent) + "::block::" + block_name;
                if (cache_.find(block_key) == cache_.end()) {
                    CompiledTemplate stub;
                    stub.code.push_back(Instruction{Op::Return, 0});
                    stub.lines.push_back(0);
                    cache_.emplace(block_key, std::move(stub));
                }
            }
        }

        auto inserted = cache_.emplace(key, std::move(compiled));
        return &inserted.first->second;

    } catch (const std::runtime_error& e) {
        return error::make_error(
            error::ErrorCode::TemplateParseError,
            e.what(),
            std::string(name)
        );
    }
}

// ---------------------------------------------------------------------------
// Engine::render
// ---------------------------------------------------------------------------

error::Result<std::string> Engine::render(std::string_view template_name, Context& ctx) {
    GUSS_TRY(const CompiledTemplate* tpl, load(template_name));
    std::string out;
    out.reserve(WRITE_BUFFER_SIZE);
    execute(*tpl, ctx, out);
    return out;
}

// ---------------------------------------------------------------------------
// Engine::execute — hot path, no heap allocation inside the loop
// ---------------------------------------------------------------------------

void Engine::execute(const CompiledTemplate& tpl, Context& ctx, std::string& out) {
    // Fixed-size value stack — 64 slots is sufficient for realistic templates.
    Value  stack[MAX_VALUE_STACK_SIZE];
    size_t sp  = 0;
    size_t pc  = 0;
    const size_t end = tpl.code.size();

    // Fixed-size loop stack — 16 levels of nesting.
    struct LoopFrame {
        Value  array;
        size_t index;
        size_t length;
        size_t var_name_idx;  // index into tpl.paths for the loop variable name
    };
    LoopFrame loop_stack[MAX_LOOP_STACK_SIZE];
    size_t    lsp = 0;

    while (pc < end) {
        const Instruction& instr = tpl.code[pc++];

        switch (instr.op) {

            case Op::EmitText:
                out += tpl.strings[static_cast<size_t>(instr.operand)];
                break;

            case Op::Resolve:
                assert(sp < 64 && "engine: value stack overflow");
                stack[sp++] = ctx.resolve(tpl.paths[static_cast<size_t>(instr.operand)]);
                break;

            case Op::Push:
                assert(sp < 64 && "engine: value stack overflow");
                stack[sp++] = tpl.constants[static_cast<size_t>(instr.operand)];
                break;

            case Op::Emit:
                assert(sp > 0 && "engine: value stack underflow");
                detail::html_escape_into(stack[--sp].to_string(), out);
                break;

            case Op::EmitRaw:
                assert(sp > 0 && "engine: value stack underflow");
                out += stack[--sp].to_string();
                break;

            case Op::Filter: {
                const size_t filter_id = static_cast<size_t>(instr.operand >> 8);
                const size_t arg_count = static_cast<size_t>(instr.operand & 0xFF);
                assert(sp >= arg_count + 1 && "engine: value stack underflow");
                sp -= arg_count;
                Value& subject = stack[sp - 1];
                subject = filters_[filter_id](
                    subject,
                    std::span<const Value>(stack + sp, arg_count)
                );
                break;
            }

            case Op::BinaryOp:
                assert(sp >= 2 && "engine: value stack underflow");
                stack[sp - 2] = eval_binary(instr.operand, stack[sp - 2], stack[sp - 1]);
                --sp;
                break;

            case Op::UnaryOp:
                assert(sp > 0 && "engine: value stack underflow");
                stack[sp - 1] = eval_unary(instr.operand, stack[sp - 1]);
                break;

            case Op::JumpIfFalse:
                assert(sp > 0 && "engine: value stack underflow");
                if (!stack[--sp].is_truthy()) {
                    pc = static_cast<size_t>(
                        static_cast<int32_t>(pc) + instr.operand - 1);
                }
                break;

            case Op::Jump:
                pc = static_cast<size_t>(
                    static_cast<int32_t>(pc) + instr.operand - 1);
                break;

            case Op::ForBegin: {
                assert(lsp < 16 && "engine: loop stack overflow");
                const size_t iterable_idx =
                    static_cast<size_t>((instr.operand >> 16) & 0xFFFF);
                const size_t var_idx =
                    static_cast<size_t>(instr.operand & 0xFFFF);
                Value iterable = ctx.resolve(tpl.paths[iterable_idx]);
                const size_t length = iterable.size();
                loop_stack[lsp++] = {std::move(iterable), 0, length, var_idx};
                break;
            }

            case Op::ForNext: {
                assert(lsp > 0 && "engine: loop stack underflow");
                LoopFrame& frame = loop_stack[lsp - 1];
                if (frame.index >= frame.length) {
                    --lsp;
                    pc = static_cast<size_t>(
                        static_cast<int32_t>(pc) + instr.operand - 1);
                } else {
                    ctx.set(tpl.paths[frame.var_name_idx], frame.array[frame.index]);
                    ctx.set("loop.index",
                            Value(static_cast<int64_t>(frame.index + 1)));
                    ctx.set("loop.index0",
                            Value(static_cast<int64_t>(frame.index)));
                    ctx.set("loop.first",  Value(frame.index == 0));
                    ctx.set("loop.last",
                            Value(frame.length > 0 &&
                                  frame.index == frame.length - 1));
                    ctx.set("loop.length",
                            Value(static_cast<int64_t>(frame.length)));
                    ++frame.index;
                }
                break;
            }

            case Op::ForEnd:
                // No-op marker — signals end of for-loop region to analysis tools.
                break;

            case Op::BlockCall:
                // TODO: Full template inheritance — look up block override in cache
                // using the key "<template_name>::block::<block_name>" and execute it.
                // For this phase BlockCall is a no-op; the inheritance test covers this.
                break;

            case Op::Return:
                return;
        }
    }
}

// ---------------------------------------------------------------------------
// Engine::register_builtin_filters
// ---------------------------------------------------------------------------

void Engine::register_builtin_filters() {
    filters::register_all(filters_, filter_index_);
}

} // namespace guss::render
