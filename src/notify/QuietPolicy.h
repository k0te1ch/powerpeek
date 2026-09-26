#pragma once

#include "core/Settings.h"

namespace peek::notify {

// Why the user would rather not be interrupted right now, as Windows reports it.
enum class UserBusyState {
    // Nothing going on; notify as usual.
    Available,
    // A full-screen application or game has the screen, exclusive or borderless.
    FullScreen,
    // Presentation settings are on, or a presentation is being shown.
    Presentation,
    // Focus Assist (Do Not Disturb on Windows 11), or the quiet hour after first sign-in.
    QuietTime,
};

// Which of an event's three outputs actually fire.
struct Delivery {
    bool playSound = false;
    bool showFlyout = false;
    bool showSystemToast = false;

    bool any() const { return playSound || showFlyout || showSystemToast; }
};

// Turns an event's configuration and the user's current state into the outputs to fire.
//
// Free of Win32 so the rules can be tested: the busy state is read by the platform layer and
// handed in. `ignoreEnabled` is the test button's path, which has to demonstrate an event
// even while it is switched off.
//
// While the user is busy and quiet mode is on, every event is dropped except a critical
// battery. That one still matters mid-game -- the pad is about to die -- so it keeps the
// application's own card and nothing else: no sound over the game's audio, and no Windows
// notification, which Focus Assist would hold back anyway and would then replay into the
// Action Center long after it stopped being news. A user who had only the Windows
// notification switched on for it gets the card instead, so the event is never silent.
Delivery decideDelivery(NotificationEvent event, EventSettings const& config, bool quietMode,
                        UserBusyState state, bool ignoreEnabled);

}  // namespace peek::notify
