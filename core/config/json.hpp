#pragma once

// A small, strict JSON reader for configuration: scenarios, engines, spacecraft.
//
// JSON with line comments, read-only, no dependencies.  Written rather than
// vendored so that error messages can name the line, the column and the field a
// user got wrong -- a bad configuration file is the most likely way someone meets
// this project for the first time.  See ADR-0007.

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sf::config::json {

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

class Value {
public:
    // List and Map, not Array and Object: those name the aliases above, and GCC -Wshadow
    // flags an enumerator that reuses them.
    enum class Type { Null, Bool, Number, String, List, Map };

    Value() = default;
    explicit Value(bool b) : type_(Type::Bool), bool_(b) {}
    explicit Value(double n) : type_(Type::Number), number_(n) {}
    explicit Value(std::string s) : type_(Type::String), string_(std::move(s)) {}
    explicit Value(Array a) : type_(Type::List), array_(std::move(a)) {}
    explicit Value(Object o) : type_(Type::Map), object_(std::move(o)) {}

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool is_null() const noexcept { return type_ == Type::Null; }
    [[nodiscard]] bool is_object() const noexcept { return type_ == Type::Map; }
    [[nodiscard]] bool is_array() const noexcept { return type_ == Type::List; }

    // Typed accessors; each throws ParseError naming `context` on mismatch.
    [[nodiscard]] double as_number(const std::string& context) const;
    [[nodiscard]] const std::string& as_string(const std::string& context) const;
    [[nodiscard]] bool as_bool(const std::string& context) const;
    [[nodiscard]] const Array& as_array(const std::string& context) const;
    [[nodiscard]] const Object& as_object(const std::string& context) const;

    // Object field lookup.  `get` returns nullptr when absent; `require` throws.
    [[nodiscard]] const Value* get(const std::string& key) const;
    [[nodiscard]] const Value& require(const std::string& key, const std::string& context) const;

    [[nodiscard]] double number_or(const std::string& key, double fallback) const;
    [[nodiscard]] std::string string_or(const std::string& key, const std::string& fallback) const;
    [[nodiscard]] bool bool_or(const std::string& key, bool fallback) const;

private:
    Type type_{Type::Null};
    bool bool_{false};
    double number_{0.0};
    std::string string_;
    Array array_;
    Object object_;
};

Value parse(const std::string& text);
Value parse_file(const std::string& path);

}  // namespace sf::config::json
