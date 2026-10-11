#include "Json.h"

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace yinamp::media::native::json {
namespace {

void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

int Hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + c - 'a';
    if (c >= 'A' && c <= 'F')
        return 10 + c - 'A';
    throw std::runtime_error("Invalid JSON unicode escape");
}

class Parser final {
  public:
    explicit Parser(std::string_view input) : input_(input) {}

    Value ParseValue() {
        if (depth_ >= 128)
            throw std::runtime_error("JSON nesting limit exceeded");
        ++depth_;
        struct DepthScope {
            unsigned& depth;
            ~DepthScope() {
                --depth;
            }
        } scope{depth_};
        Skip();
        if (End())
            throw std::runtime_error("Unexpected end of JSON");
        switch (Peek()) {
        case '{':
            return ParseObject();
        case '[':
            return ParseArray();
        case '"':
            return Value{ParseString()};
        case 't':
            ConsumeLiteral("true");
            return Value{true};
        case 'f':
            ConsumeLiteral("false");
            return Value{false};
        case 'n':
            ConsumeLiteral("null");
            return Value{nullptr};
        default:
            return Value{ParseNumber()};
        }
    }

    void RequireEnd() {
        Skip();
        if (!End())
            throw std::runtime_error("Trailing JSON data");
    }

  private:
    std::string_view input_;
    std::size_t pos_{};
    unsigned depth_{};

    bool End() const {
        return pos_ >= input_.size();
    }
    char Peek() const {
        return input_[pos_];
    }
    char Take() {
        if (End())
            throw std::runtime_error("Unexpected end of JSON");
        return input_[pos_++];
    }
    void Skip() {
        while (!End() && (Peek() == ' ' || Peek() == '\t' || Peek() == '\r' || Peek() == '\n'))
            ++pos_;
    }
    void Expect(char c) {
        if (Take() != c)
            throw std::runtime_error("Unexpected JSON token");
    }
    void ConsumeLiteral(std::string_view literal) {
        if (input_.substr(pos_, literal.size()) != literal)
            throw std::runtime_error("Invalid JSON literal");
        pos_ += literal.size();
    }

    std::string ParseString() {
        Expect('"');
        std::string out;
        while (!End()) {
            const char c = Take();
            if (c == '"')
                return out;
            if (static_cast<unsigned char>(c) < 0x20)
                throw std::runtime_error("Control character in JSON string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            const char esc = Take();
            switch (esc) {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '/':
                out.push_back('/');
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                std::uint32_t cp{};
                for (int i = 0; i < 4; ++i)
                    cp = (cp << 4) | static_cast<std::uint32_t>(Hex(Take()));
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (pos_ + 6 <= input_.size() && input_[pos_] == '\\' && input_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        std::uint32_t low{};
                        for (int i = 0; i < 4; ++i)
                            low = (low << 4) | static_cast<std::uint32_t>(Hex(Take()));
                        if (low >= 0xDC00 && low <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        else
                            throw std::runtime_error("Invalid JSON surrogate pair");
                    } else
                        throw std::runtime_error("Missing JSON low surrogate");
                } else if (cp >= 0xDC00 && cp <= 0xDFFF)
                    throw std::runtime_error("Unexpected JSON low surrogate");
                AppendUtf8(out, cp);
                break;
            }
            default:
                throw std::runtime_error("Invalid JSON escape");
            }
        }
        throw std::runtime_error("Unterminated JSON string");
    }

    double ParseNumber() {
        const std::size_t begin = pos_;
        if (!End() && Peek() == '-')
            ++pos_;
        while (!End() && Peek() >= '0' && Peek() <= '9')
            ++pos_;
        if (!End() && Peek() == '.') {
            ++pos_;
            while (!End() && Peek() >= '0' && Peek() <= '9')
                ++pos_;
        }
        if (!End() && (Peek() == 'e' || Peek() == 'E')) {
            ++pos_;
            if (!End() && (Peek() == '+' || Peek() == '-'))
                ++pos_;
            while (!End() && Peek() >= '0' && Peek() <= '9')
                ++pos_;
        }
        const auto token = input_.substr(begin, pos_ - begin);
        double value{};
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || !std::isfinite(value))
            throw std::runtime_error("Invalid JSON number");
        return value;
    }

    Value ParseArray() {
        Expect('[');
        Array result;
        Skip();
        if (!End() && Peek() == ']') {
            ++pos_;
            return Value{std::move(result)};
        }
        for (;;) {
            result.push_back(ParseValue());
            Skip();
            const char c = Take();
            if (c == ']')
                break;
            if (c != ',')
                throw std::runtime_error("Invalid JSON array");
        }
        return Value{std::move(result)};
    }

    Value ParseObject() {
        Expect('{');
        Object result;
        Skip();
        if (!End() && Peek() == '}') {
            ++pos_;
            return Value{std::move(result)};
        }
        for (;;) {
            Skip();
            const auto key = ParseString();
            Skip();
            Expect(':');
            result.insert_or_assign(key, ParseValue());
            Skip();
            const char c = Take();
            if (c == '}')
                break;
            if (c != ',')
                throw std::runtime_error("Invalid JSON object");
        }
        return Value{std::move(result)};
    }
};

}

const Object* Value::AsObject() const {
    return std::get_if<Object>(&data);
}
const Array* Value::AsArray() const {
    return std::get_if<Array>(&data);
}
const std::string* Value::AsString() const {
    return std::get_if<std::string>(&data);
}
std::optional<double> Value::AsNumber() const {
    if (const auto* value = std::get_if<double>(&data))
        return *value;
    return {};
}
const Value* Value::Get(std::string_view key) const {
    const auto* object = AsObject();
    if (!object)
        return nullptr;
    const auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}

Value Parse(std::string_view text) {
    Parser parser(text);
    auto value = parser.ParseValue();
    parser.RequireEnd();
    return value;
}

std::string_view ObjectAt(std::string_view text, std::size_t start) {
    if (start >= text.size() || text[start] != '{') return {};
    unsigned depth{};
    bool string{}, escape{};
    for (auto i = start; i < text.size(); ++i) {
        const auto c = text[i];
        if (string) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') string = false;
        } else if (c == '"') string = true;
        else if (c == '{' || c == '[') {
            if (++depth > 128) throw std::runtime_error("JSON nesting limit exceeded.");
        } else if (c == '}' || c == ']') {
            if (!depth) return {};
            if (--depth == 0) return text.substr(start, i - start + 1);
        }
    }
    return {};
}

std::string Escape(std::string_view value) {
    std::string out;
    for (const unsigned char c : value) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(hex[(c >> 4) & 0xF]);
                out.push_back(hex[c & 0xF]);
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

}
