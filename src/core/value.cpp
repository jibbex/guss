#include "guss/core/value.hpp"
#include "guss/util/overloaded.hpp"

// ---------------------------------------------------------------------------
// ValueMap and ValueArray — defined here so that Value's destructor (which
// is defined below) can complete the shared_ptr<ValueMap/ValueArray> types.
// ---------------------------------------------------------------------------

namespace guss::core {

struct ValueMap   : std::unordered_map<std::string, Value> {
    using unordered_map::unordered_map;
    explicit ValueMap(std::unordered_map<std::string, Value>&& m)
        : unordered_map(std::move(m)) {}
};

struct ValueArray : std::vector<Value> {
    using vector::vector;
    explicit ValueArray(std::vector<Value>&& v)
        : vector(std::move(v)) {}
};

} // namespace guss::core

// ---------------------------------------------------------------------------
// Value implementation
// ---------------------------------------------------------------------------

namespace guss::core {

// Destructor defined here where ValueMap/ValueArray are complete.
Value::~Value() = default;

// -- Constructors ------------------------------------------------------------

Value::Value(std::string_view sv)           : data_(sv)            {}
Value::Value(std::string owned)             : data_(std::move(owned)) {}
Value::Value(bool b)                        : data_(b)             {}
Value::Value(int64_t i)                     : data_(i)             {}
Value::Value(uint64_t i)                    : data_(i)             {}
Value::Value(double d)                      : data_(d)             {}
Value::Value()                              : data_(NullTag{})     {}

Value::Value(std::unordered_map<std::string, Value> map)
    : data_(std::make_shared<ValueMap>(std::move(map))) {}

Value::Value(std::vector<Value> array)
    : data_(std::make_shared<ValueArray>(std::move(array))) {}

// -- Type queries ------------------------------------------------------------

bool Value::is_null() const {
    return std::holds_alternative<NullTag>(data_);
}

bool Value::is_string() const {
    return std::holds_alternative<std::string_view>(data_)
        || std::holds_alternative<std::string>(data_);
}

bool Value::is_number() const {
    return std::holds_alternative<int64_t>(data_)
        || std::holds_alternative<uint64_t>(data_)
        || std::holds_alternative<double>(data_);
}

bool Value::is_int() const {
    return std::holds_alternative<int64_t>(data_)
        || std::holds_alternative<uint64_t>(data_);
}

bool Value::is_double() const {
    return std::holds_alternative<double>(data_);
}

bool Value::is_bool() const {
    return std::holds_alternative<bool>(data_);
}

bool Value::is_array() const {
    return std::holds_alternative<std::shared_ptr<ValueArray>>(data_);
}

bool Value::is_object() const {
    return std::holds_alternative<std::shared_ptr<ValueMap>>(data_);
}

// -- Accessors ---------------------------------------------------------------

std::string_view Value::as_string() const {
    if (std::holds_alternative<std::string_view>(data_)) return std::get<std::string_view>(data_);
    if (std::holds_alternative<std::string>(data_))      return std::get<std::string>(data_);
    return {};
}

int64_t Value::as_int() const {
    if (std::holds_alternative<int64_t>(data_))  return std::get<int64_t>(data_);
    if (std::holds_alternative<uint64_t>(data_)) return static_cast<int64_t>(std::get<uint64_t>(data_));
    if (std::holds_alternative<double>(data_))   return static_cast<int64_t>(std::get<double>(data_));
    return 0;
}

uint64_t Value::as_uint() const {
    if (std::holds_alternative<uint64_t>(data_)) return std::get<uint64_t>(data_);
    if (std::holds_alternative<int64_t>(data_))  return static_cast<uint64_t>(std::get<int64_t>(data_));
    if (std::holds_alternative<double>(data_))   return static_cast<uint64_t>(std::get<double>(data_));
    return 0;
}

double Value::as_double() const {
    if (std::holds_alternative<double>(data_))   return std::get<double>(data_);
    if (std::holds_alternative<int64_t>(data_))  return static_cast<double>(std::get<int64_t>(data_));
    if (std::holds_alternative<uint64_t>(data_)) return static_cast<double>(std::get<uint64_t>(data_));
    return 0.0;
}

bool Value::as_bool() const {
    if (std::holds_alternative<bool>(data_)) return std::get<bool>(data_);
    return false;
}

bool Value::is_truthy() const {
    if (is_null())   return false;
    if (is_bool())   return as_bool();
    if (is_number()) return as_double() != 0.0;
    if (is_string()) return !as_string().empty();
    if (is_array() || is_object()) return size() > 0;
    return true;
}

std::string Value::to_string() const {
    return std::visit(overloaded {
        [](NullTag)               -> std::string { return "null"; },
        [](std::string_view sv)   -> std::string { return std::string(sv); },
        [](const std::string& s)  -> std::string { return s; },
        [](bool b)                -> std::string { return b ? "true" : "false"; },
        [](int64_t i)             -> std::string { return std::to_string(i); },
        [](uint64_t i)            -> std::string { return std::to_string(i); },
        [](double d)              -> std::string { return std::to_string(d); },
        [](const std::shared_ptr<ValueMap>&)   -> std::string { return "{object}"; },
        [](const std::shared_ptr<ValueArray>&) -> std::string { return "[array]"; },
    }, data_);
}

// -- Object/array access -----------------------------------------------------

Value Value::operator[](std::string_view key) const {
    if (std::holds_alternative<std::shared_ptr<ValueMap>>(data_)) {
        const auto& map = *std::get<std::shared_ptr<ValueMap>>(data_);
        auto it = map.find(std::string(key));
        if (it != map.end()) return it->second;
    }
    return {};
}

Value Value::get(std::string_view key, Value default_val) const {
    Value found = (*this)[key];
    return found.is_null() ? std::move(default_val) : found;
}

bool Value::has(std::string_view key) const {
    if (std::holds_alternative<std::shared_ptr<ValueMap>>(data_)) {
        return std::get<std::shared_ptr<ValueMap>>(data_)->count(std::string(key)) > 0;
    }
    return false;
}

size_t Value::size() const {
    if (std::holds_alternative<std::shared_ptr<ValueMap>>(data_))
        return std::get<std::shared_ptr<ValueMap>>(data_)->size();
    if (std::holds_alternative<std::shared_ptr<ValueArray>>(data_))
        return std::get<std::shared_ptr<ValueArray>>(data_)->size();
    return 0;
}

Value Value::operator[](size_t index) const {
    if (std::holds_alternative<std::shared_ptr<ValueArray>>(data_)) {
        const auto& arr = *std::get<std::shared_ptr<ValueArray>>(data_);
        if (index < arr.size()) return arr[index];
    }
    return {};
}

std::shared_ptr<ValueMap> Value::as_object() {
    if (auto* p = std::get_if<std::shared_ptr<ValueMap>>(&data_)) return *p;
    return nullptr;
}

std::vector<std::string> Value::object_keys() const {
    if (!std::holds_alternative<std::shared_ptr<ValueMap>>(data_)) {
        return {};
    }
    const auto& map = *std::get<std::shared_ptr<ValueMap>>(data_);
    std::vector<std::string> keys;
    keys.reserve(map.size());
    for (const auto& [k, v] : map) {
        keys.push_back(k);
    }
    return keys;
}

void Value::set(std::string key, Value val) {
    if (auto* p = std::get_if<std::shared_ptr<ValueMap>>(&data_)) {
        (**p)[std::move(key)] = std::move(val);
    }
}

} // namespace guss::core
