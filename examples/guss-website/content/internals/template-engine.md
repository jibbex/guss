---
slug: internals/template-engine
title: Template Engine
---

Templates compile to flat bytecode at load time. The runtime executes them against a
zero-heap `Context` — no dynamic dispatch, no exceptions inside the loop.

## Compilation pipeline

```
Template file ↓ Lexer → token stream (TEXT, IDENT, PIPE, BLOCK_START, …) 
              ↓ Parser → AST 
              ↓ Compiler → CompiledTemplate (flat bytecode + string table) 
              ↓ Cache → Runtime::cache_ (name → CompiledTemplate)
```

The `Runtime` compiles each template once at startup and caches the result. Subsequent
`render()` calls are pure execution; No re-parsing.

## Bytecode opcodes

| Op            | Stack effect        | Description                                  |
|---------------|---------------------|----------------------------------------------|
| `EmitText`    | —                   | Append literal string to output              |
| `Resolve`     | → Value             | Look up dotted path in Context               |
| `Push`        | → Value             | Push compile-time constant                   |
| `Emit`        | Value →             | HTML-escape top of stack, append to output   |
| `EmitRaw`     | Value →             | Append top of stack unescaped (`\| safe`)    |
| `Filter`      | (args+subj) → Value | Apply registered filter                      |
| `BinaryOp`    | (a, b) → Value      | Arithmetic, comparison, logical              |
| `UnaryOp`     | Value → Value       | Negation, `not`                              |
| `JumpIfFalse` | Value →             | Pop, branch if not truthy                    |
| `Jump`        | —                   | Unconditional branch                         |
| `ForBegin`    | —                   | Push LoopFrame onto loop stack               |
| `ForNext`     | —                   | Advance loop or jump past body               |
| `Set`         | Value →             | Pop, store in Context                        |
| `BlockCall`   | —                   | Template inheritance: execute block override |

The value stack is a fixed C-array of 64 slots declared in `execute()`'s stack frame. It
is never heap-allocated.

## Template inheritance

```html
<!-- base.html -->
<html>
  <body>
    <nav><!-- always rendered --></nav>
    {% block content %}{% endblock %}
    <footer><!-- always rendered --></footer>
  </body>
</html>

<!-- page.html -->
{% extends "base.html" %}
{% block content %}
  <main>{{ post.title }}</main>
{% endblock %}
```

`{% extends %}` / `{% block %}` follow Jinja2 semantics. `{{ super() }}` renders the parent block's content inside an
override. `build_override_map()` walks the inheritance chain at render time; The deepest override wins.

## Filters

27 built-in filters registered at `Runtime` construction. Filter signature:

```cpp
Value fn(const Value& subject, std::span<const Value> args);
```

Filters are resolved to integer IDs at compile time. No string lookup during render.

- **String:** `truncate`, `escape`, `lower`, `upper`, `slugify`, `trim`, `capitalize`, `replace`, `urlencode`, `striptags`
- **Array/Object:** `join`, `first`, `last`, `reverse`, `sort`, `items`, `dictsort`
- **Numeric:** `abs`, `round`, `float_`, `int_`
- **Type-aware:** `length`, `default_`, `reading_minutes`, `wordcount`, `date`
- **Special:** `safe` (identity filter; the compiler emits `EmitRaw` at the call site so no filter function is actually called.)

## Hot path rules

These rules are strict for `Runtime::execute()`:

- **No heap allocation inside the bytecode loop.** Value stack and loop stack are fixed C-arrays.
- **No dynamic dispatch or virtual calls inside the loop.** The `switch` on `instr.op` is a jump table.
- **No exceptions inside the loop.** All error paths use `std::expected`. The single `try/catch` boundary is in `Runtime::load()`.
- **Output buffer pre-reserved** to 32 KiB to avoid reallocation for typical pages.
- **Context uses an 8 KiB stack arena.** Zero heap for variable bindings in the common case.
