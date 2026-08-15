// What one line of the battery log means, when a reading is worth appending at all, and how a
// drain rate is derived from the samples that survive.
//
// The log is the only user data the application keeps between runs, and it is plain text sitting
// in a folder a user can open, so it has to read back whatever a text editor, a power cut or an
// older version left in it. The rate matters just as much: it becomes the "time remaining" line
// next to a controller, where a factor-of-sixty slip is visible to anyone looking at it.

#include "TestSupport.h"

#include "battery/BatteryHistory.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using peek::BatteryHistory;
using peek::ChargeState;
using peek::ControllerInfo;
using peek::HistorySample;
using peek::PowerSource;
using peek::test::makeController;
using peek::test::TempDir;

using Clock = std::chrono::system_clock;
using std::chrono::days;
using std::chrono::hours;
using std::chrono::minutes;

// The retention window is measured against the real clock, and nothing lets a test move that
// clock. Every case that is not about retention therefore opens the window wide enough that a
// fixture anchored in a fixed year cannot fall out of it.
constexpr days kAllHistory{36500};

// The on-disk form of a timestamp is a whole number of seconds, so an anchor carrying a finer
// part would come back from a reload short by a fraction and every equality below would fail.
Clock::time_point testAnchor() {
    return std::chrono::floor<std::chrono::seconds>(peek::test::testEpoch());
}

// within() and prune() both take their cutoff from the real clock and there is no seam to
// replace it, so the retention and prune cases build their instants as offsets from now. They
// assert which side of the cutoff a sample falls on and never an instant, so the outcome is the
// same on every run.
Clock::time_point realNow() {
    return Clock::now();
}

// Pins an instant to a whole, odd second. Whole because the log stores seconds; odd because the
// JSON writer emits the shortest form that round-trips, so a timestamp with trailing zeros --
// 1.8e9, say -- legitimately comes out in exponent form, which a byte comparison would miss.
Clock::time_point oddSecond(Clock::time_point when) {
    auto whole = std::chrono::duration_cast<std::chrono::seconds>(when.time_since_epoch());
    if (whole.count() % 2 == 0) {
        whole += std::chrono::seconds{1};
    }
    return Clock::time_point{std::chrono::duration_cast<Clock::duration>(whole)};
}

std::int64_t secondsSince1970(Clock::time_point when) {
    return std::chrono::duration_cast<std::chrono::seconds>(when.time_since_epoch()).count();
}

// One record in the shape the writer produces: the keys ascend because the JSON object is a
// std::map, and there is no whitespace anywhere because the dump indent is zero.
std::string logLine(std::string_view id, int percent, std::string_view charge,
                    Clock::time_point when) {
    return std::format(R"({{"c":"{}","id":"{}","p":{},"t":{}}})", charge, id, percent,
                       secondsSince1970(when));
}

void writeText(std::filesystem::path const& file, std::string_view text) {
    std::ofstream stream(file, std::ios::binary);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void writeLog(std::filesystem::path const& file, std::initializer_list<std::string> lines) {
    std::string text;
    for (std::string const& line : lines) {
        text += line;
        text.push_back('\n');
    }
    writeText(file, text);
}

std::string readText(std::filesystem::path const& file) {
    std::ifstream stream(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// The writer terminates every record with a newline, so counting what getline yields counts
// records rather than counting a trailing terminator as an empty final line.
std::vector<std::string> readLines(std::filesystem::path const& file) {
    std::vector<std::string> lines;
    std::ifstream stream(file, std::ios::binary);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

// percent is a std::uint8_t, which a stream prints as a character; comparing it as a number is
// what makes a failure read "expected 78, got 77" instead of two pieces of punctuation.
int level(HistorySample const& sample) {
    return static_cast<int>(sample.percent);
}

}  // namespace

TEST_CASE("batteryHistory: a missing log file loads as an empty history") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");

    BatteryHistory history{file};

    CHECK(history.samplesFor(L"pad-1").empty());
    CHECK_FALSE(history.drainPercentPerHour(L"pad-1").has_value());

    // A user who never plugs a pad in must not find a zero-byte log in their data folder, and
    // the History page has to show its empty state rather than fail to start.
    CHECK_FALSE(std::filesystem::exists(file));
}

TEST_CASE("batteryHistory: an out-of-order log is served oldest first") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    writeLog(file, {logLine("pad-1", 80, "discharging", anchor + hours{2}),
                    logLine("pad-1", 100, "discharging", anchor),
                    logLine("pad-1", 90, "discharging", anchor + hours{1})});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 3u);

    // The chart draws the vector in the order it is handed, and the drain walk takes the last
    // entry as the newest reading; an unsorted file would double the line back on itself and
    // measure the slope between the wrong pair.
    CHECK(samples[0].when == anchor);
    CHECK(samples[1].when == anchor + hours{1});
    CHECK(samples[2].when == anchor + hours{2});

    CHECK(level(samples[0]) == 100);
    CHECK(level(samples[1]) == 90);
    CHECK(level(samples[2]) == 80);
}

TEST_CASE("batteryHistory: a corrupt or truncated line is skipped and the rest of the log "
          "survives") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    std::string text;
    text += logLine("pad-1", 90, "discharging", anchor);
    text += "\n";
    text += "this is not json\n";
    text += logLine("pad-1", 80, "discharging", anchor + hours{1});
    text += "\n";
    // A record cut off mid-key is what a power cut during an append leaves behind.
    text += R"({"c":"discharging","id":"pad-1","p":70,)";
    text += "\n";
    // The last record deliberately has no terminator, which is the other half of the same
    // accident: getline still yields it and it still has to parse.
    text += logLine("pad-1", 60, "discharging", anchor + hours{2});
    writeText(file, text);

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 3u);
    CHECK(level(samples[0]) == 90);
    CHECK(level(samples[1]) == 80);
    CHECK(level(samples[2]) == 60);
}

TEST_CASE("batteryHistory: a line with no usable id is not a sample") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    std::int64_t const stamp = secondsSince1970(testAnchor());

    std::string line;

    SUBCASE("no id key at all") {
        line = std::format(R"({{"p":50,"c":"discharging","t":{}}})", stamp);
    }

    SUBCASE("an id that is present but empty") {
        line = std::format(R"({{"id":"","p":50,"c":"discharging","t":{}}})", stamp);
    }

    SUBCASE("an id that is a number rather than a string") {
        line = std::format(R"({{"id":123,"p":50,"c":"discharging","t":{}}})", stamp);
    }

    writeLog(file, {line});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    // Such a sample could never be shown -- every reader filters on a controller id -- but it
    // would still take up a slot in memory and be rewritten by every prune from now on.
    CHECK(history.samplesFor(L"pad-1").empty());
    CHECK(history.samplesFor(L"").empty());
}

TEST_CASE("batteryHistory: a byte order mark, CRLF endings and blank lines still load") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    // Exactly what a user gets back after opening the log in Notepad to look at it and saving.
    std::string text;
    text += "\xEF\xBB\xBF";
    text += logLine("pad-1", 90, "discharging", anchor);
    text += "\r\n\r\n";
    text += logLine("pad-1", 80, "discharging", anchor + hours{1});
    text += "\r\n";
    writeText(file, text);

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 2u);
    CHECK(level(samples[0]) == 90);
    CHECK(level(samples[1]) == 80);
}

TEST_CASE("batteryHistory: an unknown charge word reads back as unknown") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    std::int64_t const stamp = secondsSince1970(testAnchor());

    std::string line;

    SUBCASE("a word this build has never written") {
        line = std::format(R"({{"c":"asleep","id":"pad-1","p":50,"t":{}}})", stamp);
    }

    SUBCASE("no charge key at all") {
        line = std::format(R"({{"id":"pad-1","p":50,"t":{}}})", stamp);
    }

    writeLog(file, {line});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 1u);

    // Unknown ends a discharge run, so a state word added by a later version degrades to "no
    // estimate" on this build rather than to a wrong one.
    CHECK(samples[0].charge == ChargeState::Unknown);
    CHECK(level(samples[0]) == 50);
}

TEST_CASE("batteryHistory: a level outside 0..100 in the file is clamped") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    writeLog(file, {logLine("pad-1", 250, "discharging", anchor),
                    logLine("pad-2", -40, "discharging", anchor)});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    std::vector<HistorySample> const first = history.samplesFor(L"pad-1");
    REQUIRE(first.size() == 1u);
    CHECK(level(first[0]) == 100);

    // The field is a std::uint8_t: without the clamp, -40 would wrap to 216 and the chart would
    // rescale its axis to fit a level no battery can hold.
    std::vector<HistorySample> const second = history.samplesFor(L"pad-2");
    REQUIRE(second.size() == 1u);
    CHECK(level(second[0]) == 0);
}

TEST_CASE("batteryHistory: a reading is only appended when the level or the charge state "
          "changed") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    ControllerInfo controller = makeController(L"pad-1", 78);
    controller.lastUpdate = anchor;
    history.record(controller);

    controller.lastUpdate = anchor + minutes{5};
    history.record(controller);

    controller.percent = 77;
    controller.lastUpdate = anchor + minutes{10};
    history.record(controller);

    // Every poll records every controller, so without this guard a pad idling at 78% would cost
    // a line every few seconds and the log would grow by megabytes a day.
    CHECK(readLines(file).size() == 2u);

    std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 2u);
    CHECK(level(samples[0]) == 78);
    CHECK(level(samples[1]) == 77);
}

TEST_CASE("batteryHistory: the same level on a different charge state is a new sample") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    ControllerInfo controller = makeController(L"pad-1", 78, ChargeState::Discharging);
    controller.lastUpdate = anchor;
    history.record(controller);

    controller.charge = ChargeState::Charging;
    controller.lastUpdate = anchor + minutes{5};
    history.record(controller);

    controller.charge = ChargeState::Discharging;
    controller.lastUpdate = anchor + minutes{10};
    history.record(controller);

    CHECK(readLines(file).size() == 3u);

    // Cable in and cable out without the level moving is precisely the transition that ends one
    // discharge run and starts the next; deduping on the level alone would merge the two and
    // halve the drain the user is shown.
    std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 3u);
    CHECK(samples[0].charge == ChargeState::Discharging);
    CHECK(samples[1].charge == ChargeState::Charging);
    CHECK(samples[2].charge == ChargeState::Discharging);
    CHECK(level(samples[0]) == 78);
    CHECK(level(samples[1]) == 78);
    CHECK(level(samples[2]) == 78);
}

TEST_CASE("batteryHistory: each controller keeps its own last reading") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    ControllerInfo first = makeController(L"pad-1", 78);
    first.lastUpdate = anchor;
    history.record(first);

    ControllerInfo second = makeController(L"pad-2", 78);
    second.lastUpdate = anchor + minutes{1};
    history.record(second);

    first.lastUpdate = anchor + minutes{2};
    history.record(first);

    // Two identical pads sitting at the same level is the ordinary two-player case; a single
    // shared baseline would log only whichever pad the poll loop reached first.
    CHECK(readLines(file).size() == 2u);
    CHECK(history.samplesFor(L"pad-1").size() == 1u);
    CHECK(history.samplesFor(L"pad-2").size() == 1u);
}

TEST_CASE("batteryHistory: reopening the log does not re-append the last reading") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    ControllerInfo controller = makeController(L"pad-1", 78);
    controller.lastUpdate = anchor;

    {
        BatteryHistory history{file};
        history.setRetention(kAllHistory);
        history.record(controller);
        REQUIRE(readLines(file).size() == 1u);
    }

    BatteryHistory reopened{file};
    reopened.setRetention(kAllHistory);
    reopened.record(controller);

    // The baseline is seeded from the file at load, so restarting the application cannot plant
    // a duplicate at the current level and turn the chart into a stair of flat repeats.
    CHECK(readLines(file).size() == 1u);
    CHECK(reopened.samplesFor(L"pad-1").size() == 1u);
}

TEST_CASE("batteryHistory: a pad with no battery is never recorded") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    SUBCASE("a pad that reports no level") {
        history.record(makeController(L"pad-1", -1));
        CHECK_FALSE(std::filesystem::exists(file));
        CHECK(history.samplesFor(L"pad-1").empty());
    }

    SUBCASE("a pad running off the cable, which has no battery to log") {
        ControllerInfo controller = makeController(L"pad-1", 80);
        controller.source = PowerSource::Wired;
        history.record(controller);
        CHECK_FALSE(std::filesystem::exists(file));
        CHECK(history.samplesFor(L"pad-1").empty());
    }

    SUBCASE("a pad with no id to file the sample under") {
        history.record(makeController(L"", 80));
        CHECK_FALSE(std::filesystem::exists(file));
        CHECK(history.samplesFor(L"").empty());
    }

    SUBCASE("a pad whose power source could not be determined, which is still logged") {
        // The load-bearing case: a wireless pad with a pack the provider could not identify is
        // reported as Unknown rather than as Wired, and hasBattery tests "not wired" rather
        // than "on battery" exactly so those readings keep being recorded.
        ControllerInfo controller = makeController(L"pad-1", 80);
        controller.source = PowerSource::Unknown;
        history.record(controller);
        CHECK(std::filesystem::exists(file));
        CHECK(history.samplesFor(L"pad-1").size() == 1u);
    }
}

TEST_CASE("batteryHistory: a level above 100 is stored as 100") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    ControllerInfo controller = makeController(L"pad-1", 150);
    controller.lastUpdate = anchor;
    history.record(controller);

    controller.percent = 101;
    controller.lastUpdate = anchor + minutes{5};
    history.record(controller);

    // The clamp runs before the duplicate test, so a future provider reporting a raw ratio can
    // neither write a level the chart cannot scale nor defeat dedup by jittering above 100.
    std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 1u);
    CHECK(level(samples[0]) == 100);
}

TEST_CASE("batteryHistory: the sample carries the reading's own timestamp, to the second") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    ControllerInfo controller = makeController(L"pad-1", 78);
    controller.lastUpdate = anchor + hours{3};

    {
        BatteryHistory history{file};
        history.setRetention(kAllHistory);
        history.record(controller);

        // The chart plots this instant, so a sample has to be stamped when the pad was read
        // rather than when the log happened to be flushed.
        std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
        REQUIRE(samples.size() == 1u);
        CHECK(samples[0].when == anchor + hours{3});
    }

    BatteryHistory reopened{file};
    reopened.setRetention(kAllHistory);

    std::vector<HistorySample> const samples = reopened.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 1u);
    CHECK(samples[0].when == anchor + hours{3});
}

TEST_CASE("batteryHistory: a reading with no timestamp of its own is stamped now") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");

    ControllerInfo controller = makeController(L"pad-1", 78);
    controller.lastUpdate = {};

    BatteryHistory history{file};

    // Bracketed rather than pinned: the point is only that a provider which forgets to stamp a
    // reading still lands inside the retention window, instead of dating the sample 1970 and
    // having the next prune delete it.
    Clock::time_point const before = realNow();
    history.record(controller);
    Clock::time_point const after = realNow();

    std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 1u);
    CHECK(samples[0].when >= before);
    CHECK(samples[0].when <= after);
}

TEST_CASE("batteryHistory: a non-ascii controller id survives the round trip") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");

    // Spelled with escapes so the test does not depend on how the compiler reads this file's
    // own encoding. The conversions either side pin UTF-8 explicitly, so this has to hold on a
    // machine whose ANSI code page is not the Cyrillic one.
    std::wstring const id = L"\u0413\u0435\u0439\u043C\u043F\u0430\u0434-\u03A9";

    ControllerInfo controller = makeController(id, 78);
    controller.lastUpdate = testAnchor();

    {
        BatteryHistory history{file};
        history.setRetention(kAllHistory);
        history.record(controller);
    }

    BatteryHistory reopened{file};
    reopened.setRetention(kAllHistory);

    // A mojibake round trip would orphan the pad's whole history behind an id that no longer
    // matches the live controller.
    std::vector<HistorySample> const samples = reopened.samplesFor(id);
    REQUIRE(samples.size() == 1u);
    CHECK(samples[0].controllerId == id);
    CHECK(reopened.samplesFor(L"pad-1").empty());
}

TEST_CASE("batteryHistory: one recorded reading is one line of json") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");

    ControllerInfo controller = makeController(L"pad-1", 78, ChargeState::Discharging);
    controller.lastUpdate = Clock::time_point{std::chrono::seconds{1234567890}};

    BatteryHistory history{file};
    history.record(controller);

    // The on-disk shape is the compatibility surface between versions of the application:
    // renaming a key or indenting the output would make every existing user's history
    // unreadable after an upgrade, and it would fail silently, because a line that does not
    // decode is simply skipped. The timestamp is deliberately not a round number, since the
    // JSON writer emits the shortest form that round-trips and a round one would be written in
    // exponent form.
    std::vector<std::string> const lines = readLines(file);
    REQUIRE(lines.size() == 1u);
    CHECK(lines[0] == R"({"c":"discharging","id":"pad-1","p":78,"t":1234567890})");
}

TEST_CASE("batteryHistory: setEnabled(false) stops recording without touching the file") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    ControllerInfo controller = makeController(L"pad-1", 78);
    controller.lastUpdate = anchor;

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    history.setEnabled(false);
    history.record(controller);

    // Turning recording off is a request for nothing at all to reach the disk, not for a file
    // that keeps being created and emptied.
    CHECK_FALSE(std::filesystem::exists(file));
    CHECK(history.samplesFor(L"pad-1").empty());

    history.setEnabled(true);
    history.record(controller);

    CHECK(readLines(file).size() == 1u);
    CHECK(history.samplesFor(L"pad-1").size() == 1u);
}

TEST_CASE("batteryHistory: recording off still serves the samples already logged") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                    logLine("pad-1", 90, "discharging", anchor + hours{2})});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);
    history.setEnabled(false);

    // The setting is "record history", not "forget history": turning it off must not blank the
    // History page or the time-remaining line for data the user already has.
    CHECK(history.samplesFor(L"pad-1").size() == 2u);

    std::optional<double> const rate = history.drainPercentPerHour(L"pad-1");
    REQUIRE(rate.has_value());
    CHECK(*rate == doctest::Approx(5.0));
}

TEST_CASE("batteryHistory: samplesFor returns only the requested controller") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    ControllerInfo first = makeController(L"pad-1", 90);
    first.lastUpdate = anchor;
    history.record(first);

    ControllerInfo second = makeController(L"pad-2", 50);
    second.lastUpdate = anchor + minutes{30};
    history.record(second);

    first.percent = 80;
    first.lastUpdate = anchor + hours{1};
    history.record(first);

    // One chart is drawn for the controller picked from the dropdown; a second pad's levels
    // mixed into that series produce a sawtooth belonging to nobody.
    std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 2u);
    CHECK(samples[0].controllerId == L"pad-1");
    CHECK(samples[1].controllerId == L"pad-1");

    CHECK(history.samplesFor(L"pad-2").size() == 1u);
    CHECK(history.samplesFor(L"pad-3").empty());
}

TEST_CASE("batteryHistory: samplesFor drops samples older than the retention window") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const now = realNow();

    BatteryHistory history{file};

    ControllerInfo controller = makeController(L"pad-1", 90);
    controller.lastUpdate = now - days{40};
    history.record(controller);

    controller.percent = 80;
    controller.lastUpdate = now - hours{1};
    history.record(controller);

    SUBCASE("the default window, which is thirty days") {
        std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
        REQUIRE(samples.size() == 1u);
        CHECK(level(samples[0]) == 80);
    }

    SUBCASE("a window the user widened past the older sample") {
        history.setRetention(days{60});

        std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
        REQUIRE(samples.size() == 2u);
        CHECK(level(samples[0]) == 90);
        CHECK(level(samples[1]) == 80);
    }
}

TEST_CASE("batteryHistory: retention is never shorter than a day") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const now = realNow();

    BatteryHistory history{file};

    ControllerInfo controller = makeController(L"pad-1", 90);
    controller.lastUpdate = now - days{2};
    history.record(controller);

    controller.percent = 80;
    controller.lastUpdate = now - hours{2};
    history.record(controller);

    // The settings layer clamps this too, but the class has to defend itself: a zero window
    // puts the cutoff at this instant, so even the reading just taken would vanish and the
    // chart would be permanently empty.
    SUBCASE("zero days") { history.setRetention(days{0}); }
    SUBCASE("a negative number of days") { history.setRetention(days{-5}); }

    std::vector<HistorySample> const samples = history.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 1u);
    CHECK(level(samples[0]) == 80);
}

TEST_CASE("batteryHistory: drainPercentPerHour needs at least two samples") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    SUBCASE("an empty log") {
        BatteryHistory history{file};
        history.setRetention(kAllHistory);
        CHECK_FALSE(history.drainPercentPerHour(L"pad-1").has_value());
    }

    SUBCASE("a single reading, which is a point and not a slope") {
        writeLog(file, {logLine("pad-1", 90, "discharging", anchor)});

        BatteryHistory history{file};
        history.setRetention(kAllHistory);
        CHECK_FALSE(history.drainPercentPerHour(L"pad-1").has_value());
    }

    SUBCASE("a controller the log has never seen") {
        writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                        logLine("pad-1", 90, "discharging", anchor + hours{2})});

        BatteryHistory history{file};
        history.setRetention(kAllHistory);
        CHECK_FALSE(history.drainPercentPerHour(L"pad-2").has_value());
    }
}

TEST_CASE("batteryHistory: drainPercentPerHour is the slope of the current discharge run") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    ControllerInfo controller = makeController(L"pad-1", 100);
    controller.lastUpdate = anchor;
    history.record(controller);

    controller.percent = 90;
    controller.lastUpdate = anchor + hours{2};
    history.record(controller);

    // Rendered verbatim under the chart, so a slip between minutes and hours would show up as
    // "300 %/h" on a pad that is perfectly healthy.
    std::optional<double> const rate = history.drainPercentPerHour(L"pad-1");
    REQUIRE(rate.has_value());
    CHECK(*rate == doctest::Approx(5.0));
}

TEST_CASE("batteryHistory: drainPercentPerHour is nothing while the pad is charging") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    std::string newest;

    SUBCASE("the cable went in") {
        newest = logLine("pad-1", 85, "charging", anchor + hours{3});
    }

    SUBCASE("the pack has finished charging") {
        newest = logLine("pad-1", 100, "full", anchor + hours{3});
    }

    SUBCASE("the state could not be read") {
        newest = logLine("pad-1", 82, "unknown", anchor + hours{3});
    }

    writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                    logLine("pad-1", 90, "discharging", anchor + hours{1}),
                    logLine("pad-1", 80, "discharging", anchor + hours{2}), newest});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    // A pad on the cable has no meaningful time to empty, and quoting the drain it had before
    // it was plugged in would warn the user that a charging controller is about to die.
    CHECK_FALSE(history.drainPercentPerHour(L"pad-1").has_value());
}

TEST_CASE("batteryHistory: a charge in the middle of the log ends the run") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                    logLine("pad-1", 90, "discharging", anchor + hours{1}),
                    logLine("pad-1", 85, "charging", anchor + hours{2}),
                    logLine("pad-1", 80, "discharging", anchor + hours{3}),
                    logLine("pad-1", 70, "discharging", anchor + hours{5})});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    // Only the two samples after the top-up count. Averaging across the charge would mix a rise
    // into a fall and understate how fast the battery is going down now.
    std::optional<double> const rate = history.drainPercentPerHour(L"pad-1");
    REQUIRE(rate.has_value());
    CHECK(*rate == doctest::Approx(5.0));
}

TEST_CASE("batteryHistory: a rising level ends the run") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    SUBCASE("a fresh pack fitted deeper in the log") {
        writeLog(file, {logLine("pad-1", 30, "discharging", anchor),
                        logLine("pad-1", 100, "discharging", anchor + hours{1}),
                        logLine("pad-1", 90, "discharging", anchor + hours{3})});

        BatteryHistory history{file};
        history.setRetention(kAllHistory);

        // Swapping in charged cells is routine on an Xbox pad. Without the guard the 30-to-100
        // jump would fail the "the level must fall" test and the user would lose the estimate
        // altogether rather than get the rate of the pack now fitted.
        std::optional<double> const rate = history.drainPercentPerHour(L"pad-1");
        REQUIRE(rate.has_value());
        CHECK(*rate == doctest::Approx(5.0));
    }

    SUBCASE("a fresh pack fitted just now, which leaves a single sample") {
        writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                        logLine("pad-1", 50, "discharging", anchor + hours{1}),
                        logLine("pad-1", 60, "discharging", anchor + hours{2})});

        BatteryHistory history{file};
        history.setRetention(kAllHistory);

        CHECK_FALSE(history.drainPercentPerHour(L"pad-1").has_value());
    }
}

TEST_CASE("batteryHistory: the drain window is the four most recent edges") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                    logLine("pad-1", 90, "discharging", anchor + hours{10}),
                    logLine("pad-1", 80, "discharging", anchor + hours{11}),
                    logLine("pad-1", 70, "discharging", anchor + hours{12}),
                    logLine("pad-1", 60, "discharging", anchor + hours{13})});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    // The uneven spacing is the point: a pad left in a drawer for ten hours and then played for
    // three has to report the drain of the session. Taking the fifth edge in as well would
    // dilute it to roughly 3 %/h, so the two answers are easy to tell apart.
    std::optional<double> const rate = history.drainPercentPerHour(L"pad-1");
    REQUIRE(rate.has_value());
    CHECK(*rate == doctest::Approx(10.0));
}

TEST_CASE("batteryHistory: a run shorter than twenty minutes is not a rate") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    SUBCASE("two edges nineteen minutes apart") {
        writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                        logLine("pad-1", 90, "discharging", anchor + minutes{19})});

        BatteryHistory history{file};
        history.setRetention(kAllHistory);

        // A firmware level that steps from 100 to 90 shortly after the pad wakes would
        // otherwise be quoted as 300 %/h, and read back as twenty minutes left on a full pack.
        CHECK_FALSE(history.drainPercentPerHour(L"pad-1").has_value());
    }

    SUBCASE("two edges exactly twenty minutes apart, which is the boundary") {
        writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                        logLine("pad-1", 90, "discharging", anchor + minutes{20})});

        BatteryHistory history{file};
        history.setRetention(kAllHistory);

        std::optional<double> const rate = history.drainPercentPerHour(L"pad-1");
        REQUIRE(rate.has_value());
        CHECK(*rate == doctest::Approx(30.0));
    }

    SUBCASE("a short final gap inside a long enough run") {
        writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                        logLine("pad-1", 90, "discharging", anchor + minutes{25}),
                        logLine("pad-1", 80, "discharging", anchor + minutes{30})});

        BatteryHistory history{file};
        history.setRetention(kAllHistory);

        // The span measured is the whole run, not the five minutes between the last two edges.
        std::optional<double> const rate = history.drainPercentPerHour(L"pad-1");
        REQUIRE(rate.has_value());
        CHECK(*rate == doctest::Approx(40.0));
    }
}

TEST_CASE("batteryHistory: two readings at the same level are not a rate") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    // Only reachable through a hand-written file, since record() treats the second reading as a
    // duplicate of the first.
    writeLog(file, {logLine("pad-1", 50, "discharging", anchor),
                    logLine("pad-1", 50, "discharging", anchor + hours{2})});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    // A zero numerator would report 0 %/h, which reads as "this battery never runs down";
    // nothing at all correctly reads as "not enough data yet".
    CHECK_FALSE(history.drainPercentPerHour(L"pad-1").has_value());
}

TEST_CASE("batteryHistory: estimatedRemaining is the level divided by the drain rate") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                    logLine("pad-1", 50, "discharging", anchor + hours{5})});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    // The estimate is taken from the live reading rather than from the newest logged row, so a
    // pad read at half must be quoted five hours whatever the log's last line happens to say.
    std::optional<minutes> const remaining =
        history.estimatedRemaining(makeController(L"pad-1", 50));
    REQUIRE(remaining.has_value());
    CHECK(*remaining == minutes{300});
}

TEST_CASE("batteryHistory: estimatedRemaining is nothing for a pad that is not discharging") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                    logLine("pad-1", 50, "discharging", anchor + hours{5})});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    ControllerInfo controller = makeController(L"pad-1", 50);

    SUBCASE("on the cable") { controller.charge = ChargeState::Charging; }
    SUBCASE("charged") { controller.charge = ChargeState::Full; }
    SUBCASE("in a state the provider could not read") { controller.charge = ChargeState::Unknown; }
    SUBCASE("wired, so there is no battery to run down") {
        controller.source = PowerSource::Wired;
    }
    SUBCASE("reporting no level") { controller.percent = -1; }

    // "3 h 20 m remaining" beside a controller sitting on the charger is worse than no figure
    // at all.
    CHECK_FALSE(history.estimatedRemaining(controller).has_value());
}

TEST_CASE("batteryHistory: estimatedRemaining is nothing without a drain rate") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");

    SUBCASE("an empty log, which is every fresh install for its first few hours") {
        BatteryHistory history{file};
        history.setRetention(kAllHistory);
        CHECK_FALSE(history.estimatedRemaining(makeController(L"pad-1", 50)).has_value());
    }

    SUBCASE("a log holding a single reading") {
        writeLog(file, {logLine("pad-1", 50, "discharging", testAnchor())});

        BatteryHistory history{file};
        history.setRetention(kAllHistory);
        CHECK_FALSE(history.estimatedRemaining(makeController(L"pad-1", 50)).has_value());
    }
}

TEST_CASE("batteryHistory: an estimate beyond seventy-two hours is not shown") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    // Both logs give a rate of exactly one percent an hour, so the only thing separating the
    // two cases is which side of the ceiling the projection lands on.
    SUBCASE("exactly seventy-two hours, which is still shown") {
        writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                        logLine("pad-1", 72, "discharging", anchor + hours{28})});

        BatteryHistory history{file};
        history.setRetention(kAllHistory);

        std::optional<minutes> const remaining =
            history.estimatedRemaining(makeController(L"pad-1", 72));
        REQUIRE(remaining.has_value());
        CHECK(*remaining == minutes{4320});
    }

    SUBCASE("one hour past it, which is noise dressed up as a number") {
        writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                        logLine("pad-1", 73, "discharging", anchor + hours{27})});

        BatteryHistory history{file};
        history.setRetention(kAllHistory);

        CHECK_FALSE(history.estimatedRemaining(makeController(L"pad-1", 73)).has_value());
    }
}

TEST_CASE("batteryHistory: an empty pad gets no estimate") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    Clock::time_point const anchor = testAnchor();

    writeLog(file, {logLine("pad-1", 100, "discharging", anchor),
                    logLine("pad-1", 20, "discharging", anchor + hours{8})});

    BatteryHistory history{file};
    history.setRetention(kAllHistory);

    // A flat pad still counts as having a battery, so it reaches the arithmetic and truncates
    // to nothing. A live countdown of "0 minutes" is noise: the low-battery notification is the
    // surface that should be speaking by then.
    CHECK_FALSE(history.estimatedRemaining(makeController(L"pad-1", 0)).has_value());
}

TEST_CASE("batteryHistory: prune drops old samples and rewrites the file") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    std::filesystem::path temporary = file;
    temporary += L".tmp";

    Clock::time_point const recent = oddSecond(realNow() - hours{1});

    BatteryHistory history{file};

    ControllerInfo controller = makeController(L"pad-1", 90);
    controller.lastUpdate = recent - days{40};
    history.record(controller);

    controller.percent = 80;
    controller.lastUpdate = recent;
    history.record(controller);

    REQUIRE(readLines(file).size() == 2u);

    history.prune();

    // Pruning is the only thing that ever shrinks the log, and it works by writing a temporary
    // and renaming it over the original: a half-finished rename would either leave the file
    // growing forever or lose the whole history.
    std::vector<std::string> const lines = readLines(file);
    REQUIRE(lines.size() == 1u);
    CHECK(lines[0] == logLine("pad-1", 80, "discharging", recent));
    CHECK_FALSE(std::filesystem::exists(temporary));

    BatteryHistory reopened{file};
    std::vector<HistorySample> const samples = reopened.samplesFor(L"pad-1");
    REQUIRE(samples.size() == 1u);
    CHECK(level(samples[0]) == 80);
}

TEST_CASE("batteryHistory: prune leaves the file untouched when nothing is old enough") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");

    std::string text = logLine("pad-1", 80, "discharging", realNow() - hours{1});
    text += "\nnot a sample at all\n";
    writeText(file, text);

    BatteryHistory history{file};
    history.prune();

    // Prune runs at every start and after every settings change. Rewriting a file that needed
    // nothing costs a disk write per launch and turns any crash during the rename into data
    // loss for no gain -- and the surviving garbage line is the proof no rewrite happened, as a
    // rewrite emits only the lines that decoded.
    CHECK(readText(file) == text);
}

TEST_CASE("batteryHistory: prune does not create a file that was never there") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"history.jsonl");
    std::filesystem::path temporary = file;
    temporary += L".tmp";

    BatteryHistory history{file};
    history.prune();

    // Prune is called unconditionally at startup, so a first run -- or a run with recording
    // turned off -- must not leave an empty log or an orphaned temporary in the data folder.
    CHECK_FALSE(std::filesystem::exists(file));
    CHECK_FALSE(std::filesystem::exists(temporary));
}
