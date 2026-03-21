---
slug: internals/value-system
title: Value System
---

`guss::core::Value` is a discriminated union of native C++ types. It exists because passing
simdjson objects into the template runtime is not feasible — for five independent reasons.

## The five reasons

1. **simdjson On-demand is single-pass and destructive.**
Iterating an array consumes it. Template loops need re-traversal; this is a hard blocker.

2. **simdjson values are buffer views.**
Lifetime is tied to the padded input buffer. In OpenMP parallel rendering across hundreds
of pages, this causes dangling references.

3. **simdjson is read-only.**
Adapter enrichment (`year`, `month`, `day`, `permalink`, `output_path`) inserts computed
fields that do not exist in the API response — impossible with simdjson DOM.

4. **simdjson field lookup is O(n).**
Template engines access many fields per page. `ValueMap` gives O(1).

5. **simdjson does not exist in the render layer.**
The dependency boundary is strict: `guss-render` has zero knowledge of simdjson. The adapter
converts simdjson → Value exactly once at fetch time. After that, simdjson is gone.

## The variant

```cpp
using ValueVariant = std::variant<
    NullTag,
    std::string_view,
    std::string,
    bool,
    int64_t,
    uint64_t,
    double,
    std::shared_ptr<ValueMap>,
    std::shared_ptr<ValueArray>
>;
```

`ValueMap` is `unordered_map<string, Value>`. Copying a `Value` that holds an object or array is O(1). Reference count bump.
Archive pages that reference all posts copy zero data.

## Hot path rules

Inside `Runtime::execute()`:

- Value stack is a fixed C-array of 64 slots declared in `execute()`'s stack frame. Never `new`'d.
- Copying a leaf `Value` (string, int, bool) is cheap. It's a variant copy.
- Copying a compound `Value` (`ValueMap`, `ValueArray`) is a reference count bump.

## Why `unordered_map`, not `flat_map`

`ValueMap` uses `std::unordered_map`. Enrichment is user-configurable (field maps, computed fields); The number of
post-construction insertions is unknown. `flat_map`'s batch-construct optimization requires knowing all fields upfront.
This is not possible here.

`Context::locals_` uses `std::pmr::unordered_map`. `flat_map` with a monotonic allocator causes waste: the backing vectors
grow, allocate new blocks, and abandon old ones _(monotonic cannot free)_. Fixed-size `unordered_map` nodes pack cleanly
into the arena.
