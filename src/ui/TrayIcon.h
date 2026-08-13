#pragma once

#include <memory>
#include <vector>

#include "battery/ControllerInfo.h"
#include "core/Settings.h"
#include "core/Signal.h"
#include "core/Win.h"

namespace peek::ui {

// The notification-area icon, whose bitmap is rendered with Direct2D on every change
// rather than picked from a set of prebuilt images.
//
// Drawing it means the level is exact instead of quantised to whatever icons were
// shipped, the colours follow the theme and the accent, and the same code produces the
// 16, 20 and 24 pixel variants the shell asks for at different scale factors.
class TrayIcon {
public:
    // The message the shell posts to `owner` for this icon. The owner's window procedure
    // forwards it to handleCallback().
    static constexpr UINT kCallbackMessage = WM_APP + 0x10;

    // RegisterWindowMessage("TaskbarCreated"), registered on the first call. The owner has
    // to compare against this before its switch statement, because a registered message is
    // >= 0xC000 and cannot be a case label.
    static UINT taskbarCreatedMessage();

    // `owner` receives the callback message; it must outlive the icon.
    explicit TrayIcon(HWND owner);
    ~TrayIcon();

    TrayIcon(TrayIcon const&) = delete;
    TrayIcon& operator=(TrayIcon const&) = delete;

    bool add();
    void remove();

    // Explorer restarting destroys the icon; the owner window forwards the registered
    // "TaskbarCreated" message here and the icon is put back.
    void reAdd();

    void update(std::vector<ControllerInfo> const& controllers, Settings const& settings);

    // Forwarded from the owner's window procedure for the callback message.
    void handleCallback(WPARAM wparam, LPARAM lparam);

    // Screen rectangle of the icon, for positioning a flyout next to it. Empty if the
    // shell will not say (which happens while the icon is in the overflow area).
    RECT iconRect() const;

    Signal<> activated;         // left click or Enter
    Signal<> openRequested;     // "Open" from the menu, or double click
    Signal<> refreshRequested;  // "Refresh" from the menu
    Signal<> exitRequested;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace peek::ui
