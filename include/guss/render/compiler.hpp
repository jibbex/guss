/**
 * \file compiler.hpp
 * \brief Bytecode compiler that lowers \c ast::Template to \c CompiledTemplate.
 *
 * \details
 * The compiler performs a single-pass walk of the AST produced by \c Parser
 * and emits a flat sequence of \c Instruction values into a \c CompiledTemplate.
 * Control-flow instructions (\c JumpIfFalse, \c Jump, \c ForNext) carry relative
 * signed offsets; forward-jump targets are filled in by back-patching once the
 * target instruction index is known.
 *
 * String data (raw text segments, variable paths, filter names) is interned into
 * parallel tables to avoid redundant allocations across the instruction stream.
 *
 * The \c CompiledTemplate::lines array is a parallel debug-info array (see its
 * field doc for population status).  The invariant \c code.size() == lines.size()
 * is always maintained.
 */
#pragma once
#include "guss/render/ast.hpp"
#include "guss/render/value.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace guss::render {

// ---------------------------------------------------------------------------
// Opcode enumeration
// ---------------------------------------------------------------------------

/**
 * \brief Opcode for a single bytecode instruction.
 *
 * \details
 * Each opcode has a fixed operand stored in \c Instruction::operand.  The
 * interpretation of that operand is opcode-specific and documented per
 * enumerator below.
 */
enum class Op : uint8_t {
    EmitText,   ///< \c operand: index into \c CompiledTemplate::strings. Appends the raw string to output.
    Resolve,    ///< \c operand: index into \c CompiledTemplate::paths. Pushes the resolved value onto the stack.
    Push,       ///< \c operand: index into \c CompiledTemplate::constants. Pushes a literal value onto the stack.
    Emit,       ///< Pops a value, HTML-escapes it, and appends to output.
    EmitRaw,    ///< Pops a value and appends to output without escaping (used by the \c safe filter).
    Filter,     /**< \c operand: bits[31:8]=filter_name_idx (24 bits), bits[7:0]=arg_count (8 bits).
                 *   Encoding: \c (filter_name_idx << 8) | (arg_count & 0xFF).
                 *   Decoding: \c name_idx = static_cast<size_t>(operand >> 8) (no mask, full 24 bits);
                 *             \c arg_count = static_cast<size_t>(operand & 0xFF).
                 *   Pops \c arg_count args (right-to-left) then the subject; pushes the filter result.
                 *   \c filter_name_idx indexes \c CompiledTemplate::filter_names. */
    BinaryOp,   ///< \c operand: \c static_cast<int32_t>(lexer::TokenType) of the operator. Pops two values, pushes result.
    UnaryOp,    ///< \c operand: \c static_cast<int32_t>(lexer::TokenType) of the operator. Pops one value, pushes result.
    /**
     * \c operand: signed offset stored as \c target_index - instruction_index.
     *
     * \details Jump offset convention (also applies to \c Jump and \c ForNext):
     * The execute loop \b pre-increments \c pc past the current instruction
     * before reading the operand, so it computes:
     * \code
     * pc_new = pc + operand - 1   // yields target_index
     * \endcode
     * This means the stored offset is \b not a traditional "relative to pc
     * after fetch" offset; it is relative to the instruction's own index, and
     * the \c -1 correction accounts for the pre-increment.
     *
     * Pops condition; jumps when not truthy.
     */
    JumpIfFalse,
    /**
     * \c operand: signed offset using the same convention as \c JumpIfFalse.
     * Unconditional branch.
     */
    Jump,
    ForBegin,    /**< \c operand: bits[31:16]=iterable_path_idx, bits[15:0]=var_name_path_idx.
                  *   Both indices are capped at 65535.
                  *   Encoding: \c (iterable_path_idx << 16) | (var_name_path_idx & 0xFFFF).
                  *   Initialises the loop iterator for the iterable at \c iterable_path_idx;
                  *   the loop variable name is at \c var_name_path_idx in \c paths. */
    /**
     * \c operand: signed offset using the same convention as \c JumpIfFalse.
     * Jumps when the iterable is exhausted; otherwise advances the iterator.
     */
    ForNext,
    ForEnd,      ///< No-op marker — signals the end of a for-loop region to analysis tools.
    BlockCall,   /**< \c operand: \c (skip_dist<<16)|(block_idx&0xFFFF).
                  *   \c skip_dist is capped at 15 bits (max 32767); \c block_idx at 16 bits.
                  *   If an override is found: execute it and jump \c skip_dist instructions
                  *   past the \c BlockCall to skip the inline default body.
                  *   Otherwise: fall through to execute the inline default body. */
    BlockEnd,    ///< No-op marker — end of a block's default body region.
    Include,     ///< \c operand: index into \c CompiledTemplate::include_names. Renders the named template inline.
    Set,         ///< \c operand: index into \c CompiledTemplate::paths. Pops one value from stack and sets it in the context.
    Super,       ///< Renders the parent block body (from \c super_chain) into a string and pushes it onto the value stack (net +1).
    Return,      ///< Terminates execution of the current template.
};

// ---------------------------------------------------------------------------
// Instruction
// ---------------------------------------------------------------------------

/**
 * \brief A single compiled instruction carrying an opcode and a 32-bit operand.
 */
struct Instruction {
    Op      op;
    int32_t operand = 0;
};

// ---------------------------------------------------------------------------
// Execute-stack limits (shared between Compiler verifier and Runtime executor)
// ---------------------------------------------------------------------------

/**
 * \brief Maximum depth of the value stack used by the bytecode executor.
 *
 * \details
 * The value stack in \c execute() is a fixed C array of this size.
 * \c Compiler::verify_stack_depths() enforces this limit at compile time so
 * that no overflow checks are needed on the hot render path.
 */
constexpr size_t MAX_VALUE_STACK_SIZE = 64;

/**
 * \brief Maximum nesting depth of the loop stack used by the bytecode executor.
 *
 * \details
 * The loop stack in \c execute() is a fixed C array of this size.
 * \c Compiler::verify_stack_depths() enforces this limit at compile time.
 */
constexpr size_t MAX_LOOP_STACK_SIZE = 16;

// ---------------------------------------------------------------------------
// CompiledTemplate
// ---------------------------------------------------------------------------

/**
 * \brief The output of the compiler: a flat instruction stream plus interned tables.
 *
 * \details
 * All index fields in instructions reference entries in the parallel tables held
 * here.  The \c lines array is a debug-only parallel array (\c code.size() ==
 * lines.size() is always maintained).  See the \c lines field doc for current
 * population status.
 */
struct CompiledTemplate {
    std::string              name;          ///< Template name/key (set by Runtime::load).
    std::string              parent_name;   ///< Non-empty when this template extends another.
    std::vector<Instruction> code;          ///< Flat instruction sequence.
    std::vector<std::string> strings;       ///< Interned raw text segments (for \c EmitText).
    std::vector<std::string> paths;         ///< Interned dotted resolve paths (for \c Resolve / \c ForBegin).
    std::vector<Value>       constants;     ///< Interned literal constants (for \c Push).
    std::vector<std::string> blocks;        ///< Block names referenced by \c BlockCall instructions.
    std::vector<std::string> filter_names;  ///< Interned filter names (for \c Filter instructions).
    std::vector<std::string> include_names; ///< Template names referenced by \c Op::Include.

    /** Debug info: source line per instruction (parallel to \c code).
     *  Currently populated as 0 for every instruction; requires the parser
     *  to expose token line numbers before this can be filled meaningfully. */
    std::vector<uint32_t>    lines;
};

// ---------------------------------------------------------------------------
// Compiler
// ---------------------------------------------------------------------------

/**
 * \brief Single-pass AST-to-bytecode compiler.
 *
 * \details
 * Construct a \c Compiler and call \c compile() with a fully parsed
 * \c ast::Template.  The AST is consumed (all \c string_view fields must
 * remain valid during the call) and may be freed once \c compile() returns.
 *
 * The compiler is single-use: constructing a new instance for each template
 * is the expected usage pattern.
 */
class Compiler {
public:
    /**
     * \brief Compile a parsed template into a \c CompiledTemplate.
     *
     * \param tpl  The fully parsed template AST.  All \c string_view fields
     *             inside the AST (pointing into \c ast::Template::source) must
     *             remain valid for the duration of this call.
     * \retval CompiledTemplate The compiled bytecode and interned tables.
     */
    CompiledTemplate compile(const ast::Template& tpl);

    /**
     * \brief Compile a node list as a complete standalone \c CompiledTemplate (with Return).
     *
     * \details
     * Used by \c Runtime::load() to extract block default bodies and child block
     * overrides as independently executable templates.
     *
     * \param nodes  The AST node list to compile.  All \c string_view fields
     *               inside the nodes must remain valid for the duration of this call.
     * \retval CompiledTemplate The compiled bytecode and interned tables.
     */
    static CompiledTemplate compile_standalone(const std::vector<ast::Node>& nodes);

private:
    CompiledTemplate out_;

    void   compile_nodes(const std::vector<ast::Node>& nodes);
    void   compile_node(const ast::Node& node);
    void   compile_expr(const ast::Expr& expr);
    void   compile_for(const ast::ForNode& node);
    void   compile_if(const ast::IfNode& node);

    /**
     * \brief Post-compile verifier: simulates sp and lsp over the emitted bytecode.
     *
     * \details
     * Performs a single linear pass over \c out_.code after compilation, tracking
     * the integer value-stack pointer (\c sp) and loop-stack pointer (\c lsp).
     * Throws \c std::runtime_error if either counter would exceed the runtime
     * array bounds declared in \c execute() (\c MAX_VALUE_STACK_SIZE and
     * \c MAX_LOOP_STACK_SIZE).  The exception is caught by \c Runtime::load()'s
     * single \c try/catch boundary and returned as \c TemplateParseError.
     *
     * This eliminates the need for any overflow/underflow checks inside the hot
     * render path.
     */
    void   verify_stack_depths();

    /** \brief Emit an instruction and return its index for back-patching. */
    size_t emit(Op op, int32_t operand = 0);

    /** \brief Fill in a previously emitted jump's offset now that the target is known. */
    void   patch(size_t instruction_index, int32_t offset);

    /**
     * \brief Intern a raw text string; return its index in \c out_.strings.
     * \details Deduplication is not performed; each call appends a new entry.
     */
    size_t intern_string(std::string_view s);

    /**
     * \brief Intern a dotted variable path; return its index in \c out_.paths.
     * \details Deduplication is not performed; each call appends a new entry.
     */
    size_t intern_path(std::string_view s);

    /**
     * \brief Intern a filter name; return its index in \c out_.filter_names.
     * \details Deduplication is not performed; each call appends a new entry.
     */
    size_t intern_filter(std::string_view s);

    /**
     * \brief Intern a constant \c Value; return its index in \c out_.constants.
     */
    size_t intern_constant(Value v);
};

} // namespace guss::render
