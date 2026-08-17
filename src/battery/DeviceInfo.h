#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace peek {

// Where a reading came from, because the two sources differ by an order of magnitude in
// resolution and the UI has to be honest about that.
enum class Fidelity {
    // Windows.Devices.Power reported real capacity figures: the percentage is a number.
    Exact,
    // XInput only classifies into empty/low/medium/full: the percentage is a stand-in for
    // a bucket and must be rendered as an approximation.
    Coarse,
};

enum class PowerSource {
    Unknown,
    // Wired pad, or a wireless one running off the cable: there is no battery to report.
    Wired,
    Battery,
};

// What the device is, so that a card, a portrait and a tray rule can stop assuming a pad.
// Every reading available today is a gamepad; the other values exist because the providers
// that produce them are the next step, not because anything sets them yet.
enum class DeviceKind {
    Gamepad,
    Headset,
    Mouse,
    Keyboard,
    Pen,
    Other,
};

enum class ChargeState {
    Unknown,
    Discharging,
    Charging,
    Full,
};

struct DeviceInfo {
    // Stable across reconnects of the same physical device. Derived from the WinRT
    // NonRoamableId where available, otherwise from vendor/product/slot.
    std::wstring id;
    std::wstring name;

    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;

    DeviceKind kind = DeviceKind::Other;

    // -1 when the level is not known (wired device, or one that reports no battery).
    int percent = -1;
    Fidelity fidelity = Fidelity::Coarse;
    PowerSource source = PowerSource::Unknown;
    ChargeState charge = ChargeState::Unknown;

    // A Microsoft-made Xbox controller, as opposed to a third-party pad that happens to
    // present the same HID interface. Drives the "show non-Xbox gamepads" setting, and so
    // only means anything on a Gamepad.
    bool isXboxController = false;

    // Populated only on the XInput path; -1 for WinRT-only devices.
    int xinputSlot = -1;

    std::chrono::system_clock::time_point firstSeen{};
    std::chrono::system_clock::time_point lastUpdate{};

    bool hasBattery() const noexcept { return percent >= 0 && source != PowerSource::Wired; }
};

std::wstring_view toString(PowerSource source);
std::wstring_view toString(ChargeState state);

}  // namespace peek
