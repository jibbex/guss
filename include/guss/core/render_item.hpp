/**
 * \file render_item.hpp
 * \brief RenderItem and CollectionMap — the transfer types between adapters and pipeline.
 *
 * \details
 * RenderItem is the unit of work for the render and write phases. It holds
 * a pre-computed output path, a template name, and the item's Value data.
 *
 * CollectionMap groups RenderItems by collection name (e.g. "posts", "tags").
 * It is produced by adapters and consumed by the pipeline.
 *
 * Both types live in guss-core so that guss-adapters and guss-builder can
 * use them without creating a circular dependency.
 *
 * \note No simdjson types. No model structs.
 */
#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "guss/core/value.hpp"

namespace guss::render {

/**
 * \brief A single renderable page: output path, template, and data Value.
 *
 * \details
 * Produced by the adapter (item_template pages) and by the pipeline prepare
 * phase (archive pages). The render phase iterates all RenderItems uniformly
 * regardless of their original content type.
 *
 * \par context_key
 * Name under which \c data is exposed in the template (e.g. "post", "tag").
 * Empty means no single-item variable is set (archive pages use extra_context only).
 *
 * \par extra_context
 * Additional root-level template variables injected alongside \c data.
 * Used by archive pages to pass "posts", "tags", "pagination", etc.
 */
struct RenderItem {
    std::filesystem::path output_path;   ///< Where to write the rendered HTML.
    std::string           template_name; ///< Template file to render (e.g. "post.html").
    Value                 data;          ///< Per-item data exposed under context_key.
    std::string           context_key = "item"; ///< Template variable name for data.
    std::vector<std::pair<std::string, Value>> extra_context = {}; ///< Additional root-level variables.
};

/**
 * \brief Maps collection names to their RenderItems.
 *
 * \details
 * Adapters populate this map. Keys are collection names from the
 * collections: YAML block (e.g. "posts", "tags", "authors").
 * The pipeline iterates this map to generate archive pages and then
 * renders all items uniformly.
 *
 * Distinct from config::CollectionCfgMap which maps collection names
 * to their configuration.
 */
using CollectionMap = std::unordered_map<std::string, std::vector<RenderItem>>;

} // namespace guss::render
