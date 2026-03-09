#include "guss/render/context.hpp"

#ifndef MAX_RECURSION
#define MAX_RECURSION 10000
#endif

namespace guss::render {

Context Context::child() const {
    Context ctx(locals_.get_allocator().resource());
    ctx.parent_ = this;
    return ctx;
}

void Context::set(std::string key, Value val) {
    locals_.insert_or_assign(std::move(key), std::move(val));
}

// ---------------------------------------------------------------------------
// navigate_value — file-local helper
// ---------------------------------------------------------------------------

/**
 * \brief Walk a dotted path through a Value's sub-properties.
 *
 * \details
 * Used by resolve() when a local variable holds an object/array and the
 * template accesses a sub-field (e.g. "post.title" where "post" is a local).
 *
 * \param val  Starting Value (already resolved for the first path component).
 * \param path Remaining dotted path after the first component (e.g. "title"
 *             or "author.name").
 * \retval Value The resolved sub-value, or a null Value if not found.
 */
static Value navigate_value(Value val, std::string_view path) {
    while (!path.empty()) {
        const size_t dot = path.find('.');
        const std::string_view key =
            (dot == std::string_view::npos) ? path : path.substr(0, dot);

        val = val[key];
        if (val.is_null()) return {};
        if (dot == std::string_view::npos) break;
        path = path.substr(dot + 1);
    }
    return val;
}

// ---------------------------------------------------------------------------
// Context::resolve
// ---------------------------------------------------------------------------

Value Context::resolve(std::string_view dotted_path, int depth) const {
    if (depth >= MAX_RECURSION) return {};

    const Context* current_ctx = this;
    while (current_ctx) {
        // 1. Exact-key match — handles "loop.index" style flat dotted keys
        //    that were stored verbatim via ctx.set("loop.index", ...).
        {
            auto it = current_ctx->locals_.find(std::string(dotted_path));
            if (it != current_ctx->locals_.end()) return it->second;
        }

        // 2. Prefix match + navigate into the value — handles "post.title"
        //    when "post" is a locally-set object Value.
        const size_t dot = dotted_path.find('.');
        if (dot != std::string_view::npos) {
            auto it = current_ctx->locals_.find(
                std::string(dotted_path.substr(0, dot)));
            if (it != current_ctx->locals_.end()) {
                return navigate_value(it->second, dotted_path.substr(dot + 1));
            }
        }

        current_ctx = current_ctx->parent_;
    }

    return {};
}

} // namespace guss::render
