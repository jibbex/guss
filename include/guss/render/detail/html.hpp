/**
 * \file html.hpp
 * \brief Internal helper for HTML escaping.
 *
 * \details
 * This header is an implementation detail of the guss::render library.
 * It is NOT part of the public API and must not be included from headers
 * under include/.
 */
#pragma once
#include <string>
#include <string_view>

namespace guss::render::detail {

/**
 * \brief  Append an HTML-escaped version of \p s to \p out.
 * \details
 * This function iterates through the input string \p s and appends an escaped
 * version to \p out, replacing special characters with their corresponding HTML entities:
 * - `&` becomes `&amp;`
 * - `<` becomes `&lt;`
 * - `>` becomes `&gt;`
 * - `"` becomes `&quot;`
 * - `'` becomes `&#39;`
 * All other characters are appended unchanged.
 * \param[in] s   The input string to escape.
 * \param[out] out The output string to which the escaped version of \p s will be appended
 */
inline void html_escape_into(std::string_view s, std::string &out) {
    for (const unsigned char c: s) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                out += static_cast<char>(c);
                break;
        }
    }
}

} // namespace guss::render::detail
