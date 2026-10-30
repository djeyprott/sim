#include "histsim/json.hpp"

#include <cmath>

namespace histsim::json {

namespace {

[[noreturn]] void type_error(const Value& v, const char* wanted) {
    throw std::runtime_error("content: " + (v.path().empty() ? std::string("value") : v.path()) + " should be " +
                             wanted);
}

const Value& null_value() {
    static const Value v;
    return v;
}

}  // namespace

bool Value::as_bool() const {
    if (kind_ != Kind::Bool) type_error(*this, "true or false");
    return bool_;
}

double Value::as_number() const {
    if (kind_ != Kind::Number) type_error(*this, "a number");
    return number_;
}

std::int64_t Value::as_int() const {
    const double v = as_number();
    if (v != std::floor(v) || std::fabs(v) > 9007199254740992.0) type_error(*this, "a whole number");
    return static_cast<std::int64_t>(v);
}

const std::string& Value::as_string() const {
    if (kind_ != Kind::String) type_error(*this, "a string");
    return string_;
}

const std::vector<Value>& Value::as_array() const {
    if (kind_ != Kind::Array) type_error(*this, "an array");
    return array_;
}

const std::vector<std::pair<std::string, Value>>& Value::members() const {
    if (kind_ != Kind::Object) type_error(*this, "an object");
    return object_;
}

const Value& Value::get(std::string_view key) const {
    for (const auto& [k, v] : members()) {
        if (k == key) return v;
    }
    return null_value();
}

bool Value::has(std::string_view key) const {
    for (const auto& [k, v] : members()) {
        if (k == key) return true;
    }
    return false;
}

const Value& Value::at(std::string_view key) const {
    for (const auto& [k, v] : members()) {
        if (k == key) return v;
    }
    throw std::runtime_error("content: " + (path_.empty() ? std::string("object") : path_) + " is missing \"" +
                             std::string(key) + "\"");
}

double Value::number_or(std::string_view key, double fallback) const {
    const Value& v = get(key);
    return v.is_null() ? fallback : v.as_number();
}

std::string Value::string_or(std::string_view key, std::string fallback) const {
    const Value& v = get(key);
    return v.is_null() ? fallback : v.as_string();
}

class Parser {
public:
    Parser(std::string_view text, std::string_view source) : s_(text), source_(source) {}

    Value document() {
        Value v = value("");
        ws();
        if (i_ != s_.size()) fail("unexpected text after the document");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        std::size_t line = 1, col = 1;
        for (std::size_t k = 0; k < i_ && k < s_.size(); ++k) {
            if (s_[k] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        throw std::runtime_error(std::string(source_) + ":" + std::to_string(line) + ":" + std::to_string(col) +
                                 ": " + what);
    }

    void ws() {
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i_;
            } else if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '/') {
                // Line comments are allowed in content files (not standard JSON).
                while (i_ < s_.size() && s_[i_] != '\n') ++i_;
            } else {
                break;
            }
        }
    }

    char peek() {
        ws();
        if (i_ >= s_.size()) fail("unexpected end of file");
        return s_[i_];
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++i_;
    }

    bool literal(std::string_view word) {
        if (s_.substr(i_, word.size()) == word) {
            i_ += word.size();
            return true;
        }
        return false;
    }

    Value value(const std::string& path) {
        Value v;
        v.path_ = path;
        const char c = peek();
        if (c == '{') {
            v.kind_ = Value::Kind::Object;
            ++i_;
            if (peek() == '}') {
                ++i_;
                return v;
            }
            for (;;) {
                if (peek() != '"') fail("expected a quoted key");
                std::string key = string_body();
                for (const auto& [k, existing] : v.object_) {
                    if (k == key) fail("duplicate key \"" + key + "\"");
                }
                expect(':');
                Value member = value(path.empty() ? key : path + "." + key);
                v.object_.emplace_back(std::move(key), std::move(member));
                if (peek() == ',') {
                    ++i_;
                    continue;
                }
                expect('}');
                return v;
            }
        }
        if (c == '[') {
            v.kind_ = Value::Kind::Array;
            ++i_;
            if (peek() == ']') {
                ++i_;
                return v;
            }
            for (;;) {
                v.array_.push_back(value(path + "[" + std::to_string(v.array_.size()) + "]"));
                if (peek() == ',') {
                    ++i_;
                    continue;
                }
                expect(']');
                return v;
            }
        }
        if (c == '"') {
            v.kind_ = Value::Kind::String;
            v.string_ = string_body();
            return v;
        }
        if (literal("true")) {
            v.kind_ = Value::Kind::Bool;
            v.bool_ = true;
            return v;
        }
        if (literal("false")) {
            v.kind_ = Value::Kind::Bool;
            return v;
        }
        if (literal("null")) {
            return v;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            v.kind_ = Value::Kind::Number;
            v.number_ = number();
            return v;
        }
        fail("expected a value");
    }

    std::string string_body() {
        ++i_;  // opening quote
        std::string out;
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c == '\\') {
                if (i_ >= s_.size()) fail("unfinished escape");
                const char e = s_[i_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    default: fail("unsupported escape (use UTF-8 text directly)");
                }
            } else {
                if (c == '\n') fail("newline inside a string");
                out += c;
            }
        }
        if (i_ >= s_.size()) fail("unterminated string");
        ++i_;  // closing quote
        return out;
    }

    // Exact decimal parsing: digits into an integer mantissa, then one
    // multiplication or division by an exactly representable power of ten.
    double number() {
        bool negative = false;
        if (s_[i_] == '-') {
            negative = true;
            ++i_;
        }
        std::uint64_t mantissa = 0;
        int digits = 0;
        int exponent = 0;
        auto take_digits = [&](bool fractional) {
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') {
                const int d = s_[i_++] - '0';
                if (digits == 0 && d == 0) {  // leading zeros carry no precision
                    if (fractional) --exponent;
                    continue;
                }
                if (++digits > 15) fail("too many significant digits (15 at most)");
                mantissa = mantissa * 10 + static_cast<std::uint64_t>(d);
                if (fractional) --exponent;
            }
        };
        const std::size_t start = i_;
        take_digits(false);
        if (i_ == start) fail("expected digits");
        if (i_ < s_.size() && s_[i_] == '.') {
            ++i_;
            const std::size_t frac_start = i_;
            take_digits(true);
            if (i_ == frac_start) fail("expected digits after the decimal point");
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            ++i_;
            int sign = 1;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) {
                sign = s_[i_] == '-' ? -1 : 1;
                ++i_;
            }
            int e = 0;
            const std::size_t exp_start = i_;
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') {
                e = e * 10 + (s_[i_++] - '0');
                if (e > 400) fail("exponent out of range");
            }
            if (i_ == exp_start) fail("expected exponent digits");
            exponent += sign * e;
        }
        static constexpr double kPow10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                            1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                            1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
        double v = static_cast<double>(mantissa);  // exact: at most 15 digits < 2^53
        if (mantissa != 0) {
            if (exponent > 22 || exponent < -22) fail("number out of the exactly representable range");
            v = exponent >= 0 ? v * kPow10[exponent] : v / kPow10[-exponent];
        }
        return negative ? -v : v;
    }

    std::string_view s_;
    std::string_view source_;
    std::size_t i_ = 0;
};

Value parse(std::string_view text, std::string_view source_name) {
    Parser p(text, source_name);
    return p.document();
}

}  // namespace histsim::json
