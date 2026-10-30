// A small JSON reader for content packs (data/*.json).
//
// Content feeds simulation state, so numbers must parse to the same double on
// every platform. Library string-to-double functions are not guaranteed to
// agree, so numbers are parsed here: a decimal mantissa of at most 15 digits
// times an exact power of ten (|exponent| <= 22), one correctly rounded IEEE
// operation (Clinger's fast path). Numbers outside that are rejected, which
// is no limit for hand-written data.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace histsim::json {

class Value {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Value() = default;

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool is_null() const noexcept { return kind_ == Kind::Null; }

    // Typed access; throws std::runtime_error naming the path on a mismatch.
    [[nodiscard]] bool as_bool() const;
    [[nodiscard]] double as_number() const;
    [[nodiscard]] std::int64_t as_int() const;  // a number with no fractional part
    [[nodiscard]] const std::string& as_string() const;
    [[nodiscard]] const std::vector<Value>& as_array() const;

    // Object lookup. `at` throws if the key is missing; `get` returns null.
    [[nodiscard]] const Value& at(std::string_view key) const;
    [[nodiscard]] const Value& get(std::string_view key) const;
    [[nodiscard]] bool has(std::string_view key) const;
    [[nodiscard]] const std::vector<std::pair<std::string, Value>>& members() const;

    // Convenience: object member with a default when absent.
    [[nodiscard]] double number_or(std::string_view key, double fallback) const;
    [[nodiscard]] std::string string_or(std::string_view key, std::string fallback) const;

    // Where this value sits in the document, e.g. "species[3].niche", for errors.
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    friend class Parser;
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::vector<std::pair<std::string, Value>> object_;  // in document order
    std::string path_;
};

// Parse a whole document. Throws std::runtime_error with line and column.
Value parse(std::string_view text, std::string_view source_name = "json");

}  // namespace histsim::json
