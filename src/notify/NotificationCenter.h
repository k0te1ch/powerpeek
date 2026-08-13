#pragma once

#include <memory>

#include "battery/EventDetector.h"
#include "core/Settings.h"
#include "core/Win.h"

namespace peek::notify {

// The single place an "event happened" turns into something the user perceives.
//
// One detected event can produce up to three outputs -- a sound, the application's own
// Fluent flyout, and a real Windows toast -- each independently switchable per event in
// the settings. Routing them from one place keeps the three in step and means the
// settings page's preview button exercises exactly the same path as a real event.
class NotificationCenter {
public:
    // Posted to `owner` when the user clicks one of the application's own flyouts, which
    // is the only thing a battery notification can usefully lead to: show the window.
    // Posted rather than sent, because it arrives from inside the flyout's own painting.
    static constexpr UINT kFlyoutClickedMessage = WM_APP + 0x11;

    explicit NotificationCenter(HWND owner);
    ~NotificationCenter();

    NotificationCenter(NotificationCenter const&) = delete;
    NotificationCenter& operator=(NotificationCenter const&) = delete;

    void post(DetectedEvent const& event);

    // Fires the event's outputs with a stand-in controller, for the "test" button.
    void preview(NotificationEvent event);

    // Plays only the sound configured for an event; used while the user is picking one.
    void previewSound(NotificationEvent event);

    void applySettings(Settings const& settings);

    // Drops decoded sound clips so a changed file is picked up on next play.
    void invalidateSoundCache();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace peek::notify
