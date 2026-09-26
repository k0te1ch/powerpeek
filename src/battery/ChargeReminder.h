#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "battery/DeviceInfo.h"
#include "battery/EventDetector.h"
#include "core/Settings.h"

namespace peek {

// The two reminders that are about time passing rather than about a reading changing: a
// device left on the charger long after it filled up, and a device put away flat that never
// made it onto the charger.
//
// Separate from EventDetector because the detector only runs when a snapshot differs from
// the last one, and a pad sitting at 100 % on its cable produces the same snapshot for hours.
// This one has to be asked again on a timer with nothing new to look at, and it takes the
// clock as an argument so that "thirty minutes later" is a test step, not a wait.
class ChargeReminder {
public:
    // Returns the reminders that fell due at `now`. Safe to call as often as wanted with the
    // same snapshot: each reminder fires once and is latched until the situation that caused
    // it is over -- the device leaves the charger, or comes back from being put away.
    std::vector<DetectedEvent> update(std::vector<DeviceInfo> const& snapshot,
                                      Settings const& settings,
                                      std::chrono::system_clock::time_point now);

private:
    struct DeviceState {
        DeviceInfo last;
        bool present = false;
        // When the device was first seen full on the charger in the current cycle.
        std::optional<std::chrono::system_clock::time_point> fullSince;
        // Latched until the device runs on its battery again or goes away, so a cell that
        // tops itself up from 99 % does not start a second reminder in the same cycle.
        bool unplugReminded = false;
        // When a low device went away. Cleared by any return: a device that comes back is
        // either on the charger, which is the point, or being used, and the low-battery
        // events cover it from there.
        std::optional<std::chrono::system_clock::time_point> awayLowSince;
    };

    std::map<std::wstring, DeviceState> m_states;
};

}  // namespace peek
