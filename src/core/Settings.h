#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>

#include "core/Signal.h"

namespace peek {

// The five things worth telling the user about. The order matches the RCDATA sound
// resources in resources/resource.h, so the built-in sound for an event is
// IDW_SOUND_FIRST + index.
enum class NotificationEvent {
    Connected,
    Disconnected,
    BatteryLow,
    BatteryCritical,
    FullyCharged,
};

inline constexpr std::size_t kNotificationEventCount = 5;

constexpr std::size_t index(NotificationEvent event) noexcept {
    return static_cast<std::size_t>(event);
}

std::wstring_view displayName(NotificationEvent event);

enum class ThemePreference {
    System,
    Light,
    Dark,
};

enum class LanguagePreference {
    System,
    English,
    Russian,
};

// What the system paints behind the window.
//
// Anything other than Opaque hands the window frame to the compositor, which draws its
// material across the whole window rectangle. The window gives up its own shadow margin in
// return; see ui::D2DWindow::setBackdrop for why the two cannot coexist.
//
// Not every mode exists on every Windows build. platform::effectiveBackdrop resolves a
// requested mode against the running system, and the settings page offers only what that
// system can actually do.
enum class BackdropMode {
    Opaque,
    Blur,
    Acrylic,
    Mica,
};

// The lowest alpha the window background may be painted at.
//
// This is a contrast floor, not a matter of taste. The title bar caption and the navigation
// labels sit directly on the window background, and the worst case is white body text in
// dark theme over a white desktop: at 0.7 the composite reads #636363 and the contrast ratio
// against white is about 6:1, comfortably past the 4.5:1 the text has to clear. At 0.5 the
// same case falls to about 3:1 and the labels start to disappear into the wallpaper.
inline constexpr float kMinimumWindowOpacity = 0.7f;

// How the notification-area icon renders the level.
enum class TrayStyle {
    // A battery outline whose fill tracks the level.
    Battery,
    // A ring gauge with the number in the middle.
    Ring,
    // The number alone, largest and most legible at small sizes.
    Percentage,
};

// What a healthy charge is drawn in on the notification-area icon.
//
// Only a healthy charge: low and critical keep their amber and red whatever is chosen here,
// because those two carry meaning rather than taste. Auto measures the taskbar and picks black
// or white against it, which is the only choice that cannot end up invisible -- a taskbar tinted
// with the accent swallows an accent-coloured mark completely.
enum class TrayColor {
    Auto,
    Accent,
    White,
    Green,
    Blue,
    Pink,
};

struct EventSettings {
    bool enabled = true;
    bool playSound = true;
    // The application's own Fluent flyout, drawn by ui::ToastWindow.
    bool showFlyout = true;
    // A real Windows notification that lands in the Action Center.
    bool showSystemToast = false;
    // Empty means "use the sound embedded in the executable".
    std::wstring soundFile;
    float volume = 1.0f;
};

struct Settings {
    // Bumped when a migration is needed; unknown future versions are left alone and the
    // defaults are used, so an older build cannot corrupt a newer file.
    int version = 1;

    bool startWithWindows = false;
    bool startMinimised = true;
    bool minimiseToTrayOnClose = true;

    // Third-party pads report battery through the same APIs; off by default because the
    // readings are frequently wrong on non-Microsoft hardware.
    bool includeNonXboxGamepads = false;

    int pollIntervalSeconds = 30;
    int lowThresholdPercent = 20;
    int criticalThresholdPercent = 10;

    // Stops a controller hovering on a threshold from re-notifying every poll.
    int notificationCooldownMinutes = 30;

    ThemePreference theme = ThemePreference::System;
    LanguagePreference language = LanguagePreference::System;
    TrayStyle trayStyle = TrayStyle::Battery;
    TrayColor trayColor = TrayColor::Auto;

    BackdropMode backdrop = BackdropMode::Opaque;
    // Alpha of the window's own background layer, in [kMinimumWindowOpacity, 1]. Fully
    // opaque by default: the backdrop is opt-in and nothing about the shipped window
    // changes until it is chosen.
    float windowOpacity = 1.0f;

    // Scales every notification sound; the per-event volume multiplies into this.
    float masterVolume = 0.8f;

    bool historyEnabled = true;
    int historyRetentionDays = 30;

    std::array<EventSettings, kNotificationEventCount> events = defaultEvents();

    EventSettings const& forEvent(NotificationEvent event) const { return events[index(event)]; }
    EventSettings& forEvent(NotificationEvent event) { return events[index(event)]; }

    static std::array<EventSettings, kNotificationEventCount> defaultEvents();

    // A missing or unreadable file yields defaults rather than an error: losing settings
    // must never stop the application from starting.
    static Settings load(std::filesystem::path const& file);

    // Writes through a temporary file and replaces atomically, so a crash mid-write
    // cannot leave a truncated settings file behind.
    bool save(std::filesystem::path const& file) const;
};

// The single mutable settings instance, plus a signal every subsystem listens to so a
// change in the settings page takes effect without a restart.
class SettingsStore {
public:
    static SettingsStore& instance();

    Settings const& get() const { return m_settings; }

    // Applies `next`, persists it, and raises `changed` with the previous value so
    // listeners can diff (the poll interval and the autostart entry both need that).
    void apply(Settings next);

    // Reads the settings file into the store. Raises nothing: this runs at startup,
    // before any subsystem exists to listen.
    void load();

    // Raised as (current, previous).
    Signal<Settings const&, Settings const&> changed;

private:
    SettingsStore() = default;

    Settings m_settings;
};

}  // namespace peek
