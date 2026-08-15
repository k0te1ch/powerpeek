#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "battery/ControllerInfo.h"
#include "core/Settings.h"

namespace peek {

struct DetectedEvent {
    NotificationEvent event;
    ControllerInfo controller;
};

// Turns a stream of controller snapshots into the handful of moments worth announcing.
//
// All of the awkward parts of "notify me when the battery is low" live here: a pad that
// hovers on the threshold must not notify twice, a threshold crossed while the
// application was closed must still fire once, and reconnecting a pad must not replay
// every event it triggered last session. Keeping this free of Win32 and of the audio and
// toast machinery is what makes those rules checkable.
class EventDetector {
public:
    // Returns the events that became true between the previous snapshot and this one.
    // The first call after construction reports nothing at all -- it only records the
    // baseline -- so launching with a flat controller already connected neither shouts
    // about the battery nor announces a connection that happened before startup.
    std::vector<DetectedEvent> update(std::vector<ControllerInfo> const& snapshot,
                                      Settings const& settings,
                                      std::chrono::system_clock::time_point now);

    // Forgets what the threshold rules already reported -- the levels they compare
    // against and the cooldown latches -- so that events the old thresholds swallowed can
    // fire again. Which controllers are connected is kept: a threshold change is not a
    // reconnection and must not chime like one. Neither is the last reading itself
    // forgotten; the disconnection card is still owed the level the pad went away on.
    void reset();

private:
    struct ControllerState {
        // The whole previous reading rather than just the level, because a disconnection
        // has to announce a controller that is no longer in the snapshot -- by name, and
        // with the charge it had when it went.
        ControllerInfo last;
        // What the low and critical rules compare the new reading against. It tracks
        // last.percent everywhere except through reset(), which clears this one alone.
        // Clearing the reading instead would take the level off the disconnection card:
        // raise the low threshold, unplug a pad before the next poll, and it would be
        // announced as though it had never reported a charge at all.
        int thresholdBaseline = -1;
        // Kept across a disconnection, so that reconnecting a pad does not replay the
        // events it already fired: only the presence flag is cleared.
        bool present = false;
        std::map<NotificationEvent, std::chrono::system_clock::time_point> lastNotified;
    };

    std::map<std::wstring, ControllerState> m_states;
    bool m_seenFirstSnapshot = false;
};

}  // namespace peek
