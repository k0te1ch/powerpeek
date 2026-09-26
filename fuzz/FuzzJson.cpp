#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/Json.h"

// Parses arbitrary bytes and, when they are valid JSON, checks that what dump() writes
// parses back to text that dumps identically: the settings file is rewritten on every save.
extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size) {
    std::string_view const text(reinterpret_cast<char const*>(data), size);
    std::string error;
    peek::json::Value const value = peek::json::parse(text, &error);
    if (!error.empty()) {
        return 0;
    }
    std::string const once = peek::json::dump(value, 2);
    std::string reparseError;
    std::string const twice = peek::json::dump(peek::json::parse(once, &reparseError), 2);
    if (!reparseError.empty() || once != twice) {
        __debugbreak();
    }
    return 0;
}
