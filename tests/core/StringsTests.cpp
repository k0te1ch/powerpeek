// The localisation table: that every Text id has a row, that the row says something in both
// languages, and that formatText substitutes into what the row holds.
//
// Strings.cpp guards the table at compile time, but only its shape -- one row per enumerator,
// in enum order. Nothing there looks at what a row contains, so an empty column, an English
// sentence left in the Russian column, or a {} in a label nobody formats all compile clean and
// reach the user as a blank button, an untranslated caption, or literal braces on screen.
//
// Every expectation here is ASCII. The Russian column is checked by script range and by where
// the substituted argument lands rather than by comparing against Russian literals, so nothing
// in this file depends on the charset the test binary is compiled with.

#include "TestSupport.h"

#include "core/Strings.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/Settings.h"

namespace {

using peek::LanguagePreference;
using peek::Text;
using peek::formatText;
using peek::setLanguage;
using peek::text;

// Derived the way Strings.cpp derives it for its own static_assert, so a new enumerator joins
// every sweep below without anyone editing a count.
constexpr std::size_t kTextCount = static_cast<std::size_t>(Text::Restore) + 1;

Text textAt(std::size_t index) {
    return static_cast<Text>(static_cast<int>(index));
}

// The language is a process-wide global with no getter, so whatever a case here switches to
// stays switched for the rest of the binary -- and Settings and ControllerInfo read the table
// too. Restoring the value the process starts on keeps cases in other files that never mention
// a language reading the same strings whatever order doctest runs them in.
struct LanguageRestored {
    ~LanguageRestored() { setLanguage(LanguagePreference::English); }
};

// One language's whole table, in enum order. text() returns views into static storage which
// stay valid but stop being current the moment the language changes, so whole-table comparisons
// need copies.
std::vector<std::wstring> collectAll(LanguagePreference preference) {
    setLanguage(preference);

    std::vector<std::wstring> all;
    all.reserve(kTextCount);
    for (std::size_t i = 0; i < kTextCount; ++i) {
        all.emplace_back(text(textAt(i)));
    }
    return all;
}

// The nine rows whose two columns hold the same literal, and the reason each may: the product
// name, three connection technologies that keep their names in any language, the two endonyms
// in the language picker (which draws each language in its own script), the two toast bodies
// that are skeletons with no words in them, and the percent sign.
constexpr Text kIdenticalInBothLanguages[] = {
    Text::AppName,
    Text::ConnectionUsb,
    Text::ConnectionWireless,
    Text::ConnectionBluetooth,
    Text::LanguageEnglish,
    Text::LanguageRussian,
    Text::ToastBodyLevel,
    Text::ToastBodyNoLevel,
    Text::UnitPercent,
};

bool isIdenticalInBothLanguages(Text id) {
    return std::find(std::begin(kIdenticalInBothLanguages), std::end(kIdenticalInBothLanguages),
                     id) != std::end(kIdenticalInBothLanguages);
}

bool hasCyrillic(std::wstring_view value) {
    return std::any_of(value.begin(), value.end(), [](wchar_t c) {
        auto const code = static_cast<unsigned>(c);
        return code >= 0x0400u && code <= 0x04FFu;
    });
}

int placeholderCount(std::wstring_view value) {
    int count = 0;
    for (std::size_t at = value.find(L"{}"); at != std::wstring_view::npos;
         at = value.find(L"{}", at + 2)) {
        ++count;
    }
    return count;
}

// A brace that is not half of a {} pair. No entry escapes a literal brace as {{ or }}, and none
// carries a format spec, so any other brace is either a typo on screen or a replacement field
// that will throw when the page holding it is drawn.
bool hasLoneBrace(std::wstring_view value) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'}') {
            return true;
        }
        if (value[i] == L'{') {
            if (i + 1 >= value.size() || value[i + 1] != L'}') {
                return true;
            }
            ++i;
        }
    }
    return false;
}

bool isBlank(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

bool hasEdgeWhitespace(std::wstring_view value) {
    return !value.empty() && (isBlank(value.front()) || isBlank(value.back()));
}

struct PlaceholderRow {
    Text id;
    int count;
};

// Every entry with a {} in it, paired with the number of arguments the one formatText call site
// that owns it passes: ControllersPage for the two device lines, HistoryPage for the drain rate,
// AboutPage for the version, TrayIcon for the tooltip overflow, NotificationCenter for the four
// toast strings. Anything else with a placeholder has no caller to substitute it.
constexpr PlaceholderRow kPlaceholderRows[] = {
    {Text::EstimatedRemaining, 1},
    {Text::UpdatedMinutesAgo, 1},
    {Text::DrainRate, 1},
    {Text::AboutVersion, 1},
    {Text::TrayMoreControllers, 1},
    {Text::ToastConnected, 1},
    {Text::ToastDisconnected, 1},
    {Text::ToastBodyLevel, 2},
    {Text::ToastBodyNoLevel, 1},
};

int expectedPlaceholders(Text id) {
    for (PlaceholderRow const& row : kPlaceholderRows) {
        if (row.id == id) {
            return row.count;
        }
    }
    return 0;
}

// The seven places where two or three enumerators are meant to resolve to the same words: a nav
// entry and the title of the page it opens, a button and the tray menu item that does the same
// thing, an event and the toast announcing it, and the two "System" options.
std::vector<std::vector<Text>> deliberateAliases() {
    return {
        {Text::NavDevices, Text::DevicesTitle},
        {Text::NavSettings, Text::SettingsTitle},
        {Text::Refresh, Text::MenuRefresh},
        {Text::StatusFull, Text::EventCharged, Text::ToastCharged},
        {Text::EventLow, Text::ToastLow},
        {Text::EventCritical, Text::ToastCritical},
        {Text::ThemeSystem, Text::LanguageSystem},
    };
}

// The same seven groups as index lists, each in ascending order so that a group listed out of
// enum order still compares equal to the one the table produces.
std::vector<std::vector<std::size_t>> aliasesAsIndexGroups() {
    std::vector<std::vector<std::size_t>> groups;
    for (auto const& alias : deliberateAliases()) {
        auto& indices = groups.emplace_back();
        indices.reserve(alias.size());
        for (Text const id : alias) {
            indices.push_back(static_cast<std::size_t>(id));
        }
        std::sort(indices.begin(), indices.end());
    }
    return groups;
}

// The ids sharing each distinct string, as ascending index lists.
std::vector<std::vector<std::size_t>> groupsOfEqualStrings(std::vector<std::wstring> const& all) {
    std::map<std::wstring, std::vector<std::size_t>> byValue;
    for (std::size_t i = 0; i < all.size(); ++i) {
        byValue[all[i]].push_back(i);
    }

    std::vector<std::vector<std::size_t>> groups;
    groups.reserve(byValue.size());
    for (auto const& bucket : byValue) {
        groups.push_back(bucket.second);
    }
    return groups;
}

// For each id, the lowest id that says the same thing. Two languages agreeing on this mapping is
// the same statement as their partitions being identical, and it fails per id rather than per
// table, which is what makes the failure readable.
std::vector<std::size_t> firstIdWithSameText(std::vector<std::wstring> const& all) {
    std::vector<std::size_t> canonical(all.size(), 0);
    for (auto const& group : groupsOfEqualStrings(all)) {
        for (std::size_t const index : group) {
            canonical[index] = group.front();
        }
    }
    return canonical;
}

}  // namespace

TEST_CASE("strings: every text has a non-empty string in both languages") {
    LanguageRestored const restored;

    SUBCASE("english") {
        setLanguage(LanguagePreference::English);
        for (std::size_t i = 0; i < kTextCount; ++i) {
            CAPTURE(i);
            CHECK_FALSE(text(textAt(i)).empty());
        }
    }

    SUBCASE("russian") {
        setLanguage(LanguagePreference::Russian);
        for (std::size_t i = 0; i < kTextCount; ++i) {
            CAPTURE(i);
            CHECK_FALSE(text(textAt(i)).empty());
        }
    }
}

TEST_CASE("strings: the accessor returns the row that belongs to the id") {
    LanguageRestored const restored;
    setLanguage(LanguagePreference::English);

    CHECK(text(Text::AppName) == L"PowerPeek");
    CHECK(text(Text::AppTagline) == L"Controller battery, at a glance");
    CHECK(text(Text::UnitPercent) == L"%");

    // The load-bearing one: a lookup is a plain index, so a row inserted in the wrong place
    // shifts every string after it and the whole UI reads one label off with nothing to say so.
    // Restore is the last enumerator and the last row, so pinning it along with the first and two
    // in between catches such a shift even if the compile-time order guard is ever relaxed.
    CHECK(text(Text::Restore) == L"Restore");
}

TEST_CASE("strings: the russian table differs from the english one everywhere it should") {
    LanguageRestored const restored;

    auto const en = collectAll(LanguagePreference::English);
    auto const ru = collectAll(LanguagePreference::Russian);

    // This is what makes a missing translation impossible: an English literal copy-pasted into
    // the ru column compiles, passes both static_asserts, and ships English into a Russian UI.
    for (std::size_t i = 0; i < kTextCount; ++i) {
        CAPTURE(i);
        if (isIdenticalInBothLanguages(textAt(i))) {
            CHECK(en[i] == ru[i]);
        } else {
            CHECK(en[i] != ru[i]);
        }
    }
}

TEST_CASE("strings: every translated string is actually russian") {
    LanguageRestored const restored;
    setLanguage(LanguagePreference::Russian);

    // Catches what the previous case cannot: a different English string typed into the ru column,
    // say "Keep history" against "Keep history for". The two columns differ, so the case above is
    // satisfied, and a Russian user still reads English.
    for (std::size_t i = 0; i < kTextCount; ++i) {
        Text const id = textAt(i);
        if (isIdenticalInBothLanguages(id)) {
            continue;
        }
        CAPTURE(i);
        CHECK(hasCyrillic(text(id)));
    }
}

TEST_CASE("strings: no english string is cyrillic except the language endonym") {
    LanguageRestored const restored;
    setLanguage(LanguagePreference::English);

    for (std::size_t i = 0; i < kTextCount; ++i) {
        Text const id = textAt(i);
        if (id == Text::LanguageRussian) {
            continue;
        }
        CAPTURE(i);
        CHECK_FALSE(hasCyrillic(text(id)));
    }

    // The one deliberate exception, and the mirror of English staying English in the Russian
    // column: a language list is drawn in each language's own script.
    CHECK(hasCyrillic(text(Text::LanguageRussian)));
}

TEST_CASE("strings: only the deliberate aliases share wording") {
    LanguageRestored const restored;

    auto const aliases = aliasesAsIndexGroups();

    for (auto const preference : {LanguagePreference::English, LanguagePreference::Russian}) {
        auto const all = collectAll(preference);

        for (auto const& alias : aliases) {
            for (std::size_t const index : alias) {
                CAPTURE(index);
                CHECK(all[index] == all[alias.front()]);
            }
        }

        // The failure this closes is a half-finished row: a new enumerator whose two columns
        // were copied wholesale from a neighbour. It is non-empty, its columns differ from each
        // other, it is Cyrillic where it should be -- and the user gets a control captioned with
        // somebody else's words. An eighth collision has to be an explicit edit to the list.
        std::size_t shared = 0;
        for (auto const& group : groupsOfEqualStrings(all)) {
            if (group.size() == 1) {
                continue;
            }
            ++shared;
            CAPTURE(group.front());
            CHECK(std::find(aliases.begin(), aliases.end(), group) != aliases.end());
        }
        CHECK(shared == aliases.size());
    }
}

TEST_CASE("strings: the two languages agree on which texts are the same text") {
    LanguageRestored const restored;

    auto const en = firstIdWithSameText(collectAll(LanguagePreference::English));
    auto const ru = firstIdWithSameText(collectAll(LanguagePreference::Russian));

    // A copy-paste that only half happened: two rows given the same English literal but different
    // Russian ones, or a translation that merges two ids the English keeps apart, so a Russian
    // user cannot tell the low-battery event from the critical one while an English user can.
    // Unlike the case above this needs no maintenance when a string is added.
    for (std::size_t i = 0; i < kTextCount; ++i) {
        CAPTURE(i);
        CHECK(en[i] == ru[i]);
    }
}

TEST_CASE("strings: only the texts with a formatText caller carry placeholders") {
    LanguageRestored const restored;

    // text() returns the raw row with no substitution, so a {} in a label nobody formats renders
    // as braces on screen; dropping one while editing a format string makes the version number or
    // the drain rate vanish from the UI with no error anywhere.
    for (auto const preference : {LanguagePreference::English, LanguagePreference::Russian}) {
        auto const all = collectAll(preference);
        for (std::size_t i = 0; i < kTextCount; ++i) {
            CAPTURE(i);
            CHECK(placeholderCount(all[i]) == expectedPlaceholders(textAt(i)));
            CHECK_FALSE(hasLoneBrace(all[i]));
        }
    }
}

TEST_CASE("strings: a translation keeps the placeholder count of its english original") {
    LanguageRestored const restored;

    auto const en = collectAll(LanguagePreference::English);
    auto const ru = collectAll(LanguagePreference::Russian);

    // vformat throws the moment a runtime format string names an argument the caller did not
    // pass, so a translation with one placeholder too many is a crash only Russian users ever
    // hit, on whichever screen owns that string.
    for (std::size_t i = 0; i < kTextCount; ++i) {
        CAPTURE(i);
        CHECK(placeholderCount(en[i]) == placeholderCount(ru[i]));
    }
}

TEST_CASE("strings: every entry is a format string vformat can consume") {
    LanguageRestored const restored;

    // A stray brace in a newly added translation throws when that one page is drawn, in one
    // language, and nowhere else -- a defect that reaches a user before it reaches a developer.
    // Two arguments cover the widest entry, and std::format discards the ones a string does not
    // name.
    for (auto const preference : {LanguagePreference::English, LanguagePreference::Russian}) {
        setLanguage(preference);
        for (std::size_t i = 0; i < kTextCount; ++i) {
            CAPTURE(i);
            std::wstring formatted;
            CHECK_NOTHROW(formatted = formatText(textAt(i), std::wstring(L"a"), 1));
            CHECK(formatted.find(L'{') == std::wstring::npos);
            CHECK(formatted.find(L'}') == std::wstring::npos);
        }
    }

    setLanguage(LanguagePreference::English);
    // The entry that looks dangerous and is not: a percent sign carries no meaning to
    // std::format, whatever it means to printf.
    CHECK(formatText(Text::UnitPercent, 1) == L"%");
}

TEST_CASE("strings: format arguments substitute into the english table") {
    LanguageRestored const restored;
    setLanguage(LanguagePreference::English);

    // Same argument types as the real call sites pass, so the instantiations tested here are the
    // ones that ship: an already formatted duration and drain rate, a raw minute count, a device
    // name, a battery level.
    CHECK(formatText(Text::AboutVersion, std::wstring(L"1.4.2")) == L"Version 1.4.2");
    CHECK(formatText(Text::UpdatedMinutesAgo, 7) == L"Updated 7 min ago");
    CHECK(formatText(Text::TrayMoreControllers, 3) == L"and 3 more");
    CHECK(formatText(Text::EstimatedRemaining, std::wstring(L"2 h 15 min")) ==
          L"About 2 h 15 min left");
    CHECK(formatText(Text::DrainRate, std::wstring(L"4.5")) == L"4.5% per hour");
    CHECK(formatText(Text::ToastConnected, std::wstring(L"Xbox Wireless Controller")) ==
          L"Xbox Wireless Controller connected");

    // The separator is U+2014 EM DASH rather than a hyphen, written as an escape so the
    // expectation holds however this file is decoded. Two arguments in a fixed order: swapped,
    // the toast reads "78 - Xbox Wireless Controller".
    CHECK(formatText(Text::ToastBodyLevel, std::wstring(L"Xbox Wireless Controller"), 78) ==
          L"Xbox Wireless Controller \u2014 78%");
    CHECK(formatText(Text::ToastBodyNoLevel, std::wstring(L"Xbox Wireless Controller")) ==
          L"Xbox Wireless Controller");
}

TEST_CASE("strings: format arguments substitute into the russian table") {
    LanguageRestored const restored;
    setLanguage(LanguagePreference::Russian);

    // Asserted by position rather than by wording, and the positions are the ones the English
    // strings never exercise: were formatText ever reduced to an append or a fixed splice, a
    // placeholder at either extreme is what would expose it.
    auto const version = formatText(Text::AboutVersion, std::wstring(L"1.4.2"));
    CHECK(version.ends_with(L"1.4.2"));
    CHECK(version.find(L'{') == std::wstring::npos);
    CHECK(version.find(L'}') == std::wstring::npos);

    auto const toast = formatText(Text::ToastConnected, std::wstring(L"Xbox Wireless Controller"));
    CHECK(toast.starts_with(L"Xbox Wireless Controller"));
    // Real words follow the name here, so an empty tail would mean the Russian row never loaded.
    CHECK(toast != L"Xbox Wireless Controller");
    CHECK(toast.find(L'{') == std::wstring::npos);
    CHECK(toast.find(L'}') == std::wstring::npos);

    auto const body = formatText(Text::ToastBodyNoLevel, std::wstring(L"Xbox Wireless Controller"));
    CHECK(body == L"Xbox Wireless Controller");
}

TEST_CASE("strings: a missing format argument throws instead of leaking braces") {
    LanguageRestored const restored;
    setLanguage(LanguagePreference::English);

    CHECK_THROWS_AS(formatText(Text::AboutVersion), std::format_error);

    // The half the sweeping cases lean on: a surplus argument is discarded rather than appended,
    // which is the only reason handing two arguments to every entry in the table means anything.
    CHECK(formatText(Text::AboutVersion, std::wstring(L"1.4.2"), 99) == L"Version 1.4.2");
    CHECK(formatText(Text::Refresh, 1) == L"Refresh");
}

TEST_CASE("strings: no entry carries stray leading or trailing whitespace") {
    LanguageRestored const restored;

    // Several entries are written as two or three adjacent literals wrapped over source lines,
    // which is exactly where a space gets doubled or dropped: "...can " "actually draw" works
    // only because the space sits inside the first fragment. A leading space misaligns a label
    // against every other control in its column, a trailing one opens a gap before a colon.
    for (auto const preference : {LanguagePreference::English, LanguagePreference::Russian}) {
        auto const all = collectAll(preference);
        for (std::size_t i = 0; i < kTextCount; ++i) {
            CAPTURE(i);
            CHECK_FALSE(hasEdgeWhitespace(all[i]));
        }
    }
}

TEST_CASE("strings: setLanguage swaps the whole table and swaps back") {
    LanguageRestored const restored;

    auto const first = collectAll(LanguagePreference::English);
    auto const russian = collectAll(LanguagePreference::Russian);
    auto const again = collectAll(LanguagePreference::English);

    // The window is rebuilt from the table whenever the user picks a language. If a switch were
    // one-way, or cached anything per string, a user who tries Russian and changes their mind
    // would be stuck with it until they restarted.
    CHECK(first != russian);
    CHECK(first == again);

    setLanguage(LanguagePreference::Russian);
    CHECK(text(Text::NavDevices) != L"Controllers");
    setLanguage(LanguagePreference::English);
    CHECK(text(Text::NavDevices) == L"Controllers");
}

TEST_CASE("strings: the system preference resolves to one concrete table") {
    LanguageRestored const restored;

    auto const en = collectAll(LanguagePreference::English);
    auto const ru = collectAll(LanguagePreference::Russian);
    auto const system = collectAll(LanguagePreference::System);

    // System is the shipped default and so the path almost every user takes. Which of the two it
    // lands on depends on the display language of the machine running this, so the assertion is
    // that it landed on one of them whole: half-applying would come up with labels drawn from
    // two languages at once.
    CHECK((system == en || system == ru));
}

TEST_CASE("strings: the system preference ignores the language set before it") {
    LanguageRestored const restored;

    setLanguage(LanguagePreference::Russian);
    setLanguage(LanguagePreference::System);
    auto const afterRussian = std::wstring(text(Text::NavDevices));

    setLanguage(LanguagePreference::English);
    setLanguage(LanguagePreference::System);
    auto const afterEnglish = std::wstring(text(Text::NavDevices));

    // System is the one arm of the switch that falls out of the block instead of returning.
    // Turn that break into a return -- an easy edit when a fourth language arrives -- and the
    // global keeps whatever it held, so a user on Russian who switches to System sees nothing
    // change and concludes the setting is broken. Which language the two agree on is the
    // machine's business, and never asserted.
    CHECK(afterRussian == afterEnglish);
}

TEST_CASE("strings: a view taken before a switch keeps pointing at static storage") {
    LanguageRestored const restored;
    setLanguage(LanguagePreference::English);

    std::wstring_view const captured = text(Text::NavDevices);
    CHECK(captured == L"Controllers");

    setLanguage(LanguagePreference::Russian);

    // Both halves of the accessor's contract, which callers depend on in opposite ways: the view
    // is into string literals, so it never dangles and can go straight into a format call or a
    // wstring constructor -- but it does go stale, so any UI holding one has to re-read it after
    // the language changes.
    CHECK(captured == L"Controllers");
    CHECK(text(Text::NavDevices) != captured);
}
