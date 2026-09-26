#include "battery/ChargeReminder.h"

#include <algorithm>
#include <set>

namespace peek {
namespace {

// Some pads never say Full and instead sit on "charging, 100 %" until they are unplugged.
bool fullOnCharger(DeviceInfo const& device) {
    return device.charge == ChargeState::Full ||
           (device.charge == ChargeState::Charging && device.percent >= 100);
}

// Only a reading that says the pad was running on its own battery counts: a wired pad has no
// level to be low at, and one that was charging when it went is not in need of a charger.
bool putAwayLow(DeviceInfo const& device, int lowThreshold) {
    return device.charge == ChargeState::Discharging && device.percent >= 0 &&
           device.percent <= lowThreshold;
}

}  // namespace

std::vector<DetectedEvent> ChargeReminder::update(std::vector<DeviceInfo> const& snapshot,
                                                  Settings const& settings,
                                                  std::chrono::system_clock::time_point now) {
    std::vector<DetectedEvent> events;

    auto const delay = std::chrono::minutes{std::max(1, settings.reminderDelayMinutes)};
    int const low = std::clamp(settings.lowThresholdPercent, 0, 100);

    // A reminder that is switched off is not latched either: turning it on later should
    // still remind about a pad that has been sitting on the charger all along.
    auto wanted = [&](NotificationEvent event) {
        return settings.remindersEnabled && settings.forEvent(event).enabled;
    };

    std::set<std::wstring> present;

    for (DeviceInfo const& device : snapshot) {
        if (device.id.empty()) {
            continue;
        }
        present.insert(device.id);

        DeviceState& state = m_states[device.id];
        state.present = true;
        state.last = device;
        state.awayLowSince.reset();

        if (device.charge == ChargeState::Discharging) {
            state.unplugReminded = false;
        }
        if (!fullOnCharger(device)) {
            state.fullSince.reset();
            continue;
        }
        if (!state.fullSince) {
            state.fullSince = now;
        }
        if (!state.unplugReminded && now - *state.fullSince >= delay &&
            wanted(NotificationEvent::UnplugReminder)) {
            state.unplugReminded = true;
            events.push_back(DetectedEvent{NotificationEvent::UnplugReminder, device});
        }
    }

    for (auto& [id, state] : m_states) {
        if (present.contains(id)) {
            continue;
        }
        if (state.present) {
            state.present = false;
            state.fullSince.reset();
            state.unplugReminded = false;
            if (putAwayLow(state.last, low)) {
                state.awayLowSince = now;
            }
        }
        if (state.awayLowSince && now - *state.awayLowSince >= delay &&
            wanted(NotificationEvent::ChargeReminder)) {
            state.awayLowSince.reset();
            events.push_back(DetectedEvent{NotificationEvent::ChargeReminder, state.last});
        }
    }

    return events;
}

}  // namespace peek
