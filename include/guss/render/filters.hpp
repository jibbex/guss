/**
 * \file filters.hpp
 * \brief Built-in filter function declarations for the Guss template engine.
 *
 * \details
 * Each filter is a free function that accepts a subject Value and a span of
 * argument Values and returns the transformed Value.  All 15 built-in filters
 * are declared here so they can be called directly from unit tests without
 * going through the Engine.
 *
 * \c register_all() is the single entry-point called from the Engine
 * constructor.  It populates the engine's filter registry and index map with
 * all built-in filters.
 */
#pragma once
#include "guss/render/value.hpp"

#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace guss::render::filters {

/**
 * \brief Assumed average adult reading speed used by the \c reading_minutes filter.
 *
 * \details
 * Words per minute rate applied when no custom rate is supplied via
 * \c args[0].  Adjust this constant to recalibrate the default estimate
 * for a different target audience.
 *
 * \see reading_minutes
 */
constexpr size_t WORDS_PER_MINUTE = 256;

/// Callable type matching Engine::FilterFn, redeclared here to avoid a
/// circular include between engine.hpp and filters.hpp.
using FilterFn = std::function<Value(const Value&, std::span<const Value>)>;

/**
 * \brief Register all built-in filters.
 *
 * \details
 * Appends one entry per filter to \p registry and inserts the corresponding
 * name-to-index mapping into \p index.  Called exactly once from the Engine
 * constructor.
 *
 * \param registry  The engine's flat filter vector (indexed by id).
 * \param index     The engine's name-to-id map.
 */
void register_all(std::vector<FilterFn>&                  registry,
                  std::unordered_map<std::string, size_t>& index);

/**
 * \brief Format a datetime string.
 *
 * \details
 * Parses an ISO 8601 datetime string (e.g. \c "2024-01-15T10:30:00.000Z",
 * the format emitted by Ghost CMS) and reformats it with \c strftime.
 * \c args[0] is the format string; defaults to \c "%Y-%m-%d" when absent.
 * Returns the original Value unchanged if parsing fails.
 *
 * \param v    Subject value (string).
 * \param args Optional format string argument.
 * \retval Value Formatted date string, or the original value on parse failure.
 */
Value date(const Value& v, std::span<const Value> args);

/**
 * \brief Truncate a string to at most N UTF-8 code points.
 *
 * \details
 * Appends the horizontal ellipsis character U+2026 (\c \xe2\x80\xa6) when the
 * string is actually truncated.  \c args[0] is the character limit; defaults
 * to 255 when absent.
 *
 * \param v    Subject value (string).
 * \param args Optional length argument (integer).
 * \retval Value Truncated string, or the original string if it fits within the limit.
 */
Value truncate(const Value& v, std::span<const Value> args);

/**
 * \brief HTML-escape a string value.
 *
 * \details
 * Replaces \c &, \c <, \c >, \c ", and \c ' with their HTML entity
 * equivalents.
 *
 * \param v    Subject value (string).
 * \param args Unused.
 * \retval Value HTML-escaped string.
 */
Value escape(const Value& v, std::span<const Value> args);

/**
 * \brief Identity filter; marks content as safe for raw output.
 *
 * \details
 * The compiler detects the \c | safe suffix and emits \c Op::EmitRaw instead
 * of \c Op::Emit, bypassing the HTML auto-escape in the execute loop.  This
 * function is still registered so that \c safe used mid-pipeline (chained with
 * other filters) acts as an identity.
 *
 * \param v    Subject value.
 * \param args Unused.
 * \retval Value The subject value unchanged.
 */
Value safe(const Value& v, std::span<const Value> args);

/**
 * \brief Return a default value when the subject is null or falsy.
 *
 * \details
 * If \p v is truthy the original value is returned unchanged.
 * Otherwise \c args[0] is returned, or a null Value when \p args is empty.
 *
 * \param v    Subject value.
 * \param args Optional default value argument.
 * \retval Value The original value, \c args[0], or null.
 */
Value default_(const Value& v, std::span<const Value> args);

/**
 * \brief Return the number of elements or code points in a value.
 *
 * \details
 * For arrays and objects the number of child elements is returned.
 * For strings the number of UTF-8 code points (not bytes) is returned.
 * For null and all other types zero is returned.
 *
 * \param v    Subject value.
 * \param args Unused.
 * \retval Value Integer count.
 */
Value length(const Value& v, std::span<const Value> args);

/**
 * \brief Convert a string to ASCII lowercase.
 *
 * \param v    Subject value (string).
 * \param args Unused.
 * \retval Value Lowercased string.
 */
Value lower(const Value& v, std::span<const Value> args);

/**
 * \brief Convert a string to ASCII uppercase.
 *
 * \param v    Subject value (string).
 * \param args Unused.
 * \retval Value Uppercased string.
 */
Value upper(const Value& v, std::span<const Value> args);

/**
 * \brief Convert a string to a URL-safe slug.
 *
 * \details
 * The transformation proceeds in order:
 * -# Convert to ASCII lowercase.
 * -# Replace spaces and underscores with hyphens.
 * -# Remove characters that are not ASCII alphanumeric or hyphens.
 * -# Collapse consecutive hyphens to a single hyphen.
 * -# Strip leading and trailing hyphens.
 *
 * \param v    Subject value (string).
 * \param args Unused.
 * \retval Value URL slug string.
 */
Value slugify(const Value& v, std::span<const Value> args);

/**
 * \brief Join an array of values with a separator string.
 *
 * \details
 * Each element is converted to a string via \c Value::to_string() before
 * joining.  \c args[0] is the separator; defaults to an empty string when
 * absent.
 *
 * \param v    Subject value (array).
 * \param args Optional separator argument.
 * \retval Value Joined string, or null if \p v is not an array.
 */
Value join(const Value& v, std::span<const Value> args);

/**
 * \brief Return the first element of an array.
 *
 * \param v    Subject value (array).
 * \param args Unused.
 * \retval Value First element, or null if the array is empty or \p v is not an array.
 */
Value first(const Value& v, std::span<const Value> args);

/**
 * \brief Return the last element of an array.
 *
 * \param v    Subject value (array).
 * \param args Unused.
 * \retval Value Last element, or null if the array is empty or \p v is not an array.
 */
Value last(const Value& v, std::span<const Value> args);

/**
 * \brief Reverse a string or array.
 *
 * \details
 * For strings the byte sequence is reversed (ASCII; multi-byte UTF-8 sequences
 * are reversed byte-by-byte within each code point, which is correct for
 * single-byte ASCII text and preserves the byte content for non-ASCII input).
 * For arrays the elements are copied into a new ValueArray in reverse order.
 *
 * \param v    Subject value (string or array).
 * \param args Unused.
 * \retval Value Reversed string or array; null for other types.
 */
Value reverse(const Value& v, std::span<const Value> args);

/**
 * \brief Strip HTML tags from a string.
 *
 * \details
 * Characters between \c < and \c > (inclusive) are removed.  The
 * implementation handles only well-formed, non-nested tags; malformed markup
 * may produce unexpected output.
 *
 * \param v    Subject value (string).
 * \param args Unused.
 * \retval Value String with HTML tags removed.
 */
Value striptags(const Value& v, std::span<const Value> args);

/**
 * \brief URL-encode a string.
 *
 * \details
 * Every byte that is not an unreserved URI character (\c A-Z, \c a-z,
 * \c 0-9, \c -, \c _, \c ., \c ~) is percent-encoded as \c %XX using
 * upper-case hexadecimal digits.
 *
 * \param v    Subject value (string).
 * \param args Unused.
 * \retval Value URL-encoded string.
 */
Value urlencode(const Value& v, std::span<const Value> args);

/**
 * \brief Estimate the reading time of an HTML or plain-text string in minutes.
 *
 * \details
 * Strips HTML tags from \p v (if any) and counts the resulting words.
 * The word count is divided by the assumed average adult reading speed to
 * produce a whole-minute estimate, with a minimum of 1 minute.
 * \c args[0] may supply a custom words-per-minute rate (integer); defaults
 * to 265 when absent. \see WORDS_PER_MINUTE
 *
 * \param v    Subject value (string — HTML or plain text).
 * \param args Optional words-per-minute rate argument (integer).
 * \retval Value Integer number of minutes (≥ 1), or null if \p v is not a string.
 */
Value reading_minutes(const Value& v, std::span<const Value> args);

} // namespace guss::render::filters
