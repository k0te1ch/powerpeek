#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "battery/DeviceInfo.h"

namespace peek {

// Logitech wireless gaming headsets on their Lightspeed dongles, read without G HUB. Windows
// knows nothing about their battery -- no device-tree property, no standard HID usage -- so the
// level is asked of the headset in HID++, as LogitechHeadsetProtocol describes.
//
// Each dongle is sent two kinds of request, both reads: once per arrival, the question of where
// the measurement feature lives, and on every poll the measurement itself.
//
// A dongle whose headset is off answers with an error, and then there is no headset to show:
// unlike a mouse that dozes between movements, a headset that is off has been switched off or
// has run flat, and a card for it would only say "not connected" next to a level from before.
class LogitechHeadsetProvider {
public:
    // Looks for dongles again. It walks every HID interface on the machine, so the monitor runs
    // it on the device-tree schedule rather than on every poll.
    void rescan();

    // Asks every dongle the last rescan found.
    std::vector<DeviceInfo> poll();

private:
    enum class Status {
        Unknown,
        Answering,
        Off,
        NoFeature,
        Unreachable,
    };

    struct Dongle {
        std::wstring path;
        std::wstring id;
        std::wstring name;
        std::uint16_t productId = 0;
        // Where the measurement feature answers, once the headset has said.
        std::optional<std::uint8_t> adcIndex;
        // What the last poll got. Kept so that the log records a change, not every poll.
        Status status = Status::Unknown;
    };

    static Status locateFeature(Dongle& dongle);

    std::vector<Dongle> m_dongles;
};

}  // namespace peek
