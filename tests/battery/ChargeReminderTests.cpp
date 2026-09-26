// The unplug and charge reminders, driven by a hand-held clock.
//
// Both reminders are about nothing happening for half an hour, which is exactly the kind of
// rule that cannot be watched in the shipped application without waiting for it. The unit
// takes its clock as an argument, so every "thirty minutes later" below is one call.

#include "TestSupport.h"

#include "battery/ChargeReminder.h"

#include <chrono>
#include <vector>

namespace {

using peek::ChargeReminder;
using peek::ChargeState;
using peek::DetectedEvent;
using peek::DeviceInfo;
using peek::NotificationEvent;
using peek::Settings;
using peek::test::makeController;
using peek::test::testEpoch;

std::chrono::system_clock::time_point at(int minutes) {
    return testEpoch() + std::chrono::minutes{minutes};
}

Settings remindersOn() {
    Settings settings;
    settings.remindersEnabled = true;
    settings.reminderDelayMinutes = 30;
    return settings;
}

DeviceInfo full(wchar_t const* id) { return makeController(id, 100, ChargeState::Full); }

bool fired(std::vector<DetectedEvent> const& events, NotificationEvent event) {
    return events.size() == 1 && events.front().event == event;
}

}  // namespace

TEST_CASE("chargeReminder: a pad full on the charger is reminded once the delay has passed") {
    Settings const settings = remindersOn();
    ChargeReminder reminder;

    CHECK(reminder.update({full(L"pad-a")}, settings, at(0)).empty());
    CHECK(reminder.update({full(L"pad-a")}, settings, at(29)).empty());

    auto const events = reminder.update({full(L"pad-a")}, settings, at(30));
    REQUIRE(fired(events, NotificationEvent::UnplugReminder));
    CHECK(events.front().controller.id == L"pad-a");
}

TEST_CASE("chargeReminder: the unplug reminder fires once per charge cycle") {
    Settings const settings = remindersOn();
    ChargeReminder reminder;

    reminder.update({full(L"pad-a")}, settings, at(0));
    REQUIRE(fired(reminder.update({full(L"pad-a")}, settings, at(30)),
                  NotificationEvent::UnplugReminder));
    CHECK(reminder.update({full(L"pad-a")}, settings, at(90)).empty());

    SUBCASE("topping up from 99 % is still the same cycle") {
        reminder.update({makeController(L"pad-a", 99, ChargeState::Charging)}, settings, at(100));
        reminder.update({full(L"pad-a")}, settings, at(101));
        CHECK(reminder.update({full(L"pad-a")}, settings, at(200)).empty());
    }

    SUBCASE("running on the battery ends the cycle") {
        reminder.update({makeController(L"pad-a", 95)}, settings, at(100));
        reminder.update({full(L"pad-a")}, settings, at(200));
        CHECK(fired(reminder.update({full(L"pad-a")}, settings, at(230)),
                    NotificationEvent::UnplugReminder));
    }

    SUBCASE("going away ends the cycle") {
        reminder.update({}, settings, at(100));
        reminder.update({full(L"pad-a")}, settings, at(200));
        CHECK(fired(reminder.update({full(L"pad-a")}, settings, at(230)),
                    NotificationEvent::UnplugReminder));
    }
}

TEST_CASE("chargeReminder: charging at 100 % counts as full") {
    Settings const settings = remindersOn();
    ChargeReminder reminder;
    DeviceInfo const topped = makeController(L"pad-a", 100, ChargeState::Charging);

    reminder.update({topped}, settings, at(0));
    CHECK(fired(reminder.update({topped}, settings, at(30)), NotificationEvent::UnplugReminder));
}

TEST_CASE("chargeReminder: the delay restarts when the pad drops off full") {
    Settings const settings = remindersOn();
    ChargeReminder reminder;

    reminder.update({full(L"pad-a")}, settings, at(0));
    reminder.update({makeController(L"pad-a", 99, ChargeState::Charging)}, settings, at(20));
    reminder.update({full(L"pad-a")}, settings, at(25));
    CHECK(reminder.update({full(L"pad-a")}, settings, at(40)).empty());
    CHECK(fired(reminder.update({full(L"pad-a")}, settings, at(55)),
                NotificationEvent::UnplugReminder));
}

TEST_CASE("chargeReminder: a pad put away low is reminded if it does not come back") {
    Settings const settings = remindersOn();
    ChargeReminder reminder;

    reminder.update({makeController(L"pad-a", 15)}, settings, at(0));
    CHECK(reminder.update({}, settings, at(1)).empty());
    CHECK(reminder.update({}, settings, at(30)).empty());

    auto const events = reminder.update({}, settings, at(31));
    REQUIRE(fired(events, NotificationEvent::ChargeReminder));
    // The pad is gone, so the card can only describe it from the reading it left on.
    CHECK(events.front().controller.id == L"pad-a");
    CHECK(events.front().controller.percent == 15);

    CHECK(reminder.update({}, settings, at(300)).empty());
}

TEST_CASE("chargeReminder: coming back within the delay cancels the charge reminder") {
    Settings const settings = remindersOn();
    ChargeReminder reminder;

    reminder.update({makeController(L"pad-a", 15)}, settings, at(0));
    reminder.update({}, settings, at(1));
    reminder.update({makeController(L"pad-a", 16, ChargeState::Charging)}, settings, at(10));
    CHECK(reminder.update({makeController(L"pad-a", 20, ChargeState::Charging)}, settings, at(60))
              .empty());
    CHECK(reminder.update({}, settings, at(61)).empty());
    CHECK(reminder.update({}, settings, at(120)).empty());
}

TEST_CASE("chargeReminder: only a pad that went away low and on its battery is chased") {
    Settings const settings = remindersOn();
    ChargeReminder reminder;

    SUBCASE("above the low threshold") {
        reminder.update({makeController(L"pad-a", 60)}, settings, at(0));
    }
    SUBCASE("low but already charging") {
        reminder.update({makeController(L"pad-a", 15, ChargeState::Charging)}, settings, at(0));
    }
    SUBCASE("no level at all") {
        reminder.update({makeController(L"pad-a", -1)}, settings, at(0));
    }

    reminder.update({}, settings, at(1));
    CHECK(reminder.update({}, settings, at(120)).empty());
}

TEST_CASE("chargeReminder: nothing fires while reminders are off, and nothing is latched") {
    Settings settings = remindersOn();
    settings.remindersEnabled = false;
    ChargeReminder reminder;

    reminder.update({full(L"pad-a")}, settings, at(0));
    CHECK(reminder.update({full(L"pad-a")}, settings, at(60)).empty());

    settings.remindersEnabled = true;
    CHECK(fired(reminder.update({full(L"pad-a")}, settings, at(61)),
                NotificationEvent::UnplugReminder));
}

TEST_CASE("chargeReminder: each reminder follows its own event switch") {
    Settings settings = remindersOn();
    settings.forEvent(NotificationEvent::UnplugReminder).enabled = false;
    ChargeReminder reminder;

    reminder.update({full(L"pad-a"), makeController(L"pad-b", 10)}, settings, at(0));
    CHECK(reminder.update({full(L"pad-a")}, settings, at(60)).empty());

    settings.forEvent(NotificationEvent::UnplugReminder).enabled = true;
    settings.forEvent(NotificationEvent::ChargeReminder).enabled = false;
    ChargeReminder other;
    other.update({makeController(L"pad-b", 10)}, settings, at(0));
    other.update({}, settings, at(1));
    CHECK(other.update({}, settings, at(60)).empty());
}

TEST_CASE("chargeReminder: the delay comes from the settings") {
    Settings settings = remindersOn();
    settings.reminderDelayMinutes = 120;
    ChargeReminder reminder;

    reminder.update({full(L"pad-a")}, settings, at(0));
    CHECK(reminder.update({full(L"pad-a")}, settings, at(119)).empty());
    CHECK(fired(reminder.update({full(L"pad-a")}, settings, at(120)),
                NotificationEvent::UnplugReminder));
}
