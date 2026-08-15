#include "core/Win.h"

#include <cstdint>
#include <format>
#include <limits>

namespace peek {
namespace {

// The logger converts every line through narrow() while holding its lock, so a failure
// here cannot be reported through peek::log without re-entering it. The debugger channel
// is the only safe outlet.
void reportToDebugger(wchar_t const* what) {
    OutputDebugStringW(what);
}

}  // namespace

std::wstring widen(std::string_view utf8) {
    // A zero length is rejected with ERROR_INVALID_PARAMETER rather than treated as an
    // empty conversion, so it never reaches the API.
    if (utf8.empty()) {
        return {};
    }

    // Both APIs take the length as an int, and the cast below is what enforces that. A view
    // longer than INT_MAX wraps to some negative number, and one of the numbers it can wrap
    // to is -1 -- which these functions read as "the string is null-terminated". A bounded
    // conversion would become a scan off the end of a buffer this function does not own.
    if (utf8.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        reportToDebugger(L"xbs: refusing to widen a string longer than INT_MAX\n");
        return {};
    }

    // The explicit length (rather than -1) is what makes embedded nulls survive: the
    // conversion then neither stops at nor appends a terminator.
    int const length = static_cast<int>(utf8.size());
    int const needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), length, nullptr, 0);
    if (needed <= 0) {
        // Without MB_ERR_INVALID_CHARS malformed UTF-8 is replaced with U+FFFD instead of
        // failing, so a zero here means the arguments were rejected outright.
        reportToDebugger(L"xbs: MultiByteToWideChar rejected a UTF-8 string\n");
        return {};
    }

    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), length, result.data(), needed);
    return result;
}

std::string narrow(std::wstring_view utf16) {
    if (utf16.empty()) {
        return {};
    }

    // Same trap as widen, from the other direction.
    if (utf16.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        reportToDebugger(L"xbs: refusing to narrow a string longer than INT_MAX\n");
        return {};
    }

    int const length = static_cast<int>(utf16.size());
    // CP_UTF8 requires both default-character arguments to be null; passing either one
    // fails the call with ERROR_INVALID_PARAMETER.
    int const needed =
        WideCharToMultiByte(CP_UTF8, 0, utf16.data(), length, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        reportToDebugger(L"xbs: WideCharToMultiByte rejected a UTF-16 string\n");
        return {};
    }

    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), length, result.data(), needed, nullptr,
                        nullptr);
    return result;
}

std::wstring describeHresult(HRESULT hr) {
    wchar_t* buffer = nullptr;
    // FORMAT_MESSAGE_ALLOCATE_BUFFER redefines lpBuffer as an out-pointer, hence the cast;
    // IGNORE_INSERTS is mandatory because system messages containing %1 otherwise fail the
    // call and, with an argument array, would read from one that does not exist.
    DWORD const length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);

    std::wstring text;
    if (length != 0 && buffer != nullptr) {
        text.assign(buffer, length);
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }

    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ' ||
                             text.back() == L'.')) {
        text.pop_back();
    }

    auto const code = static_cast<std::uint32_t>(hr);
    if (text.empty()) {
        return std::format(L"0x{:08X}", code);
    }
    return std::format(L"0x{:08X} ({})", code, text);
}

}  // namespace peek
