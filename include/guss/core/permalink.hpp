/**
 * \file permalink.hpp
 * \brief Permalink pattern expansion for Guss SSG.
 *
 * \details
 * Expands a pattern string by replacing {token} placeholders with the
 * corresponding field from a Value map. Token lookup is a plain string
 * field lookup — no date parsing, no type-specific logic.
 *
 * Adapters are responsible for pre-computing convenience fields like
 * year, month, day and inserting them into the Value before calling expand().
 */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include "guss/core/value.hpp"

namespace guss::core {

/**
 * \brief Permalink pattern expander.
 */
class PermalinkGenerator final {
public:
    /**
     * \brief Expand a permalink pattern using fields from a Value.
     * \param pattern  Pattern string, e.g. "/{year}/{month}/{slug}/".
     * \param data     Value map containing the token fields.
     * \retval std::string  Expanded permalink. Missing tokens produce empty strings for that token.
     */
    [[nodiscard]] static std::string expand(std::string_view pattern,
                                            const Value& data);

    /**
     * \brief Convert a permalink string to an output file path.
     * \param permalink  e.g. "/2024/03/my-post/"
     * \retval std::filesystem::path  e.g. "2024/03/my-post/index.html"
     */
    [[nodiscard]] static std::filesystem::path permalink_to_path(std::string_view permalink);
};

} // namespace guss::core
