#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace peek::json {

// A small UTF-8 JSON DOM.
//
// The application persists one settings file and one history log; pulling in a general
// purpose JSON library for that would cost more than it saves. Objects preserve sorted
// key order, which keeps the settings file diffable.

class Value;

using Object = std::map<std::string, Value, std::less<>>;
using Array = std::vector<Value>;

class Value {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Value();
    Value(std::nullptr_t);
    Value(bool value);
    Value(double value);
    // Stored as the double the float's own shortest decimal parses to, so a setting held
    // as a float is written the way it was written down: 0.8f as 0.8 rather than as the
    // 0.800000011920929 that widening it to double really is.
    Value(float value);
    Value(int value);
    Value(std::string value);
    Value(std::string_view value);
    Value(char const* value);
    Value(Array value);
    Value(Object value);

    Value(Value const& other);
    Value(Value&& other) noexcept;
    Value& operator=(Value const& other);
    Value& operator=(Value&& other) noexcept;
    ~Value();

    Kind kind() const noexcept;
    bool isNull() const noexcept;

    // Typed reads that fall back to `fallback` when the key is absent or the stored type
    // does not match. Settings files are user-editable, so a wrong type is expected input
    // rather than a programming error.
    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    int asInt(int fallback = 0) const;
    float asFloat(float fallback = 0.0f) const;
    std::string asString(std::string_view fallback = {}) const;
    std::wstring asWide(std::wstring_view fallback = {}) const;

    Array const& asArray() const;
    Object const& asObject() const;

    // Member lookup on a non-object returns a null Value, so chains such as
    // root["events"]["low"]["volume"].asFloat(1.0f) never need intermediate checks.
    Value const& operator[](std::string_view key) const;

    bool contains(std::string_view key) const;

    // Mutating access; converts the value to an object or array first if it is not one.
    Value& set(std::string key, Value value);
    void push(Value value);

private:
    struct Storage;
    std::unique_ptr<Storage> m_storage;
};

// Returns a null Value and fills `error` when the text is malformed.
Value parse(std::string_view text, std::string* error = nullptr);

// `indent` of 0 emits a single line; the settings writer uses 2.
std::string dump(Value const& value, int indent = 2);

}  // namespace peek::json
