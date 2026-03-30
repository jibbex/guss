---
slug: internals/template-engine
title: Template Engine
---

Templates compile to flat bytecode at load time. The runtime executes them against a
zero-heap `Context` — no dynamic dispatch, no exceptions inside the loop.

## Compilation pipeline

```
Template file
     ↓ Lexer      → token stream (TEXT, IDENT, PIPE, BLOCK_START, …)
     ↓ Parser     → AST
     ↓ Compiler   → CompiledTemplate (flat bytecode + string table + constant table + path table)
     ↓ Cache      → Runtime::cache_ (name → CompiledTemplate)
```

The `Runtime` compiles each template once and caches the result. Subsequent `render()`
calls are pure execution — no re-parsing, no re-compilation.

## Bytecode opcodes

| Op            | Stack effect        | Description                                                   |
|---------------|---------------------|---------------------------------------------------------------|
| `EmitText`    | —                   | Append literal string to output (no stack involvement)        |
| `Resolve`     | → Value             | Look up dotted path in Context                                |
| `Push`        | → Value             | Push compile-time constant                                    |
| `Emit`        | Value →             | HTML-escape top of stack, append to output                    |
| `EmitRaw`     | Value →             | Append top of stack unescaped (`\| safe`)                     |
| `Filter`      | (args+subj) → Value | Apply registered filter; args popped first, then subject      |
| `BinaryOp`    | (a, b) → Value      | Arithmetic, comparison, logical                               |
| `UnaryOp`     | Value → Value       | Negation, `not`                                               |
| `JumpIfFalse` | Value →             | Pop, branch if not truthy                                     |
| `Jump`        | —                   | Unconditional branch                                          |
| `ForBegin`    | —                   | Push LoopFrame onto loop stack                                |
| `ForNext`     | —                   | Advance loop or jump past body; sets loop variable + `loop.*` |
| `ForEnd`      | —                   | No-op marker — end of for-loop region                         |
| `Set`         | Value →             | Pop, store in Context under path name                         |
| `Include`     | —                   | Render named template inline, inheriting current Context      |
| `BlockCall`   | —                   | Template inheritance: execute block override or fall through  |
| `BlockEnd`    | —                   | No-op marker — end of block's default body                    |
| `Super`       | → Value             | Render parent block into a string, push onto stack            |
| `Return`      | —                   | Terminate execution of current template                       |

The value stack is a fixed C-array of 64 slots declared in `execute()`'s stack frame.
The loop stack is a fixed C-array of 16 slots. Neither is ever heap-allocated.

## Concrete bytecode walkthrough

Consider this template expression:

```jinja
{{ post.title | upper | truncate(50) }}
```

The compiler emits these instructions:

```
Push    50                  # compile-time constant → stack: [50]
Resolve "post.title"        # context lookup → stack: [50, "My Post Title"]
Filter  upper   (0 args)    # pop subject, push result → stack: [50, "MY POST TITLE"]
Filter  truncate (1 arg)    # pop 1 arg + subject, push result → stack: ["MY POST TI…"]
Emit                        # pop, HTML-escape, append to output → stack: []
```

Stack trace step by step:

| Step | Instruction               | Stack after             |
|------|---------------------------|-------------------------|
| 1    | `Push 50`                 | `[50]`                  |
| 2    | `Resolve "post.title"`    | `[50, "My Post Title"]` |
| 3    | `Filter upper (0 args)`   | `[50, "MY POST TITLE"]` |
| 4    | `Filter truncate (1 arg)` | `["MY POST TI…"]`       |
| 5    | `Emit`                    | `[]`                    |

The stack is always empty at statement boundaries — this is a compiler invariant, not
a runtime check. `Filter` with `N` args pops args right-to-left, then the subject.

## Template inheritance

```html
<!-- base.html -->
<html>
  <body>
    {% block content %}{% endblock %}
  </body>
</html>

<!-- post.html -->
{% extends "base.html" %}
{% block content %}
  <h1>{{ post.title }}</h1>
  {{ post.html | safe }}
{% endblock %}
```

`{% extends %}` / `{% block %}` follow Jinja2 semantics. `{{ super() }}` renders the
parent block's content inside an override. `build_override_map()` walks the inheritance
chain at render time; the deepest override wins.

## `{% include %}` partials

```html
{% include "nav.html" %}
```

Renders the named template inline, inheriting the current Context. All variables
visible at the call site are visible inside the partial. Templates referenced by
`{% include %}` are pre-loaded and compiled when the parent template is loaded — no
runtime file I/O inside the render loop.

## `{% set %}` variable assignment

```jinja
{% set year = post.published_at | date("%Y") %}
<time>{{ year }}</time>
```

`{% set %}` evaluates the right-hand expression and stores the result in the current
Context under the given name. The variable is visible in all subsequent expressions
within the same scope, including inside nested `{% for %}` loops and `{% include %}`
calls.

## Context and scope chaining

The `Context` stores variables in a `pmr::unordered_map` backed by an 8 KiB stack
arena:

```cpp
std::byte buf[8192];
std::pmr::monotonic_buffer_resource arena(buf, sizeof(buf));
Context ctx(&arena);   // zero heap for variable bindings in the common case
```

Each `{% for %}` loop iteration creates a child scope chained to the parent:

```
root_ctx  [site, post]
  └── loop_ctx  [tag, loop.index, loop.first, loop.last]
```

`Resolve("post.title")` walks the chain — child first, then parent. No data is copied
between scopes; only the chain pointer is set. When the loop ends the child scope is
discarded.

**`loop.*` variables** available inside every `{% for %}` body:

| Variable      | Type | Description                              |
|---------------|------|------------------------------------------|
| `loop.index`  | int  | 1-based iteration counter                |
| `loop.index0` | int  | 0-based iteration counter                |
| `loop.first`  | bool | `true` on the first iteration            |
| `loop.last`   | bool | `true` on the last iteration             |
| `loop.length` | int  | Total number of elements in the iterable |

## Filters

27 built-in filters registered at `Runtime` construction. Filter function signature:

```cpp
Value fn(const Value& subject, std::span<const Value> args);
```

Filters are resolved to integer IDs at **compile time**. No string lookup happens
during rendering — the bytecode carries the integer ID directly.

See the [Filters Reference](/internals/filters/) for the complete list with signatures
and examples.

## Hot path rules

These constraints apply strictly to `Runtime::execute()`:

- **No heap allocation inside the bytecode loop.** Value stack and loop stack are
  fixed C-arrays on the stack frame.
- **No dynamic dispatch or virtual calls inside the loop.** The `switch` on `instr.op`
  is a jump table.
- **No exceptions inside the loop.** All error paths use `std::expected`. The only
  `try/catch` boundary is in `Runtime::load()` (compilation, not execution).
- **Output buffer pre-reserved to 32 KiB** (`WRITE_BUFFER_SIZE = 0x8000`) to avoid
  reallocation for typical page payloads.
- **Context uses an 8 KiB stack arena.** Zero heap for variable bindings in the
  common case.
- **Stack depths verified at compile time.** `Compiler::verify_stack_depths()` walks
  the bytecode before caching and rejects templates that would overflow the 64-slot
  value stack or 16-slot loop stack — no overflow checks needed in the hot path.
