#include "core/Json.h"

#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <format>
#include <system_error>
#include <utility>
#include <variant>

#include "core/Win.h"

namespace peek::json {
namespace {

// A settings file is a few hundred bytes; anything deeper than this is either a mistake
// or an attempt to recurse the parser off the stack.
constexpr int kMaxDepth = 64;

constexpr char32_t kReplacementCharacter = 0xFFFD;

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

void appendUtf8(std::string& out, char32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : m_text(text) {}

    bool run(Value& out) {
        skipByteOrderMark();
        skipWhitespace();
        if (!parseValue(out, 0)) {
            return false;
        }
        skipWhitespace();
        if (m_pos != m_text.size()) {
            return fail("unexpected trailing data");
        }
        return true;
    }

    std::string const& error() const { return m_error; }

private:
    bool fail(std::string_view what) {
        if (m_error.empty()) {
            m_error = std::format("{} at byte {}", what, m_pos);
        }
        return false;
    }

    bool atEnd() const { return m_pos >= m_text.size(); }

    char peek() const { return atEnd() ? '\0' : m_text[m_pos]; }

    bool consume(char c) {
        if (peek() != c) {
            return false;
        }
        ++m_pos;
        return true;
    }

    void skipByteOrderMark() {
        // Notepad and PowerShell both write a BOM; JSON itself has no place for one.
        if (m_text.substr(m_pos).starts_with("\xEF\xBB\xBF")) {
            m_pos += 3;
        }
    }

    void skipWhitespace() {
        while (!atEnd()) {
            char const c = m_text[m_pos];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                return;
            }
            ++m_pos;
        }
    }

    bool parseValue(Value& out, int depth) {
        if (depth > kMaxDepth) {
            return fail("nesting is too deep");
        }
        switch (peek()) {
            case '{':
                return parseObject(out, depth);
            case '[':
                return parseArray(out, depth);
            case '"': {
                std::string text;
                if (!parseString(text)) {
                    return false;
                }
                out = std::move(text);
                return true;
            }
            case 't':
                return parseLiteral("true", Value(true), out);
            case 'f':
                return parseLiteral("false", Value(false), out);
            case 'n':
                return parseLiteral("null", Value(nullptr), out);
            case '\0':
                return fail("unexpected end of input");
            default:
                return parseNumber(out);
        }
    }

    bool parseLiteral(std::string_view word, Value value, Value& out) {
        if (!m_text.substr(m_pos).starts_with(word)) {
            return fail("invalid literal");
        }
        m_pos += word.size();
        out = std::move(value);
        return true;
    }

    bool parseObject(Value& out, int depth) {
        ++m_pos;
        Object object;

        skipWhitespace();
        if (!consume('}')) {
            for (;;) {
                skipWhitespace();
                if (peek() != '"') {
                    return fail("expected a quoted key");
                }
                std::string key;
                if (!parseString(key)) {
                    return false;
                }

                skipWhitespace();
                if (!consume(':')) {
                    return fail("expected ':'");
                }

                skipWhitespace();
                Value value;
                if (!parseValue(value, depth + 1)) {
                    return false;
                }
                // Duplicate keys are legal JSON and the last one wins, which is also what a
                // user who pasted a block twice expects.
                object.insert_or_assign(std::move(key), std::move(value));

                skipWhitespace();
                if (consume(',')) {
                    continue;
                }
                if (consume('}')) {
                    break;
                }
                return fail("expected ',' or '}'");
            }
        }

        out = std::move(object);
        return true;
    }

    bool parseArray(Value& out, int depth) {
        ++m_pos;
        Array array;

        skipWhitespace();
        if (!consume(']')) {
            for (;;) {
                skipWhitespace();
                Value value;
                if (!parseValue(value, depth + 1)) {
                    return false;
                }
                array.push_back(std::move(value));

                skipWhitespace();
                if (consume(',')) {
                    continue;
                }
                if (consume(']')) {
                    break;
                }
                return fail("expected ',' or ']'");
            }
        }

        out = std::move(array);
        return true;
    }

    bool parseString(std::string& out) {
        ++m_pos;
        for (;;) {
            if (atEnd()) {
                return fail("unterminated string");
            }

            auto const c = static_cast<unsigned char>(m_text[m_pos]);
            if (c == '"') {
                ++m_pos;
                return true;
            }
            if (c == '\\') {
                ++m_pos;
                if (!parseEscape(out)) {
                    return false;
                }
                continue;
            }
            if (c < 0x20) {
                return fail("a raw control character in a string");
            }

            // Bytes above ASCII are copied through unvalidated: the file is UTF-8 by
            // definition and widen() substitutes U+FFFD for anything malformed.
            out.push_back(static_cast<char>(c));
            ++m_pos;
        }
    }

    bool parseEscape(std::string& out) {
        if (atEnd()) {
            return fail("unterminated escape");
        }

        char const c = m_text[m_pos++];
        switch (c) {
            case '"':
            case '\\':
            case '/':
                out.push_back(c);
                return true;
            case 'b':
                out.push_back('\b');
                return true;
            case 'f':
                out.push_back('\f');
                return true;
            case 'n':
                out.push_back('\n');
                return true;
            case 'r':
                out.push_back('\r');
                return true;
            case 't':
                out.push_back('\t');
                return true;
            case 'u':
                return parseUnicodeEscape(out);
            default:
                return fail("unknown escape");
        }
    }

    bool parseUnicodeEscape(std::string& out) {
        char32_t code = 0;
        if (!readHexQuad(code)) {
            return false;
        }

        if (code >= 0xD800 && code <= 0xDBFF) {
            std::size_t const afterHigh = m_pos;
            char32_t low = 0;
            if (m_text.substr(m_pos).starts_with("\\u")) {
                m_pos += 2;
                if (!readHexQuad(low)) {
                    return false;
                }
            }

            if (low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            } else {
                // A lone surrogate has no UTF-8 encoding. Substituting one character beats
                // rejecting an entire settings file over one bad escape, and whatever
                // followed is re-read as a character in its own right.
                m_pos = afterHigh;
                code = kReplacementCharacter;
            }
        } else if (code >= 0xDC00 && code <= 0xDFFF) {
            code = kReplacementCharacter;
        }

        appendUtf8(out, code);
        return true;
    }

    bool readHexQuad(char32_t& out) {
        if (m_text.size() - m_pos < 4) {
            return fail("truncated \\u escape");
        }

        char32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            char const c = m_text[m_pos + static_cast<std::size_t>(i)];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<char32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<char32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<char32_t>(c - 'A' + 10);
            } else {
                return fail("a \\u escape needs four hex digits");
            }
        }

        m_pos += 4;
        out = value;
        return true;
    }

    bool parseNumber(Value& out) {
        std::size_t const start = m_pos;

        consume('-');
        if (consume('0')) {
            if (isDigit(peek())) {
                return fail("a leading zero");
            }
        } else {
            if (!isDigit(peek())) {
                return fail("expected a value");
            }
            while (isDigit(peek())) {
                ++m_pos;
            }
        }

        if (consume('.')) {
            if (!isDigit(peek())) {
                return fail("expected digits after '.'");
            }
            while (isDigit(peek())) {
                ++m_pos;
            }
        }

        if (peek() == 'e' || peek() == 'E') {
            ++m_pos;
            if (peek() == '+' || peek() == '-') {
                ++m_pos;
            }
            if (!isDigit(peek())) {
                return fail("expected digits in the exponent");
            }
            while (isDigit(peek())) {
                ++m_pos;
            }
        }

        std::string_view const span = m_text.substr(start, m_pos - start);
        double number = 0.0;
        auto const result = std::from_chars(span.data(), span.data() + span.size(), number);
        if (result.ec != std::errc{} || result.ptr != span.data() + span.size()) {
            // The grammar above already matched, so the only way to land here is a literal
            // that does not fit a double, such as 1e400.
            m_pos = start;
            return fail("a number outside the range of a double");
        }

        out = number;
        return true;
    }

    std::string_view m_text;
    std::size_t m_pos = 0;
    std::string m_error;
};

void writeIndent(std::string& out, int indent, int depth) {
    if (indent <= 0) {
        return;
    }
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth), ' ');
}

void writeString(std::string& out, std::string_view text) {
    out.push_back('"');
    for (char const raw : text) {
        auto const c = static_cast<unsigned char>(raw);
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
                    out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                } else {
                    // Everything above ASCII stays as its UTF-8 bytes; escaping it would
                    // make a Russian sound name unreadable in the settings file.
                    out.push_back(raw);
                }
                break;
        }
    }
    out.push_back('"');
}

void writeNumber(std::string& out, double number) {
    if (!std::isfinite(number)) {
        // JSON has no infinity and no NaN, and null is the only value every reader accepts.
        out += "null";
        return;
    }

    std::array<char, 32> buffer{};
    auto const result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    if (result.ec != std::errc{}) {
        out += "null";
        return;
    }
    // The shortest round-tripping form is what to_chars produces by default, so 3.0 comes
    // back as "3" and 0.8 stays "0.8" instead of drifting to 0.80000000000000004.
    out.append(buffer.data(), result.ptr);
}

void writeValue(std::string& out, Value const& value, int indent, int depth) {
    switch (value.kind()) {
        case Value::Kind::Null:
            out += "null";
            return;
        case Value::Kind::Bool:
            out += value.asBool() ? "true" : "false";
            return;
        case Value::Kind::Number:
            writeNumber(out, value.asNumber());
            return;
        case Value::Kind::String:
            writeString(out, value.asString());
            return;
        case Value::Kind::Array: {
            Array const& array = value.asArray();
            if (array.empty()) {
                out += "[]";
                return;
            }
            out.push_back('[');
            bool first = true;
            for (Value const& item : array) {
                if (!first) {
                    out.push_back(',');
                }
                first = false;
                writeIndent(out, indent, depth + 1);
                writeValue(out, item, indent, depth + 1);
            }
            writeIndent(out, indent, depth);
            out.push_back(']');
            return;
        }
        case Value::Kind::Object: {
            Object const& object = value.asObject();
            if (object.empty()) {
                out += "{}";
                return;
            }
            out.push_back('{');
            bool first = true;
            for (auto const& [key, item] : object) {
                if (!first) {
                    out.push_back(',');
                }
                first = false;
                writeIndent(out, indent, depth + 1);
                writeString(out, key);
                out.push_back(':');
                if (indent > 0) {
                    out.push_back(' ');
                }
                writeValue(out, item, indent, depth + 1);
            }
            writeIndent(out, indent, depth);
            out.push_back('}');
            return;
        }
    }
}

// The double a float should be stored as.
//
// Widening 0.8f gives 0.800000011920929..., and writeNumber is then right to print all of
// it: that is genuinely the shortest form of that double. Round-tripping through the
// shortest decimal the *float* needs gives the double 0.8 instead, which prints as "0.8"
// and reads back as the same float -- a double is far finer-grained than the gap between
// two neighbouring floats, so the second rounding cannot land on a different one.
double asStoredDouble(float value) {
    if (!std::isfinite(value)) {
        // No decimal form to round-trip through; writeNumber turns it into null regardless.
        return static_cast<double>(value);
    }

    std::array<char, 32> buffer{};
    auto const printed = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (printed.ec != std::errc{}) {
        return static_cast<double>(value);
    }

    double widened = 0.0;
    auto const parsed = std::from_chars(buffer.data(), printed.ptr, widened);
    if (parsed.ec != std::errc{}) {
        return static_cast<double>(value);
    }
    return widened;
}

}  // namespace

struct Value::Storage {
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data;
};

Value::Value() : m_storage(std::make_unique<Storage>()) {}

Value::Value(std::nullptr_t) : Value() {}

Value::Value(bool value) : m_storage(std::make_unique<Storage>(Storage{value})) {}

Value::Value(double value) : m_storage(std::make_unique<Storage>(Storage{value})) {}

Value::Value(float value) : Value(asStoredDouble(value)) {}

Value::Value(int value) : Value(static_cast<double>(value)) {}

Value::Value(std::string value) : m_storage(std::make_unique<Storage>(Storage{std::move(value)})) {}

Value::Value(std::string_view value) : Value(std::string(value)) {}

// Without this overload a string literal would bind to Value(bool), which is the classic
// way to store `true` where "text" was meant.
Value::Value(char const* value) : Value(std::string(value == nullptr ? "" : value)) {}

Value::Value(Array value) : m_storage(std::make_unique<Storage>(Storage{std::move(value)})) {}

Value::Value(Object value) : m_storage(std::make_unique<Storage>(Storage{std::move(value)})) {}

Value::Value(Value const& other)
    : m_storage(other.m_storage ? std::make_unique<Storage>(*other.m_storage)
                                : std::make_unique<Storage>()) {}

Value::Value(Value&& other) noexcept = default;

Value& Value::operator=(Value const& other) {
    if (this != &other) {
        m_storage = other.m_storage ? std::make_unique<Storage>(*other.m_storage)
                                    : std::make_unique<Storage>();
    }
    return *this;
}

Value& Value::operator=(Value&& other) noexcept = default;

Value::~Value() = default;

Value::Kind Value::kind() const noexcept {
    static_assert(std::variant_size_v<decltype(Storage::data)> ==
                      static_cast<std::size_t>(Kind::Object) + 1,
                  "the alternatives of the variant are indexed by Kind");

    // A moved-from Value owns no storage; every accessor below reaches the variant only
    // after this has confirmed a matching kind, so none of them needs its own check.
    if (!m_storage) {
        return Kind::Null;
    }
    return static_cast<Kind>(m_storage->data.index());
}

bool Value::isNull() const noexcept {
    return kind() == Kind::Null;
}

bool Value::asBool(bool fallback) const {
    return kind() == Kind::Bool ? std::get<bool>(m_storage->data) : fallback;
}

double Value::asNumber(double fallback) const {
    return kind() == Kind::Number ? std::get<double>(m_storage->data) : fallback;
}

int Value::asInt(int fallback) const {
    if (kind() != Kind::Number) {
        return fallback;
    }

    double const number = std::get<double>(m_storage->data);
    if (!(number >= static_cast<double>(INT_MIN) && number <= static_cast<double>(INT_MAX))) {
        return fallback;
    }
    // JSON has one numeric type, so a hand-edited 30.0 has to read back as 30.
    return static_cast<int>(std::llround(number));
}

float Value::asFloat(float fallback) const {
    return static_cast<float>(asNumber(static_cast<double>(fallback)));
}

std::string Value::asString(std::string_view fallback) const {
    return kind() == Kind::String ? std::get<std::string>(m_storage->data)
                                  : std::string(fallback);
}

std::wstring Value::asWide(std::wstring_view fallback) const {
    return kind() == Kind::String ? widen(std::get<std::string>(m_storage->data))
                                  : std::wstring(fallback);
}

Array const& Value::asArray() const {
    static Array const kEmpty;
    return kind() == Kind::Array ? std::get<Array>(m_storage->data) : kEmpty;
}

Object const& Value::asObject() const {
    static Object const kEmpty;
    return kind() == Kind::Object ? std::get<Object>(m_storage->data) : kEmpty;
}

Value const& Value::operator[](std::string_view key) const {
    static Value const kNull;
    if (kind() != Kind::Object) {
        return kNull;
    }

    Object const& object = std::get<Object>(m_storage->data);
    auto const it = object.find(key);
    return it == object.end() ? kNull : it->second;
}

bool Value::contains(std::string_view key) const {
    return kind() == Kind::Object && std::get<Object>(m_storage->data).contains(key);
}

Value& Value::set(std::string key, Value value) {
    if (!m_storage) {
        m_storage = std::make_unique<Storage>();
    }
    if (m_storage->data.index() != static_cast<std::size_t>(Kind::Object)) {
        m_storage->data.emplace<Object>();
    }

    Object& object = std::get<Object>(m_storage->data);
    return object.insert_or_assign(std::move(key), std::move(value)).first->second;
}

void Value::push(Value value) {
    if (!m_storage) {
        m_storage = std::make_unique<Storage>();
    }
    if (m_storage->data.index() != static_cast<std::size_t>(Kind::Array)) {
        m_storage->data.emplace<Array>();
    }

    std::get<Array>(m_storage->data).push_back(std::move(value));
}

Value parse(std::string_view text, std::string* error) {
    Parser parser(text);
    Value value;
    if (!parser.run(value)) {
        if (error != nullptr) {
            *error = parser.error();
        }
        return Value();
    }

    if (error != nullptr) {
        error->clear();
    }
    return value;
}

std::string dump(Value const& value, int indent) {
    std::string out;
    writeValue(out, value, indent, 0);
    return out;
}

}  // namespace peek::json
