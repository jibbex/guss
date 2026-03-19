/**
 * \file filters.hpp
 * \brief Built-in filter function declarations for the Guss template engine.
 *
 * \details
 * Each filter is a free function that accepts a subject Value and a span of
 * argument Values and returns the transformed Value.  All 27 built-in filters
 * are declared here so they can be called directly from unit tests without
 * going through the Runtime.
 *
 * \c register_all() is the single entry-point called from the Runtime
 * constructor.  It populates the runtime's filter registry and index map with
 * all built-in filters.
 */
#pragma once
#include "guss/core/value.hpp"

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

/// Callable type matching Runtime::FilterFn, redeclared here to avoid a
/// circular include between runtime.hpp and filters.hpp.
using FilterFn = std::function<core::Value(const core::Value&, std::span<const core::Value>)>;

/**
 * \brief Register all built-in filters.
 *
 * \details
 * Appends one entry per filter to \p registry and inserts the corresponding
 * name-to-index mapping into \p index.  Called exactly once from the Runtime
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
core::Value date(const core::Value& v, std::span<const core::Value> args);

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
core::Value truncate(const core::Value& v, std::span<const core::Value> args);

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
core::Value escape(const core::Value& v, std::span<const core::Value> args);

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
core::Value safe(const core::Value& v, std::span<const core::Value> args);

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
core::Value default_(const core::Value& v, std::span<const core::Value> args);

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
core::Value length(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Convert a string to ASCII lowercase.
 *
 * \param v    Subject value (string).
 * \param args Unused.
 * \retval Value Lowercased string.
 */
core::Value lower(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Convert a string to ASCII uppercase.
 *
 * \param v    Subject value (string).
 * \param args Unused.
 * \retval Value Uppercased string.
 */
core::Value upper(const core::Value& v, std::span<const core::Value> args);

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
core::Value slugify(const core::Value& v, std::span<const core::Value> args);

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
core::Value join(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Return the first element of an array.
 *
 * \param v    Subject value (array).
 * \param args Unused.
 * \retval Value First element, or null if the array is empty or \p v is not an array.
 */
core::Value first(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Return the last element of an array.
 *
 * \param v    Subject value (array).
 * \param args Unused.
 * \retval Value Last element, or null if the array is empty or \p v is not an array.
 */
core::Value last(const core::Value& v, std::span<const core::Value> args);

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
core::Value reverse(const core::Value& v, std::span<const core::Value> args);

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
core::Value striptags(const core::Value& v, std::span<const core::Value> args);

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
core::Value urlencode(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Estimate the reading time of an HTML or plain-text string in minutes.
 *
 * \details
 * Strips HTML tags from \p v (if any) and counts the resulting words.
 * The word count is divided by the assumed average adult reading speed to
 * produce a whole-minute estimate, with a minimum of 1 minute.
 * \c args[0] may supply a custom words-per-minute rate (integer); defaults
 * to 256 when absent. \see WORDS_PER_MINUTE
 *
 * \param v    Subject value (string — HTML or plain text).
 * \param args Optional words-per-minute rate argument (integer).
 * \retval Value Integer number of minutes (≥ 1), or null if \p v is not a string.
 */
core::Value reading_minutes(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Replace all occurrences of a substring with another string.
 *
 * \details
 * \c args[0] is the needle string; \c args[1] is the replacement string.
 * If \p v is not a string the value is returned unchanged.
 *
 * \param v    Subject value (string).
 * \param args args[0]=needle, args[1]=replacement.
 * \retval Value String with substitutions applied, or the original value unchanged.
 */
core::Value replace(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Strip leading and trailing ASCII whitespace from a string.
 *
 * \details
 * Only ASCII whitespace bytes (space, tab, newline, carriage return,
 * form feed, vertical tab) are removed.  Multi-byte UTF-8 sequences are
 * left untouched.  If \p v is not a string the value is returned unchanged.
 *
 * \param v    Subject value (string).
 * \param args Unused.
 * \retval Value Trimmed string, or the original value unchanged.
 */
core::Value trim(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Uppercase the first character and lowercase the rest of a string.
 *
 * \details
 * Only ASCII characters (bytes < 0x80) are transformed; multi-byte UTF-8
 * sequences are passed through unchanged.  If \p v is not a string the
 * value is returned unchanged.
 *
 * \param v    Subject value (string).
 * \param args Unused.
 * \retval Value Capitalized string, or the original value unchanged.
 */
core::Value capitalize(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Return the absolute value of a number.
 *
 * \details
 * Works on \c int64_t and \c double operands.  For all other types the value
 * is returned unchanged.
 *
 * \param v    Subject value (int64_t or double).
 * \param args Unused.
 * \retval Value Absolute value with the same numeric type, or the original value.
 */
core::Value abs(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Round a number to N decimal places.
 *
 * \details
 * \c args[0] is the number of decimal places (default 0).  For integer input
 * with zero decimal places the value is returned as-is; otherwise the result
 * is always a \c double.  For all other types the value is returned unchanged.
 *
 * \param v    Subject value (int64_t or double).
 * \param args Optional decimal-places argument (integer).
 * \retval Value Rounded double, or the original value unchanged.
 */
core::Value round(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Convert a value to a double (floating-point).
 *
 * \details
 * - \c int64_t input is cast to \c double.
 * - \c double input is returned unchanged.
 * - \c string input is parsed with \c std::stod; on failure the original
 *   value is returned unchanged.
 * - All other types are returned unchanged.
 *
 * \param v    Subject value.
 * \param args Unused.
 * \retval Value A double Value, or the original value on failure.
 */
core::Value float_(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Convert a value to an integer (int64_t), truncating towards zero.
 *
 * \details
 * - \c double input is cast to \c int64_t (truncation).
 * - \c int64_t input is returned unchanged.
 * - \c string input is parsed with \c std::stoll; on failure the original
 *   value is returned unchanged.
 * - All other types are returned unchanged.
 *
 * \param v    Subject value.
 * \param args Unused.
 * \retval Value An int64_t Value, or the original value on failure.
 */
core::Value int_(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Count the number of whitespace-separated words in a string.
 *
 * \details
 * No HTML stripping is performed; tags are counted as words if they contain
 * non-whitespace characters.  If \p v is not a string, zero is returned.
 *
 * \param v    Subject value (string).
 * \param args Unused.
 * \retval Value Integer word count.
 */
core::Value wordcount(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Convert an object (ValueMap) to an array of [key, value] pairs.
 *
 * \details
 * Each element of the returned array is a two-element array: the first
 * element is the key as a \c string Value, the second is the associated
 * value.  Iteration order is unspecified (unordered map order).
 * If \p v is not an object the value is returned unchanged.
 *
 * \param v    Subject value (object/ValueMap).
 * \param args Unused.
 * \retval Value Array of [key, value] pairs, or the original value unchanged.
 */
core::Value items(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Sort an array in ascending order.
 *
 * \details
 * Strings are compared lexicographically; numbers are compared numerically.
 * For mixed-type arrays elements are compared by their \c to_string()
 * representation.  A new array is returned; the original is not modified.
 * If \p v is not an array the value is returned unchanged.
 *
 * \param v    Subject value (array).
 * \param args Unused.
 * \retval Value Sorted array, or the original value unchanged.
 */
core::Value sort(const core::Value& v, std::span<const core::Value> args);

/**
 * \brief Convert an object to an array of [key, value] pairs sorted by key.
 *
 * \details
 * Equivalent to \c items but the resulting pairs are sorted lexicographically
 * by key.  If \p v is not an object the value is returned unchanged.
 *
 * \param v    Subject value (object/ValueMap).
 * \param args Unused.
 * \retval Value Sorted array of [key, value] pairs, or the original value unchanged.
 */
core::Value dictsort(const core::Value& v, std::span<const core::Value> args);

} // namespace guss::render::filters
