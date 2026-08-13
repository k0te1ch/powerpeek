#pragma once

#include <d2d1.h>

#include <optional>
#include <string>

#include "core/Win.h"

namespace peek::platform {

// Runtime capability probing.
//
// The application targets Windows 10 but lights up the Windows 11 window attributes when
// it finds itself there. Everything is probed at runtime through GetProcAddress or a
// tolerated DwmSetWindowAttribute failure, never at compile time, so one binary covers
// both.
bool isWindows11OrGreater();

// DWMWA_USE_IMMERSIVE_DARK_MODE moved from attribute 19 to 20 in Windows 10 2004; this
// applies whichever the running build understands and is a no-op if neither works.
void setTitleBarDarkMode(HWND window, bool dark);

// Rounded window corners and Mica, both Windows 11 only. No-ops elsewhere; the window
// draws its own corners and background regardless, so these only remove a redundant
// square DWM frame when the OS can do better.
void setRoundedCorners(HWND window, bool rounded);

bool systemUsesLightTheme();
D2D1_COLOR_F systemAccentColor();

// Restores and activates a window from a message handler. SetForegroundWindow on its own
// is refused whenever the calling process does not already own the foreground, so this
// also performs the input-queue attachment that lifts the restriction, and flashes the
// taskbar button when even that is refused.
void bringToForeground(HWND window);

// HKCU\...\CurrentVersion\Run. Reports what is actually in the registry rather than what
// was last requested, so a user removing the entry by hand is reflected in the UI.
bool isAutostartEnabled();
bool setAutostartEnabled(bool enabled);

// A named mutex plus a broadcast message: the second instance asks the first to show its
// window and then exits.
class SingleInstance {
public:
    SingleInstance();
    ~SingleInstance();

    SingleInstance(SingleInstance const&) = delete;
    SingleInstance& operator=(SingleInstance const&) = delete;

    // False when another instance already holds the mutex; it has been asked to come to
    // the foreground and this process should exit quietly.
    bool claim();

    // The registered message the running instance receives when a second one starts.
    static UINT activationMessage();

private:
    HANDLE m_mutex = nullptr;
};

// Toast notifications from an unpackaged desktop application require an AppUserModelID
// that Explorer can resolve, which in practice means a Start Menu shortcut carrying
// System.AppUserModel.ID. This creates it once and sets the ID on the process.
namespace shell {

std::wstring appUserModelId();

// Sets the process AUMID; call before any window is created so the taskbar groups
// correctly. Cheap, always safe.
void applyAppUserModelId();

// Creates or repairs the Start Menu shortcut. Returns false if it could not be written,
// in which case system toasts are unavailable and the caller should fall back to the
// application's own flyouts.
bool ensureStartMenuShortcut();

}  // namespace shell

}  // namespace peek::platform
