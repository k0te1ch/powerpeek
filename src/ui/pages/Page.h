#pragma once

#include <functional>
#include <vector>

#include "battery/BatteryHistory.h"
#include "battery/ControllerInfo.h"
#include "core/Settings.h"
#include "core/Win.h"
#include "notify/NotificationCenter.h"
#include "ui/Widgets.h"

namespace peek::ui {

// Everything a page of the main window is allowed to reach outside itself. The application
// owns all of it and outlives the window, so the raw pointers are stable for the page's life.
struct PageContext {
    // The controller monitor's latest snapshot, kept by the window and refreshed in place.
    std::vector<ControllerInfo> const* controllers = nullptr;
    BatteryHistory* history = nullptr;
    notify::NotificationCenter* notifications = nullptr;

    // Asks the controller monitor to poll out of band, for the refresh button.
    std::function<void()> refreshControllers;

    // Writes through SettingsStore::apply so every other subsystem reacts live. Goes through
    // the window rather than straight to the store because the window has to know that the
    // resulting `changed` signal is its own doing: rebuilding a page from inside the event
    // handler of the control that caused the change would destroy that control mid-call.
    std::function<void(Settings)> applySettings;

    // Owner for the modal file dialog; a modal parented to nothing lands behind the window.
    HWND owner = nullptr;

    // False when the Start Menu shortcut could not be written. Without it an unpackaged
    // process cannot raise Windows toasts, and the switches asking for them have to say so.
    bool systemToastsAvailable = false;
};

// One page of the navigation shell: a scrolling column of Fluent cards, rebuilt from scratch
// whenever the data behind it changes shape.
class Page : public ScrollView {
public:
    explicit Page(PageContext context);

    // Marks the content stale. The rebuild itself happens in tick(), on the next frame and
    // only while the page is visible: a control that asks for it -- a combo box picking a
    // different controller, say -- is about to be destroyed by it, and destroying a widget
    // from inside its own event handler unwinds through freed memory.
    void invalidateContent();

    // Updates the values a rebuild would not change the shape of -- battery levels, mainly.
    virtual void refreshValues() {}

    bool tick(std::chrono::steady_clock::time_point now) override;

protected:
    // Fills `column` with the page's cards. The column is empty and already spaced and padded.
    virtual void build(StackPanel& column) = 0;

    PageContext m_context;

private:
    void rebuild();

    bool m_dirty = true;
};

}  // namespace peek::ui
