/**
 * \file runtime.cpp
 * \brief Template runtime implementation: bytecode executor, filter registry, and cache.
 */
#include "guss/render/runtime.hpp"
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
using guss::core::Value;

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
// Runtime — constructor
// ---------------------------------------------------------------------------

Runtime::Runtime(std::filesystem::path theme_dir) {
    search_paths_.push_back(std::move(theme_dir));
    register_builtin_filters();
}

// ---------------------------------------------------------------------------
// Runtime::add_search_path
// ---------------------------------------------------------------------------

void Runtime::add_search_path(const std::filesystem::path& dir) {
    search_paths_.push_back(dir);
}

// ---------------------------------------------------------------------------
// Runtime::resolve_path
// ---------------------------------------------------------------------------

std::filesystem::path Runtime::resolve_path(std::string_view name) const {
    for (const auto& base : search_paths_) {
        auto candidate = base / name;
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error(
        std::format("runtime: template not found in any search path: '{}'",
                    std::string(name)));
}

// ---------------------------------------------------------------------------
// Runtime::resolve_filter_id
// ---------------------------------------------------------------------------

size_t Runtime::resolve_filter_id(const std::string& name) const {
    auto it = filter_index_.find(name);
    if (it == filter_index_.end()) {
        throw std::runtime_error(
            std::format("runtime: unknown filter '{}'", name));
    }
    return it->second;
}

// ---------------------------------------------------------------------------
// Runtime::resolve_filter_ids
// ---------------------------------------------------------------------------

void Runtime::resolve_filter_ids(CompiledTemplate& tpl) {
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
// Runtime::load
// ---------------------------------------------------------------------------

core::error::Result<const CompiledTemplate*> Runtime::load(std::string_view name) {
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
                std::format("runtime: cannot open template file '{}'", path.string()));
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

        // Pre-load all included templates so they are in cache at execute() time.
        for (const auto& inc_name : compiled.include_names) {
            auto r = load(inc_name);
            if (!r) return r;
        }

        // Extract each block's default body as a standalone template.
        // Cached as "<template_name>::block::<blockname>::default".
        // Needed as the base of the super() chain (Task 5).
        for (const auto& [block_name, block_ptr] : parsed_ast.blocks) {
            const std::string default_key = key + "::block::" + block_name + "::default";
            if (cache_.find(default_key) == cache_.end()) {
                CompiledTemplate def_tpl =
                    Compiler::compile_standalone(block_ptr->body);
                resolve_filter_ids(def_tpl);
                def_tpl.name = default_key;
                cache_.emplace(default_key, std::move(def_tpl));
            }
        }

        // Store parent_name from parsed AST.
        compiled.parent_name = parsed_ast.parent.value_or("");

        // If this is a child, compile each block override as a standalone template.
        // Cached as "<child_name>::block::<blockname>".
        if (parsed_ast.parent) {
            for (const auto& [block_name, block_ptr] : parsed_ast.blocks) {
                const std::string override_key = key + "::block::" + block_name;
                if (cache_.find(override_key) == cache_.end()) {
                    CompiledTemplate ovr_tpl =
                        Compiler::compile_standalone(block_ptr->body);
                    resolve_filter_ids(ovr_tpl);
                    ovr_tpl.name = override_key;
                    cache_.emplace(override_key, std::move(ovr_tpl));
                }
            }
            // Ensure parent is loaded (needed for block defaults and chain traversal).
            auto parent_result = load(*parsed_ast.parent);
            if (!parent_result) return parent_result;
        }

        auto inserted = cache_.emplace(key, std::move(compiled));
        inserted.first->second.name = key;
        return &inserted.first->second;

    } catch (const std::runtime_error& e) {
        return core::error::make_error(
            core::error::ErrorCode::TemplateParseError,
            e.what(),
            std::string(name)
        );
    }
}

// ---------------------------------------------------------------------------
// Runtime::build_override_map
// ---------------------------------------------------------------------------

std::pair<const CompiledTemplate*, Runtime::BlockOverrideMap>
Runtime::build_override_map(const CompiledTemplate& leaf_tpl) {
    // Walk up the chain to find the root (no parent_name).
    std::vector<const CompiledTemplate*> chain;
    const CompiledTemplate* cur = &leaf_tpl;
    while (!cur->parent_name.empty()) {
        chain.push_back(cur);
        auto it = cache_.find(cur->parent_name);
        assert(it != cache_.end() && "parent not in cache — should have been loaded");
        // Release-build safety net: if the parent is somehow missing (compiler bug
        // guard — load() should have prevented this), treat the current node as root.
        if (it == cache_.end()) break;
        cur = &it->second;
    }
    const CompiledTemplate* root = cur;

    // Walk chain from root-adjacent toward leaf, building override map.
    // chain is [leaf, ..., root-adjacent]; iterate in reverse (root-first).
    BlockOverrideMap overrides;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const CompiledTemplate* child = *it;
        for (const std::string& block_name : root->blocks) {
            const std::string ovr_key = child->name + "::block::" + block_name;
            auto ovr_it = cache_.find(ovr_key);
            if (ovr_it == cache_.end()) continue;

            auto& vec = overrides[block_name];
            if (vec.empty()) {
                // Seed with the base template's default body.
                const std::string def_key =
                    root->name + "::block::" + block_name + "::default";
                auto def_it = cache_.find(def_key);
                if (def_it != cache_.end()) {
                    vec.push_back(&def_it->second);
                }
            }
            vec.push_back(&ovr_it->second);
        }
    }
    return {root, std::move(overrides)};
}

// ---------------------------------------------------------------------------
// Runtime::render
// ---------------------------------------------------------------------------

core::error::Result<std::string> Runtime::render(std::string_view template_name, Context& ctx) {
    GUSS_TRY(const CompiledTemplate* tpl, load(template_name));
    std::string out;
    out.reserve(WRITE_BUFFER_SIZE);

    if (!tpl->parent_name.empty()) {
        auto [root, overrides] = build_override_map(*tpl);
        execute(*root, ctx, out, &overrides, {});
    } else {
        execute(*tpl, ctx, out, nullptr, {});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Runtime::execute — hot path, no heap allocation inside the loop
// ---------------------------------------------------------------------------

void Runtime::execute(
    const CompiledTemplate&                    tpl,
    Context&                                   ctx,
    std::string&                               out,
    const BlockOverrideMap*                    overrides,
    std::span<const CompiledTemplate* const>   super_chain)
{
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
#ifdef USE_RUNTIME_CHECKS
                assert(sp < MAX_VALUE_STACK_SIZE && "runtime: value stack overflow");
#endif
                stack[sp++] = ctx.resolve(tpl.paths[static_cast<size_t>(instr.operand)]);
                break;

            case Op::Push:
#ifdef USE_RUNTIME_CHECKS
                assert(sp < MAX_VALUE_STACK_SIZE && "runtime: value stack overflow");
#endif
                stack[sp++] = tpl.constants[static_cast<size_t>(instr.operand)];
                break;

            case Op::Emit:
#ifdef USE_RUNTIME_CHECKS
                assert(sp > 0 && "runtime: value stack underflow");
#endif
                detail::html_escape_into(stack[--sp].to_string(), out);
                break;

            case Op::EmitRaw:
#ifdef USE_RUNTIME_CHECKS
                assert(sp > 0 && "runtime: value stack underflow");
#endif
                out += stack[--sp].to_string();
                break;

            case Op::Filter: {
                const size_t filter_id = static_cast<size_t>(instr.operand >> 8);
                const size_t arg_count = static_cast<size_t>(instr.operand & 0xFF);
#ifdef USE_RUNTIME_CHECKS
                assert(sp >= arg_count + 1 && "runtime: value stack underflow");
#endif
                sp -= arg_count;
                Value& subject = stack[sp - 1];
                subject = filters_[filter_id](
                    subject,
                    std::span<const Value>(stack + sp, arg_count)
                );
                break;
            }

            case Op::BinaryOp:
#ifdef USE_RUNTIME_CHECKS
                assert(sp >= 2 && "runtime: value stack underflow");
#endif
                stack[sp - 2] = eval_binary(instr.operand, stack[sp - 2], stack[sp - 1]);
                --sp;
                break;

            case Op::UnaryOp:
#ifdef USE_RUNTIME_CHECKS
                assert(sp > 0 && "runtime: value stack underflow");
#endif
                stack[sp - 1] = eval_unary(instr.operand, stack[sp - 1]);
                break;

            case Op::JumpIfFalse:
#ifdef USE_RUNTIME_CHECKS
                assert(sp > 0 && "runtime: value stack underflow");
#endif
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
#ifdef USE_RUNTIME_CHECKS
                assert(lsp < MAX_LOOP_STACK_SIZE && "runtime: loop stack overflow");
#endif
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
#ifdef USE_RUNTIME_CHECKS
                assert(lsp > 0 && "runtime: loop stack underflow");
#endif
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
                    ctx.set("loop.revindex",
                            Value(static_cast<int64_t>(frame.length - frame.index)));
                    ctx.set("loop.revindex0",
                            Value(static_cast<int64_t>(frame.length - frame.index - 1)));
                    ++frame.index;
                }
                break;
            }

            case Op::ForEnd:
                // No-op marker — signals end of for-loop region to analysis tools.
                break;

            case Op::BlockCall: {
                const size_t idx  = static_cast<size_t>(instr.operand & 0xFFFF);
                const int    skip = static_cast<int>(
                    (static_cast<uint32_t>(instr.operand) >> 16) & 0xFFFF);
                const std::string& block_name = tpl.blocks[idx];

                if (overrides) {
                    auto ov_it = overrides->find(block_name);
                    if (ov_it != overrides->end() && !ov_it->second.empty()) {
                        const auto& chain = ov_it->second;
                        // Execute deepest override; chain[0..n-2] is its super chain.
                        execute(*chain.back(), ctx, out, overrides,
                                std::span<const CompiledTemplate* const>(
                                    chain.data(), chain.size() - 1));
                        // Jump past the inline default body.
                        pc = static_cast<size_t>(static_cast<int>(pc) + skip - 1);
                        break;
                    }
                }
                // No override: fall through to the inline default body.
                break;
            }

            case Op::BlockEnd:
                // No-op — marks end of a block's default body region.
                break;

            case Op::Include: {
                const std::string& inc_name =
                    tpl.include_names[static_cast<size_t>(instr.operand)];
                auto it = cache_.find(inc_name);
                // Unreachable in correct operation: Runtime::load() pre-loads all includes
                // and returns an error if any are missing, so execute() is never called
                // with an un-cached include target.                
                assert(it != cache_.end() && "runtime: include target not in cache — pre-load failed silently");
                if (it == cache_.end()) break;  // Release-build safety net; pre-load should have caught this.
                execute(it->second, ctx, out, overrides, {});
                break;
            }

            case Op::Set: {
                // Unreachable in correct operation: verify_stack_depths() in the compiler
                // ensures sp > 0 before any Set instruction is reachable.
#ifdef USE_RUNTIME_CHECKS
                assert(sp > 0 && "runtime: value stack underflow in Op::Set — compiler bug");
#endif
                if (sp == 0) break;  // Release-build safety net; verifier should have caught this.
                ctx.set(tpl.paths[static_cast<size_t>(instr.operand)], stack[--sp]);
                break;
            }

            case Op::Super: {
#ifdef USE_RUNTIME_CHECKS
                assert(sp < MAX_VALUE_STACK_SIZE && "runtime: value stack overflow");
                if (sp >= MAX_VALUE_STACK_SIZE) break;  // release safety net
#endif
                if (!super_chain.empty()) {
                    // Render the immediate parent block body into a temporary string,
                    // passing its own ancestor chain for nested super() calls.
                    std::string super_out;
                    execute(*super_chain.back(), ctx, super_out,
                            overrides,
                            super_chain.subspan(0, super_chain.size() - 1));
                    stack[sp++] = Value(std::move(super_out));
                } else {
                    // No parent block available — push empty string.
                    stack[sp++] = Value(std::string{});
                }
                break;
            }

            case Op::Return:
                return;
        }
    }
}

// ---------------------------------------------------------------------------
// Runtime::register_builtin_filters
// ---------------------------------------------------------------------------

void Runtime::register_builtin_filters() {
    filters::register_all(filters_, filter_index_);
}

} // namespace guss::render
