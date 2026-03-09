# Layer Boundaries

This document records hard architectural rules about which layers may use which
libraries. Violations must never be introduced without explicit discussion.

---

## simdjson — adapters only

simdjson is used **exclusively** in the adapter layer to parse CMS REST API
responses as fast as possible into domain structs.

```
Ghost REST API ──► GhostAdapter    ──► simdjson parse ──► model::Post / Page / Author / …
WordPress API ──► WordPressAdapter ──► simdjson parse ──► model::Post / Page / Author / …
Markdown files ──► MarkdownAdapter ──► (no simdjson)  ──► model::Post / Page / …
```

Once the adapter has produced a domain struct, **simdjson is gone**. No
`simdjson::dom::element`, no `simdjson::dom::object::iterator`, no
`simdjson::dom::array::iterator` anywhere outside `guss::adapters`.

### What this means in practice

| Layer | simdjson allowed? |
|-------|------------------|
| `guss::adapters` (GhostAdapter, WordPressAdapter, …) | **Yes — parsing only** |
| `guss::model` (Post, Page, Author, …) | No |
| `guss::render` (Value, Context, Engine, filters, …) | **No** |
| `guss::builder` (pipeline) | No |
| `guss::config` | No |
| Tests | No (use native Value/Context API) |

---

## render::Value lives in guss-core

`Value` is defined in `guss-core`, not `guss-render`. This allows model structs,
the pipeline, and the render layer to all use `Value` without a circular dependency.

```
guss-core     ← Value, ValueMap, ValueArray, model structs (Post, Page, Author…)
guss-adapters ← depends on guss-core; uses simdjson internally to build Values
guss-render   ← depends on guss-core; uses Value/Context; zero simdjson
guss-builder  ← depends on guss-core + guss-render
```

---

## render::Value — native types only

`Value` is a discriminated union of native C++ types:

```
null | std::string | std::string_view | bool | int64_t | uint64_t | double
    | std::shared_ptr<ValueMap>    (key → Value)
    | std::shared_ptr<ValueArray>  (indexed Value)
```

`simdjson::dom::element` is **not** a storage kind.

The `Value(simdjson::dom::element)` constructor exists only as a **conversion
helper** used at the adapter→render boundary. It deep-converts the element into
native types (owned strings, ValueMap, ValueArray) and retains no reference to
the simdjson document.

`ValueIterator` must not involve any simdjson iterator type.

---

## model structs — Value for render data, typed fields for pipeline concerns

Domain model structs (`Post`, `Page`, `Author`, …) live in `guss-core` alongside `Value`.

Each struct carries:
- **Typed C++ fields** for pipeline concerns: `slug`, `output_path`, `template_name` —
  strongly typed, used for routing and file writing.
- **`Value data`** — a pre-built `ValueMap` (behind `shared_ptr`) constructed once by the
  adapter. Handed directly to `Context` at render time: `ctx.set("post", post.data)`.
  O(1) copy across all parallel renders.

The template author is responsible for knowing which keys each adapter provides.
The structure of `data` is documented per-adapter, not enforced by the compiler.

---

## render::Context — locals only

`Context` holds only `pmr::unordered_map<string, Value> locals_` and a
`parent_` pointer. No simdjson element is stored. No root element of any kind.

Populated at render time by the pipeline:
```cpp
ctx.set("post", post.data);     // O(1) — shared_ptr copy
ctx.set("site", site_value);    // built once at pipeline construction
ctx.set("permalink", Value(url));
```

---

## Error handling — no try/catch in hot paths

All production code outside the single `load()` boundary uses
`error::Result<T>` / `GUSS_TRY` / `GUSS_TRY_VOID`. No `try`/`catch` in
render loops or pipeline loops. Filesystem operations use `std::error_code`
overloads.

---

## Config — dependency injection, not singleton

`Config` is a plain value type constructed from a YAML file path.
No `Config::instance()` static method. Callers that need config receive it as a
constructor argument or function parameter.
