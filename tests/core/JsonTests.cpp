// Unit tests for core/Json.h: the hand-written JSON parser, DOM and serialiser.
//
// Everything the application remembers between runs passes through this one file. The
// settings the user edits by hand, the battery history log, the sound-file paths: all of it
// is read by parse() and written by dump(). That makes two properties worth pinning down
// rather than assuming.
//
// The first is that a damaged file fails loudly and at the right byte. Every error carries
// the offset it gave up on and the settings loader shows that text to the user, so an offset
// pointing at the wrong token is worse than no offset at all; the tests below spell out the
// exact message for every failure the grammar can produce.
//
// The second is that save-load-save changes nothing. The writer's escaping, the sorted key
// order and the shortest round-tripping number form exist so that a save rewrites only what
// the user actually changed, and each of those is pinned to its exact bytes here.

#include "TestSupport.h"

#include "core/Json.h"

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using peek::json::Array;
using peek::json::Object;
using peek::json::Value;
using peek::json::dump;
using peek::json::parse;

namespace {

// Reading the message rather than the returned Value is the only way to test a failure:
// parse() hands back a null Value on every error, which is indistinguishable from a document
// that legitimately holds null.
std::string parseError(std::string_view text) {
    std::string error;
    parse(text, &error);
    return error;
}

// U+FFFD as its UTF-8 bytes. Written out rather than typed so the expectation does not depend
// on how this file is encoded or on the execution character set of the test target.
constexpr std::string_view kReplacementUtf8 = "\xEF\xBF\xBD";

}  // namespace

TEST_CASE("json: an empty document is rejected") {
    // A zero-length settings file is the commonest form of corruption, because it is what an
    // interrupted write leaves behind. Accepting it would silently adopt a null root.
    std::string error = "left over from an earlier read";
    Value const value = parse("", &error);
    CHECK(value.isNull());
    CHECK(error == "unexpected end of input at byte 0");

    // Whitespace is skipped before a value is looked for, so the offset names the end of the
    // blank run rather than the start of the file.
    CHECK(parseError("   ") == "unexpected end of input at byte 3");
    CHECK(parseError("\t\r\n") == "unexpected end of input at byte 3");
}

TEST_CASE("json: only space, tab, carriage return and newline count as whitespace") {
    CHECK(parseError(" \t\r\n[ 1 , 2 ]\r\n ").empty());
    CHECK(parseError("{ \"a\" : 1 }").empty());

    // A form feed and a vertical tab are whitespace to the C library but not to JSON. Anyone
    // widening the set to isspace() would make parsing depend on the machine's code page,
    // and these two are the characters that would let that through unnoticed.
    CHECK(parseError("\f1") == "expected a value at byte 0");
    CHECK(parseError("[1,\v2]") == "expected a value at byte 3");
}

TEST_CASE("json: a byte order mark is skipped once, and only at the very start") {
    // Notepad and PowerShell redirection both write a marker. Without the skip, every
    // settings file the user opens in an editor fails to load and their settings reset.
    // Splicing the literals apart is not decoration: a hex escape swallows every hex digit
    // that follows it, so "\xEF\xBB\xBF42" would parse \xBF42 as one over-long escape.
    CHECK(parse("\xEF\xBB\xBF" "42").asInt() == 42);
    CHECK(parseError("\xEF\xBB\xBF" "42").empty());
    CHECK(parse("\xEF\xBB\xBF{}").kind() == Value::Kind::Object);

    // A file an editor saved as empty still carries the marker, and the offset has to account
    // for the three bytes already consumed.
    CHECK(parseError("\xEF\xBB\xBF") == "unexpected end of input at byte 3");

    // The other half of the rule: this is not a general "strip a marker anywhere" pass. One
    // that follows a blank is data, and so is a second one.
    CHECK(parseError(" \xEF\xBB\xBF{}") == "expected a value at byte 1");
    CHECK(parseError("\xEF\xBB\xBF\xEF\xBB\xBF{}") == "expected a value at byte 3");
}

TEST_CASE("json: the three literals parse, and a truncated one does not") {
    CHECK(parse("true").kind() == Value::Kind::Bool);
    CHECK(parse("true").asBool());
    CHECK(parse("false").kind() == Value::Kind::Bool);
    CHECK_FALSE(parse("false").asBool(true));
    CHECK(parse("null").kind() == Value::Kind::Null);

    CHECK(parseError("tru") == "invalid literal at byte 0");
    CHECK(parseError("nul") == "invalid literal at byte 0");

    // The literal branch only requires a prefix match, so a longer word consumes its four
    // bytes and is caught by the trailing-data check instead. Were that check ever dropped,
    // `truthy` would read as `true`.
    CHECK(parseError("truex") == "unexpected trailing data at byte 4");

    // Case matters: an uppercase spelling never reaches the literal branch at all and is
    // offered to the number parser.
    CHECK(parseError("True") == "expected a value at byte 0");
}

TEST_CASE("json: a failed parse and a parsed null are both null values") {
    // The trap every caller has to know about: isNull() cannot tell a valid null apart from a
    // failure, so the error string is the only signal there is.
    std::string good = "stale";
    CHECK(parse("null", &good).isNull());
    CHECK(good.empty());

    std::string bad;
    CHECK(parse("nul", &bad).isNull());
    CHECK_FALSE(bad.empty());

    // A half-built container is never handed back, so a truncated file cannot load half its
    // settings and leave the rest at their defaults without anyone noticing.
    CHECK(parse("[1,2,").asArray().empty());
    CHECK(parse("{\"a\":1").asObject().empty());

    // The error pointer is optional, and a caller that does not want the detail must not
    // crash on the failing path. Null on its own proves little -- a failed parse and a
    // document holding literal null give back the same thing -- so the successful call
    // beside it is what shows this overload still parses rather than always giving up.
    CHECK(parse("{").isNull());
    CHECK(parse("{\"a\":1}").asObject().size() == 1u);
}

TEST_CASE("json: anything after the root value is trailing data") {
    // Two concatenated documents are what an append that should have been a rewrite produces.
    // Reading only the first would quietly discard everything the second one changed.
    CHECK(parseError("{} {}") == "unexpected trailing data at byte 3");
    CHECK(parseError("1 2") == "unexpected trailing data at byte 2");
    CHECK(parseError("[1][2]") == "unexpected trailing data at byte 3");

    // Trailing whitespace is not trailing data: a file ending in a newline is ordinary.
    CHECK(parseError("42 \r\n\t").empty());
}

TEST_CASE("json: integers, fractions and exponents all land in one numeric kind") {
    SUBCASE("spellings the grammar accepts") {
        CHECK(parse("0").kind() == Value::Kind::Number);
        CHECK(parse("0").asNumber() == doctest::Approx(0.0));
        CHECK(parse("42").asNumber() == doctest::Approx(42.0));
        CHECK(parse("-42").asNumber() == doctest::Approx(-42.0));
        CHECK(parse("0.5").asNumber() == doctest::Approx(0.5));
        CHECK(parse("-0.25").asNumber() == doctest::Approx(-0.25));
        CHECK(parse("1e3").asNumber() == doctest::Approx(1000.0));
        CHECK(parse("1E3").asNumber() == doctest::Approx(1000.0));
        CHECK(parse("1e+3").asNumber() == doctest::Approx(1000.0));
        CHECK(parse("1e-3").asNumber() == doctest::Approx(0.001));
        CHECK(parse("1.5e2").asNumber() == doctest::Approx(150.0));

        // The leading-zero rule covers the integer part only; an exponent may carry as many
        // as it likes.
        CHECK(parse("1e007").asNumber() == doctest::Approx(1e7));
    }

    SUBCASE("spellings it does not") {
        // The scan is hand-rolled and from_chars runs afterwards on the bytes it matched, so
        // a grammar that accepted "1." or ".5" would hand the conversion something it reads
        // differently from the scan.
        CHECK(parseError(".5") == "expected a value at byte 0");
        CHECK(parseError("-") == "expected a value at byte 1");
        CHECK(parseError("+1") == "expected a value at byte 0");
        CHECK(parseError("1.") == "expected digits after '.' at byte 2");
        CHECK(parseError("1e") == "expected digits in the exponent at byte 2");
        CHECK(parseError("1e+") == "expected digits in the exponent at byte 3");

        // Two decimal points are an error rather than a number that stops at the second one.
        CHECK(parseError("1.2.3") == "unexpected trailing data at byte 3");
    }
}

TEST_CASE("json: a leading zero is rejected") {
    // 007 must not mean seven to one reader and sixty-three to another, so the octal-looking
    // spelling is refused outright rather than reinterpreted.
    CHECK(parseError("01") == "a leading zero at byte 1");
    CHECK(parseError("00") == "a leading zero at byte 1");
    CHECK(parseError("-01") == "a leading zero at byte 2");

    // The check fires on a following digit only, so an ordinary zero is untouched.
    CHECK(parse("0.5").asNumber() == doctest::Approx(0.5));
    CHECK(parse("0e0").asNumber() == doctest::Approx(0.0));
}

TEST_CASE("json: minus zero keeps its sign") {
    Value const value = parse("-0");
    CHECK(value.kind() == Value::Kind::Number);
    CHECK(value.asNumber() == doctest::Approx(0.0));

    // Approx cannot see the sign bit, and the sign is the whole point: the grammar takes the
    // minus and the conversion has to carry it through rather than collapsing to a plain zero.
    CHECK(std::signbit(value.asNumber()));
    CHECK(std::signbit(parse("-0.0").asNumber()));

    // Rounding a negative zero still gives an int zero, which is what a threshold read as an
    // int gets from a file someone typed "-0" into.
    CHECK(value.asInt(7) == 0);
}

TEST_CASE("json: a number too large for a double is rejected") {
    // The grammar has already matched here, so this is the one failure that comes from the
    // conversion rather than the scan. A hand-edited or corrupted number must not become
    // infinity by way of a silent overflow.
    CHECK(parseError("1e400") == "a number outside the range of a double at byte 0");
    CHECK(parseError("-1e400") == "a number outside the range of a double at byte 0");

    // The offset is rewound to the start of the literal, which is what the nested case proves:
    // without the rewind it would name the end of the number, and a hard-coded zero would name
    // the start of the document instead of the start of the literal.
    CHECK(parseError("[1e400]") == "a number outside the range of a double at byte 1");

    CHECK(parse("1e308").asNumber() == doctest::Approx(1e308));
}

TEST_CASE("json: infinity and NaN have no spelling the parser accepts") {
    // parse() is the only route a number takes into the DOM, so this is what guarantees that
    // a non-finite value can never reach asFloat or asInt from a file. Volume, opacity and
    // the thresholds all assume finite input.
    CHECK(parseError("Infinity") == "expected a value at byte 0");
    CHECK(parseError("-Infinity") == "expected a value at byte 1");
    CHECK(parseError("NaN") == "expected a value at byte 0");
    CHECK(parseError("inf") == "expected a value at byte 0");

    // Only this one reaches the literal branch, because 'n' is also how null starts.
    CHECK(parseError("nan") == "invalid literal at byte 0");
}

TEST_CASE("json: an unterminated string is rejected") {
    // A file truncated by a power loss mid-write ends inside a string, and the two messages
    // tell the user whether the cut landed on a character or inside an escape.
    CHECK(parseError("\"abc") == "unterminated string at byte 4");
    CHECK(parseError("\"") == "unterminated string at byte 1");
    CHECK(parseError("\"abc\\") == "unterminated escape at byte 5");
    CHECK(parseError("[\"a\", \"b") == "unterminated string at byte 8");
}

TEST_CASE("json: raw control characters in a string are rejected") {
    // A literal newline inside a string means the writer failed to escape it. Accepting it
    // would let a device name containing one produce a file no other JSON reader can load.
    CHECK(parseError("\"a\nb\"") == "a raw control character in a string at byte 2");
    CHECK(parseError("\"a\tb\"") == "a raw control character in a string at byte 2");
    CHECK(parseError("\"a\rb\"") == "a raw control character in a string at byte 2");
    CHECK(parseError(std::string("\"a\0b\"", 5)) ==
          "a raw control character in a string at byte 2");

    // The bar is exactly U+0020 rather than "anything unprintable", so DEL sits above it and
    // is copied like any other byte. The literals are spliced because an \x escape swallows
    // every hex digit that follows it, and 'b' is one.
    CHECK(parse("\"a\x7F" "b\"").asString() == "a\x7F" "b");
}

TEST_CASE("json: the two-character escapes") {
    CHECK(parse("\"\\\"\"").asString() == "\"");
    CHECK(parse("\"\\\\\"").asString() == "\\");
    CHECK(parse("\"\\b\\f\\n\\r\\t\"").asString() == "\b\f\n\r\t");

    // Nothing in this codebase ever writes an escaped solidus, which makes it exactly the
    // escape a hand-edit introduces and exactly the one a reimplementation forgets.
    CHECK(parse("\"\\/\"").asString() == "/");

    // The offset points past the offending character, because it is consumed before the
    // switch that rejects it.
    CHECK(parseError("\"a\\x\"") == "unknown escape at byte 4");
    CHECK(parseError("\"\\'\"") == "unknown escape at byte 3");

    // The unicode escape is case-sensitive; a capital U is not it.
    CHECK(parseError("\"\\U0041\"") == "unknown escape at byte 3");
}

TEST_CASE("json: bytes above ASCII pass through the parser unvalidated") {
    // The file is UTF-8 by definition, so a byte the parser cannot prove is well formed is
    // copied rather than rejected: losing every setting over one stray byte would cost far
    // more than carrying it through to widen(), which substitutes U+FFFD.
    Value const raw = parse("\"\xFF\"");
    CHECK(raw.kind() == Value::Kind::String);
    CHECK(raw.asString() == "\xFF");

    // Controller names and sound-file paths are non-ASCII in the Russian UI, so the ordinary
    // case has to survive byte for byte on the way in and convert cleanly on the way out.
    Value const utf8 = parse("\"\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82\"");
    CHECK(utf8.asString() == "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
    CHECK(utf8.asWide() == L"\u041F\u0440\u0438\u0432\u0435\u0442");
}

TEST_CASE("json: backslash-u escapes") {
    CHECK(parse("\"\\u0041\"").asString() == "A");
    CHECK(parse("\"\\u20AC\"").asString() == "\xE2\x82\xAC");

    // Both spellings of the hex digits, because a hand-typed accented character arrives in
    // either and the two must decode identically.
    CHECK(parse("\"\\u00e9\"").asString() == "\xC3\xA9");
    CHECK(parse("\"\\u00E9\"").asString() == "\xC3\xA9");

    // A decoded quote is data. A parser that re-scanned what an escape produced would end
    // the string here and truncate every value containing an escaped quote.
    CHECK(parse("\"a\\u0022b\"").asString() == "a\"b");
    CHECK(parse("\"a\\u005Cb\"").asString() == "a\\b");

    // U+0000 is a legal escape, and unlike a C string the result keeps its length.
    CHECK(parse("\"a\\u0000b\"").asString() == std::string("a\0b", 3));
    CHECK(parse("\"a\\u0000b\"").asString().size() == 3u);

    // Too few bytes left in the document and four bytes that are not all hex are separate
    // failures, and neither advances past the escape.
    CHECK(parseError("\"\\u12\"") == "truncated \\u escape at byte 3");
    CHECK(parseError("\"\\u\"") == "truncated \\u escape at byte 3");
    CHECK(parseError("\"\\u12g4\"") == "a \\u escape needs four hex digits at byte 3");
    CHECK(parseError("\"\\u 123\"") == "a \\u escape needs four hex digits at byte 3");
}

TEST_CASE("json: a surrogate pair becomes one code point") {
    // U+1F600. An escaped astral character is how another tool would write an emoji into a
    // device or sound name; getting the shift or the bias wrong yields a different character
    // rather than an error.
    Value const emoji = parse("\"\\uD83D\\uDE00\"");
    CHECK(emoji.asString() == "\xF0\x9F\x98\x80");

    // What reaches the Win32 side has to be a well-formed pair rather than two replacement
    // characters, which only the UTF-16 view can show.
    std::wstring const wide = emoji.asWide();
    REQUIRE(wide.size() == 2u);
    CHECK(wide[0] == static_cast<wchar_t>(0xD83D));
    CHECK(wide[1] == static_cast<wchar_t>(0xDE00));

    CHECK(parse("\"\\ud83d\\ude00\"").asString() == "\xF0\x9F\x98\x80");
}

TEST_CASE("json: a lone surrogate becomes U+FFFD") {
    // The documented degradation policy: a lone surrogate has no UTF-8 encoding, and one bad
    // escape must not cost the user every other setting in the file.
    std::string const replacement(kReplacementUtf8);

    CHECK(parse("\"\\uD83D\"").asString() == replacement);
    CHECK(parse("\"\\uDE00\"").asString() == replacement);
    CHECK(parse("\"\\uD83Dx\"").asString() == replacement + "x");

    // Whatever followed the high surrogate is re-read as a character in its own right, so an
    // escape that is not a low surrogate is decoded rather than swallowed.
    CHECK(parse("\"\\uD83D\\u0041\"").asString() == replacement + "A");
    CHECK(parse("\"\\uD83D\\uD83D\"").asString() == replacement + replacement);

    // The rewind happens only once the second escape has been read successfully, so a
    // malformed one still fails the document instead of being substituted away.
    CHECK(parseError("\"\\uD83D\\uZZZZ\"") == "a \\u escape needs four hex digits at byte 9");
}

TEST_CASE("json: objects") {
    SUBCASE("well-formed") {
        CHECK(parse("{}").kind() == Value::Kind::Object);
        CHECK(parse("{}").asObject().empty());
        CHECK(parse("{ }").asObject().empty());

        Value const root = parse("{\"a\":1,\"b\":\"two\",\"c\":[3],\"d\":{},\"e\":null}");
        CHECK(root.asObject().size() == 5u);
        CHECK(root["a"].asInt() == 1);
        CHECK(root["b"].asString() == "two");
        CHECK(root["c"].asArray().size() == 1u);
        CHECK(root["d"].kind() == Value::Kind::Object);
        CHECK(root["e"].isNull());

        // An empty key is an ordinary key, not a missing one.
        CHECK(parse("{\"\":1}").contains(""));
        CHECK(parse("{\"\":1}")[""].asInt() == 1);
    }

    SUBCASE("malformed") {
        // An unquoted key and a single-quoted one are what a user who thinks JSON is
        // JavaScript will type, so each has to name the real problem and the offending byte
        // rather than failing somewhere further along.
        CHECK(parseError("{a:1}") == "expected a quoted key at byte 1");
        CHECK(parseError("{'a':1}") == "expected a quoted key at byte 1");
        CHECK(parseError("{\"a\" 1}") == "expected ':' at byte 5");
        CHECK(parseError("{\"a\":1") == "expected ',' or '}' at byte 6");
        CHECK(parseError("{") == "expected a quoted key at byte 1");
        CHECK(parseError("{\"a\":1 \"b\":2}") == "expected ',' or '}' at byte 7");
    }
}

TEST_CASE("json: the last of a set of duplicate keys wins") {
    // Someone who pasted a block twice expects the copy nearer the bottom to be the live one.
    // Swapping the insert_or_assign for an insert or an emplace -- an easy tidy-up -- would
    // reverse that and make the edit appear to do nothing at all.
    Value const root = parse("{\"a\":1,\"b\":2,\"a\":3}");
    CHECK(root.asObject().size() == 2u);
    CHECK(root["a"].asInt() == 3);
    CHECK(dump(root, 0) == "{\"a\":3,\"b\":2}");
}

TEST_CASE("json: arrays") {
    CHECK(parse("[]").kind() == Value::Kind::Array);
    CHECK(parse("[]").asArray().empty());
    CHECK(parse("[ ]").asArray().empty());

    Value const list = parse("[1,\"two\",true,null,[],{}]");
    REQUIRE(list.asArray().size() == 6u);
    CHECK(list.asArray()[0].asInt() == 1);
    CHECK(list.asArray()[1].asString() == "two");
    CHECK(list.asArray()[2].asBool());
    CHECK(list.asArray()[3].isNull());
    CHECK(list.asArray()[4].kind() == Value::Kind::Array);
    CHECK(list.asArray()[5].kind() == Value::Kind::Object);

    // The history log is a stream of arrays, so a missing separator has to be reported at the
    // separator rather than at the end of the file, or the bad line cannot be found.
    CHECK(parseError("[") == "unexpected end of input at byte 1");
    CHECK(parseError("[1") == "expected ',' or ']' at byte 2");
    CHECK(parseError("[1 2]") == "expected ',' or ']' at byte 3");
    CHECK(parseError("[1,,2]") == "expected a value at byte 3");
    CHECK(parseError("[,]") == "expected a value at byte 1");
}

TEST_CASE("json: trailing commas are rejected") {
    // The single most common hand-edit mistake in JSON. Both containers keep looking for the
    // next element after a comma, so the closing bracket is what gets reported -- and both
    // offsets have to land on the bracket, or the user deletes the wrong character.
    CHECK(parseError("{\"a\":1,}") == "expected a quoted key at byte 7");
    CHECK(parseError("[1,]") == "expected a value at byte 3");
    CHECK(parseError("[[1,],2]") == "expected a value at byte 4");
}

TEST_CASE("json: nesting deeper than 64 levels is rejected") {
    // The cap stops a hostile or corrupt file recursing the parser off the stack. It counts
    // values rather than brackets and the root is depth zero, so 65 empty arrays sit exactly
    // on the limit while the scalar inside 65 arrays is one level past it.
    std::string const openers(65, '[');
    std::string const closers(65, ']');

    CHECK(parseError(openers + closers).empty());
    CHECK(parse(openers + closers).kind() == Value::Kind::Array);
    CHECK(parseError(std::string(64, '[') + "1" + std::string(64, ']')).empty());

    CHECK(parseError(openers + "1" + closers) == "nesting is too deep at byte 65");
    CHECK(parseError(std::string(66, '[') + std::string(66, ']')) ==
          "nesting is too deep at byte 65");
}

TEST_CASE("json: serialising a string escapes what has to be escaped") {
    CHECK(dump(Value("plain"), 0) == "\"plain\"");
    CHECK(dump(Value("a\"b"), 0) == "\"a\\\"b\"");
    CHECK(dump(Value("a\\b"), 0) == "\"a\\\\b\"");
    CHECK(dump(Value("a\nb"), 0) == "\"a\\nb\"");
    CHECK(dump(Value("a\rb"), 0) == "\"a\\rb\"");
    CHECK(dump(Value("a\tb"), 0) == "\"a\\tb\"");
    CHECK(dump(Value("a\bb"), 0) == "\"a\\bb\"");
    CHECK(dump(Value("a\fb"), 0) == "\"a\\fb\"");

    // Anything else below U+0020 takes the numeric escape, in lower-case hex. Failing to
    // escape one would produce a file this very parser rejects on the next load.
    CHECK(dump(Value(std::string("a\x01" "b")), 0) == "\"a\\u0001b\"");
    CHECK(dump(Value(std::string("a\x1F" "b")), 0) == "\"a\\u001fb\"");
    CHECK(dump(Value(std::string("a\0b", 3)), 0) == "\"a\\u0000b\"");

    // The solidus and DEL need no escape, and neither does anything above ASCII: the settings
    // file is meant to be human-editable, and escaping non-ASCII would turn a Russian sound
    // name into \uXXXX soup.
    CHECK(dump(Value("a/b"), 0) == "\"a/b\"");
    CHECK(dump(Value(std::string("a\x7F" "b")), 0) == "\"a\x7F" "b\"");
    CHECK(dump(parse("\"caf\\u00e9\""), 0) == "\"caf\xC3\xA9\"");
}

TEST_CASE("json: serialising a number uses the shortest round-tripping form") {
    // Every save rewrites the whole settings file. A whole number that picked up a decimal
    // point, or a 0.8 that drifted to 0.80000000000000004, would produce a diff for settings
    // the user never touched -- which is the very thing sorted key order exists to prevent.
    CHECK(dump(Value(3.0), 0) == "3");
    CHECK(dump(Value(0.8), 0) == "0.8");
    CHECK(dump(Value(42), 0) == "42");
    CHECK(dump(Value(0.0), 0) == "0");
    CHECK(dump(Value(-1.5), 0) == "-1.5");
    CHECK(dump(Value(100.0), 0) == "100");
}

TEST_CASE("json: a float is written as the decimal it was set from") {
    // The event volumes, the master volume and the window opacity are all floats, and every
    // save writes all of them out again. Widening 0.7f to a double first is faithful to the
    // bits and useless to whoever opens the file: it then reads 0.699999988079071 for a slider
    // nobody has touched, and the number is no longer one anybody can edit by hand. Storing the
    // double that the float's own shortest decimal parses to keeps the written form the one
    // that was chosen.
    SUBCASE("the written form is the float's own shortest decimal") {
        CHECK(dump(Value(0.8f), 0) == "0.8");
        CHECK(dump(Value(0.1f), 0) == "0.1");
        CHECK(dump(Value(0.35f), 0) == "0.35");
        CHECK(dump(Value(0.7f), 0) == "0.7");
        CHECK(dump(Value(1.0f), 0) == "1");

        // Shortest is not the same as short. This one needs eight digits, and a rule that
        // trimmed it to something tidier would name a different float.
        CHECK(dump(Value(1.0f / 3.0f), 0) == "0.33333334");
    }

    SUBCASE("what comes back is the identical float") {
        // Equality rather than Approx, because what this rules out is precisely a value that
        // returns close enough to look right and then writes itself differently on the save
        // after that, producing a diff in a setting the user never touched.
        CHECK(parse(dump(Value(0.8f), 0)).asFloat(-1.0f) == 0.8f);
        CHECK(parse(dump(Value(0.1f), 0)).asFloat(-1.0f) == 0.1f);
        CHECK(parse(dump(Value(0.35f), 0)).asFloat(-1.0f) == 0.35f);
        CHECK(parse(dump(Value(1.0f), 0)).asFloat(-1.0f) == 1.0f);
        CHECK(parse(dump(Value(1.0f / 3.0f), 0)).asFloat(-1.0f) == 1.0f / 3.0f);

        // The rounding happens in the constructor rather than in the writer, so the stored
        // value is already the same float before any text exists.
        CHECK(Value(0.8f).asFloat(-1.0f) == 0.8f);
        CHECK(Value(1.0f / 3.0f).asFloat(-1.0f) == 1.0f / 3.0f);
    }

    SUBCASE("a double keeps every digit it has") {
        // Only the float overload shortens. A double has no coarser type behind it whose
        // spelling could be recovered, so trimming one would throw away digits its caller meant
        // to store -- and this exact text is what a float would write again if the new overload
        // were ever removed and 0.8f widened on its way in.
        CHECK(dump(Value(static_cast<double>(0.8f)), 0) == "0.800000011920929");
        CHECK(dump(Value(0.8), 0) == "0.8");
        CHECK(dump(Value(0.1), 0) == "0.1");
    }

    SUBCASE("a non-finite float has no decimal to round-trip through") {
        // The shortening step sees these before the writer does and has no decimal to work
        // with, so whatever it hands on has to be the same non-finite number -- otherwise the
        // rule that turns both into null is applied to something else.
        CHECK(dump(Value(std::numeric_limits<float>::infinity()), 0) == "null");
        CHECK(dump(Value(-std::numeric_limits<float>::infinity()), 0) == "null");
        CHECK(dump(Value(std::numeric_limits<float>::quiet_NaN()), 0) == "null");
    }
}

TEST_CASE("json: infinity and NaN serialise as null") {
    // Neither can arrive from parse(), so both can only come from code -- and writing `inf`
    // or `nan` would produce a file that this parser and every other one refuses on the next
    // load, turning one bad computed value into a total loss of settings.
    CHECK(dump(Value(std::numeric_limits<double>::infinity()), 0) == "null");
    CHECK(dump(Value(-std::numeric_limits<double>::infinity()), 0) == "null");
    CHECK(dump(Value(std::numeric_limits<double>::quiet_NaN()), 0) == "null");

    Value root;
    root.set("volume", Value(std::numeric_limits<double>::infinity()));
    CHECK(dump(root, 0) == "{\"volume\":null}");

    // The substitution is lossy on purpose: what comes back is a null, not a number.
    CHECK(parse(dump(root, 0))["volume"].isNull());
}

TEST_CASE("json: object keys are written in sorted byte order") {
    // Insertion order must never leak into the output, because sorted order is what keeps a
    // saved settings file diffable. Both pairs are chosen so that a locale-aware collation
    // would order them the other way round: bytes put upper case first and "10" before "9".
    CHECK(dump(parse("{\"zebra\":1,\"Mango\":2,\"apple\":3}"), 0) ==
          "{\"Mango\":2,\"apple\":3,\"zebra\":1}");
    CHECK(dump(parse("{\"9\":1,\"10\":2}"), 0) == "{\"10\":2,\"9\":1}");

    Value built;
    built.set("zebra", 1);
    built.set("Mango", 2);
    built.set("apple", 3);

    std::vector<std::string> keys;
    for (auto const& entry : built.asObject()) {
        keys.push_back(entry.first);
    }
    std::vector<std::string> const expected{"Mango", "apple", "zebra"};
    CHECK(keys == expected);
}

TEST_CASE("json: indent 2 is the settings file layout") {
    // Pinning the exact bytes means a formatting change surfaces as a failing test rather
    // than as a whole-file diff in the user's settings the next time anything is saved.
    Value root;
    root.set("b", 2);
    root.set("a", Value(Array{Value(1), Value(2)}));
    CHECK(dump(root, 2) == "{\n  \"a\": [\n    1,\n    2\n  ],\n  \"b\": 2\n}");

    // Empty containers stay on one line whatever the indent, so an unused section costs one
    // line rather than three.
    Value empties;
    empties.set("list", Value(Array{}));
    empties.set("map", Value(Object{}));
    CHECK(dump(empties, 2) == "{\n  \"list\": [],\n  \"map\": {}\n}");
    CHECK(dump(Value(Array{}), 2) == "[]");
    CHECK(dump(Value(Object{}), 2) == "{}");
}

TEST_CASE("json: an indent of zero or less emits one line") {
    Value root;
    root.set("b", 2);
    root.set("a", Value(Array{Value(1), Value(2)}));

    // The history log writes one sample per line at indent 0, and its reader is line
    // oriented: a stray newline in here would break it.
    CHECK(dump(root, 0) == "{\"a\":[1,2],\"b\":2}");

    // The newline and the space after a colon are gated by the same predicate, so a negative
    // indent cannot produce a half-formatted document.
    CHECK(dump(root, -1) == dump(root, 0));
}

TEST_CASE("json: parse and dump round-trip") {
    // Save-load-save is the actual lifecycle of the settings file, and doing the trip through
    // the API rather than by hand-counting escapes is what catches an asymmetry between the
    // writer's escaping and the parser's un-escaping.
    Value root;
    root.set("count", 3);
    root.set("volume", 0.8);
    root.set("name", "say \"hi\"\n");
    root.set("nested", Value(Array{Value(true), Value(), Value(-0.5)}));

    std::string const text = dump(root, 2);
    std::string error = "stale";
    Value const again = parse(text, &error);

    CHECK(error.empty());
    CHECK(dump(again, 2) == text);
    CHECK(again["count"].asInt() == 3);
    CHECK(again["volume"].asNumber() == doctest::Approx(0.8));
    CHECK(again["name"].asString() == "say \"hi\"\n");
    REQUIRE(again["nested"].asArray().size() == 3u);
    CHECK(again["nested"].asArray()[0].asBool());
    CHECK(again["nested"].asArray()[1].isNull());
    CHECK(again["nested"].asArray()[2].asNumber() == doctest::Approx(-0.5));

    // Keys go through the same escaping as values, which anything derived from a device
    // identifier relies on.
    Value const awkward = parse("{\"a\\u0000b\":1}");
    CHECK(awkward.contains(std::string("a\0b", 3)));
    CHECK(dump(awkward, 0) == "{\"a\\u0000b\":1}");
}

TEST_CASE("json: a default Value is null") {
    // Kind is derived from the variant's index, so reordering its alternatives would remap
    // every kind at once. The static_assert in the source catches the count; this catches the
    // order at the one end everything else is measured from.
    Value const value;
    CHECK(value.kind() == Value::Kind::Null);
    CHECK(value.isNull());
    CHECK(dump(value, 0) == "null");
    CHECK(Value(nullptr).isNull());
}

TEST_CASE("json: the constructors pick the kind a caller expects") {
    CHECK(Value(true).kind() == Value::Kind::Bool);
    CHECK(Value(1.5).kind() == Value::Kind::Number);
    CHECK(Value(7).kind() == Value::Kind::Number);
    CHECK(Value(7).asNumber() == doctest::Approx(7.0));
    CHECK(Value(std::string("text")).kind() == Value::Kind::String);
    CHECK(Value(Array{}).kind() == Value::Kind::Array);
    CHECK(Value(Object{}).kind() == Value::Kind::Object);

    // Deleting the char-pointer overload as redundant is a plausible tidy-up, and it would
    // turn every string literal in the settings writer into `true` -- silently, since a bool
    // and a string both dump without error.
    Value const literal("true");
    CHECK(literal.kind() == Value::Kind::String);
    CHECK(literal.asString() == "true");

    // A null pointer becomes an empty string rather than being dereferenced.
    char const* missing = nullptr;
    Value const fromNull(missing);
    CHECK(fromNull.kind() == Value::Kind::String);
    CHECK(fromNull.asString().empty());

    // A view is copied, and only over the bytes it covers: the tail of the buffer behind it
    // is not part of the value.
    std::string_view const view = "hello world";
    CHECK(Value(view.substr(0, 5)).asString() == "hello");
}

TEST_CASE("json: the float constructor did not change what an int or a bool selects") {
    // Value(float) sits between the two numeric overloads the settings writer already used, and
    // both an int and a bool convert to a float as readily as they do to a double. Only the
    // exact match on their own overload keeps them off it, and deleting either as redundant is
    // a plausible tidy-up -- one that would route every count and every flag in the settings
    // file through 24 bits of mantissa.
    CHECK(Value(true).kind() == Value::Kind::Bool);
    CHECK(dump(Value(true), 0) == "true");
    CHECK(dump(Value(false), 0) == "false");

    // Two to the power 24 plus one is the smallest positive int no float can hold, which makes
    // it the one value that tells the two conversions apart: through a float it comes back as
    // 16777216. Nothing in the application counts that high today, but the failure would be a
    // number silently changing rather than anything visible at the call site.
    Value const large(16777217);
    CHECK(large.kind() == Value::Kind::Number);
    CHECK(large.asInt() == 16777217);
    CHECK(dump(large, 0) == "16777217");
    CHECK(dump(Value(-16777217), 0) == "-16777217");
}

TEST_CASE("json: copying a Value deep-copies its storage") {
    // Value holds a unique_ptr, so the compiler cannot generate a copy; the hand-written one
    // is the only thing standing between the settings model and two objects sharing a buffer.
    Value original;
    original.set("k", 1);

    Value copy = original;
    copy.set("k", 2);
    CHECK(original["k"].asInt() == 1);
    CHECK(copy["k"].asInt() == 2);

    Value assigned(true);
    assigned = original;
    assigned.set("k", 3);
    CHECK(original["k"].asInt() == 1);
    CHECK(assigned["k"].asInt() == 3);

    // Self-assignment through a reference is what a `settings = settings.normalised()` style
    // call produces, and without the guard the assignment frees the storage it then reads.
    Value* const alias = &original;
    original = *alias;
    CHECK(original["k"].asInt() == 1);
}

TEST_CASE("json: a moved-from Value reads as null and can be reused") {
    Value source;
    source.set("k", 1);

    Value const moved = std::move(source);
    CHECK(moved["k"].asInt() == 1);

    // The null-storage state is load-bearing: every accessor routes through kind(), and
    // without its guard each of these would dereference a null unique_ptr.
    CHECK(source.kind() == Value::Kind::Null);
    CHECK(source.isNull());
    CHECK(source.asInt(9) == 9);
    CHECK(source.asArray().empty());
    CHECK(source["k"].isNull());
    CHECK_FALSE(source.contains("k"));
    CHECK(dump(source, 0) == "null");

    // Moving a Value out of a container and reusing the shell is ordinary code, so both
    // mutators have to allocate storage again rather than fault.
    source.set("again", 2);
    CHECK(source["again"].asInt() == 2);

    Value list;
    list.push(1);
    Value const taken = std::move(list);
    CHECK(taken.asArray().size() == 1u);
    list.push(2);
    REQUIRE(list.asArray().size() == 1u);
    CHECK(list.asArray()[0].asInt() == 2);
}

TEST_CASE("json: typed reads fall back instead of coercing") {
    // The whole robustness story for a hand-edited file: `"enabled": 0` or `"volume": "loud"`
    // leaves that one setting at its default and touches nothing else. A helpful coercion
    // would turn a typo into a silently changed setting instead.
    Value const number(0.0);
    CHECK(number.asBool(true));
    CHECK_FALSE(number.asBool(false));
    CHECK(number.asString("fallback") == "fallback");
    CHECK(number.asWide(L"fallback") == L"fallback");
    CHECK(number.asArray().empty());
    CHECK(number.asObject().empty());

    Value const text("42");
    CHECK(text.asNumber(-1.0) == doctest::Approx(-1.0));
    CHECK(text.asInt(-1) == -1);
    CHECK(text.asFloat(0.25f) == doctest::Approx(0.25f));

    Value const flag(true);
    CHECK(flag.asNumber(-1.0) == doctest::Approx(-1.0));
    CHECK(flag.asString("fallback") == "fallback");

    Value const nothing;
    CHECK(nothing.asBool(true));
    CHECK(nothing.asNumber(2.5) == doctest::Approx(2.5));
    CHECK(nothing.asString("fallback") == "fallback");
    CHECK(nothing.asObject().empty());
}

TEST_CASE("json: asInt rounds and refuses what will not fit an int") {
    // The poll interval, the thresholds and the retention days are all read this way from a
    // file where a user may well have typed 30.0. Truncating instead of rounding would read a
    // 29.999999 back as 29.
    CHECK(parse("30.0").asInt() == 30);
    CHECK(parse("30.4").asInt() == 30);
    CHECK(parse("30.5").asInt() == 31);
    CHECK(parse("-30.5").asInt() == -31);
    CHECK(parse("1e2").asInt() == 100);

    // Without the range guard the cast would be undefined behaviour on a number a corrupted
    // file can easily hold, so the ends of the range are fenced exactly.
    CHECK(parse("2147483647").asInt(-1) == 2147483647);
    CHECK(parse("2147483648").asInt(-1) == -1);
    CHECK(parse("-2147483648").asInt(1) == std::numeric_limits<int>::min());
    CHECK(parse("-2147483649").asInt(1) == 1);

    // A NaN compares false against both ends, which is why the guard is one negated
    // conjunction rather than two separate tests.
    CHECK(Value(std::numeric_limits<double>::quiet_NaN()).asInt(7) == 7);
    CHECK(Value(std::numeric_limits<double>::infinity()).asInt(7) == 7);
    CHECK(Value(-std::numeric_limits<double>::infinity()).asInt(7) == 7);
}

TEST_CASE("json: asWide converts UTF-8 to UTF-16") {
    // Every wide string the UI shows arrives through here: the sound-file path out of the
    // settings and the controller id out of the history.
    CHECK(parse("\"caf\\u00e9\"").asWide() == L"caf\u00E9");

    // The conversion is length driven, so an embedded null survives rather than cutting the
    // string off at the first zero byte.
    CHECK(parse("\"a\\u0000b\"").asWide() == std::wstring(L"a\0b", 3));

    // The subtle one: an empty string is a string, not a missing value. A user who clears a
    // sound-file path must get an empty path back, not the built-in default resurrected by
    // the fallback.
    CHECK(parse("\"\"").asWide(L"fallback").empty());
    CHECK(parse("42").asWide(L"fallback") == L"fallback");
}

TEST_CASE("json: member lookup on a non-object yields null") {
    // This exact chain is the documented use and appears throughout the settings loader. If a
    // missing intermediate returned anything but a stable null, loading a file written by an
    // older version -- where the section does not exist yet -- would crash rather than fall
    // back.
    Value const missing = parse("{}");
    CHECK(missing["events"]["low"]["volume"].asFloat(1.0f) == doctest::Approx(1.0f));
    CHECK(missing["events"].isNull());

    Value const number(42);
    CHECK(number["anything"].isNull());

    // An array is not an object, so an index spelled as a key finds nothing.
    CHECK(parse("[1,2]")["0"].isNull());

    Value root;
    root.set("events", Value()).set("low", Value()).set("volume", 0.5);
    CHECK(root["events"]["low"]["volume"].asFloat(1.0f) == doctest::Approx(0.5f));
    CHECK(root["events"]["high"]["volume"].asFloat(1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("json: contains distinguishes an absent key from a null one") {
    // "Present but null" and "absent" mean different things to a settings migration: the
    // default was left alone, or the user deliberately cleared it. Member lookup cannot tell
    // them apart because it answers null to both.
    Value root;
    root.set("present", 1);
    root.set("explicitNull", Value());

    CHECK(root.contains("present"));
    CHECK(root.contains("explicitNull"));
    CHECK_FALSE(root.contains("absent"));
    CHECK(root["explicitNull"].isNull());
    CHECK(root["absent"].isNull());

    // And it must not throw on a value that is not an object at all.
    CHECK_FALSE(Value(42).contains("present"));
    CHECK_FALSE(Value().contains("present"));
}

TEST_CASE("json: set replaces the value in place and returns a usable reference") {
    Value value(42);
    value.set("a", 1);
    CHECK(value.kind() == Value::Kind::Object);

    // The number it used to hold is gone rather than shadowed.
    CHECK(value.asNumber(-1.0) == doctest::Approx(-1.0));

    value.set("a", 2);
    CHECK(value.asObject().size() == 1u);
    CHECK(value["a"].asInt() == 2);

    // The reference is what makes chained construction work. Were it a copy, the chain would
    // build a tree that is thrown away and the settings writer would emit empty sections.
    Value& stored = value.set("a", 3);
    stored = Value("through the reference");
    CHECK(value["a"].asString() == "through the reference");
}

TEST_CASE("json: push turns whatever was there into an array") {
    Value list("text");
    list.push(1);
    list.push("two");

    CHECK(list.kind() == Value::Kind::Array);
    REQUIRE(list.asArray().size() == 2u);
    CHECK(list.asArray()[0].asInt() == 1);
    CHECK(list.asArray()[1].asString() == "two");

    // Both mutators are destructive by design. Anyone later "fixing" push() to keep the old
    // value as the first element would quietly double the first entry of every history array.
    CHECK(list.asString("gone") == "gone");

    // The conversion runs the other way just as bluntly.
    list.set("key", 1);
    CHECK(list.kind() == Value::Kind::Object);
    CHECK(list.asArray().empty());
}

TEST_CASE("json: keys are compared by their full length") {
    // Keys can be derived from device identifiers, which the Win32 layer produces as counted
    // strings. If the map ever compared them as C strings, two distinct keys sharing a prefix
    // up to a null would collide and one device's history would overwrite another's.
    std::string const key("a\0b", 3);
    Value root;
    root.set(key, 1);

    CHECK(root.contains(key));
    CHECK_FALSE(root.contains("a"));
    CHECK(root[key].asInt() == 1);
    CHECK(root["a"].isNull());
}
