/**
 * \file context.hpp
 * \brief Defines the Context class for managing variable scopes in template rendering.
 *
 * \details
 * The Context class provides a hierarchical structure for variable resolution during
 * template rendering. It holds only locally-set Value variables and a parent pointer
 * for scope chaining. No simdjson or JSON types appear here.
 *
 * locals_ uses std::pmr::unordered_map so the caller can supply a
 * stack-allocated monotonic_buffer_resource — zero heap allocation for the
 * common case where the number of template variables fits in the buffer.
 */
#pragma once
#include <guss/core/value.hpp>
#include <memory_resource>
#include <string>

namespace guss::render {

/**
 * \class Context
 * \brief Manages a hierarchical variable context for template rendering.
 *
 * \details
 * Holds only \c locals_ (a pmr map of name → Value) and a \c parent_ pointer.
 * Variables are resolved by walking the scope chain from child to root.
 * Dotted paths (e.g. "post.title") are resolved by prefix-matching a local
 * variable name and then navigating into the Value's sub-properties.
 *
 * \see Value
 */
class Context {
public:
    /// Construct with heap allocation (default memory resource).
    Context() : locals_(std::pmr::get_default_resource()) {}

    /// Construct with a caller-supplied memory resource (e.g. a stack arena).
    explicit Context(std::pmr::memory_resource* mr) : locals_(mr) {}

    /// Create a child context that shares this context's memory resource.
    Context child() const;

    /// Set or overwrite a local variable in this context.
    void set(std::string key, core::Value val);

    /// Resolve a dotted path against locals (nearest scope first).
    core::Value resolve(std::string_view dotted_path, int depth = 0) const;

private:
    const Context* parent_ = nullptr;                        ///< nullptr if root context.
    std::pmr::unordered_map<std::string, core::Value> locals_;    ///< Per-scope variables.
};

} // namespace guss::render
