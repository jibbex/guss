# Guss Template Engine Replacement — Implementation Plan (Revised)

## The Problem

Two JSON parsers in one binary is not "defense in depth." It's an architectural schism.

```
Current flow:
  CMS API → simdjson (parse) → nlohmann::json (convert) → inja (render) → HTML

Target flow:
  CMS API → simdjson (parse) → guss::render (render) → HTML
```

One parser. One representation. One truth.

---

## Phase 0: Inventory & Impact Analysis

**What nlohmann/json touches today:**

| Component     | File                                   | Usage                                                       |
|---------------|----------------------------------------|-------------------------------------------------------------|
| guss-core     | `config.cpp`                           | Config serialization (but config comes from YAML, not JSON) |
| guss-adapters | `ghost_adapter.cpp`, `json_parser.cpp` | CMS response parsing (simdjson already does this)           |
| guss-render   | `inja_engine.cpp`                      | Template context — the ONLY reason nlohmann exists          |
| guss-builder  | `pipeline.cpp`                         | Passes nlohmann objects between adapter → render            |

**Verdict:** nlohmann/json exists solely to satisfy inja's type requirements. Remove inja, remove nlohmann.

**What stays untouched:**
- yaml-cpp (config parsing — different domain, different format)
- simdjson (the one true parser)
- cmark (markdown → HTML conversion)
- Everything else in the stack

---

## Architecture Decision: Bytecode Compiler

The original plan proposed an AST-walking evaluator. This is replaced by a **bytecode compiler**.

The AST-walking approach forces the renderer to recursively chase `unique_ptr` nodes across heap
memory on every page render. With 1000 pages, that is 1000 tree traversals with random memory
access patterns — cache-unfriendly and allocation-heavy.

The bytecode approach compiles each template **once** to a flat `std::vector<Instruction>`.
Rendering is then a tight linear loop — sequential memory access, no heap allocation, no
recursion. The same compiled template is reused across all pages with different contexts.

```
Template source (loaded once)
        │
        ▼
    Lexer → tokens (string_view into source, zero allocation per token)
        │
        ▼
    Parser → AST (transient — exists only long enough to compile)
        │
        ▼
    Compiler → CompiledTemplate (flat bytecode + string/path tables)
        │
        ▼  AST is discarded here
    Template cache: unordered_map<string, CompiledTemplate>
        │
        ▼  called N times per build (once per post/page)
    execute(compiled, ctx, output_string)  ← pure linear loop, zero allocation
```

The AST never participates in rendering. It is a compilation artifact.

---

## Phase 1: Value + Context  ✅ COMPLETE (⚠ see Phase 11 for pending refactor)

Files implemented:
- `include/guss/render/value.hpp`
- `include/guss/render/context.hpp`
- `src/render/value.cpp`
- `src/render/context.cpp`
- `tests/test_value.cpp`
- `tests/test_context.cpp`

> **Note:** `Value` and `Context` were initially placed in `guss-render` and `Value` stored
> `simdjson::dom::element` as one of its variant alternatives. Phase 11 corrects this:
> `Value` moves to `guss-core`, simdjson is removed from its variant entirely.

---

## Phase 2: Lexer  ✅ COMPLETE

All files implemented: `include/guss/render/lexer.hpp`, `src/render/lexer.cpp`, `tests/test_lexer.cpp`.

---

_Original spec retained for reference:_

### 2.1 — Lexer class

```cpp
// lexer.hpp — add to existing file

struct Token {
    TokenType        type;
    std::string_view value;   // view into Template::source — zero allocation
    uint32_t         line;
    uint32_t         col;
};

class Lexer {
public:
    explicit Lexer(std::string_view source);

    // Tokenize the entire source in one pass.
    // Returns a flat vector of tokens. All string_view fields point into source.
    // source must outlive the returned tokens.
    std::vector<Token> tokenize();

private:
    std::string_view   source_;
    size_t             pos_  = 0;
    uint32_t           line_ = 1;
    uint32_t           col_  = 1;
    std::vector<Token> tokens_;

    void      scan_text();           // accumulate raw text until {{ {% {#
    void      scan_tag();            // tokenize inside delimiters
    void      scan_string();         // handle "..." literals
    void      scan_number();         // int or float
    Token     make(TokenType t, std::string_view v) const;
    void      advance(size_t n = 1);
    char      peek(size_t offset = 0) const;
    bool      match(std::string_view s) const;
    TokenType keyword_or_identifier(std::string_view s) const;
    void      skip_comment();        // {# ... #} — consumes entirely, no token emitted
};
```

### 2.2 — Implementation rules

The lexer operates in two modes:

**Text mode:** accumulate characters verbatim until `{{`, `{%`, or `{#` is detected.
Emit a single `Text` token covering the entire span. Do not emit empty text tokens.

**Tag mode:** skip leading whitespace, then tokenize identifiers, operators, and literals
until the matching closing delimiter is found.

Whitespace trimming: `{%-` strips all whitespace before the tag. `-%}` strips all whitespace
after the tag. Strip means consume whitespace characters from the adjacent text token, or
if the text token becomes empty, remove it entirely.

Comments `{# ... #}` are consumed completely. No tokens emitted.

String literals: the `Token::value` view excludes the surrounding quote characters.

**Deliverables:**
- `src/render/lexer.cpp` ✅
- `tests/test_lexer.cpp` ✅

---

## Phase 3: Parser  ✅ COMPLETE

All files implemented: `include/guss/render/parser.hpp`, `src/render/parser.cpp`, `tests/test_parser.cpp`.

---

_Original spec retained for reference:_

Recursive descent over the token stream. Produces the AST defined in `ast.hpp`.
The AST is **transient** — it is discarded after the compiler runs.

### 3.1 — Parser class

```cpp
// parser.hpp
namespace guss::render {

class Parser {
public:
    // tokens: output of Lexer::tokenize()
    // source: the original template source (moved into Template::source)
    // template_name: used in error messages
    explicit Parser(std::vector<lexer::Token> tokens,
                    std::string               source,
                    std::string_view          template_name);

    ast::Template parse();

private:
    std::vector<lexer::Token> tokens_;
    size_t                    pos_ = 0;
    std::string               source_;
    std::string_view          template_name_;

    // Statement parsing
    ast::Node        parse_node();
    ast::Node        parse_block_tag();
    ast::ForNode     parse_for();
    ast::IfNode      parse_if();
    ast::BlockNode   parse_block();
    ast::ExtendsNode parse_extends();
    ast::IncludeNode parse_include();

    // Expression parsing — precedence levels (low to high):
    //   or → and → not → comparison → primary → filter chain
    ast::Expr parse_expr();
    ast::Expr parse_or();
    ast::Expr parse_and();
    ast::Expr parse_not();
    ast::Expr parse_comparison();
    ast::Expr parse_primary();
    ast::Expr parse_filter_chain(ast::Expr base);

    // Token stream helpers
    lexer::Token              consume(lexer::TokenType expected);
    lexer::Token              peek(size_t offset = 0) const;
    bool                      check(lexer::TokenType t) const;
    bool                      at_end() const;
    [[noreturn]] void         error(std::string_view message) const;
};

} // namespace guss::render
```

### 3.2 — Error format

```
parse error in 'post.html' at line 42: unexpected token '}', expected 'endfor'
```

**Deliverables:**
- `include/guss/render/parser.hpp` ✅
- `src/render/parser.cpp` ✅
- `tests/test_parser.cpp` ✅

---

## Phase 4: Compiler  ✅ COMPLETE

All files implemented: `include/guss/render/compiler.hpp`, `src/render/compiler.cpp`, `tests/test_compiler.cpp`.

---

_Original spec retained for reference:_

Lowers `ast::Template` → `CompiledTemplate`. This is the heart of the new architecture.

### 4.1 — Instruction set

```cpp
// compiler.hpp
namespace guss::render {

enum class Op : uint8_t {
    EmitText,       // operand: index into strings table
    Resolve,        // operand: index into paths table         → push value onto stack
    Push,           // operand: index into constants table     → push literal onto stack
    Emit,           // pop value → html-escape → append to output
    EmitRaw,        // pop value → append to output without escaping (| safe)
    Filter,         // operand: hi8=filter_id lo8=arg_count    → pop args+subject, push result
    BinaryOp,       // operand: operator id                    → pop two, push result
    UnaryOp,        // operand: operator id                    → pop one, push result
    JumpIfFalse,    // operand: relative signed offset         → pop, jump if not truthy
    Jump,           // operand: relative signed offset
    ForBegin,       // operand: hi16=iterable_path_idx lo16=var_name_path_idx
    ForNext,        // operand: relative signed offset if loop exhausted
    ForEnd,         // no-op marker (loop already popped in ForNext)
    BlockCall,      // operand: index into blocks table (template inheritance)
    Return,
};

struct Instruction {
    Op      op;
    int32_t operand = 0;
};

struct CompiledTemplate {
    std::vector<Instruction> code;
    std::vector<std::string> strings;    // interned raw text segments
    std::vector<std::string> paths;      // interned dotted resolve paths
    std::vector<Value>       constants;  // int / float / bool / string literals
    std::vector<std::string> blocks;     // block names for inheritance

    // Debug info — parallel array to code, not touched in execute()
    std::vector<uint32_t>    lines;
};

} // namespace guss::render
```

### 4.2 — Compiler class

```cpp
class Compiler {
public:
    // Compile a parsed template. The AST is consumed and can be freed after this call.
    CompiledTemplate compile(const ast::Template& tpl);

private:
    CompiledTemplate out_;

    void   compile_nodes(const std::vector<ast::Node>& nodes);
    void   compile_node(const ast::Node& node);
    void   compile_expr(const ast::Expr& expr);
    void   compile_for(const ast::ForNode& node);
    void   compile_if(const ast::IfNode& node);

    // Emit an instruction and return its index for back-patching
    size_t emit(Op op, int32_t operand = 0);

    // Fill in a previously emitted jump's offset now that the target is known
    void   patch(size_t instruction_index, int32_t offset);

    size_t intern_string(std::string_view s);
    size_t intern_path(std::string_view s);
    size_t intern_constant(Value v);
};
```

### 4.3 — Back-patching

Every forward jump is emitted with operand 0, then patched once the target address is known:

```cpp
void Compiler::compile_if(const ast::IfNode& node) {
    std::vector<size_t> end_jumps;

    for (const auto& branch : node.branches) {
        compile_expr(branch.condition);
        size_t false_jump = emit(Op::JumpIfFalse, 0);   // target unknown

        compile_nodes(branch.body);

        size_t end_jump = emit(Op::Jump, 0);             // jump to endif
        end_jumps.push_back(end_jump);

        // Patch the false_jump to land here (start of next branch or else)
        patch(false_jump,
              static_cast<int32_t>(out_.code.size()) - static_cast<int32_t>(false_jump));
    }

    compile_nodes(node.else_body);

    // Patch all end_jumps to land here (after endif)
    for (size_t site : end_jumps) {
        patch(site,
              static_cast<int32_t>(out_.code.size()) - static_cast<int32_t>(site));
    }
}
```

### 4.4 — For loop compilation

```cpp
void Compiler::compile_for(const ast::ForNode& node) {
    // Pack iterable path index and var name path index into one operand
    size_t iterable_idx = intern_path(get_path(node.iterable));
    size_t var_idx      = intern_path(node.var_name);

    emit(Op::ForBegin,
         static_cast<int32_t>((iterable_idx << 16) | (var_idx & 0xFFFF)));

    size_t loop_top = out_.code.size();

    size_t exhausted_jump = emit(Op::ForNext, 0);  // patch when body is compiled

    compile_nodes(node.body);

    // Jump back to ForNext (negative offset)
    emit(Op::Jump,
         static_cast<int32_t>(loop_top) - static_cast<int32_t>(out_.code.size()));

    // Patch ForNext to jump here when exhausted
    patch(exhausted_jump,
          static_cast<int32_t>(out_.code.size()) - static_cast<int32_t>(exhausted_jump));

    if (!node.else_body.empty()) {
        compile_nodes(node.else_body);
    }

    emit(Op::ForEnd);
}
```

**Deliverables:**
- `include/guss/render/compiler.hpp` ✅
- `src/render/compiler.cpp` ✅
- `tests/test_compiler.cpp` ✅

---

## Phase 5: Engine (Execute Loop)  ✅ COMPLETE

All files implemented: `include/guss/render/engine.hpp`, `src/render/engine.cpp`, `tests/test_engine.cpp`, `tests/test_inheritance.cpp`.

---

_Original spec retained for reference:_

The engine owns the template cache and runs the bytecode. Only `execute()` is on the hot path.

### 5.1 — Engine class

```cpp
// engine.hpp
namespace guss::render {

class Engine {
public:
    explicit Engine(const config::RenderConfig& cfg);

    void add_search_path(const std::filesystem::path& dir);

    // Load, compile, and cache a template. Subsequent calls return the cached result.
    // Thread-safe for reads after all templates are loaded.
    const CompiledTemplate& load(std::string_view name);

    // Render a template. Thread-safe: compiled template is const after load().
    // Each call gets its own stack and output string.
    std::string render(std::string_view template_name, Context& ctx);

private:
    config::RenderConfig                              cfg_;
    std::vector<std::filesystem::path>                search_paths_;
    std::unordered_map<std::string, CompiledTemplate> cache_;

    // Filter registry — flat vector, indexed by filter id for O(1) lookup
    using FilterFn = std::function<Value(const Value&, std::span<const Value>)>;
    std::vector<FilterFn>                             filters_;
    std::unordered_map<std::string, size_t>           filter_index_;
    void register_builtin_filters();

    std::filesystem::path resolve_path(std::string_view name) const;

    void execute(
        const CompiledTemplate& tpl,
        Context&                ctx,
        std::string&            out
    );
};

} // namespace guss::render
```

### 5.2 — Execute loop

This is the only function that matters for performance. No allocation inside the loop.

```cpp
void Engine::execute(const CompiledTemplate& tpl, Context& ctx, std::string& out) {
    // Fixed-size stack — templates do not nest deeply enough to overflow this
    Value  stack[64];
    size_t sp  = 0;
    size_t pc  = 0;
    const size_t end = tpl.code.size();

    // Loop frames for {% for %} — max nesting depth 16
    struct LoopFrame {
        Value  array;
        size_t index;
        size_t length;
        size_t var_name_idx;   // index into tpl.paths
    };
    LoopFrame loop_stack[16];
    size_t    lsp = 0;

    while (pc < end) {
        const Instruction& instr = tpl.code[pc++];

        switch (instr.op) {

        case Op::EmitText:
            out += tpl.strings[static_cast<size_t>(instr.operand)];
            break;

        case Op::Resolve:
            stack[sp++] = ctx.resolve(tpl.paths[static_cast<size_t>(instr.operand)]);
            break;

        case Op::Push:
            stack[sp++] = tpl.constants[static_cast<size_t>(instr.operand)];
            break;

        case Op::Emit:
            out += html_escape(stack[--sp].to_string());
            break;

        case Op::EmitRaw:
            out += stack[--sp].to_string();
            break;

        case Op::Filter: {
            const size_t filter_id = static_cast<size_t>((instr.operand >> 8) & 0xFF);
            const size_t arg_count = static_cast<size_t>(instr.operand & 0xFF);
            sp -= arg_count;
            Value& subject = stack[sp - 1];
            subject = filters_[filter_id](
                subject,
                std::span<const Value>(stack + sp, arg_count)
            );
            break;
        }

        case Op::JumpIfFalse:
            if (!stack[--sp].is_truthy())
                pc = static_cast<size_t>(static_cast<int32_t>(pc) + instr.operand - 1);
            break;

        case Op::Jump:
            pc = static_cast<size_t>(static_cast<int32_t>(pc) + instr.operand - 1);
            break;

        case Op::ForBegin: {
            const size_t iterable_idx = static_cast<size_t>((instr.operand >> 16) & 0xFFFF);
            const size_t var_idx      = static_cast<size_t>(instr.operand & 0xFFFF);
            Value iterable = ctx.resolve(tpl.paths[iterable_idx]);
            const size_t length = iterable.size();
            loop_stack[lsp++] = { std::move(iterable), 0, length, var_idx };
            break;
        }

        case Op::ForNext: {
            LoopFrame& frame = loop_stack[lsp - 1];
            if (frame.index >= frame.length) {
                --lsp;
                pc = static_cast<size_t>(static_cast<int32_t>(pc) + instr.operand - 1);
            } else {
                ctx.set(tpl.paths[frame.var_name_idx], frame.array[frame.index]);
                ctx.set("loop.index",
                    Value(static_cast<int64_t>(frame.index + 1)));
                ctx.set("loop.index0",
                    Value(static_cast<int64_t>(frame.index)));
                ctx.set("loop.first",
                    Value(frame.index == 0));
                ctx.set("loop.last",
                    Value(frame.index == frame.length - 1));
                ctx.set("loop.length",
                    Value(static_cast<int64_t>(frame.length)));
                ++frame.index;
            }
            break;
        }

        case Op::ForEnd:
            break;

        case Op::Return:
            return;
        }
    }
}
```

**Deliverables:**
- `include/guss/render/engine.hpp` ✅
- `src/render/engine.cpp` ✅
- `tests/test_engine.cpp` ✅
- `tests/test_inheritance.cpp` ✅

---

## Phase 6: Filters  ✅ COMPLETE

All files implemented: `include/guss/render/filters.hpp`, `src/render/filters.cpp`, `tests/test_filters.cpp`.

---

_Original spec retained for reference:_

Registered as a flat vector indexed by id. Name-to-id mapping is resolved at compile time,
so filter dispatch in `execute()` is a direct array index — no hash lookup on the hot path.

### 6.1 — Built-in filter set

```cpp
// filters.hpp
namespace guss::render::filters {

// Register all built-in filters into the engine's registry.
// Called once in Engine constructor.
void register_all(std::vector<Engine::FilterFn>& registry,
                  std::unordered_map<std::string, size_t>& index);

Value date(const Value& v, std::span<const Value> args);       // format ISO date string
Value truncate(const Value& v, std::span<const Value> args);   // truncate with ellipsis
Value escape(const Value& v, std::span<const Value> args);     // HTML entity escaping
Value safe(const Value& v, std::span<const Value> args);       // mark as pre-escaped
Value default_(const Value& v, std::span<const Value> args);   // fallback value
Value length(const Value& v, std::span<const Value> args);     // array/string length
Value lower(const Value& v, std::span<const Value> args);      // lowercase
Value upper(const Value& v, std::span<const Value> args);      // uppercase
Value slugify(const Value& v, std::span<const Value> args);    // URL-safe slug
Value join(const Value& v, std::span<const Value> args);       // join array with separator
Value first(const Value& v, std::span<const Value> args);      // first element
Value last(const Value& v, std::span<const Value> args);       // last element
Value reverse(const Value& v, std::span<const Value> args);    // reverse array
Value striptags(const Value& v, std::span<const Value> args);  // strip HTML tags
Value urlencode(const Value& v, std::span<const Value> args);  // URL-encode string

} // namespace guss::render::filters
```

The `date` filter reuses `parse_timestamp()` from `json_parser.cpp` — move that function
to a shared utility header so both the adapter layer and the filter layer can use it
without duplication.

**Deliverables:**
- `include/guss/render/filters.hpp` ✅
- `src/render/filters.cpp` ✅
- `tests/test_filters.cpp` ✅

---

## Phase 7: CMake Surgery  ✅ COMPLETE

`nlohmann/json` and `inja` CPMAddPackage blocks removed. `guss-render` now lists only the new source files. No nlohmann or inja references anywhere in `include/` or `src/`.

### 7.1 — Remove  ✅

```cmake
# DELETED — both CPMAddPackage blocks are gone from CMakeLists.txt
# - nlohmann/json
# - inja
```

### 7.2 — Replace guss-render

```cmake
add_library(guss-render
    src/render/value.cpp
    src/render/context.cpp
    src/render/lexer.cpp
    src/render/parser.cpp
    src/render/compiler.cpp    # new — was not in original plan
    src/render/engine.cpp
    src/render/filters.cpp
)

target_include_directories(guss-render PUBLIC include)
target_link_libraries(guss-render PUBLIC
    guss-core        # brings simdjson transitively
    spdlog::spdlog
)
# No nlohmann. No inja.
```

### 7.3 — Clean up guss-core

```cmake
target_link_libraries(guss-core PUBLIC
    simdjson
    yaml-cpp
    spdlog::spdlog
    expected
)
# nlohmann_json::nlohmann_json removed
```

### 7.4 — Render config

Add to the config schema — `html_reserve_bytes` controls the initial `reserve()` on each
output string. Tune this to your p95 rendered post size to avoid reallocation in the
common case.

```yaml
render:
  html_reserve_bytes: 32768   # initial output buffer size per page
```

---

## Phase 8: Adapter Layer Migration  ⚠ SUPERSEDED by Phase 11

The original Phase 8 spec (retained below) described passing raw simdjson elements through
the pipeline. This was wrong. See Phase 11 for the correct architecture.

---

_Original spec (superseded — do not implement):_

> ~~Phase 8.1, 8.2, 8.3 described FetchResult holding simdjson::dom::element vectors and~~
> ~~Context being constructed from a simdjson element. This approach has been discarded.~~
> ~~simdjson is adapter-only. See docs/layer-boundaries.md.~~

---

## Phase 9: Testing & Validation  ⚠ PARTIAL

### 9.1 — Test matrix

| Suite                  | Scope                          | Status |
|------------------------|--------------------------------|--------|
| `test_value.cpp`       | Value type, native types only  | ⚠ needs update (Phase 11) |
| `test_context.cpp`     | Scope resolution, dot-paths    | ⚠ needs update (Phase 11) |
| `test_lexer.cpp`       | All token types, trim, errors  | ✅ done |
| `test_parser.cpp`      | AST construction, precedence   | ✅ done |
| `test_compiler.cpp`    | Bytecode emission, jump offsets| ✅ done |
| `test_engine.cpp`      | End-to-end rendering           | ✅ done |
| `test_filters.cpp`     | Each filter individually       | ✅ done |
| `test_inheritance.cpp` | extends/block                  | ✅ done |
| `test_regression.cpp`  | Full build diff                | 🔲 TODO |

### 9.2 — Regression strategy

Before touching any existing code:

```bash
# Snapshot current output with inja
guss build --output-dir snapshots/before/

# ... implement all phases ...

# Compare
guss build --output-dir snapshots/after/
diff -r snapshots/before/ snapshots/after/
# Must be empty or whitespace-only differences from trim behavior
```

### 9.3 — Performance benchmark

```cpp
// benchmarks/bench_render.cpp
// Render 1000 posts with realistic Ghost API data.
// Metrics: wall time, peak RSS.
// Expected outcome: strictly better than inja + nlohmann due to:
//   - no simdjson → nlohmann conversion
//   - bytecode linear scan vs recursive AST walk
//   - fixed stack vs heap allocation per render
```

---

## Phase 10: Cleanup  ⚠ PARTIAL

```bash
grep -r "nlohmann" include/ src/   # ✅ zero results
grep -r "inja"     include/ src/   # ⚠ still matches inja_engine.cpp and inja_engine.hpp
```

**Remaining:**
- `src/render/inja_engine.cpp` — still exists, not in build, needs deletion
- `include/guss/render/inja_engine.hpp` — still exists, needs deletion
- CPM blocks: already removed ✅
- All `#include <nlohmann/json.hpp>` and `#include <inja/inja.hpp>`: only in the above two files

---

## Execution Order & Status

| Phase | Description                   | Status       |
|-------|-------------------------------|--------------|
| 0     | Inventory                     | ✅ complete  |
| 1     | Value + Context               | ✅ complete  |
| 2     | Lexer                         | ✅ complete  |
| 3     | Parser                        | ✅ complete  |
| 4     | Compiler                      | ✅ complete  |
| 5     | Engine execute loop           | ✅ complete  |
| 6     | Filters                       | ✅ complete  |
| 7     | CMake surgery                 | ✅ complete  |
| 8     | Adapter + pipeline migration  | ✅ complete  |
| 9     | Testing & validation          | ⚠ partial — `test_regression.cpp` missing |
| 10    | Cleanup                       | ⚠ partial — `inja_engine.cpp` + `inja_engine.hpp` not yet deleted |

---

## Risk Assessment

| Risk                             | Impact                           | Mitigation                                                          |
|----------------------------------|----------------------------------|---------------------------------------------------------------------|
| ~~simdjson DOM lifetime~~        | ~~eliminated~~                   | simdjson not present in render layer (Phase 11)                     |
| Jump offset arithmetic           | High — wrong bytecode is silent  | Compiler unit tests verify every jump target independently          |
| Template edge cases missed       | Medium — broken themes           | Golden file regression before any code changes                      |
| Filter behavior diverges         | Medium — subtle rendering bugs   | Per-filter tests with inja output as ground truth                   |
| Template inheritance complexity  | Medium — extends/block is tricky | Implement last; keep inja branch alive for comparison until Phase 9 |
| OpenMP false sharing             | Low — performance only           | Each thread writes to a distinct `outputs[i]` slot                  |

---

## File Manifest

### Complete after all phases

```
include/guss/render/
├── value.hpp            ✅ done
├── context.hpp          ✅ done
├── lexer.hpp            ✅ done
├── ast.hpp              ✅ done
├── parser.hpp           ✅ done
├── compiler.hpp         ✅ done
├── engine.hpp           ✅ done
├── filters.hpp          ✅ done
├── detail/html.hpp      ✅ done
└── inja_engine.hpp      ⚠  DELETE (Phase 10)

src/render/
├── value.cpp            ✅ done
├── context.cpp          ✅ done
├── lexer.cpp            ✅ done
├── parser.cpp           ✅ done
├── compiler.cpp         ✅ done
├── engine.cpp           ✅ done
├── filters.cpp          ✅ done
└── inja_engine.cpp      ⚠  DELETE (Phase 10) — not in build

tests/
├── test_value.cpp       ✅ done
├── test_context.cpp     ✅ done
├── test_lexer.cpp       ✅ done
├── test_parser.cpp      ✅ done
├── test_compiler.cpp    ✅ done
├── test_engine.cpp      ✅ done
├── test_filters.cpp     ✅ done
├── test_inheritance.cpp ✅ done
└── test_regression.cpp  🔲 TODO (Phase 9)

benchmarks/
└── bench_render.cpp     🔲 TODO (Phase 9)
```

### To Delete (Phase 10)

```
src/render/inja_engine.cpp      (not in build — orphaned)
include/guss/render/inja_engine.hpp
```

### Already Modified

```
CMakeLists.txt                       ✅ compiler.cpp added, nlohmann + inja removed
src/adapters/ghost/ghost_adapter.cpp ✅ no nlohmann
src/adapters/ghost/json_parser.cpp   ✅ no nlohmann
src/builder/pipeline.cpp             ✅ uses render::Context
```

---

## Success Criteria

1. `grep -r "nlohmann\|inja" include/ src/` → zero matches  ⚠ blocked by inja_engine files
2. All existing themes render identically to current output (diff test)  🔲 test_regression.cpp needed
3. Build time decreases — nlohmann headers alone add seconds to a clean build  ✅ nlohmann gone
4. Runtime performance equal or better — no conversion layer, bytecode linear scan  ✅ architecture in place
5. One JSON parser. One representation. One truth.  ✅

## Phase 11: Remove simdjson from Value + Context; move Value to guss-core  🔲 TODO

The Phase 1 implementation stored `simdjson::dom::element` inside `Value`'s variant and
inside `Context` as a root element. This violates the layer boundary: simdjson must not
appear anywhere outside `guss-adapters`. See `docs/layer-boundaries.md`.

### 11.1 — Move Value to guss-core

`include/guss/render/value.hpp` → `include/guss/core/value.hpp`
`src/render/value.cpp`          → `src/core/value.cpp`

Update `CMakeLists.txt`:
- Remove `value.cpp` from `guss-render` sources; add to `guss-core` sources.
- `guss-render` already depends on `guss-core` → no new link dependency needed.
- Update all `#include "guss/render/value.hpp"` → `#include "guss/core/value.hpp"`.

### 11.2 — Remove simdjson from Value's variant

**Before:**
```cpp
std::variant<NullTag, simdjson::dom::element, std::string_view, std::string,
             bool, int64_t, uint64_t, double,
             std::shared_ptr<ValueMap>, std::shared_ptr<ValueArray>>
```

**After:**
```cpp
std::variant<NullTag, std::string_view, std::string,
             bool, int64_t, uint64_t, double,
             std::shared_ptr<ValueMap>, std::shared_ptr<ValueArray>>
```

The `Value(simdjson::dom::element)` constructor remains but becomes a **deep-conversion**
constructor: scalars map to their native types, objects become `ValueMap`, arrays become
`ValueArray`, strings become owned `std::string` (not string_view — simdjson memory
lifetime is not guaranteed beyond the constructor call).

This constructor is only callable from `guss-adapters`. It must not be used in `guss-render`
or `guss-builder`. Consider marking it with a comment or moving it to an adapter-only utility.

### 11.3 — Remove simdjson branches from Value methods

Delete all `if (std::holds_alternative<simdjson::dom::element>(data_))` branches from:
- `is_array()`, `is_object()`
- `operator[](string_view)`, `operator[](size_t)`
- `has()`, `size()`
- `begin()`, `end()`
- `to_string()`

### 11.4 — Remove simdjson iterators from ValueIterator

**Before:**
```cpp
using ValueIterator = std::variant<simdjson::dom::object::iterator,
                                   simdjson::dom::array::iterator>;
```

**After:** `ValueIterator` becomes a proper iterator over `ValueMap` / `ValueArray`.
Or remove it entirely if only used internally.

### 11.5 — Clean up Context

Remove `Context(simdjson::dom::element)` constructors entirely.
Context is populated explicitly by the pipeline:
```cpp
ctx.set("post",      post.data);    // Value(ValueMap) — shared_ptr copy, O(1)
ctx.set("site",      site_value);   // built once at pipeline init
ctx.set("permalink", Value(url));
```

### 11.6 — Model structs get a Value data field

Each domain struct in `guss-core` gains a `Value data` field:
```cpp
struct Post {
    std::string           slug;          // pipeline: routing, file naming
    std::filesystem::path output_path;   // pipeline: write destination
    std::string           template_name; // pipeline: which template to render

    Value data;  // pre-built ValueMap; handed to Context at render time
};
```

The adapter constructs `data` once during fetch using `Value(simdjson::dom::element)`
deep-conversion. All subsequent accesses are zero-copy (shared_ptr).

### 11.7 — Update FetchResult

```cpp
struct FetchResult {
    std::vector<model::Post>   posts;
    std::vector<model::Page>   pages;
    std::vector<model::Author> authors;
    std::vector<model::Tag>    tags;
};
```

No simdjson elements. No document lifetime management. Clean ownership.

### 11.8 — Update tests

All tests that construct `Context ctx(json_element)` must be rewritten to use
`ctx.set(key, Value(...))` directly with native types. No simdjson in test code.

### 11.9 — Execution order

1. Move `value.hpp` / `value.cpp` to `guss-core`, update CMakeLists + all includes.
2. Remove simdjson from `Value` variant; implement deep-conversion constructor.
3. Remove simdjson branches from all Value methods; remove simdjson from ValueIterator.
4. Remove simdjson constructors from `Context`; verify `context.cpp` has no simdjson.
5. Add `Value data` to model structs.
6. Update `FetchResult` to use model structs.
7. Update adapters to build `post.data` during fetch.
8. Update pipeline to populate Context from model struct fields.
9. Update all tests.
10. Verify: `grep -r "simdjson" include/ src/` → matches only in `guss-adapters`.

---

## Remaining Work

1. **Phase 9** — Write `tests/test_regression.cpp` and `benchmarks/bench_render.cpp`.
2. **Phase 10** — Delete `src/render/inja_engine.cpp` and `include/guss/render/inja_engine.hpp`.
3. **Phase 11** — Remove simdjson from Value/Context; move Value to guss-core; model structs get `Value data`.