/**
 * \file engine.hpp
 * \brief Template engine that owns the compiled-template cache and executes bytecode.
 *
 * \details
 * The Engine is the top-level object used by callers to load and render templates.
 * It orchestrates lexing, parsing, compilation, and bytecode execution, and owns
 * the per-process filter registry and template cache.
 *
 * \note After all templates have been loaded (i.e. the load phase is complete),
 *       the cache is read-only and \c render() is safe to call from multiple
 *       threads concurrently.  Loading templates from multiple threads is \b not
 *       thread-safe.
 */
#pragma once

#include "guss/core/error.hpp"
#include "guss/render/compiler.hpp"
#include "guss/render/context.hpp"
#include "guss/render/value.hpp"

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace guss::render {

/**
* \brief Initial size of the output buffer used by the bytecode executor.
*
* \details
* The output buffer is a std::string that is appended to during execution.  By
* reserving 32 KiB upfront, we can avoid multiple reallocations for typical page
* payloads, which improves performance on the hot path.
*/
constexpr size_t WRITE_BUFFER_SIZE = 0x8000;

/**
 * \brief Maximum size of the value stack used by the bytecode executor.
 *
 * \details
 * The value stack is a fixed-size array allocated on the stack in \c execute().
 * 64 slots is sufficient for realistic templates; if this limit is exceeded, the
 * engine will throw a runtime error.
 */
constexpr size_t MAX_VALUE_STACK_SIZE = 64;

/**
 * \brief Maximum size of the loop stack used by the bytecode executor.
 *
 * \details
 * The loop stack is a fixed-size array allocated on the stack in \c execute().
 * 16 levels of nesting is sufficient for realistic templates; if this limit is
 * exceeded, the engine will throw a runtime error.
 */
constexpr size_t MAX_LOOP_STACK_SIZE = 16;

/** \brief Template engine: cache, filter registry, and bytecode executor. */
class Engine {
public:
    /**
     * \brief Construct an Engine rooted at the given theme directory.
     * \param theme_dir Absolute or relative path to the directory containing
     *                  template files.  Added as the first search path.
     */
    explicit Engine(std::filesystem::path theme_dir);

    /**
     * \brief Append an additional directory to the template search path.
     * \param dir Directory to append.  Searched after all previously added paths.
     */
    void add_search_path(const std::filesystem::path& dir);

    /**
     * \brief Load, compile, and cache a template by name.
     *
     * \details
     * On first call the template file is located, read, lexed, parsed, and
     * compiled.  Subsequent calls return the cached result directly.
     *
     * \param name Template name relative to one of the search paths.
     * \retval const CompiledTemplate* Pointer to the cached entry on success.
     * \retval error::Error            If the file cannot be found, read, lexed, parsed, or compiled.
     */
    error::Result<const CompiledTemplate*> load(std::string_view name);

    /**
     * \brief Render a template by name against the provided context.
     *
     * \details
     * Calls \c load() then executes the bytecode with \c execute().  The
     * output buffer is pre-reserved to 32 KiB to reduce reallocations for
     * typical page payloads.  No exceptions escape this function.
     *
     * \param template_name Template name forwarded to \c load().
     * \param ctx           Rendering context supplying variable bindings.
     * \retval std::string  Rendered output on success.
     * \retval error::Error Propagated from \c load() if the template cannot be loaded.
     */
    error::Result<std::string> render(std::string_view template_name, Context& ctx);

private:
    std::vector<std::filesystem::path>                search_paths_;
    std::unordered_map<std::string, CompiledTemplate> cache_;

    /** \brief Callable type for a registered filter function. */
    using FilterFn = std::function<Value(const Value&, std::span<const Value>)>;

    std::vector<FilterFn>                  filters_;
    std::unordered_map<std::string, size_t> filter_index_;

    /** \brief Register all built-in filters into \c filters_ and \c filter_index_. */
    void register_builtin_filters();

    /**
     * \brief Locate a template file by searching \c search_paths_ in order.
     * \note Only called from within \c load()'s single catch boundary.
     * \throws std::runtime_error if no matching file is found.
     */
    std::filesystem::path resolve_path(std::string_view name) const;

    /**
     * \brief Look up a filter by name and return its index into \c filters_.
     * \note Only called from within \c load()'s single catch boundary.
     * \throws std::runtime_error if the filter name is not registered.
     */
    size_t resolve_filter_id(const std::string& name) const;

    /**
     * \brief Resolve all Filter operand name indices to direct filter IDs.
     * \note Only called from within \c load()'s single catch boundary.
     * \throws std::runtime_error if any filter name is not registered.
     */
    void resolve_filter_ids(CompiledTemplate& tpl);

    /**
     * \brief Execute a compiled template's bytecode, appending output to \p out.
     *
     * \details
     * This is the hot path.  The value stack and loop stack are stack-allocated
     * fixed-size arrays; no heap allocation occurs inside the loop.
     *
     * \param tpl  The compiled template to execute.
     * \param ctx  Rendering context for variable resolution and loop bindings.
     * \param out  Output string; output is appended (not replaced).
     */
    void execute(
        const CompiledTemplate& tpl,
        Context&                ctx,
        std::string&            out
    );
};

} // namespace guss::render
