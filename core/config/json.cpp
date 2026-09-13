#include "core/config/json.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sf::config::json {
namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    Value parse_document() {
        skip_whitespace();
        Value v = parse_value();
        skip_whitespace();
        if (pos_ != text_.size()) {
            fail("trailing characters after the top-level value");
        }
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        std::size_t line = 1;
        std::size_t column = 1;
        for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        throw ParseError("JSON error at line " + std::to_string(line) + ", column " +
                         std::to_string(column) + ": " + what);
    }

    void skip_whitespace() {
        while (pos_ < text_.size()) {
            const char ch = text_[pos_];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                ++pos_;
            } else if (ch == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
                // Line comments are not JSON, but scenario files are written by
                // humans who need to explain their numbers.
                while (pos_ < text_.size() && text_[pos_] != '\n') {
                    ++pos_;
                }
            } else {
                break;
            }
        }
    }

    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    void expect(char ch) {
        if (peek() != ch) {
            fail(std::string{"expected '"} + ch + "'");
        }
        ++pos_;
    }

    bool literal(const char* word) {
        const std::size_t n = std::char_traits<char>::length(word);
        if (text_.compare(pos_, n, word) == 0) {
            pos_ += n;
            return true;
        }
        return false;
    }

    Value parse_value() {
        switch (peek()) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': return Value{parse_string()};
            case 't': if (literal("true")) return Value{true}; fail("invalid literal");
            case 'f': if (literal("false")) return Value{false}; fail("invalid literal");
            case 'n': if (literal("null")) return Value{}; fail("invalid literal");
            default: return parse_number();
        }
    }

    Value parse_object() {
        expect('{');
        Object obj;
        skip_whitespace();
        if (peek() == '}') {
            ++pos_;
            return Value{std::move(obj)};
        }
        while (true) {
            skip_whitespace();
            std::string key = parse_string();
            skip_whitespace();
            expect(':');
            skip_whitespace();
            obj.emplace(std::move(key), parse_value());
            skip_whitespace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect('}');
            break;
        }
        return Value{std::move(obj)};
    }

    Value parse_array() {
        expect('[');
        Array arr;
        skip_whitespace();
        if (peek() == ']') {
            ++pos_;
            return Value{std::move(arr)};
        }
        while (true) {
            skip_whitespace();
            arr.push_back(parse_value());
            skip_whitespace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect(']');
            break;
        }
        return Value{std::move(arr)};
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) {
                fail("unterminated string");
            }
            const char ch = text_[pos_++];
            if (ch == '"') {
                break;
            }
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (pos_ >= text_.size()) {
                fail("unterminated escape");
            }
            const char esc = text_[pos_++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: fail("unsupported escape sequence");
            }
        }
        return out;
    }

    Value parse_number() {
        const std::size_t start = pos_;
        if (peek() == '-' || peek() == '+') {
            ++pos_;
        }
        while (pos_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.' ||
                text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '+' || text_[pos_] == '-')) {
            ++pos_;
        }
        if (start == pos_) {
            fail("expected a value");
        }
        const std::string token = text_.substr(start, pos_ - start);
        try {
            std::size_t consumed = 0;
            const double value = std::stod(token, &consumed);
            if (consumed != token.size()) {
                fail("malformed number '" + token + "'");
            }
            return Value{value};
        } catch (const ParseError&) {
            throw;
        } catch (const std::exception&) {
            fail("malformed number '" + token + "'");
        }
    }

    const std::string& text_;
    std::size_t pos_{0};
};

}  // namespace

double Value::as_number(const std::string& context) const {
    if (type_ != Type::Number) {
        throw ParseError(context + ": expected a number");
    }
    return number_;
}

const std::string& Value::as_string(const std::string& context) const {
    if (type_ != Type::String) {
        throw ParseError(context + ": expected a string");
    }
    return string_;
}

bool Value::as_bool(const std::string& context) const {
    if (type_ != Type::Bool) {
        throw ParseError(context + ": expected true or false");
    }
    return bool_;
}

const Array& Value::as_array(const std::string& context) const {
    if (type_ != Type::Array) {
        throw ParseError(context + ": expected an array");
    }
    return array_;
}

const Object& Value::as_object(const std::string& context) const {
    if (type_ != Type::Object) {
        throw ParseError(context + ": expected an object");
    }
    return object_;
}

const Value* Value::get(const std::string& key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    const auto it = object_.find(key);
    return it == object_.end() ? nullptr : &it->second;
}

const Value& Value::require(const std::string& key, const std::string& context) const {
    const Value* v = get(key);
    if (v == nullptr) {
        throw ParseError(context + ": missing required field \"" + key + "\"");
    }
    return *v;
}

double Value::number_or(const std::string& key, double fallback) const {
    const Value* v = get(key);
    return v != nullptr ? v->as_number(key) : fallback;
}

std::string Value::string_or(const std::string& key, const std::string& fallback) const {
    const Value* v = get(key);
    return v != nullptr ? v->as_string(key) : fallback;
}

bool Value::bool_or(const std::string& key, bool fallback) const {
    const Value* v = get(key);
    return v != nullptr ? v->as_bool(key) : fallback;
}

Value parse(const std::string& text) {
    Parser parser{text};
    return parser.parse_document();
}

Value parse_file(const std::string& path) {
    std::ifstream in{path};
    if (!in) {
        throw ParseError("cannot open " + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    return parse(text);
}

}  // namespace sf::config::json
