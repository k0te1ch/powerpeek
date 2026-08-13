#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "battery/ControllerInfo.h"
#include "core/Win.h"

namespace peek {

// Watches connected controllers on a background thread and tells the UI thread when
// anything changed.
//
// Threading contract: everything except snapshot() runs on the caller's thread and is
// cheap; polling and the WinRT calls happen on an owned MTA thread, because
// TryGetBatteryReport can block on a wireless device that has gone to sleep. The monitor
// never invokes callbacks itself -- it posts `changedMessage` to `notifyWindow`, and the
// UI thread then reads snapshot(). That keeps every observer single-threaded.
class ControllerMonitor {
public:
    ControllerMonitor(HWND notifyWindow, UINT changedMessage);
    ~ControllerMonitor();

    ControllerMonitor(ControllerMonitor const&) = delete;
    ControllerMonitor& operator=(ControllerMonitor const&) = delete;

    void start(std::chrono::seconds interval);
    void stop();

    // Takes effect at the next tick; does not force an immediate poll.
    void setInterval(std::chrono::seconds interval);

    // Include pads that are not Microsoft Xbox controllers.
    void setIncludeNonXbox(bool include);

    // Polls out of band, e.g. after the user opens the window.
    void refreshNow();

    // The most recent reading. Safe to call from any thread.
    std::vector<ControllerInfo> snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace peek
