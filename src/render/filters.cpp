/**
 * \file filters.cpp
 * \brief Implementations of all built-in template filters.
 *
 * \details
 * Each filter is a free function that accepts a subject Value and a span of
 * argument Values, performs a transformation, and returns the result as a new
 * Value.  The register_all() function binds each filter to its canonical name
 * and inserts it into the engine's registry.
 */
#include "guss/render/filters.hpp"
#include "guss/render/detail/html.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace guss::render::filters {

// ---------------------------------------------------------------------------
// count_words — file-local helper
// ---------------------------------------------------------------------------

/**
 * \brief Count whitespace-delimited words in \p sv, ignoring HTML markup.
 *
 * \details
 * Single pass over the input: characters inside \c <…> tags are skipped
 * entirely; transitions from whitespace (or start-of-input) to a non-
 * whitespace, non-tag character increment the word counter.
 *
 * \param sv Input string view — HTML or plain text.
 * \retval   Number of words found.
 */
static size_t count_words(const std::string_view sv) {
    size_t count      = 0;
    bool   inside_tag = false;
    bool   in_word    = false;
    for (const unsigned char c : sv) {
        if (c == '<') {
            inside_tag = true;
        } else if (c == '>') {
            inside_tag = false;
        } else if (!inside_tag) {
            if (std::isspace(c)) {
                in_word = false;
            } else if (!in_word) {
                in_word = true;
                ++count;
            }
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// utf8_codepoint_count — file-local helper
// ---------------------------------------------------------------------------

/**
 * \brief Count the number of UTF-8 code points in \p s.
 *
 * \details
 * Iterates the byte sequence and counts leading bytes (any byte that is not
 * a UTF-8 continuation byte, i.e. not of the form 10xxxxxx).
 *
 * \param s Input UTF-8 string view.
 * \retval size_t Number of code points.
 */
static size_t utf8_codepoint_count(std::string_view s) {
    size_t count = 0;
    for (const unsigned char c : s) {
        // Continuation bytes are in the range 0x80–0xBF.
        if ((c & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// utf8_truncate — file-local helper
// ---------------------------------------------------------------------------

/**
 * \brief Return a copy of \p s truncated to at most \p max_codepoints code
 *        points.
 *
 * \details
 * Advances through the byte sequence counting leading UTF-8 bytes.  When
 * \p max_codepoints leading bytes have been seen the byte offset at that point
 * is returned as the cut position.  The returned string does not include the
 * ellipsis; the caller is responsible for appending it.
 *
 * \param s              Input UTF-8 string view.
 * \param max_codepoints Maximum number of code points to keep.
 * \retval std::string   Truncated copy (without ellipsis).
 */
static std::string utf8_truncate(std::string_view s, size_t max_codepoints) {
    if (s.empty()) {
        return {};
    }
    size_t codepoints = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0u) != 0x80u) {
            // This byte starts a new code point.
            if (codepoints == max_codepoints) {
                // Cut here: return everything before byte i.
                return std::string(s.substr(0, i));
            }
            ++codepoints;
        }
    }
    // The string has fewer than max_codepoints code points; return in full.
    return std::string(s);
}

// ---------------------------------------------------------------------------
// date
// ---------------------------------------------------------------------------

Value date(const Value& v, std::span<const Value> args) {
    if (!v.is_string()) {
        return v;
    }

    const std::string_view sv    = v.as_string();
    const std::string      fmt   = args.empty() ? "%Y-%m-%d"
                                                : std::string(args[0].as_string());

    // Parse Ghost CMS format: "YYYY-MM-DDTHH:MM:SS.mmmZ"
    // and the simpler ISO form  "YYYY-MM-DDTHH:MM:SSZ".
    std::tm tm{};
    int     year   = 0;
    int     month  = 0;
    int     day    = 0;
    int     hour   = 0;
    int     minute = 0;
    int     second = 0;

    // Try the Ghost millisecond variant first, then the plain variant.
    const std::string s(sv);
    int matched = std::sscanf(s.c_str(),
                              "%4d-%2d-%2dT%2d:%2d:%2d",
                              &year, &month, &day,
                              &hour, &minute, &second);
    if (matched < 3) {
        // Parsing failed — return the original value unchanged.
        return v;
    }

    tm.tm_year  = year  - 1900;
    tm.tm_mon   = month - 1;
    tm.tm_mday  = day;
    tm.tm_hour  = hour;
    tm.tm_min   = minute;
    tm.tm_sec   = second;
    tm.tm_isdst = 0;

    // Normalise the broken-down UTC time to a time_t, then back to a UTC tm.
    // This accounts for DST and timezone correctly.
#if defined(_WIN32)
    time_t t = _mkgmtime(&tm);
#else
    time_t t = timegm(&tm);
#endif
    if (t == -1) { return v; }  // normalisation failed, return original
    struct std::tm* utc_tm = std::gmtime(&t);
    if (!utc_tm) { return v; }

    char buf[256];
    std::size_t written = std::strftime(buf, sizeof(buf), fmt.c_str(), utc_tm);
    if (written == 0) {
        return v;
    }
    return Value(std::string(buf, written));
}

// ---------------------------------------------------------------------------
// truncate
// ---------------------------------------------------------------------------

Value truncate(const Value& v, std::span<const Value> args) {
    if (!v.is_string()) {
        return v;
    }

    const size_t limit = args.empty()
        ? size_t{255}
        : static_cast<size_t>(args[0].as_int());

    const std::string_view sv = v.as_string();
    const size_t codepoints   = utf8_codepoint_count(sv);

    if (codepoints <= limit) {
        return v;
    }

    // Truncate and append the Unicode horizontal ellipsis (U+2026, UTF-8: E2 80 A6).
    std::string result = utf8_truncate(sv, limit);
    result += "\xe2\x80\xa6";
    return Value(std::move(result));
}

// ---------------------------------------------------------------------------
// escape
// ---------------------------------------------------------------------------

Value escape(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_string()) {
        return v;
    }
    std::string out;
    out.reserve(v.as_string().size() + 16);
    detail::html_escape_into(v.as_string(), out);
    return Value(std::move(out));
}

// ---------------------------------------------------------------------------
// safe
// ---------------------------------------------------------------------------

Value safe(const Value& v, std::span<const Value> /*args*/) {
    // Identity: the compiler emits Op::EmitRaw when it detects "| safe", so
    // the auto-escaping in Op::Emit is bypassed at the emit site.  This
    // function exists so that | safe can also be used mid-pipeline.
    return v;
}

// ---------------------------------------------------------------------------
// default_
// ---------------------------------------------------------------------------

Value default_(const Value& v, std::span<const Value> args) {
    if (v.is_truthy()) {
        return v;
    }
    if (!args.empty()) {
        return args[0];
    }
    return Value{};
}

// ---------------------------------------------------------------------------
// length
// ---------------------------------------------------------------------------

Value length(const Value& v, std::span<const Value> /*args*/) {
    if (v.is_array() || v.is_object()) {
        return Value(static_cast<int64_t>(v.size()));
    }
    if (v.is_string()) {
        return Value(static_cast<int64_t>(utf8_codepoint_count(v.as_string())));
    }
    return Value(int64_t{0});
}

// ---------------------------------------------------------------------------
// lower
// ---------------------------------------------------------------------------

Value lower(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_string()) {
        return v;
    }
    std::string s(v.as_string());
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return Value(std::move(s));
}

// ---------------------------------------------------------------------------
// upper
// ---------------------------------------------------------------------------

Value upper(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_string()) {
        return v;
    }
    std::string s(v.as_string());
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return Value(std::move(s));
}

// ---------------------------------------------------------------------------
// slugify
// ---------------------------------------------------------------------------

Value slugify(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_string()) {
        return v;
    }
    const std::string_view sv = v.as_string();

    // Step 1: convert to ASCII lowercase.
    std::string s;
    s.reserve(sv.size());
    for (const unsigned char c : sv) {
        s += static_cast<char>(std::tolower(c));
    }

    // Step 2: replace spaces and underscores with hyphens.
    for (char& c : s) {
        if (c == ' ' || c == '_') {
            c = '-';
        }
    }

    // Step 3: keep ASCII alphanumeric and hyphens; percent-encode non-ASCII bytes;
    // drop all other ASCII characters (special chars already converted to '-' for
    // spaces/underscores in step 2, remaining ASCII specials are discarded).
    std::string filtered;
    filtered.reserve(s.size() * 3);  // worst case: all chars encoded
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c == '-') {
            filtered += static_cast<char>(c);
        } else if (c > 0x7F) {
            // Percent-encode non-ASCII bytes to preserve Unicode slugs
            char enc[4];
            std::snprintf(enc, sizeof(enc), "%%%02X", static_cast<unsigned>(c));
            filtered += enc;
        }
        // ASCII non-alphanumeric chars (already converted to '-' in step 2 for
        // spaces/underscores) are dropped here.
    }

    // Step 4: collapse consecutive hyphens.
    std::string collapsed;
    collapsed.reserve(filtered.size());
    bool last_was_hyphen = false;
    for (const char c : filtered) {
        if (c == '-') {
            if (!last_was_hyphen) {
                collapsed += c;
            }
            last_was_hyphen = true;
        } else {
            collapsed += c;
            last_was_hyphen = false;
        }
    }

    // Step 5: strip leading and trailing hyphens.
    const size_t start = collapsed.find_first_not_of('-');
    if (start == std::string::npos) {
        return Value(std::string{});
    }
    const size_t end = collapsed.find_last_not_of('-');
    return Value(collapsed.substr(start, end - start + 1));
}

// ---------------------------------------------------------------------------
// join
// ---------------------------------------------------------------------------

Value join(const Value& v, std::span<const Value> args) {
    if (!v.is_array()) {
        return v;
    }
    const std::string sep = args.empty() ? "" : std::string(args[0].as_string());

    std::string result;
    const size_t n = v.size();
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) {
            result += sep;
        }
        result += v[i].to_string();
    }
    return Value(std::move(result));
}

// ---------------------------------------------------------------------------
// first
// ---------------------------------------------------------------------------

Value first(const Value& v, std::span<const Value> /*args*/) {
    if (v.is_array() && v.size() > 0) {
        return v[size_t{0}];
    }
    return Value{};
}

// ---------------------------------------------------------------------------
// last
// ---------------------------------------------------------------------------

Value last(const Value& v, std::span<const Value> /*args*/) {
    const size_t n = v.size();
    if (v.is_array() && n > 0) {
        return v[n - 1];
    }
    return Value{};
}

// ---------------------------------------------------------------------------
// reverse
// ---------------------------------------------------------------------------

Value reverse(const Value& v, std::span<const Value> /*args*/) {
    if (v.is_string()) {
        std::string s(v.as_string());
        std::reverse(s.begin(), s.end());
        return Value(std::move(s));
    }
    if (v.is_array()) {
        const size_t n = v.size();
        std::vector<Value> result;
        result.reserve(n);
        for (size_t i = n; i-- > 0; ) {
            result.push_back(v[i]);
        }
        return Value(std::move(result));
    }
    return Value{};
}

// ---------------------------------------------------------------------------
// striptags
// ---------------------------------------------------------------------------

Value striptags(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_string()) {
        return v;
    }
    const std::string_view sv = v.as_string();
    std::string result;
    result.reserve(sv.size());

    bool inside_tag = false;
    for (const char c : sv) {
        if (c == '<') {
            inside_tag = true;
        } else if (c == '>') {
            inside_tag = false;
        } else if (!inside_tag) {
            result += c;
        }
    }
    return Value(std::move(result));
}

// ---------------------------------------------------------------------------
// urlencode
// ---------------------------------------------------------------------------

Value urlencode(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_string()) {
        return v;
    }
    const std::string_view sv = v.as_string();
    std::string result;
    result.reserve(sv.size() * 3);

    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char c : sv) {
        // Unreserved URI characters per RFC 3986: A-Z a-z 0-9 - _ . ~
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += static_cast<char>(c);
        } else {
            result += '%';
            result += hex[(c >> 4) & 0x0Fu];
            result += hex[c & 0x0Fu];
        }
    }
    return Value(std::move(result));
}

// ---------------------------------------------------------------------------
// reading_minutes
// ---------------------------------------------------------------------------

Value reading_minutes(const Value& v, std::span<const Value> args) {
    if (!v.is_string()) {
        return {};
    }

    const size_t wpm = (!args.empty() && args[0].is_number() && args[0].as_uint() > 0)
        ? static_cast<size_t>(args[0].as_uint())
        : WORDS_PER_MINUTE;

    const uint64_t minutes = std::max(
        uint64_t{1},
        static_cast<uint64_t>((count_words(v.as_string()) + wpm - 1) / wpm)
    );
    return Value(minutes);
}

// ---------------------------------------------------------------------------
// replace
// ---------------------------------------------------------------------------

Value replace(const Value& v, std::span<const Value> args) {
    if (!v.is_string()) {
        return v;
    }
    if (args.size() < 2) {
        return v;
    }
    const std::string needle      = std::string(args[0].as_string());
    const std::string replacement = std::string(args[1].as_string());

    std::string result = std::string(v.as_string());
    if (needle.empty()) {
        return Value(std::move(result));
    }

    std::string out;
    out.reserve(result.size());
    size_t pos = 0;
    while (true) {
        const size_t found = result.find(needle, pos);
        if (found == std::string::npos) {
            out.append(result, pos, std::string::npos);
            break;
        }
        out.append(result, pos, found - pos);
        out += replacement;
        pos = found + needle.size();
    }
    return Value(std::move(out));
}

// ---------------------------------------------------------------------------
// trim
// ---------------------------------------------------------------------------

Value trim(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_string()) {
        return v;
    }
    const std::string_view sv = v.as_string();
    const auto is_ws = [](unsigned char c) -> bool {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r'
            || c == '\f' || c == '\v';
    };

    size_t start = 0;
    while (start < sv.size() && is_ws(static_cast<unsigned char>(sv[start]))) {
        ++start;
    }
    size_t end = sv.size();
    while (end > start && is_ws(static_cast<unsigned char>(sv[end - 1]))) {
        --end;
    }
    return Value(std::string(sv.substr(start, end - start)));
}

// ---------------------------------------------------------------------------
// capitalize
// ---------------------------------------------------------------------------

Value capitalize(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_string()) {
        return v;
    }
    const std::string_view sv = v.as_string();
    if (sv.empty()) return v;
    std::string s(sv);
    // Uppercase first ASCII character, lowercase the rest.
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    for (size_t i = 1; i < s.size(); ++i) {
        s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    }
    return Value(std::move(s));
}

// ---------------------------------------------------------------------------
// abs
// ---------------------------------------------------------------------------

Value abs(const Value& v, std::span<const Value> /*args*/) {
    if (v.is_int()) {
        const int64_t i = v.as_int();
        // INT64_MIN has no positive int64_t representation; clamp to INT64_MAX.
        if (i == std::numeric_limits<int64_t>::min()) {
            return Value(std::numeric_limits<int64_t>::max());
        }
        return Value(std::abs(i));
    }
    if (v.is_double()) {
        return Value(std::abs(v.as_double()));
    }
    return v;
}

// ---------------------------------------------------------------------------
// round
// ---------------------------------------------------------------------------

Value round(const Value& v, std::span<const Value> args) {
    const int64_t decimals = args.empty() ? int64_t{0}
                                          : args[0].as_int();
    if (v.is_int()) {
        if (decimals <= 0) {
            return v;
        }
        // Convert to double and apply rounding to requested precision.
        const double factor = std::pow(10.0, static_cast<double>(decimals));
        return Value(std::round(static_cast<double>(v.as_int()) * factor) / factor);
    }
    if (v.is_double()) {
        if (decimals <= 0) {
            return Value(std::round(v.as_double()));
        }
        const double factor = std::pow(10.0, static_cast<double>(decimals));
        return Value(std::round(v.as_double() * factor) / factor);
    }
    return v;
}

// ---------------------------------------------------------------------------
// float_
// ---------------------------------------------------------------------------

Value float_(const Value& v, std::span<const Value> /*args*/) {
    if (v.is_int()) {
        return Value(static_cast<double>(v.as_int()));
    }
    if (v.is_double()) {
        return v;
    }
    if (v.is_string()) {
        try {
            const std::string s(v.as_string());
            return Value(std::stod(s));
        } catch (const std::exception&) {
            return v;
        }
    }
    return v;
}

// ---------------------------------------------------------------------------
// int_
// ---------------------------------------------------------------------------

Value int_(const Value& v, std::span<const Value> /*args*/) {
    if (v.is_double()) {
        return Value(static_cast<int64_t>(v.as_double()));
    }
    if (v.is_int()) {
        return v;
    }
    if (v.is_string()) {
        try {
            const std::string s(v.as_string());
            return Value(static_cast<int64_t>(std::stoll(s)));
        } catch (const std::exception&) {
            return v;
        }
    }
    return v;
}

// ---------------------------------------------------------------------------
// wordcount
// ---------------------------------------------------------------------------

Value wordcount(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_string()) {
        return Value(uint64_t{0});
    }
    return Value(static_cast<uint64_t>(count_words(v.as_string())));
}

// ---------------------------------------------------------------------------
// items
// ---------------------------------------------------------------------------

Value items(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_object()) {
        return v;
    }
    const std::vector<std::string> keys = v.object_keys();
    std::vector<Value> result;
    result.reserve(keys.size());
    for (const auto& key : keys) {
        std::vector<Value> pair;
        pair.reserve(2);
        pair.push_back(Value(std::string(key)));
        pair.push_back(v[key]);
        result.push_back(Value(std::move(pair)));
    }
    return Value(std::move(result));
}

// ---------------------------------------------------------------------------
// sort
// ---------------------------------------------------------------------------

Value sort(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_array()) {
        return v;
    }
    const size_t n = v.size();
    std::vector<Value> arr;
    arr.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        arr.push_back(v[i]);
    }

    std::stable_sort(arr.begin(), arr.end(), [](const Value& a, const Value& b) {
        // Both strings: lexicographic comparison.
        if (a.is_string() && b.is_string()) {
            return a.as_string() < b.as_string();
        }
        // Both numbers: numeric comparison.
        if (a.is_number() && b.is_number()) {
            const double da = a.is_double() ? a.as_double()
                                            : static_cast<double>(a.as_int());
            const double db = b.is_double() ? b.as_double()
                                            : static_cast<double>(b.as_int());
            return da < db;
        }
        // Mixed types: fall back to string representation.
        return a.to_string() < b.to_string();
    });

    return Value(std::move(arr));
}

// ---------------------------------------------------------------------------
// dictsort
// ---------------------------------------------------------------------------

Value dictsort(const Value& v, std::span<const Value> /*args*/) {
    if (!v.is_object()) {
        return v;
    }
    // Collect and sort keys.
    std::vector<std::string> keys = v.object_keys();
    std::sort(keys.begin(), keys.end());

    // Build result array of [key, value] pairs in sorted key order.
    std::vector<Value> result;
    result.reserve(keys.size());
    for (const auto& key : keys) {
        std::vector<Value> pair;
        pair.reserve(2);
        pair.push_back(Value(std::string(key)));
        pair.push_back(v[key]);
        result.push_back(Value(std::move(pair)));
    }
    return Value(std::move(result));
}

// ---------------------------------------------------------------------------
// register_all
// ---------------------------------------------------------------------------

void register_all(std::vector<FilterFn>&                  registry,
                  std::unordered_map<std::string, size_t>& index) {
    // Register a single filter by name into the registry and index.
    auto reg = [&registry, &index](std::string name, FilterFn fn) {
        const size_t id = registry.size();
        registry.push_back(std::move(fn));
        index.emplace(std::move(name), id);
    };

    reg("date",             date);
    reg("truncate",         truncate);
    reg("escape",           escape);
    reg("safe",             safe);
    reg("default",          default_);
    reg("length",           length);
    reg("lower",            lower);
    reg("upper",            upper);
    reg("slugify",          slugify);
    reg("join",             join);
    reg("first",            first);
    reg("last",             last);
    reg("reverse",          reverse);
    reg("striptags",        striptags);
    reg("urlencode",        urlencode);
    reg("reading_minutes",  reading_minutes);
    reg("replace",          replace);
    reg("trim",             trim);
    reg("capitalize",       capitalize);
    reg("abs",              abs);
    reg("round",            round);
    reg("float",            float_);
    reg("int",              int_);
    reg("wordcount",        wordcount);
    reg("items",            items);
    reg("sort",             sort);
    reg("dictsort",         dictsort);
}

} // namespace guss::render::filters
