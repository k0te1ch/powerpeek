#include "TestSupport.h"

#include "core/Settings.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

using peek::BackdropMode;
using peek::EventSettings;
using peek::LanguagePreference;
using peek::NotificationEvent;
using peek::Settings;
using peek::ThemePreference;
using peek::TrayColor;
using peek::TrayStyle;
using peek::test::TempDir;

void writeText(std::filesystem::path const& file, std::string_view text) {
    std::ofstream stream(file, std::ios::binary);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string readText(std::filesystem::path const& file) {
    std::ifstream stream(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// Hand-written documents all go through one name, so each test reads as "this text on disk
// produces these settings" rather than as file plumbing.
Settings loadDocument(TempDir const& dir, std::string_view document) {
    std::filesystem::path const file = dir.file(L"settings.json");
    writeText(file, document);
    return Settings::load(file);
}

void checkEventMatches(EventSettings const& actual, EventSettings const& expected) {
    CHECK(actual.enabled == expected.enabled);
    CHECK(actual.playSound == expected.playSound);
    CHECK(actual.showFlyout == expected.showFlyout);
    CHECK(actual.showSystemToast == expected.showSystemToast);
    CHECK(actual.soundFile == expected.soundFile);
    CHECK(actual.volume == doctest::Approx(expected.volume));
}

void checkSettingsMatch(Settings const& actual, Settings const& expected) {
    CHECK(actual.startWithWindows == expected.startWithWindows);
    CHECK(actual.startMinimised == expected.startMinimised);
    CHECK(actual.minimiseToTrayOnClose == expected.minimiseToTrayOnClose);
    CHECK(actual.includeNonXboxGamepads == expected.includeNonXboxGamepads);
    CHECK(actual.pollIntervalSeconds == expected.pollIntervalSeconds);
    CHECK(actual.lowThresholdPercent == expected.lowThresholdPercent);
    CHECK(actual.criticalThresholdPercent == expected.criticalThresholdPercent);
    CHECK(actual.notificationCooldownMinutes == expected.notificationCooldownMinutes);
    CHECK(actual.theme == expected.theme);
    CHECK(actual.language == expected.language);
    CHECK(actual.trayStyle == expected.trayStyle);
    CHECK(actual.trayColor == expected.trayColor);
    CHECK(actual.backdrop == expected.backdrop);
    CHECK(actual.windowOpacity == doctest::Approx(expected.windowOpacity));
    CHECK(actual.masterVolume == doctest::Approx(expected.masterVolume));
    CHECK(actual.historyEnabled == expected.historyEnabled);
    CHECK(actual.historyRetentionDays == expected.historyRetentionDays);

    for (std::size_t i = 0; i < peek::kNotificationEventCount; ++i) {
        CAPTURE(i);
        checkEventMatches(actual.events[i], expected.events[i]);
    }
}

}  // namespace

TEST_CASE("settings: the defaults match what the header documents") {
    Settings const settings{};

    CHECK(settings.version == 1);

    CHECK_FALSE(settings.startWithWindows);
    CHECK(settings.startMinimised);
    CHECK(settings.minimiseToTrayOnClose);
    // Third-party pads report battery through the same APIs but lie about the level, so
    // they stay out of the list until the user opts in.
    CHECK_FALSE(settings.includeNonXboxGamepads);

    CHECK(settings.pollIntervalSeconds == 30);
    CHECK(settings.lowThresholdPercent == 20);
    CHECK(settings.criticalThresholdPercent == 10);
    CHECK(settings.notificationCooldownMinutes == 30);

    CHECK(settings.theme == ThemePreference::System);
    CHECK(settings.language == LanguagePreference::System);
    CHECK(settings.trayStyle == TrayStyle::Battery);
    CHECK(settings.trayColor == TrayColor::Auto);

    // The backdrop is opt-in: nothing about the shipped window changes until it is chosen.
    CHECK(settings.backdrop == BackdropMode::Opaque);
    CHECK(settings.windowOpacity == doctest::Approx(1.0f));

    CHECK(settings.masterVolume == doctest::Approx(0.8f));

    CHECK(settings.historyEnabled);
    CHECK(settings.historyRetentionDays == 30);
}

TEST_CASE("settings: the window opacity floor is a contrast floor") {
    // White body text over a white desktop composites to roughly 6:1 at this alpha and to
    // roughly 3:1 at 0.5, so lowering the floor makes the title bar caption unreadable
    // rather than merely prettier.
    CHECK(peek::kMinimumWindowOpacity == doctest::Approx(0.7f));
}

TEST_CASE("settings: each event carries its own defaults") {
    Settings const settings{};

    SUBCASE("every event is announced in every way but a system toast") {
        for (std::size_t i = 0; i < peek::kNotificationEventCount; ++i) {
            CAPTURE(i);
            CHECK(settings.events[i].enabled);
            CHECK(settings.events[i].playSound);
            CHECK(settings.events[i].showFlyout);
            // Empty means the sound embedded in the executable.
            CHECK(settings.events[i].soundFile == L"");
        }
    }

    SUBCASE("only a critical battery reaches the Action Center") {
        // The one event a user must not miss because the flyout appeared while they were
        // away from the screen.
        CHECK(settings.forEvent(NotificationEvent::BatteryCritical).showSystemToast);

        CHECK_FALSE(settings.forEvent(NotificationEvent::Connected).showSystemToast);
        CHECK_FALSE(settings.forEvent(NotificationEvent::Disconnected).showSystemToast);
        CHECK_FALSE(settings.forEvent(NotificationEvent::BatteryLow).showSystemToast);
        CHECK_FALSE(settings.forEvent(NotificationEvent::FullyCharged).showSystemToast);
    }

    SUBCASE("the volumes are graded by how much the event matters") {
        CHECK(settings.forEvent(NotificationEvent::Connected).volume == doctest::Approx(0.7f));
        CHECK(settings.forEvent(NotificationEvent::Disconnected).volume == doctest::Approx(0.7f));
        CHECK(settings.forEvent(NotificationEvent::BatteryLow).volume == doctest::Approx(0.9f));
        // Critical is the only event left at full volume; defaultEvents does not touch it.
        CHECK(settings.forEvent(NotificationEvent::BatteryCritical).volume ==
              doctest::Approx(1.0f));
        CHECK(settings.forEvent(NotificationEvent::FullyCharged).volume == doctest::Approx(0.6f));
    }
}

TEST_CASE("settings: forEvent indexes the array in enum order") {
    // The order is load-bearing twice over: the built-in sound for an event is
    // IDW_SOUND_FIRST + index, and the settings file keys are matched by the same index.
    CHECK(peek::kNotificationEventCount == 5u);
    CHECK(peek::index(NotificationEvent::Connected) == 0u);
    CHECK(peek::index(NotificationEvent::Disconnected) == 1u);
    CHECK(peek::index(NotificationEvent::BatteryLow) == 2u);
    CHECK(peek::index(NotificationEvent::BatteryCritical) == 3u);
    CHECK(peek::index(NotificationEvent::FullyCharged) == 4u);

    Settings settings;
    settings.forEvent(NotificationEvent::BatteryLow).volume = 0.25f;
    CHECK(settings.events[2].volume == doctest::Approx(0.25f));
}

TEST_CASE("settings: every event has its own display name") {
    // Distinctness holds in both shipped languages, so this catches a mis-ordered name
    // table without pinning the test to whichever language ran last.
    std::wstring_view const connected = peek::displayName(NotificationEvent::Connected);
    std::wstring_view const disconnected = peek::displayName(NotificationEvent::Disconnected);
    std::wstring_view const low = peek::displayName(NotificationEvent::BatteryLow);
    std::wstring_view const critical = peek::displayName(NotificationEvent::BatteryCritical);
    std::wstring_view const charged = peek::displayName(NotificationEvent::FullyCharged);

    CHECK_FALSE(connected.empty());
    CHECK(connected != disconnected);
    CHECK(disconnected != low);
    CHECK(low != critical);
    CHECK(critical != charged);
    CHECK(charged != connected);
}

TEST_CASE("settings: a missing file yields the defaults") {
    TempDir dir;
    Settings const settings = Settings::load(dir.file(L"nothing-here.json"));

    // Losing settings must never stop the application from starting.
    checkSettingsMatch(settings, Settings{});
}

TEST_CASE("settings: an empty file yields the defaults") {
    TempDir dir;
    checkSettingsMatch(loadDocument(dir, ""), Settings{});
}

TEST_CASE("settings: a malformed file yields the defaults") {
    TempDir dir;

    SUBCASE("a truncated document") {
        checkSettingsMatch(loadDocument(dir, "{\"pollIntervalSeconds\": 45"), Settings{});
    }

    SUBCASE("a trailing comma, which a hand-edit leaves behind") {
        checkSettingsMatch(loadDocument(dir, "{\"pollIntervalSeconds\": 45,}"), Settings{});
    }

    SUBCASE("plain text") {
        checkSettingsMatch(loadDocument(dir, "not json at all"), Settings{});
    }

    SUBCASE("valid prefix followed by rubbish") {
        checkSettingsMatch(loadDocument(dir, "{\"pollIntervalSeconds\": 45} trailing"),
                           Settings{});
    }
}

TEST_CASE("settings: a document that is not an object yields the defaults") {
    TempDir dir;

    SUBCASE("an array") {
        checkSettingsMatch(loadDocument(dir, "[1, 2, 3]"), Settings{});
    }

    SUBCASE("a bare number") {
        checkSettingsMatch(loadDocument(dir, "42"), Settings{});
    }

    SUBCASE("a null document, which parses cleanly and still has no keys") {
        checkSettingsMatch(loadDocument(dir, "null"), Settings{});
    }
}

TEST_CASE("settings: an empty object yields the defaults") {
    TempDir dir;
    checkSettingsMatch(loadDocument(dir, "{}"), Settings{});
}

TEST_CASE("settings: a byte order mark and CRLF endings are tolerated") {
    TempDir dir;

    // Notepad writes both the moment a user hand-edits the file, and JSON itself has no
    // place for a BOM.
    std::string const document = "\xEF\xBB\xBF{\r\n  \"pollIntervalSeconds\": 45\r\n}\r\n";
    CHECK(loadDocument(dir, document).pollIntervalSeconds == 45);
}

TEST_CASE("settings: a newer file version is left alone and the defaults are used") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"settings.json");

    std::string const document = R"json({
  "version": 99,
  "pollIntervalSeconds": 45,
  "startWithWindows": true
})json";
    writeText(file, document);

    Settings const settings = Settings::load(file);

    // An older build must not read a newer file's keys with older meanings.
    CHECK(settings.pollIntervalSeconds == 30);
    CHECK_FALSE(settings.startWithWindows);

    // Nor may reading it rewrite it: the newer build's file has to survive intact.
    CHECK(readText(file) == document);
}

TEST_CASE("settings: a version that is not newer is read normally") {
    TempDir dir;

    SUBCASE("the current version") {
        CHECK(loadDocument(dir, R"json({"version": 1, "pollIntervalSeconds": 45})json")
                  .pollIntervalSeconds == 45);
    }

    SUBCASE("an older version, which a migration would handle") {
        CHECK(loadDocument(dir, R"json({"version": 0, "pollIntervalSeconds": 45})json")
                  .pollIntervalSeconds == 45);
    }

    SUBCASE("a missing version, which is assumed to be the current one") {
        CHECK(loadDocument(dir, R"json({"pollIntervalSeconds": 45})json").pollIntervalSeconds ==
              45);
    }

    SUBCASE("a version of the wrong type, which cannot be compared") {
        CHECK(loadDocument(dir, R"json({"version": "99", "pollIntervalSeconds": 45})json")
                  .pollIntervalSeconds == 45);
    }
}

TEST_CASE("settings: the loaded version is always this build's version") {
    TempDir dir;

    // load never copies the file's version into the struct, so the field can only ever
    // mean "what this build writes", never "what was read".
    CHECK(loadDocument(dir, R"json({"version": 0})json").version == 1);
}

TEST_CASE("settings: unknown keys are ignored without damage") {
    TempDir dir;

    std::string const document = R"json({
  "version": 1,
  "pollIntervalSeconds": 45,
  "colourOfTheBikeshed": "green",
  "futureBlock": {"nested": [1, 2, 3]},
  "events": {
    "connected": {"volume": 0.5, "nonsense": true},
    "notAnEvent": {"volume": 0.1}
  }
})json";

    Settings const settings = loadDocument(dir, document);

    CHECK(settings.pollIntervalSeconds == 45);
    CHECK(settings.forEvent(NotificationEvent::Connected).volume == doctest::Approx(0.5f));
    CHECK(settings.forEvent(NotificationEvent::Disconnected).volume == doctest::Approx(0.7f));
    CHECK(settings.masterVolume == doctest::Approx(0.8f));
}

TEST_CASE("settings: a value of the wrong type falls back to the default") {
    TempDir dir;

    // A settings file is user-editable, so a wrong type is expected input rather than a
    // reason to throw the whole file away.
    std::string const document = R"json({
  "startWithWindows": 1,
  "minimiseToTrayOnClose": 0,
  "pollIntervalSeconds": "45",
  "windowOpacity": true,
  "masterVolume": null,
  "theme": 2,
  "trayStyle": ["ring"]
})json";

    Settings const settings = loadDocument(dir, document);

    // A JSON number is not a JSON bool, so neither 1 nor 0 may flip a switch.
    CHECK_FALSE(settings.startWithWindows);
    CHECK(settings.minimiseToTrayOnClose);

    CHECK(settings.pollIntervalSeconds == 30);
    CHECK(settings.windowOpacity == doctest::Approx(1.0f));
    CHECK(settings.masterVolume == doctest::Approx(0.8f));
    CHECK(settings.theme == ThemePreference::System);
    CHECK(settings.trayStyle == TrayStyle::Battery);
}

TEST_CASE("settings: the poll interval is clamped to a sane band") {
    TempDir dir;

    SUBCASE("below the floor") {
        CHECK(loadDocument(dir, R"json({"pollIntervalSeconds": 1})json").pollIntervalSeconds == 5);
    }

    SUBCASE("zero, which would spin the poll loop") {
        CHECK(loadDocument(dir, R"json({"pollIntervalSeconds": 0})json").pollIntervalSeconds == 5);
    }

    SUBCASE("negative") {
        CHECK(loadDocument(dir, R"json({"pollIntervalSeconds": -30})json").pollIntervalSeconds ==
              5);
    }

    SUBCASE("above the ceiling") {
        CHECK(loadDocument(dir, R"json({"pollIntervalSeconds": 100000})json")
                  .pollIntervalSeconds == 3600);
    }

    SUBCASE("the bounds themselves are left alone") {
        CHECK(loadDocument(dir, R"json({"pollIntervalSeconds": 5})json").pollIntervalSeconds == 5);
        CHECK(loadDocument(dir, R"json({"pollIntervalSeconds": 3600})json").pollIntervalSeconds ==
              3600);
    }
}

TEST_CASE("settings: a number too large for an int falls back rather than clamps") {
    TempDir dir;

    // The read gives up before the clamp ever sees the value, so an absurd entry lands on
    // the default rather than on the ceiling.
    CHECK(loadDocument(dir, R"json({"pollIntervalSeconds": 1e300})json").pollIntervalSeconds ==
          30);
    CHECK(loadDocument(dir, R"json({"historyRetentionDays": -1e300})json").historyRetentionDays ==
          30);
}

TEST_CASE("settings: a fractional number is rounded to the nearest whole one") {
    TempDir dir;

    // JSON has one numeric type, so an integer setting written back with a decimal point
    // still has to read as that integer rather than fall back to the default.
    CHECK(loadDocument(dir, R"json({"pollIntervalSeconds": 45.0})json").pollIntervalSeconds == 45);
    CHECK(loadDocument(dir, R"json({"pollIntervalSeconds": 45.6})json").pollIntervalSeconds == 46);
}

TEST_CASE("settings: the thresholds are clamped to a usable band") {
    TempDir dir;

    SUBCASE("both above the ceiling") {
        Settings const settings = loadDocument(
            dir, R"json({"lowThresholdPercent": 200, "criticalThresholdPercent": 300})json");
        CHECK(settings.lowThresholdPercent == 95);
        // Clamped to the same ceiling, then pushed one below it by the ordering rule.
        CHECK(settings.criticalThresholdPercent == 94);
    }

    SUBCASE("critical below the floor") {
        Settings const settings = loadDocument(
            dir, R"json({"lowThresholdPercent": 30, "criticalThresholdPercent": -4})json");
        CHECK(settings.lowThresholdPercent == 30);
        CHECK(settings.criticalThresholdPercent == 1);
    }
}

TEST_CASE("settings: the critical threshold is forced below the low one") {
    TempDir dir;

    // Equal or inverted thresholds either never fire the critical alert or fire both
    // alerts on the same reading.
    SUBCASE("equal") {
        CHECK(loadDocument(dir,
                           R"json({"lowThresholdPercent": 20, "criticalThresholdPercent": 20})json")
                  .criticalThresholdPercent == 19);
    }

    SUBCASE("inverted") {
        CHECK(loadDocument(dir,
                           R"json({"lowThresholdPercent": 20, "criticalThresholdPercent": 50})json")
                  .criticalThresholdPercent == 19);
    }

    SUBCASE("already ordered, so nothing moves") {
        Settings const settings = loadDocument(
            dir, R"json({"lowThresholdPercent": 40, "criticalThresholdPercent": 15})json");
        CHECK(settings.lowThresholdPercent == 40);
        CHECK(settings.criticalThresholdPercent == 15);
    }

    SUBCASE("lowering only the low threshold drags the default critical one down with it") {
        Settings const settings = loadDocument(dir, R"json({"lowThresholdPercent": 5})json");
        CHECK(settings.lowThresholdPercent == 5);
        CHECK(settings.criticalThresholdPercent == 4);
    }
}

TEST_CASE("settings: the notification cooldown is clamped") {
    TempDir dir;

    SUBCASE("zero is allowed and means no cooldown") {
        CHECK(loadDocument(dir, R"json({"notificationCooldownMinutes": 0})json")
                  .notificationCooldownMinutes == 0);
    }

    SUBCASE("negative") {
        CHECK(loadDocument(dir, R"json({"notificationCooldownMinutes": -5})json")
                  .notificationCooldownMinutes == 0);
    }

    SUBCASE("beyond a day") {
        CHECK(loadDocument(dir, R"json({"notificationCooldownMinutes": 100000})json")
                  .notificationCooldownMinutes == 1440);
    }
}

TEST_CASE("settings: the history retention window is clamped") {
    TempDir dir;

    SUBCASE("zero, which would discard every sample as it arrives") {
        CHECK(loadDocument(dir, R"json({"historyRetentionDays": 0})json").historyRetentionDays ==
              1);
    }

    SUBCASE("beyond a year") {
        CHECK(loadDocument(dir, R"json({"historyRetentionDays": 10000})json")
                  .historyRetentionDays == 365);
    }
}

TEST_CASE("settings: the window opacity is held above the contrast floor") {
    TempDir dir;

    SUBCASE("below the floor") {
        CHECK(loadDocument(dir, R"json({"windowOpacity": 0.1})json").windowOpacity ==
              doctest::Approx(peek::kMinimumWindowOpacity));
    }

    SUBCASE("just below the floor") {
        CHECK(loadDocument(dir, R"json({"windowOpacity": 0.69})json").windowOpacity ==
              doctest::Approx(peek::kMinimumWindowOpacity));
    }

    SUBCASE("above one") {
        CHECK(loadDocument(dir, R"json({"windowOpacity": 2.5})json").windowOpacity ==
              doctest::Approx(1.0f));
    }

    SUBCASE("inside the band") {
        CHECK(loadDocument(dir, R"json({"windowOpacity": 0.85})json").windowOpacity ==
              doctest::Approx(0.85f));
    }
}

TEST_CASE("settings: the volumes are clamped to the unit interval") {
    TempDir dir;

    SUBCASE("the master volume") {
        CHECK(loadDocument(dir, R"json({"masterVolume": -1.0})json").masterVolume ==
              doctest::Approx(0.0f));
        CHECK(loadDocument(dir, R"json({"masterVolume": 5.0})json").masterVolume ==
              doctest::Approx(1.0f));
        CHECK(loadDocument(dir, R"json({"masterVolume": 0.35})json").masterVolume ==
              doctest::Approx(0.35f));
    }

    SUBCASE("a per-event volume") {
        CHECK(loadDocument(dir, R"json({"events": {"batteryLow": {"volume": -2.0}}})json")
                  .forEvent(NotificationEvent::BatteryLow)
                  .volume == doctest::Approx(0.0f));
        CHECK(loadDocument(dir, R"json({"events": {"batteryLow": {"volume": 4.0}}})json")
                  .forEvent(NotificationEvent::BatteryLow)
                  .volume == doctest::Approx(1.0f));
    }
}

TEST_CASE("settings: enumerations are read by name") {
    TempDir dir;

    Settings const settings = loadDocument(dir, R"json({
  "theme": "dark",
  "language": "russian",
  "trayStyle": "percentage",
  "trayColor": "pink",
  "backdrop": "mica"
})json");

    CHECK(settings.theme == ThemePreference::Dark);
    CHECK(settings.language == LanguagePreference::Russian);
    CHECK(settings.trayStyle == TrayStyle::Percentage);
    CHECK(settings.trayColor == TrayColor::Pink);
    CHECK(settings.backdrop == BackdropMode::Mica);
}

TEST_CASE("settings: an unrecognised enumeration name keeps the default") {
    TempDir dir;

    SUBCASE("a name that no longer exists") {
        CHECK(loadDocument(dir, R"json({"trayColor": "chartreuse"})json").trayColor ==
              TrayColor::Auto);
    }

    SUBCASE("the empty string") {
        CHECK(loadDocument(dir, R"json({"backdrop": ""})json").backdrop == BackdropMode::Opaque);
    }

    SUBCASE("the wrong case, because the names are matched byte for byte") {
        CHECK(loadDocument(dir, R"json({"theme": "Dark"})json").theme == ThemePreference::System);
    }
}

TEST_CASE("settings: a missing events block leaves every event at its default") {
    TempDir dir;

    Settings expected{};
    expected.masterVolume = 0.5f;

    SUBCASE("no events key at all") {
        checkSettingsMatch(loadDocument(dir, R"json({"masterVolume": 0.5})json"), expected);
    }

    SUBCASE("an events key that is not an object") {
        checkSettingsMatch(loadDocument(dir, R"json({"masterVolume": 0.5, "events": "none"})json"),
                           expected);
    }
}

TEST_CASE("settings: an event entry that is not an object is skipped") {
    TempDir dir;

    Settings const settings =
        loadDocument(dir, R"json({"events": {"batteryLow": 5, "connected": null}})json");

    CHECK(settings.forEvent(NotificationEvent::BatteryLow).volume == doctest::Approx(0.9f));
    CHECK(settings.forEvent(NotificationEvent::BatteryLow).enabled);
    CHECK(settings.forEvent(NotificationEvent::Connected).volume == doctest::Approx(0.7f));
}

TEST_CASE("settings: each file key belongs to exactly one event") {
    TempDir dir;

    // A shift in this table would silently move a user's critical-battery sound onto some
    // other event, which is the sort of thing nobody notices until the battery dies.
    Settings const settings = loadDocument(dir, R"json({
  "events": {
    "connected": {"volume": 0.11},
    "disconnected": {"volume": 0.22},
    "batteryLow": {"volume": 0.33},
    "batteryCritical": {"volume": 0.44},
    "fullyCharged": {"volume": 0.55}
  }
})json");

    CHECK(settings.forEvent(NotificationEvent::Connected).volume == doctest::Approx(0.11f));
    CHECK(settings.forEvent(NotificationEvent::Disconnected).volume == doctest::Approx(0.22f));
    CHECK(settings.forEvent(NotificationEvent::BatteryLow).volume == doctest::Approx(0.33f));
    CHECK(settings.forEvent(NotificationEvent::BatteryCritical).volume == doctest::Approx(0.44f));
    CHECK(settings.forEvent(NotificationEvent::FullyCharged).volume == doctest::Approx(0.55f));
}

TEST_CASE("settings: a partial event block keeps the fields it does not mention") {
    TempDir dir;

    Settings const settings =
        loadDocument(dir, R"json({"events": {"batteryCritical": {"enabled": false}}})json");

    EventSettings const& critical = settings.forEvent(NotificationEvent::BatteryCritical);
    CHECK_FALSE(critical.enabled);

    // Turning the event off in an old file must not also strip the Action Center entry
    // that this event alone is given.
    CHECK(critical.showSystemToast);
    CHECK(critical.playSound);
    CHECK(critical.showFlyout);
    CHECK(critical.volume == doctest::Approx(1.0f));
}

TEST_CASE("settings: an event field of the wrong type keeps the default") {
    TempDir dir;

    Settings const settings = loadDocument(dir, R"json({
  "events": {
    "batteryCritical": {"showSystemToast": "no", "soundFile": 12, "volume": "loud"}
  }
})json");

    EventSettings const& critical = settings.forEvent(NotificationEvent::BatteryCritical);
    CHECK(critical.showSystemToast);
    // A number must not be stringified into a path that no file could ever have.
    CHECK(critical.soundFile == L"");
    CHECK(critical.volume == doctest::Approx(1.0f));
}

TEST_CASE("settings: a cleared sound file is written as an empty string") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"settings.json");

    REQUIRE(Settings{}.save(file));

    // Empty is how the file says "use the sound embedded in the executable", so the key
    // has to be written rather than omitted and it has to stay a string.
    CHECK(readText(file).find("\"soundFile\": \"\"") != std::string::npos);
}

TEST_CASE("settings: save then load round-trips every field") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"settings.json");

    Settings written;
    written.startWithWindows = true;
    written.startMinimised = false;
    written.minimiseToTrayOnClose = false;
    written.includeNonXboxGamepads = true;
    written.pollIntervalSeconds = 120;
    written.lowThresholdPercent = 35;
    written.criticalThresholdPercent = 15;
    written.notificationCooldownMinutes = 5;
    written.theme = ThemePreference::Dark;
    written.language = LanguagePreference::Russian;
    written.trayStyle = TrayStyle::Percentage;
    written.trayColor = TrayColor::Pink;
    written.backdrop = BackdropMode::Mica;
    written.windowOpacity = 0.85f;
    written.masterVolume = 0.35f;
    written.historyEnabled = false;
    written.historyRetentionDays = 90;

    // Every event gets a different combination so that a swapped pair of file keys shows
    // up as a failure rather than as an accidental match.
    written.forEvent(NotificationEvent::Connected) = EventSettings{
        .enabled = false, .playSound = true, .showFlyout = false, .showSystemToast = true,
        .soundFile = L"one.wav", .volume = 0.10f};
    written.forEvent(NotificationEvent::Disconnected) = EventSettings{
        .enabled = true, .playSound = false, .showFlyout = true, .showSystemToast = false,
        .soundFile = L"two.wav", .volume = 0.20f};
    written.forEvent(NotificationEvent::BatteryLow) = EventSettings{
        .enabled = false, .playSound = false, .showFlyout = true, .showSystemToast = true,
        .soundFile = L"three.wav", .volume = 0.30f};
    written.forEvent(NotificationEvent::BatteryCritical) = EventSettings{
        .enabled = true, .playSound = true, .showFlyout = false, .showSystemToast = false,
        .soundFile = L"four.wav", .volume = 0.40f};
    written.forEvent(NotificationEvent::FullyCharged) = EventSettings{
        .enabled = false, .playSound = true, .showFlyout = true, .showSystemToast = false,
        .soundFile = L"five.wav", .volume = 0.50f};

    REQUIRE(written.save(file));
    checkSettingsMatch(Settings::load(file), written);
}

TEST_CASE("settings: the defaults survive a round trip") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"settings.json");

    Settings const written{};
    REQUIRE(written.save(file));
    checkSettingsMatch(Settings::load(file), written);
}

TEST_CASE("settings: every enumerator survives a round trip") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"settings.json");

    // Storing enums by name is what lets the enums be reordered later without changing
    // what a user's existing file means, so every name has to survive the trip.
    SUBCASE("theme") {
        for (ThemePreference const value :
             {ThemePreference::System, ThemePreference::Light, ThemePreference::Dark}) {
            Settings written;
            written.theme = value;
            REQUIRE(written.save(file));
            CHECK(Settings::load(file).theme == value);
        }
    }

    SUBCASE("language") {
        for (LanguagePreference const value : {LanguagePreference::System,
                                               LanguagePreference::English,
                                               LanguagePreference::Russian}) {
            Settings written;
            written.language = value;
            REQUIRE(written.save(file));
            CHECK(Settings::load(file).language == value);
        }
    }

    SUBCASE("tray style") {
        for (TrayStyle const value :
             {TrayStyle::Battery, TrayStyle::Ring, TrayStyle::Percentage}) {
            Settings written;
            written.trayStyle = value;
            REQUIRE(written.save(file));
            CHECK(Settings::load(file).trayStyle == value);
        }
    }

    SUBCASE("tray colour") {
        for (TrayColor const value : {TrayColor::Auto, TrayColor::Accent, TrayColor::White,
                                      TrayColor::Green, TrayColor::Blue, TrayColor::Pink}) {
            Settings written;
            written.trayColor = value;
            REQUIRE(written.save(file));
            CHECK(Settings::load(file).trayColor == value);
        }
    }

    SUBCASE("backdrop") {
        for (BackdropMode const value : {BackdropMode::Opaque, BackdropMode::Blur,
                                         BackdropMode::Acrylic, BackdropMode::Mica}) {
            Settings written;
            written.backdrop = value;
            REQUIRE(written.save(file));
            CHECK(Settings::load(file).backdrop == value);
        }
    }
}

TEST_CASE("settings: a non-ASCII sound path survives a round trip") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"settings.json");

    // Spelled with escapes so the test does not depend on how the compiler reads this
    // file's own encoding. The path is written to the file as raw UTF-8 rather than as
    // \u escapes, which is what keeps a Russian sound name readable in it.
    std::wstring const path = L"C:\\Sounds\\\u0422\u0435\u0441\u0442.wav";

    Settings written;
    written.forEvent(NotificationEvent::FullyCharged).soundFile = path;
    REQUIRE(written.save(file));

    CHECK(Settings::load(file).forEvent(NotificationEvent::FullyCharged).soundFile == path);
}

TEST_CASE("settings: save always writes this build's version") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"settings.json");

    Settings written;
    written.version = 99;
    written.pollIntervalSeconds = 90;
    REQUIRE(written.save(file));

    // The struct field is not what decides the on-disk version. Were it written through,
    // the next load would see a version from the future and throw the whole file away.
    CHECK(readText(file).find("\"version\": 1") != std::string::npos);
    CHECK(Settings::load(file).pollIntervalSeconds == 90);
}

TEST_CASE("settings: save leaves no temporary behind") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"settings.json");
    std::filesystem::path temp = file;
    temp += L".tmp";

    SUBCASE("the first save, which has no file to replace") {
        REQUIRE(Settings{}.save(file));
        CHECK(std::filesystem::exists(file));
        CHECK_FALSE(std::filesystem::exists(temp));
    }

    SUBCASE("a later save, which replaces the file in place") {
        REQUIRE(Settings{}.save(file));

        Settings written;
        written.pollIntervalSeconds = 90;
        REQUIRE(written.save(file));

        CHECK_FALSE(std::filesystem::exists(temp));
        CHECK(Settings::load(file).pollIntervalSeconds == 90);
    }
}

TEST_CASE("settings: save creates the directory it is given") {
    TempDir dir;
    std::filesystem::path const file = dir.path() / L"nested" / L"deeper" / L"settings.json";

    // The first run of a fresh install has no data folder yet.
    REQUIRE(Settings{}.save(file));
    CHECK(std::filesystem::exists(file));
    CHECK(Settings::load(file).pollIntervalSeconds == 30);
}

TEST_CASE("settings: save reports failure and writes nothing when the temporary cannot be made") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"settings.json");
    std::filesystem::path temp = file;
    temp += L".tmp";

    // A directory in the temporary's place makes the create fail the same way a locked or
    // read-only folder would.
    std::filesystem::create_directories(temp);
    REQUIRE(std::filesystem::is_directory(temp));

    CHECK_FALSE(Settings{}.save(file));

    // The point of writing through a temporary is that a failed save cannot leave a
    // half-written settings file in its place.
    CHECK_FALSE(std::filesystem::exists(file));
}

TEST_CASE("settings: the saved file is sorted, indented and newline-terminated") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"settings.json");

    REQUIRE(Settings{}.save(file));
    std::string const text = readText(file);

    // Sorted keys and two-space indentation are what make the file diffable and hand-
    // editable, which is the only reason it is JSON rather than a blob.
    CHECK(text.starts_with("{\n  \"backdrop\": \"opaque\","));

    REQUIRE_FALSE(text.empty());
    CHECK(text.back() == '\n');
}

TEST_CASE("settings: saving the same settings twice produces the same bytes") {
    TempDir dir;
    std::filesystem::path const first = dir.file(L"first.json");
    std::filesystem::path const second = dir.file(L"second.json");

    Settings written;
    written.trayColor = TrayColor::Blue;
    written.forEvent(NotificationEvent::BatteryLow).soundFile = L"alarm.wav";

    REQUIRE(written.save(first));
    REQUIRE(written.save(second));

    // Nothing about the writer may depend on the clock or on the machine, otherwise every
    // settings change would show up as a whole-file diff.
    CHECK(readText(first) == readText(second));
}

TEST_CASE("settings: unknown keys do not survive a save") {
    TempDir dir;
    std::filesystem::path const file = dir.file(L"settings.json");

    writeText(file, R"json({"version": 1, "pollIntervalSeconds": 45, "bikeshed": "green"})json");

    Settings const settings = Settings::load(file);
    REQUIRE(settings.save(file));

    std::string const text = readText(file);
    // The writer builds the document from the struct, so anything it does not know about
    // is dropped the first time the settings are written again.
    CHECK(text.find("bikeshed") == std::string::npos);
    CHECK(Settings::load(file).pollIntervalSeconds == 45);
}
