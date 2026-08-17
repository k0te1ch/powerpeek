#include "battery/EventDetector.h"

#include <algorithm>
#include <set>

namespace peek {
namespace {

// A pad flapping across the threshold would otherwise re-arm the warning the moment it
// reads one point higher, and the cooldown would never get a chance to suppress anything.
// Recovery has to be worth this much before the low-battery latch is cleared.
constexpr int kRecoveryMarginPercent = 5;

}  // namespace

std::vector<DetectedEvent> EventDetector::update(std::vector<ControllerInfo> const& snapshot,
                                                 Settings const& settings,
                                                 std::chrono::system_clock::time_point now) {
    std::vector<DetectedEvent> events;

    auto const cooldown = std::chrono::minutes{std::max(0, settings.notificationCooldownMinutes)};
    int const low = std::clamp(settings.lowThresholdPercent, 0, 100);
    int const critical = std::clamp(settings.criticalThresholdPercent, 0, low);

    auto fire = [&](NotificationEvent event, ControllerInfo const& controller,
                    ControllerState& state, bool rateLimited) {
        if (!settings.forEvent(event).enabled) {
            return;
        }
        auto const notified = state.lastNotified.find(event);
        if (rateLimited && notified != state.lastNotified.end() &&
            now - notified->second < cooldown) {
            return;
        }
        state.lastNotified[event] = now;
        events.push_back(DetectedEvent{event, controller});
    };

    std::set<std::wstring> present;

    for (ControllerInfo const& controller : snapshot) {
        if (controller.id.empty()) {
            continue;
        }
        present.insert(controller.id);

        auto [entry, inserted] = m_states.try_emplace(controller.id);
        ControllerState& state = entry->second;
        ControllerInfo const previous = state.last;
        int const baseline = state.thresholdBaseline;
        bool const wasPresent = !inserted && state.present;
        state.present = true;
        state.last = controller;
        state.thresholdBaseline = controller.percent;

        // The first snapshot only establishes the baseline. Nothing about it is news: the
        // controller was already on before the application started, and with autostart
        // enabled the alternative is a connection chime at every login.
        if (!m_seenFirstSnapshot) {
            continue;
        }

        if (!wasPresent) {
            fire(NotificationEvent::Connected, controller, state, false);
        }

        if (controller.charge == ChargeState::Charging ||
            (controller.percent >= 0 && controller.percent > low + kRecoveryMarginPercent)) {
            state.lastNotified.erase(NotificationEvent::BatteryLow);
            state.lastNotified.erase(NotificationEvent::BatteryCritical);
        }

        if (controller.charge == ChargeState::Full && previous.charge != ChargeState::Full) {
            fire(NotificationEvent::FullyCharged, controller, state, true);
        }

        // Only a discharging pad can be low. A wired one reports no level at all, and
        // reading that as an empty battery is the classic false alarm in this kind of app.
        if (controller.charge != ChargeState::Discharging || controller.percent < 0) {
            continue;
        }

        // An unknown previous level counts as "not warned yet", which is what makes a pad
        // that was already below the threshold while disconnected fire once on return --
        // and what makes the thresholds report again after reset() clears the baseline.
        bool const wasLow = baseline >= 0 && baseline <= low;
        bool const wasCritical = baseline >= 0 && baseline <= critical;

        if (controller.percent <= critical) {
            if (!wasCritical) {
                fire(NotificationEvent::BatteryCritical, controller, state, true);
            }
        } else if (controller.percent <= low && !wasLow) {
            fire(NotificationEvent::BatteryLow, controller, state, true);
        }
    }

    for (auto& [id, state] : m_states) {
        if (!state.present || present.contains(id)) {
            continue;
        }
        state.present = false;
        fire(NotificationEvent::Disconnected, state.last, state, false);
    }

    m_seenFirstSnapshot = true;
    return events;
}

void EventDetector::reset() {
    // Presence and the baseline flag survive deliberately. Clearing them would make the
    // next snapshot a fresh baseline, which announces nothing at all, and the one after it
    // would replay a connection chime for every pad that never went anywhere.
    //
    // What has to go is the level the thresholds compare against, because that is what
    // suppresses the event: raising the low threshold above a pad's current level leaves
    // `wasLow` permanently true, and the warning would then never fire again for as long
    // as the pad stays on.
    //
    // The reading in `last` is left exactly as it was. It is a different value with a
    // different job -- the disconnection card names the pad from it and shows the charge
    // it went away on -- and clearing it here used to cost a pad that vanished in the
    // first snapshot after a threshold change its level in that card.
    for (auto& [id, state] : m_states) {
        state.thresholdBaseline = -1;
        state.lastNotified.clear();
    }
}

}  // namespace peek
