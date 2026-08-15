// Unit tests for core/Win.h: the two UTF-8/UTF-16 conversions and the HRESULT formatter.
//
// These three sit underneath everything else. Every log line reaches the disk through
// narrow(), every string read out of settings.json comes back through widen(), and every
// failure the application reports is spelled out by describeHresult(). A defect in any of
// them is invisible while the application runs and only surfaces afterwards, as a file that
// no longer parses or an error code that matches nothing in any documentation.
//
// Worth knowing before reading a failure here: doctest prints a std::wstring operand by
// putting it through narrow(), so a broken narrow() also makes its own diffs untrustworthy.
// The assertions still fail; only the text reporting them is suspect.

#include "TestSupport.h"

#include "core/Win.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace {

using peek::describeHresult;
using peek::narrow;
using peek::widen;

// "Privet" in Cyrillic, written as its UTF-8 bytes and as its UTF-16 code units rather than
// typed in directly. The relationship between the two encodings is the thing under test, so
// spelling both out keeps the expectations independent of how this file happens to be read
// and of which execution character set the test target is compiled with.
constexpr std::string_view kCyrillicUtf8 = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
constexpr std::wstring_view kCyrillicUtf16 = L"\x041F\x0440\x0438\x0432\x0435\x0442";

// U+1F50B, the one character here that lives above the BMP: four bytes of UTF-8 against two
// UTF-16 code units, because wchar_t is 16 bits on Windows.
constexpr std::string_view kBatteryUtf8 = "\xF0\x9F\x94\x8B";
constexpr std::wstring_view kBatteryUtf16 = L"\U0001F50B";

constexpr wchar_t kReplacement = L'\xFFFD';

// describeHresult has exactly two shapes: the ten-character code on its own when the system
// holds no message for the id, or that code followed by the message in brackets. Which one
// comes back depends on the language pack installed on the machine running the suite, so no
// test may assert the message text -- only that the frame around it is intact.
//
// Every case using this helper is therefore satisfied by an implementation that never looks
// a message up at all. The access-denied case at the bottom of the file is the one that is
// not, and it is what keeps the FormatMessage half of the function honest.
bool hasDescribedShape(std::wstring const& described) {
    if (described.size() == 10u) {
        return true;
    }
    return described.size() >= 14u && described[10] == L' ' && described[11] == L'(' &&
           described.back() == L')';
}

// Exactly the set the strip loop in describeHresult pops, kept in one place so a test can
// say "anything but these" without spelling the list out again.
bool isStrippedTrailer(wchar_t c) {
    return c == L'\r' || c == L'\n' || c == L' ' || c == L'.';
}

}  // namespace

TEST_CASE("win: widen and narrow return an empty string for empty input") {
    // Neither empty input reaches the API: a zero length is rejected with
    // ERROR_INVALID_PARAMETER rather than treated as an empty conversion. Without the early
    // return the result would still come back empty, but it would arrive down the rejection
    // path, and every empty log field and empty settings string would fire the debugger
    // message that path reserves for arguments Windows genuinely refused.
    CHECK(widen(std::string_view{}).empty());
    CHECK(widen("").empty());
    CHECK(narrow(std::wstring_view{}).empty());
    CHECK(narrow(L"").empty());
}

TEST_CASE("win: widen converts ascii unchanged") {
    CHECK(widen("hello") == L"hello");
    CHECK(widen("hello").size() == 5u);
    // The build version is logged this way, so it is the very first line of every log file.
    CHECK(widen("PowerPeek 1.2.3") == L"PowerPeek 1.2.3");
}

TEST_CASE("win: narrow converts ascii unchanged") {
    // Every finished log line is narrowed before its bytes are written, so a fault on the
    // ASCII path corrupts the whole file rather than one exotic field in it.
    CHECK(narrow(L"hello") == "hello");
    CHECK(narrow(L"hello").size() == 5u);
}

TEST_CASE("win: widen decodes two-byte cyrillic sequences") {
    auto const result = widen(kCyrillicUtf8);
    CHECK(result == kCyrillicUtf16);
    // Twelve bytes in, six code units out. Sizing the result from the input length instead
    // of from what MultiByteToWideChar reports would leave six trailing nulls on every
    // Cyrillic string, which is how a Cyrillic sound-file path or a Cyrillic filesystem
    // error message ends up in the UI and the log with rubbish tacked on the end.
    CHECK(kCyrillicUtf8.size() == 12u);
    CHECK(result.size() == 6u);
}

TEST_CASE("win: narrow encodes cyrillic back to two-byte sequences") {
    // The controller id is stored in the history file through narrow(). A wrong byte count
    // writes a file the JSON parser then rejects, silently losing the battery history.
    auto const result = narrow(kCyrillicUtf16);
    CHECK(result == kCyrillicUtf8);
    CHECK(result.size() == 12u);
}

TEST_CASE("win: widen turns a four-byte utf-8 sequence into a surrogate pair") {
    auto const result = widen(kBatteryUtf8);
    // "One character" and "one wchar_t" part company above the BMP, so an astral character
    // legitimately costs two code units. Any rewrite that allocated one wide unit per input
    // character would cut this down to a lone high surrogate.
    REQUIRE(result.size() == 2u);
    CHECK(result[0] == static_cast<wchar_t>(0xD83D));
    CHECK(result[1] == static_cast<wchar_t>(0xDD0B));
    CHECK(result == kBatteryUtf16);
}

TEST_CASE("win: narrow turns a surrogate pair into a four-byte utf-8 sequence") {
    // Documents that the input is two code units and not one character, which is why the
    // code-unit count is the right argument to hand WideCharToMultiByte: a character count
    // would pass half a surrogate pair and produce a replacement character.
    CHECK(kBatteryUtf16.size() == 2u);

    auto const result = narrow(kBatteryUtf16);
    CHECK(result == kBatteryUtf8);
    CHECK(result.size() == 4u);
}

TEST_CASE("win: widen and narrow round-trip every representative alphabet") {
    // The settings file narrows a path on save and widens it back on load. If the pair is
    // not an exact inverse, a chosen sound file survives one restart and is mangled by the
    // next.
    SUBCASE("starting from utf-8") {
        CHECK(narrow(widen("plain ascii")) == "plain ascii");
        CHECK(narrow(widen(kCyrillicUtf8)) == kCyrillicUtf8);
        CHECK(narrow(widen(kBatteryUtf8)) == kBatteryUtf8);

        std::string const mixed = std::string("plain ascii, ") + std::string(kCyrillicUtf8) +
                                  ", " + std::string(kBatteryUtf8) + ".";
        CHECK(narrow(widen(mixed)) == mixed);
    }

    SUBCASE("starting from utf-16") {
        CHECK(widen(narrow(L"plain ascii")) == L"plain ascii");
        CHECK(widen(narrow(kCyrillicUtf16)) == kCyrillicUtf16);
        CHECK(widen(narrow(kBatteryUtf16)) == kBatteryUtf16);
    }
}

TEST_CASE("win: widen keeps an embedded nul instead of stopping at it") {
    // The classic regression is someone simplifying the call to -1 or .c_str(). Every
    // ordinary string still looks right afterwards, so it ships, and from then on any value
    // carrying an interior null is quietly cut off at the first one.
    std::string_view const src("a\0b", 3);
    auto const result = widen(src);
    REQUIRE(result.size() == 3u);
    CHECK(result == std::wstring(L"a\0b", 3));
    CHECK(result[1] == L'\0');
}

TEST_CASE("win: narrow keeps an embedded nul instead of stopping at it") {
    // The mirror of the widen case, and the more expensive one to get wrong: narrow() is
    // the last thing a log line passes through, so a truncation here loses the tail of the
    // whole record rather than one field of it.
    std::wstring_view const src(L"a\0b", 3);
    auto const result = narrow(src);
    CHECK(result.size() == 3u);
    CHECK(result == std::string("a\0b", 3));
}

TEST_CASE("win: neither conversion appends a terminating nul") {
    // Passing -1 rather than an explicit length would make the sizing call count the
    // terminator as well, and every returned string would carry a trailing null. Nothing
    // would look wrong on screen; every comparison against a literal would simply stop
    // matching, and every log line would gain an invisible byte.
    CHECK(widen("ab").size() == 2u);
    CHECK(widen("ab").find(L'\0') == std::wstring::npos);
    CHECK(narrow(L"ab").size() == 2u);
    CHECK(narrow(L"ab").find('\0') == std::string::npos);
}

TEST_CASE("win: widen preserves a utf-8 byte order mark as u+feff") {
    // Two adjacent literals rather than one, because an MSVC \x escape swallows every hex
    // digit that follows it: "\xEF\xBB\xBFab" would parse \xBFab as a single over-long
    // escape.
    auto const result = widen("\xEF\xBB\xBF"
                              "ab");
    // This is a division of labour, not an oversight: the JSON parser strips the BOM itself
    // because widen does not, Notepad and PowerShell both writing one. Were widen ever to
    // start stripping it, that step would quietly become dead code and a U+FEFF appearing
    // mid-string would be eaten along with it.
    REQUIRE(result.size() == 3u);
    CHECK(result[0] == L'\xFEFF');
    CHECK(result.substr(1) == L"ab");
}

TEST_CASE("win: widen substitutes u+fffd for malformed utf-8 instead of failing") {
    // MB_ERR_INVALID_CHARS is deliberately absent, so one mangled byte costs one glyph
    // rather than blanking the entire string: a settings.json that an editor damaged must
    // still load. How many U+FFFD Windows emits per bad sequence is its own business and has
    // differed between releases, so nothing here counts them or pins down an exact result.
    SUBCASE("a bare invalid byte") {
        auto const result = widen("\xFF");
        CHECK_FALSE(result.empty());
        CHECK(result.find(kReplacement) != std::wstring::npos);
    }

    SUBCASE("an invalid byte between valid text") {
        auto const result = widen("ab\xFF"
                                  "cd");
        CHECK(result.starts_with(L"ab"));
        CHECK(result.ends_with(L"cd"));
        CHECK(result.find(kReplacement) != std::wstring::npos);
    }
}

TEST_CASE("win: widen honours the view length and never reads past it") {
    // The parameter is a string_view, so a caller may legitimately hand over a window into a
    // larger buffer with no terminator at that point. A -1 or .c_str() regression would run
    // past the end, reading adjacent memory straight into a log line or faulting outright.
    SUBCASE("a valid prefix") {
        CHECK(widen(std::string_view("abcdef", 3)) == L"abc");
    }

    SUBCASE("a sequence cut in half") {
        // Two bytes of a three-byte sequence, so the decoder is mid-character exactly at the
        // boundary -- the point at which an over-read is most tempting.
        std::string const backing = "\xE4\xB8"
                                    "SENTINEL";
        auto const result = widen(std::string_view(backing.data(), 2));
        CHECK(result.find(L'S') == std::wstring::npos);
        CHECK(result.find(L"SENTINEL") == std::wstring::npos);
    }
}

TEST_CASE("win: narrow honours the view length and never reads past it") {
    // The same hazard in the opposite direction, and this is the one on the path to disk.
    std::wstring const backing = L"abSENTINEL";
    auto const result = narrow(std::wstring_view(backing.data(), 2));
    CHECK(result == "ab");
    CHECK(result.find("SENTINEL") == std::string::npos);
}

TEST_CASE("win: widen and narrow handle a very long string") {
    // Both functions ask the API for a size and allocate exactly that. The standard way
    // these two helpers acquire a stack overrun is someone replacing that round trip with a
    // fixed MAX_PATH or 4096-byte buffer, which passes every short-string test in the file.
    SUBCASE("ascii") {
        std::string const big(100000u, 'a');
        auto const wide = widen(big);
        CHECK(wide.size() == 100000u);
        CHECK(wide.find_first_not_of(L'a') == std::wstring::npos);
        CHECK(narrow(wide) == big);
    }

    SUBCASE("multi-byte") {
        std::string big;
        big.reserve(100000u);
        for (int i = 0; i < 50000; ++i) {
            big += "\xD0\x9F";
        }
        CHECK(big.size() == 100000u);

        // Half as many code units as bytes, which is the part that proves the allocation
        // follows the count the API reported and not the length of the input.
        auto const wide = widen(big);
        CHECK(wide.size() == 50000u);
        CHECK(narrow(wide) == big);
    }
}

TEST_CASE("win: widen and narrow round-trip the whole basic multilingual plane") {
    // One systematic pass catches a class of encoding faults -- an off-by-one at the
    // two/three-byte boundary at U+0800, a byte that got sign-extended -- that any number of
    // hand-picked strings would walk straight past. D800..DFFF is left out because a lone
    // surrogate is not a code point, and everything from FDD0 up because those are the
    // Unicode non-characters.
    std::wstring plane;
    for (unsigned int cp = 0x0001u; cp <= 0xD7FFu; ++cp) {
        plane.push_back(static_cast<wchar_t>(cp));
    }
    for (unsigned int cp = 0xE000u; cp <= 0xFDCFu; ++cp) {
        plane.push_back(static_cast<wchar_t>(cp));
    }

    std::string const bytes = narrow(plane);
    std::wstring const back = widen(bytes);
    CHECK(back.size() == plane.size());
    CHECK(bytes.size() > plane.size());

    // Located rather than compared outright: a failed == would print two operands of sixty
    // thousand characters each, whereas the index of the first difference names the code
    // point that broke.
    std::size_t firstDifference = 0;
    while (firstDifference < back.size() && firstDifference < plane.size() &&
           back[firstDifference] == plane[firstDifference]) {
        ++firstDifference;
    }
    CHECK(firstDifference == plane.size());
}

TEST_CASE("win: narrow substitutes for an unpaired surrogate rather than returning nothing") {
    // Built with the constructor rather than written as a literal: C++ forbids a
    // universal-character-name that names a surrogate code point, so the obvious wide
    // literal for U+D800 is not something that compiles.
    std::wstring const lone(1, static_cast<wchar_t>(0xD800));

    // WC_ERR_INVALID_CHARS is absent, so Windows substitutes rather than failing the call.
    // What matters is that something comes back: an empty result would mean the arguments
    // were rejected outright, and a caller cannot tell that apart from a legitimately empty
    // input. The JSON parser already replaces a lone surrogate before it can reach here, so
    // this pins down what would happen if that guard were ever removed. The exact
    // substituted bytes are a version-sensitive detail of the platform and are not asserted.
    CHECK_FALSE(narrow(lone).empty());
}

TEST_CASE("win: describeHresult formats the code as eight zero-padded uppercase hex digits") {
    // There are over fifty call sites logging a code this way, and the only thing anyone
    // ever does with one is paste it into a search engine. "0x1" and "0xdeadbeef" both find
    // nothing at all.
    SUBCASE("uppercase digits") {
        CHECK(describeHresult(static_cast<HRESULT>(0xDEADBEEF)).starts_with(L"0xDEADBEEF"));
    }

    SUBCASE("padded to eight digits") {
        CHECK(describeHresult(static_cast<HRESULT>(1)).starts_with(L"0x00000001"));
    }
}

TEST_CASE("win: describeHresult prints a negative hresult as its unsigned bit pattern") {
    // HRESULT is a signed LONG and every failing one has the top bit set, so the cast to an
    // unsigned type is on the hot path of the entire error-reporting surface. Without it
    // std::format emits a minus sign followed by the magnitude, and access denied would be
    // logged as "0x-7FFFFFFB" -- which matches nothing in any documentation.
    CHECK(describeHresult(static_cast<HRESULT>(-1)).starts_with(L"0xFFFFFFFF"));
    CHECK(describeHresult(E_ACCESSDENIED).starts_with(L"0x80070005"));
    CHECK(describeHresult(E_UNEXPECTED).starts_with(L"0x8000FFFF"));
}

TEST_CASE("win: describeHresult keeps the documented shape for a well-known failure code") {
    auto const result = describeHresult(E_ACCESSDENIED);
    CHECK(result.starts_with(L"0x80070005"));
    // Not "Access is denied": the message comes from whichever language pack the machine
    // carries, and is absent entirely on an install with no message resource for the id. The
    // shape is the part the log format depends on and the only part that holds everywhere.
    CHECK(hasDescribedShape(result));
}

TEST_CASE("win: describeHresult formats s_ok the same way as a failure code") {
    // Nothing in describeHresult tests SUCCEEDED or FAILED, and that matters because both
    // the logger and the settings writer format HRESULT_FROM_WIN32(GetLastError()) on paths
    // where the last error may legitimately be zero. A success code still has to produce a
    // parseable field, or the record loses its trailing column and whatever reads the log
    // shifts by one.
    auto const result = describeHresult(S_OK);
    CHECK(result.starts_with(L"0x00000000"));
    CHECK(hasDescribedShape(result));
}

TEST_CASE("win: describeHresult never leaves trailing punctuation from the system message") {
    // Windows system messages end with ".\r\n". Left in, every logged error would carry a
    // carriage return and a newline in the middle of its own line, and the log would stop
    // being one record per line -- the single property its whole format rests on. This holds
    // on a localised install too, because the strip runs on whatever text came back.
    std::array const codes{S_OK,
                           S_FALSE,
                           E_FAIL,
                           E_NOTIMPL,
                           E_INVALIDARG,
                           E_OUTOFMEMORY,
                           E_ACCESSDENIED,
                           E_UNEXPECTED,
                           HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)};

    for (HRESULT const code : codes) {
        CAPTURE(code);
        auto const result = describeHresult(code);
        if (result.size() > 10u) {
            CHECK(result.back() == L')');
            CHECK_FALSE(isStrippedTrailer(result[result.size() - 2]));
        }
    }
}

TEST_CASE("win: describeHresult produces the same shape for every hresult") {
    // The strongest invariant that survives a localised Windows, an English one, and a
    // stripped install with no message resource for a given id. It is also the one that
    // catches a branch added later which forgets the code prefix or the closing bracket.
    std::array const codes{0x00000000u, 0x00000001u, 0x0ABCDEF1u, 0x7FFFFFFFu, 0x80004005u,
                           0x80004001u, 0x8000FFFFu, 0x80070002u, 0x80070005u, 0x8007000Eu,
                           0x80070057u, 0xDEADBEEFu, 0xFFFFFFFFu};

    for (std::uint32_t const code : codes) {
        CAPTURE(code);
        auto const result = describeHresult(static_cast<HRESULT>(code));
        CHECK(result.size() >= 10u);
        CHECK(result.substr(0, 10) == std::format(L"0x{:08X}", code));
        CHECK(hasDescribedShape(result));
    }
}

TEST_CASE("win: an error windows always has words for comes back with them") {
    // Everything else about describeHresult is a shape check, and a version that dropped
    // FormatMessageW entirely and returned the bare code would satisfy every one of them --
    // including the repeat-call and trailing-punctuation cases, which only ever look at a
    // result that happens to be long enough. This is the case that refuses that version.
    //
    // Access denied is win32 error 5. It sits in the base message table of every Windows
    // install and every language pack, so demanding a message here does not quietly require
    // an English machine; what the message says is still nobody's business.
    std::wstring const described = describeHresult(E_ACCESSDENIED);
    REQUIRE(described.size() > 12u);
    CHECK(described.substr(0, 12) == L"0x80070005 (");
    CHECK(described.back() == L')');

    // The brackets have to hold something. A strip loop that ate the whole message would
    // otherwise leave "0x80070005 ()" and still pass every check above.
    CHECK(described.size() > 13u);
}

TEST_CASE("win: describeHresult contains no embedded nul from the FormatMessage buffer") {
    // FormatMessageW returns a length that excludes the terminator, so copying length + 1 is
    // a plausible off-by-one. The result would look right in a debugger and compare equal as
    // a wstring, while truncating the log line the moment it reached a byte-oriented writer.
    CHECK(describeHresult(E_ACCESSDENIED).find(L'\0') == std::wstring::npos);
    CHECK(describeHresult(S_OK).find(L'\0') == std::wstring::npos);
}

TEST_CASE("win: describeHresult is stable across repeated calls") {
    // The FORMAT_MESSAGE_ALLOCATE_BUFFER and LocalFree pairing is the only lifetime hazard
    // in the file, and every logged Win32 failure goes through it. A double free, or a copy
    // taken after the free, shows up here as a second result that differs from the first --
    // and under a debug CRT as an outright fault.
    auto const first = describeHresult(E_ACCESSDENIED);
    for (int i = 0; i < 100; ++i) {
        CHECK(describeHresult(E_ACCESSDENIED) == first);
    }
}
