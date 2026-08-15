// The rules that decide whether the user is interrupted, driven one snapshot at a time.
//
// Almost everything the EventDetector does is a rule about what must *not* be announced: the
// same warning twice, a level crossed while the pad was unplugged, a connection that happened
// before the process started. None of that is visible in the shipped application until it
// misfires at three in the morning, and the detector takes its clock and its snapshots as
// arguments precisely so that every one of those rules can be checked here instead.

#include "TestSupport.h"

#include "battery/EventDetector.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

using peek::ChargeState;
using peek::ControllerInfo;
using peek::DetectedEvent;
using peek::EventDetector;
using peek::NotificationEvent;
using peek::PowerSource;
using peek::Settings;
using peek::test::makeController;
using peek::test::testEpoch;

// Minutes past the fixed epoch. Every case drives the clock by hand, because the cooldown is
// the only part of this unit that cannot be observed at all without two instants.
std::chrono::system_clock::time_point at(int minutes) {
    return testEpoch() + std::chrono::minutes{minutes};
}

// makeController always builds a battery-powered pad that says it is discharging, which is
// exactly the shape this unit acts on. The readings it has to ignore -- a wired pad with no
// level, a pad whose direction is unknown, an entry with no id -- have to be built by hand.
ControllerInfo makeRawController(std::wstring id, int percent, PowerSource source,
                                 ChargeState charge) {
    ControllerInfo controller;
    controller.name = id;
    controller.id = std::move(id);
    controller.percent = percent;
    controller.source = source;
    controller.charge = charge;
    controller.isXboxController = true;
    return controller;
}

// makeController names a pad after its own id, so a case asking which pad a notification
// describes has to give it a name that cannot be confused with one.
ControllerInfo named(ControllerInfo controller, std::wstring name) {
    controller.name = std::move(name);
    return controller;
}

// doctest cannot print an enumerator, so a mismatched event reports "{?} == {?}" and leaves
// the reader counting indices. Names go through the wide-string printer TestSupport installs
// and say which event actually turned up.
std::wstring eventName(NotificationEvent event) {
    switch (event) {
        case NotificationEvent::Connected:
            return L"Connected";
        case NotificationEvent::Disconnected:
            return L"Disconnected";
        case NotificationEvent::BatteryLow:
            return L"BatteryLow";
        case NotificationEvent::BatteryCritical:
            return L"BatteryCritical";
        case NotificationEvent::FullyCharged:
            return L"FullyCharged";
    }
    return L"<unknown>";
}

// The events one update produced, in order. REQUIRE on the count rather than CHECK because it
// unwinds the whole case: the cases that go on to index the vector for an id or a level rely
// on never reaching those lines when the count is wrong.
void checkEvents(std::vector<DetectedEvent> const& actual,
                 std::vector<NotificationEvent> const& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(eventName(actual[i].event) == eventName(expected[i]));
    }
}

}  // namespace

TEST_CASE("eventDetector: the first update only records a baseline") {
    Settings const settings{};
    EventDetector detector;

    // Deep inside critical and still silent: the pad was flat before the process started, and
    // with autostart on the alternative is a warning at every single login.
    std::vector<ControllerInfo> const snapshot{makeController(L"pad-a", 5)};

    checkEvents(detector.update(snapshot, settings, at(0)), {});
    // The baseline was recorded rather than merely skipped, so the same reading is still not
    // news a minute later.
    checkEvents(detector.update(snapshot, settings, at(1)), {});
}

TEST_CASE("eventDetector: the baseline is consumed even when the first snapshot is empty") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({}, settings, at(0)), {});
    // Starting with nothing plugged in is the ordinary case, and the first real connection of
    // the session would be swallowed if the baseline were only spent on a non-empty poll.
    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(1)),
                {NotificationEvent::Connected});
}

TEST_CASE("eventDetector: a pad first seen after the baseline announces a connection") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(0)), {});

    auto const events = detector.update(
        {makeController(L"pad-a", 50), makeController(L"pad-b", 50)}, settings, at(1));
    checkEvents(events, {NotificationEvent::Connected});
    // pad-a was there all along, so the single connection reported has to be the new pad.
    CHECK(events[0].controller.id == L"pad-b");
}

TEST_CASE("eventDetector: a pad that arrives already below the threshold warns once") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({}, settings, at(0)), {});

    // A pad that ran itself down while it was unplugged has never been warned about, so the
    // connection and the warning arrive together, in that order.
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(1)),
                {NotificationEvent::Connected, NotificationEvent::BatteryLow});
    // Once, though -- not on every poll for as long as the pad stays low.
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(2)), {});
}

TEST_CASE(
    "eventDetector: falling past both thresholds in one step reports only the critical event") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(0)), {});

    // Two toasts and two sounds on top of each other for one reading is the failure the
    // else-if chain exists to prevent.
    checkEvents(detector.update({makeController(L"pad-a", 5)}, settings, at(1)),
                {NotificationEvent::BatteryCritical});
}

TEST_CASE("eventDetector: a level exactly on a threshold counts as crossed") {
    Settings const settings{};
    EventDetector detector;

    // The shipped defaults: low at 20, critical at 10, and both comparisons are <=. An
    // off-by-one either warns a point early on every pad in the field or never warns a pad
    // that settles exactly on the number the user chose.
    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(0)), {});

    SUBCASE("on the low threshold") {
        checkEvents(detector.update({makeController(L"pad-a", 20)}, settings, at(1)),
                    {NotificationEvent::BatteryLow});
    }
    SUBCASE("on the critical threshold") {
        checkEvents(detector.update({makeController(L"pad-a", 10)}, settings, at(1)),
                    {NotificationEvent::BatteryCritical});
    }
    SUBCASE("one point above the low threshold") {
        checkEvents(detector.update({makeController(L"pad-a", 21)}, settings, at(1)), {});
    }
}

TEST_CASE("eventDetector: sinking further inside critical does not warn again") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 8)}, settings, at(1)),
                {NotificationEvent::BatteryCritical});
    // A dying pad polled every thirty seconds would otherwise fire all the way down to zero.
    checkEvents(detector.update({makeController(L"pad-a", 3)}, settings, at(2)), {});
}

TEST_CASE("eventDetector: climbing from critical back to merely low does not warn again") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 8)}, settings, at(1)),
                {NotificationEvent::BatteryCritical});
    // A coarse or noisy reading wobbling back up into the low band is not fresh news: the
    // user has already been told, and told something worse.
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(2)), {});
}

TEST_CASE("eventDetector: a pad hovering on the threshold notifies only once") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 19)}, settings, at(1)),
                {NotificationEvent::BatteryLow});
    // 21 is above the threshold but short of the recovery margin, so the crossing re-arms
    // while the latch stays put, and the window is what has to catch the repeat two minutes
    // later. Without it the user gets a toast and a chime every poll until the battery dies.
    checkEvents(detector.update({makeController(L"pad-a", 21)}, settings, at(2)), {});
    checkEvents(detector.update({makeController(L"pad-a", 19)}, settings, at(3)), {});
}

TEST_CASE("eventDetector: the warning returns once the cooldown window has passed") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    // The latch is written here, one minute past the epoch, and the default window is thirty
    // minutes; every timestamp below is measured against this one.
    checkEvents(detector.update({makeController(L"pad-a", 19)}, settings, at(1)),
                {NotificationEvent::BatteryLow});
    checkEvents(detector.update({makeController(L"pad-a", 21)}, settings, at(2)), {});

    SUBCASE("one minute short of the window") {
        checkEvents(detector.update({makeController(L"pad-a", 19)}, settings, at(30)), {});
    }
    SUBCASE("exactly on the window") {
        // The comparison is a strict <, so the thirtieth minute is out of the window rather
        // than the last one inside it. An accidental <= would hide right here.
        checkEvents(detector.update({makeController(L"pad-a", 19)}, settings, at(31)),
                    {NotificationEvent::BatteryLow});
    }
}

TEST_CASE("eventDetector: a cooldown of zero warns on every fresh crossing") {
    Settings settings{};
    EventDetector detector;

    SUBCASE("zero minutes") { settings.notificationCooldownMinutes = 0; }
    // A Settings built in code never goes through the file's clamp, and a negative duration
    // reaching std::chrono would suppress every rate-limited event forever.
    SUBCASE("a negative number of minutes") { settings.notificationCooldownMinutes = -5; }

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 19)}, settings, at(1)),
                {NotificationEvent::BatteryLow});
    checkEvents(detector.update({makeController(L"pad-a", 21)}, settings, at(2)), {});
    checkEvents(detector.update({makeController(L"pad-a", 19)}, settings, at(3)),
                {NotificationEvent::BatteryLow});
}

TEST_CASE("eventDetector: the recovery margin has to be cleared before the latch is dropped") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 20)}, settings, at(1)),
                {NotificationEvent::BatteryLow});

    SUBCASE("one point short of the margin") {
        // Recovery has to be worth more than five points and the comparison is strict, so 25
        // leaves the latch in place and the window still suppresses the return to 20.
        checkEvents(detector.update({makeController(L"pad-a", 25)}, settings, at(2)), {});
        checkEvents(detector.update({makeController(L"pad-a", 20)}, settings, at(3)), {});
    }
    SUBCASE("one point past the margin") {
        checkEvents(detector.update({makeController(L"pad-a", 26)}, settings, at(2)), {});
        // Three minutes into a thirty-minute window, so it is the recovery that dropped the
        // latch rather than the window expiring.
        checkEvents(detector.update({makeController(L"pad-a", 20)}, settings, at(3)),
                    {NotificationEvent::BatteryLow});
    }
}

TEST_CASE("eventDetector: charging clears the low latch so the next drop warns immediately") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(1)),
                {NotificationEvent::BatteryLow});
    checkEvents(detector.update({makeController(L"pad-a", 40, ChargeState::Charging)}, settings,
                                at(2)),
                {});

    // Someone who charges a pad and unplugs it again is starting a new discharge; without the
    // erase the previous session's window would silently eat this warning two minutes in.
    checkEvents(detector.update({makeController(L"pad-a", 18)}, settings, at(3)),
                {NotificationEvent::BatteryLow});
}

TEST_CASE("eventDetector: charging at the same level does not re-arm the warning") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(1)),
                {NotificationEvent::BatteryLow});
    checkEvents(detector.update({makeController(L"pad-a", 15, ChargeState::Charging)}, settings,
                                at(2)),
                {});

    // A cable brushed in and out, or a dock the pad rests on for a moment: the charging poll
    // recorded a level that is still low, so the pad recovered nothing worth warning about.
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(3)), {});
}

TEST_CASE("eventDetector: charging clears the critical latch as well") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 5)}, settings, at(1)),
                {NotificationEvent::BatteryCritical});
    checkEvents(detector.update({makeController(L"pad-a", 5, ChargeState::Charging)}, settings,
                                at(2)),
                {});
    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(3)), {});

    // Both latches go together: clearing only the low one would leave a pad that was charged
    // and drained again unable to raise the more important of the two warnings.
    checkEvents(detector.update({makeController(L"pad-a", 5)}, settings, at(4)),
                {NotificationEvent::BatteryCritical});
}

TEST_CASE("eventDetector: a discharging pad above every threshold stays silent") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 80)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 70)}, settings, at(1)), {});
    checkEvents(detector.update({makeController(L"pad-a", 60)}, settings, at(2)), {});
    // One point past the recovery margin, which drops latches rather than announcing
    // anything: nothing in the ordinary life of a healthy pad is worth a toast.
    checkEvents(detector.update({makeController(L"pad-a", 26)}, settings, at(3)), {});
}

TEST_CASE("eventDetector: a wired pad with no battery never warns") {
    Settings const settings{};
    EventDetector detector;

    // No level at all, which is what a wired pad reports. Reading that as an empty battery
    // would fire a critical warning at every wired pad on the machine.
    ControllerInfo const wired =
        makeRawController(L"pad-wired", -1, PowerSource::Wired, ChargeState::Unknown);

    checkEvents(detector.update({}, settings, at(0)), {});
    checkEvents(detector.update({wired}, settings, at(1)), {NotificationEvent::Connected});
    checkEvents(detector.update({wired}, settings, at(2)), {});
}

TEST_CASE("eventDetector: a level with an unknown charge state is ignored") {
    Settings const settings{};
    EventDetector detector;

    // The level is only trusted when the pad says it is draining it: a reading whose
    // direction is unknown must not be treated as a falling one.
    ControllerInfo const unknown =
        makeRawController(L"pad-a", 5, PowerSource::Battery, ChargeState::Unknown);

    checkEvents(detector.update({}, settings, at(0)), {});
    checkEvents(detector.update({unknown}, settings, at(1)), {NotificationEvent::Connected});
}

TEST_CASE("eventDetector: a full charge is announced on the transition only") {
    Settings const settings{};
    EventDetector detector;

    ControllerInfo const charging = makeController(L"pad-a", 90, ChargeState::Charging);
    ControllerInfo const full = makeController(L"pad-a", 100, ChargeState::Full);

    checkEvents(detector.update({charging}, settings, at(0)), {});
    checkEvents(detector.update({full}, settings, at(1)), {NotificationEvent::FullyCharged});
    // A pad left on the dock reports Full on every poll and would otherwise chime all night.
    checkEvents(detector.update({full}, settings, at(2)), {});
}

TEST_CASE("eventDetector: the full-charge announcement respects the cooldown") {
    Settings const settings{};
    EventDetector detector;

    ControllerInfo const charging = makeController(L"pad-a", 90, ChargeState::Charging);
    ControllerInfo const full = makeController(L"pad-a", 100, ChargeState::Full);
    ControllerInfo const discharging = makeController(L"pad-a", 100);

    checkEvents(detector.update({charging}, settings, at(0)), {});
    checkEvents(detector.update({full}, settings, at(1)), {NotificationEvent::FullyCharged});

    // A flaky charge contact offers the transition again and again; the window is the only
    // thing between that and a chime on every poll.
    checkEvents(detector.update({discharging}, settings, at(2)), {});
    checkEvents(detector.update({full}, settings, at(3)), {});

    // Thirty minutes after the announcement that was made rather than the one that was
    // dropped: a suppressed event that quietly extended its own window would push a
    // legitimate later announcement out indefinitely.
    checkEvents(detector.update({discharging}, settings, at(29)), {});
    checkEvents(detector.update({full}, settings, at(31)), {NotificationEvent::FullyCharged});
}

TEST_CASE("eventDetector: a full pad with no level still announces") {
    Settings const settings{};
    EventDetector detector;

    // A real reading: the provider reports a finished charge from the battery status and
    // fills the percentage in only when the capacity figures parse.
    ControllerInfo const charging =
        makeRawController(L"pad-a", -1, PowerSource::Battery, ChargeState::Charging);
    ControllerInfo const full =
        makeRawController(L"pad-a", -1, PowerSource::Battery, ChargeState::Full);

    checkEvents(detector.update({charging}, settings, at(0)), {});

    auto const events = detector.update({full}, settings, at(1));
    checkEvents(events, {NotificationEvent::FullyCharged});
    // The charge finishing is the news here, not the number.
    CHECK(events[0].controller.percent == -1);
}

TEST_CASE("eventDetector: a disconnection is announced once, carrying the last reading") {
    Settings const settings{};
    EventDetector detector;

    std::vector<ControllerInfo> const connected{named(makeController(L"pad-a", 37), L"Pad One")};

    checkEvents(detector.update(connected, settings, at(0)), {});

    // The pad is gone from the snapshot, so everything the toast can say about it comes out
    // of what the detector stored -- which is why it stores the whole reading.
    auto const events = detector.update({}, settings, at(1));
    checkEvents(events, {NotificationEvent::Disconnected});
    CHECK(events[0].controller.id == L"pad-a");
    CHECK(events[0].controller.name == L"Pad One");
    CHECK(events[0].controller.percent == 37);

    checkEvents(detector.update({}, settings, at(2)), {});
}

TEST_CASE("eventDetector: reconnecting a pad does not replay its old events") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(1)),
                {NotificationEvent::BatteryLow});
    checkEvents(detector.update({}, settings, at(2)), {NotificationEvent::Disconnected});

    // A pad that sleeps and wakes every few minutes comes back through this path. Only the
    // presence flag was cleared, so the level it left on still counts as warned about.
    SUBCASE("returning at the level it left on") {
        checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(3)),
                    {NotificationEvent::Connected});
    }
    SUBCASE("returning lower but still above critical") {
        checkEvents(detector.update({makeController(L"pad-a", 12)}, settings, at(3)),
                    {NotificationEvent::Connected});
    }
}

TEST_CASE("eventDetector: a pad that comes back worse than it left warns again") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(1)),
                {NotificationEvent::BatteryLow});
    checkEvents(detector.update({}, settings, at(2)), {NotificationEvent::Disconnected});

    // The no-replay rule is not a gag order: a pad that returns in genuinely worse shape than
    // it left is new information, and the critical warning has its own untouched latch.
    checkEvents(detector.update({makeController(L"pad-a", 5)}, settings, at(3)),
                {NotificationEvent::Connected, NotificationEvent::BatteryCritical});
}

TEST_CASE("eventDetector: connection events ignore the cooldown") {
    Settings settings{};
    // Four hours, so a rate-limited event would be suppressed at every step below.
    settings.notificationCooldownMinutes = 240;
    EventDetector detector;

    // Presence is a fact rather than a warning: a pad on a failing cable that drops and
    // returns must be shown as it is, not frozen in whichever state the window opened on.
    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(0)), {});
    checkEvents(detector.update({}, settings, at(1)), {NotificationEvent::Disconnected});
    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(2)),
                {NotificationEvent::Connected});
    checkEvents(detector.update({}, settings, at(3)), {NotificationEvent::Disconnected});
    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(4)),
                {NotificationEvent::Connected});
}

TEST_CASE("eventDetector: a disabled event is dropped and does not consume the latch") {
    Settings settings{};
    settings.forEvent(NotificationEvent::BatteryLow).enabled = false;
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(1)), {});

    settings.forEvent(NotificationEvent::BatteryLow).enabled = true;

    // Had the muted crossing latched at one minute past the epoch, this crossing would be two
    // minutes into a thirty-minute window and silent -- leaving the user unwarned for most of
    // an hour after asking for the warning back.
    checkEvents(detector.update({makeController(L"pad-a", 21)}, settings, at(2)), {});
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(3)),
                {NotificationEvent::BatteryLow});
}

TEST_CASE("eventDetector: disabling one event leaves the others alone") {
    Settings settings{};
    EventDetector detector;

    // Each toggle in the notifications page owns exactly one event, and a mis-indexed lookup
    // would mute the wrong one with nothing to show for it but a missing warning.
    SUBCASE("the low warning is muted, the critical one is not") {
        settings.forEvent(NotificationEvent::BatteryLow).enabled = false;
        checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(0)), {});
        checkEvents(detector.update({makeController(L"pad-a", 5)}, settings, at(1)),
                    {NotificationEvent::BatteryCritical});
    }
    SUBCASE("the connection chime is muted") {
        settings.forEvent(NotificationEvent::Connected).enabled = false;
        checkEvents(detector.update({}, settings, at(0)), {});
        checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(1)),
                    {NotificationEvent::BatteryLow});
    }
    SUBCASE("the disconnection chime is muted") {
        settings.forEvent(NotificationEvent::Disconnected).enabled = false;
        checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
        checkEvents(detector.update({}, settings, at(1)), {});
    }
}

TEST_CASE("eventDetector: a critical threshold above the low one collapses onto it") {
    Settings settings{};
    // Settings::load normalises these two against each other, but a Settings built in code
    // does not go through load, so the detector's own clamp is the last line of defence.
    settings.lowThresholdPercent = 20;
    settings.criticalThresholdPercent = 50;
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(0)), {});

    SUBCASE("on the collapsed threshold") {
        // Both branches would otherwise match this reading; the critical one wins the else-if.
        checkEvents(detector.update({makeController(L"pad-a", 20)}, settings, at(1)),
                    {NotificationEvent::BatteryCritical});
    }
    SUBCASE("one point above the collapsed threshold") {
        checkEvents(detector.update({makeController(L"pad-a", 21)}, settings, at(1)), {});
    }
}

TEST_CASE("eventDetector: thresholds outside 0 to 100 are clamped") {
    Settings settings{};
    EventDetector detector;

    // A corrupt or hand-edited file must not be able to push the comparison outside the range
    // a percentage can take, which would either mute the warnings or fire them on a full pad.
    SUBCASE("a negative low threshold means only a flat pad warns") {
        // The critical threshold is clamped into [0, low] in turn, so both collapse to zero.
        settings.lowThresholdPercent = -5;
        checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(0)), {});
        checkEvents(detector.update({makeController(L"pad-a", 1)}, settings, at(1)), {});
        checkEvents(detector.update({makeController(L"pad-a", 0)}, settings, at(2)),
                    {NotificationEvent::BatteryCritical});
    }
    SUBCASE("a low threshold above 100 makes any discharging level low") {
        settings.lowThresholdPercent = 150;
        checkEvents(detector.update({}, settings, at(0)), {});
        checkEvents(detector.update({makeController(L"pad-a", 100)}, settings, at(1)),
                    {NotificationEvent::Connected, NotificationEvent::BatteryLow});
    }
}

TEST_CASE("eventDetector: raising the threshold alone does not warn a pad already below it") {
    Settings settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(1)), {});

    settings.lowThresholdPercent = 40;

    // The remembered level is compared against the new threshold, so the pad reads as already
    // warned about and stays that way for as long as it is on. This is why reset() exists.
    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(2)), {});
}

TEST_CASE("eventDetector: reset re-arms the warning without announcing a connection") {
    Settings settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(1)), {});

    settings.lowThresholdPercent = 40;
    detector.reset();

    // Both halves at once: the warning the user just asked for has to fire, and the poll must
    // not also read as a reconnection for a pad that never went anywhere.
    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(2)),
                {NotificationEvent::BatteryLow});
}

TEST_CASE("eventDetector: reset does not reopen the baseline") {
    Settings const settings{};
    EventDetector detector;

    // Nothing is being tracked yet, so the loop body never runs: reset() has to survive that.
    detector.reset();

    // Had the baseline been reopened, the poll after every threshold change would report
    // nothing at all and the one after it would chime a connection for every pad connected.
    checkEvents(detector.update({makeController(L"pad-a", 5)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 5)}, settings, at(1)), {});
}

TEST_CASE("eventDetector: reset keeps the connected set") {
    Settings const settings{};
    EventDetector detector;

    std::vector<ControllerInfo> const connected{named(makeController(L"pad-a", 37), L"Pad One")};

    checkEvents(detector.update(connected, settings, at(0)), {});
    checkEvents(detector.update(connected, settings, at(1)), {});

    detector.reset();

    // A threshold change immediately followed by an unplug still has to report the unplug;
    // losing the presence flag would swallow it silently.
    auto const events = detector.update({}, settings, at(2));
    checkEvents(events, {NotificationEvent::Disconnected});
    CHECK(events[0].controller.id == L"pad-a");
    CHECK(events[0].controller.name == L"Pad One");
}

TEST_CASE("eventDetector: reset does not replay a full charge") {
    Settings const settings{};
    EventDetector detector;

    ControllerInfo const charging = makeController(L"pad-a", 90, ChargeState::Charging);
    ControllerInfo const full = makeController(L"pad-a", 100, ChargeState::Full);

    checkEvents(detector.update({charging}, settings, at(0)), {});
    checkEvents(detector.update({full}, settings, at(1)), {NotificationEvent::FullyCharged});

    detector.reset();

    // reset() is deliberately narrow: it re-opens the level-driven warnings and nothing else,
    // so a pad resting on the dock does not re-announce itself every time a threshold moves.
    checkEvents(detector.update({full}, settings, at(2)), {});
}

TEST_CASE("eventDetector: each controller keeps its own latches") {
    Settings const settings{};
    EventDetector detector;

    std::vector<ControllerInfo> const bothHealthy{makeController(L"pad-a", 50),
                                                  makeController(L"pad-b", 50)};
    std::vector<ControllerInfo> const aLow{makeController(L"pad-a", 15),
                                           makeController(L"pad-b", 50)};
    std::vector<ControllerInfo> const bothLow{makeController(L"pad-a", 15),
                                              makeController(L"pad-b", 15)};

    checkEvents(detector.update(bothHealthy, settings, at(0)), {});

    auto const first = detector.update(aLow, settings, at(1));
    checkEvents(first, {NotificationEvent::BatteryLow});
    CHECK(first[0].controller.id == L"pad-a");

    // Two pads on the same desk run down at different times: pad-a is quiet because its own
    // remembered level is already low, and pad-b is not muted by pad-a's latch.
    auto const second = detector.update(bothLow, settings, at(2));
    checkEvents(second, {NotificationEvent::BatteryLow});
    CHECK(second[0].controller.id == L"pad-b");
}

TEST_CASE("eventDetector: each event keeps its own latch") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 50)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(1)),
                {NotificationEvent::BatteryLow});

    // Two minutes into the low warning's thirty-minute window. A shared latch would let it
    // suppress the one warning the user must not miss.
    checkEvents(detector.update({makeController(L"pad-a", 8)}, settings, at(2)),
                {NotificationEvent::BatteryCritical});
}

TEST_CASE("eventDetector: disconnections are reported after the present pads, in id order") {
    Settings const settings{};
    EventDetector detector;

    std::vector<ControllerInfo> const three{makeController(L"pad-a", 30),
                                            makeController(L"pad-b", 30),
                                            makeController(L"pad-c", 30)};

    checkEvents(detector.update(three, settings, at(0)), {});

    // The notification queue plays these in the order they arrive, so a disconnection chime
    // ahead of the warning would bury the one the user actually needed to hear.
    auto const events = detector.update({makeController(L"pad-c", 15)}, settings, at(1));
    checkEvents(events, {NotificationEvent::BatteryLow, NotificationEvent::Disconnected,
                         NotificationEvent::Disconnected});
    CHECK(events[0].controller.id == L"pad-c");
    CHECK(events[1].controller.id == L"pad-a");
    CHECK(events[2].controller.id == L"pad-b");
}

TEST_CASE("eventDetector: a pad that appears and disappears between snapshots") {
    Settings const settings{};
    EventDetector detector;

    std::vector<ControllerInfo> const alone{makeController(L"pad-a", 30)};
    std::vector<ControllerInfo> const both{makeController(L"pad-a", 30),
                                           makeController(L"pad-b", 30)};

    checkEvents(detector.update(alone, settings, at(0)), {});

    auto const arrival = detector.update(both, settings, at(1));
    checkEvents(arrival, {NotificationEvent::Connected});
    CHECK(arrival[0].controller.id == L"pad-b");

    auto const departure = detector.update(alone, settings, at(2));
    checkEvents(departure, {NotificationEvent::Disconnected});
    CHECK(departure[0].controller.id == L"pad-b");

    // The commonest regression in a presence sweep: repeating the disconnection on every poll
    // for the rest of the session.
    checkEvents(detector.update(alone, settings, at(3)), {});
}

TEST_CASE("eventDetector: a pad whose id changes reads as a swap") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});

    // The id is what identifies a pad across polls. If a provider ever hands back a different
    // one for the same hardware, an honest disconnect and reconnect beats a silent swap.
    auto const events = detector.update({makeController(L"pad-b", 30)}, settings, at(1));
    checkEvents(events, {NotificationEvent::Connected, NotificationEvent::Disconnected});
    CHECK(events[0].controller.id == L"pad-b");
    CHECK(events[1].controller.id == L"pad-a");
}

TEST_CASE("eventDetector: an entry with no id is ignored") {
    Settings const settings{};
    EventDetector detector;

    // An entry that cannot be told apart from the next one would be tracked under the empty
    // string, connecting and disconnecting as unidentifiable entries came and went.
    ControllerInfo const nameless =
        makeRawController(L"", 5, PowerSource::Battery, ChargeState::Discharging);

    checkEvents(detector.update({}, settings, at(0)), {});

    SUBCASE("on its own") {
        checkEvents(detector.update({nameless}, settings, at(1)), {});
    }
    SUBCASE("alongside a real pad") {
        // Skipping it must not disturb the pads sharing the snapshot with it.
        auto const events =
            detector.update({nameless, makeController(L"pad-a", 15)}, settings, at(1));
        checkEvents(events, {NotificationEvent::Connected, NotificationEvent::BatteryLow});
        CHECK(events[0].controller.id == L"pad-a");
        CHECK(events[1].controller.id == L"pad-a");
    }
}

TEST_CASE("eventDetector: the same pad listed twice in one snapshot connects once") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({}, settings, at(0)), {});

    // The two providers are kept apart upstream today, but a snapshot that ever did carry one
    // pad twice would double every chime it earns unless the loop is idempotent within a poll.
    ControllerInfo const pad = makeController(L"pad-a", 15);
    checkEvents(detector.update({pad, pad}, settings, at(1)),
                {NotificationEvent::Connected, NotificationEvent::BatteryLow});
}

TEST_CASE("eventDetector: a live event carries this snapshot's reading") {
    Settings const settings{};
    EventDetector detector;

    checkEvents(detector.update({}, settings, at(0)), {});

    // The toast body prints this percentage. Shipping the stored reading instead would print
    // the level from the previous poll, which is the very number the warning is about.
    auto const events =
        detector.update({named(makeController(L"pad-a", 15), L"Pad One")}, settings, at(1));
    checkEvents(events, {NotificationEvent::Connected, NotificationEvent::BatteryLow});
    CHECK(events[1].controller.id == L"pad-a");
    CHECK(events[1].controller.name == L"Pad One");
    CHECK(events[1].controller.percent == 15);
}

TEST_CASE("eventDetector: lowering the threshold silences a pad that is now above it") {
    Settings settings{};
    EventDetector detector;

    checkEvents(detector.update({makeController(L"pad-a", 30)}, settings, at(0)), {});
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(1)),
                {NotificationEvent::BatteryLow});

    settings.lowThresholdPercent = 10;
    settings.criticalThresholdPercent = 5;

    // A user who decided 20% was too jumpy stops being warned at 15% on the very next poll,
    // without waiting for a reset or a restart.
    checkEvents(detector.update({makeController(L"pad-a", 15)}, settings, at(2)), {});
    checkEvents(detector.update({makeController(L"pad-a", 5)}, settings, at(3)),
                {NotificationEvent::BatteryCritical});
}
