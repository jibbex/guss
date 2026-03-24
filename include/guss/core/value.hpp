/**
 * \file value.hpp
 * \brief Runtime value type for the template engine.
 *
 * \details
 * Value is a discriminated union of native C++ types. It holds every data kind
 * the template engine needs: scalars (string, bool, int, double), null, and
 * first-class map and array types built from domain structs without any JSON
 * round-trip.
 *
 * Map and Array are heap-allocated through shared_ptr so that copying a Value
 * that wraps a large object or collection is O(1).
 *
 * \note No simdjson types appear anywhere in this header or its implementation.
 *       simdjson is used exclusively in the adapter layer to parse CMS API
 *       responses. See docs/layer-boundaries.md.
 */
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace guss::core {

// ---------------------------------------------------------------------------
// Forward declarations — allow recursive Map/Array definitions.
// ---------------------------------------------------------------------------
struct ValueMap;
struct ValueArray;

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

/**
 * \class Value
 * \brief Discriminated-union runtime value for template rendering.
 *
 * \details
 * Supports six storage kinds:
 *  - null
 *  - string_view             (zero-copy view into a string owned elsewhere)
 *  - std::string             (owned string, e.g. from filter output)
 *  - bool
 *  - int64_t / uint64_t / double
 *  - shared_ptr<ValueMap>    (key→Value object built from domain structs)
 *  - shared_ptr<ValueArray>  (indexed Value array built from domain structs)
 *
 * Map and Array are behind shared_ptr so copies are O(1). The underlying
 * data is never mutated after construction.
 *
 * \note The destructor is user-declared and defined in value.cpp, where
 *       ValueMap and ValueArray are complete types.
 */
class Value final {
public:
    // -- Constructors --------------------------------------------------------

    /** \brief Construct a string-view value (zero-copy). */
    explicit Value(std::string_view sv);
    /** \brief Construct an owned-string value. */
    explicit Value(std::string owned);
    /** \brief Construct a boolean value. */
    explicit Value(bool b);
    /** \brief Construct a signed-integer value. */
    explicit Value(int64_t i);
    /** \brief Construct an unsigned-integer value. */
    explicit Value(uint64_t i);
    /** \brief Construct a double value. */
    explicit Value(double d);

    /**
     * \brief Construct an object value from a key→Value map.
     * \param map Source map; moved into heap storage.
     */
    explicit Value(std::unordered_map<std::string, Value> map);

    /**
     * \brief Construct an array value from a Value vector.
     * \param array Source vector; moved into heap storage.
     */
    explicit Value(std::vector<Value> array);

    /** \brief Construct a null value. */
    Value();

    /**
     * \brief Destructor — defined in value.cpp where ValueMap/ValueArray
     *        are complete, so shared_ptr can invoke the correct deleter.
     */
    ~Value();

    // -- Type queries --------------------------------------------------------

    [[nodiscard]] bool is_null()   const;
    [[nodiscard]] bool is_string() const;
    [[nodiscard]] bool is_number() const;
    [[nodiscard]] bool is_int()    const;   ///< True if the stored type is int64_t or uint64_t.
    [[nodiscard]] bool is_double() const;   ///< True if the stored type is double.
    [[nodiscard]] bool is_bool()   const;
    [[nodiscard]] bool is_array()  const;
    [[nodiscard]] bool is_object() const;

    // -- Accessors -----------------------------------------------------------

    [[nodiscard]] std::string_view as_string() const;
    [[nodiscard]] int64_t          as_int()    const;
    [[nodiscard]] uint64_t         as_uint()   const;
    [[nodiscard]] double           as_double() const;
    [[nodiscard]] bool             as_bool()   const;

    /** \brief Truthiness: false for null, empty string, zero, false, empty collection. */
    [[nodiscard]] bool is_truthy() const;

    /** \brief String representation (for {{ }} output and debugging). */
    [[nodiscard]] std::string to_string() const;

    /** \brief Look up a member by key (objects only; returns null if missing). */
    Value operator[](std::string_view key) const;

    /** \brief Look up a member with a default fallback. */
    [[nodiscard]] Value get(std::string_view key, Value default_val = {}) const;

    /** \brief True if this is an object that contains \p key. */
    [[nodiscard]] bool has(std::string_view key) const;

    /** \brief Number of elements (arrays and objects; 0 for other types). */
    [[nodiscard]] size_t size() const;

    /** \brief Look up an element by index (arrays only; returns null if out of range). */
    Value operator[](size_t index) const;

    /**
     * \brief Return mutable shared_ptr to the underlying ValueMap (objects only).
     * \retval shared_ptr<ValueMap>  Pointer to the map if this is an object.
     * \retval nullptr               If this Value is not an object.
     */
    [[nodiscard]] std::shared_ptr<ValueMap> as_object();

    /**
     * \brief Return the keys of an object value in unspecified order.
     *
     * \details
     * Provides a way to iterate over object key-value pairs without exposing
     * the incomplete \c ValueMap type to callers that only include value.hpp.
     * Returns an empty vector if this Value is not an object.
     *
     * \retval std::vector<std::string> Keys of the object, order unspecified.
     */
    [[nodiscard]] std::vector<std::string> object_keys() const;

    /**
     * \brief Insert or overwrite a key in the underlying object map.
     * \param key   Field name.
     * \param val   Value to store.
     * \note No-op if this Value is not an object.
     */
    void set(std::string key, Value val);

private:
    struct NullTag {};

    std::variant<
        NullTag,
        std::string_view,
        std::string,
        bool,
        int64_t,
        uint64_t,
        double,
        std::shared_ptr<ValueMap>,    ///< Object built from domain structs.
        std::shared_ptr<ValueArray>   ///< Array  built from domain structs.
    > data_;
};

} // namespace guss::core
