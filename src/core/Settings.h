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

// How the notification-area icon renders the level.
enum class TrayStyle {
    // A battery outline whose fill tracks the level.
    Battery,
    // A ring gauge with the number in the middle.
    Ring,
    // The number alone, largest and most legible at small sizes.
    Percentage,
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
