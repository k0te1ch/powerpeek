// The one rule the device model carries itself: whether a reading describes a battery at all.
//
// hasBattery is asked three times over on every poll -- the history refuses to log a device
// without one, the tray refuses to draw one, and the card refuses to estimate a time remaining
// -- so a wrong answer here is not a wrong pixel, it is a log full of entries for a cabled pad
// and a tray icon showing a charge nothing is running on. The percent field carries two
// different meanings at once (a level, and -1 for "no level known"), which is exactly the shape
// that survives a careless edit looking correct.

#include "TestSupport.h"

#include "battery/DeviceInfo.h"

namespace {

using peek::DeviceInfo;
using peek::DeviceKind;
using peek::PowerSource;

// A reading straight off a provider that found a device but no charge on it.
DeviceInfo makeReading(int percent, PowerSource source) {
    DeviceInfo device;
    device.id = L"dev-1";
    device.kind = DeviceKind::Gamepad;
    device.percent = percent;
    device.source = source;
    return device;
}

}  // namespace

TEST_CASE("deviceInfo: a device nothing has been read from yet reports no battery") {
    CHECK_FALSE(DeviceInfo{}.hasBattery());
}

TEST_CASE("deviceInfo: a level of -1 means no level, not a level below empty") {
    CHECK_FALSE(makeReading(-1, PowerSource::Battery).hasBattery());
}

TEST_CASE("deviceInfo: an empty battery is still a battery") {
    // The boundary the -1 sentinel sits next to: zero per cent is a reading, and a device
    // dropped from the tray at exactly the moment it needs the warning is the failure.
    CHECK(makeReading(0, PowerSource::Battery).hasBattery());
}

TEST_CASE("deviceInfo: a cabled device has no battery even when a level came with it") {
    // XInput reports a level for a pad on a cable; it describes the cable, not a cell to
    // watch draining.
    CHECK_FALSE(makeReading(80, PowerSource::Wired).hasBattery());
}

TEST_CASE("deviceInfo: a level from a source that did not say how it is powered counts") {
    // Not every provider reports a power source, and dropping those readings would lose the
    // charge of every device that reaches PowerPeek through one of them.
    CHECK(makeReading(80, PowerSource::Unknown).hasBattery());
}

TEST_CASE("deviceInfo: a device says nothing about what it is until a provider does") {
    // The default is deliberately not Gamepad: a reading of unknown kind must not inherit the
    // rules written for pads, including the filter that hides third-party ones.
    CHECK(DeviceInfo{}.kind == DeviceKind::Other);
}
