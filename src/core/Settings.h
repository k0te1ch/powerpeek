#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "core/Signal.h"

namespace peek {

// The things worth telling the user about. The order matches the RCDATA sound resources in
// resources/resource.h, so the built-in sound for an event is IDW_SOUND_FIRST + index. New
// events go at the end: the settings file matches them by key, but the sounds by position.
enum class NotificationEvent {
    Connected,
    Disconnected,
    BatteryLow,
    BatteryCritical,
    FullyCharged,
    // Full on the charger for longer than the reminder delay.
    UnplugReminder,
    // Put away low and not back on a charger within the reminder delay.
    ChargeReminder,
};

inline constexpr std::size_t kNotificationEventCount = 7;

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
// A double rather than a float because it is also where the slider's grid starts, and a
// grid counted from the float nearest 0.7 puts its fourth stop on 0.84999996 instead of
// on the 0.85 the label claims. Narrowed where a float is what is wanted.
inline constexpr double kMinimumWindowOpacity = 0.70;

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

// Which corner or edge of the work area the application's own notification cards appear at.
//
// Windows notifications are not covered by this and cannot be: the system decides where its
// own toasts go, and nothing an application does moves them. That is also the reason the
// setting exists -- an event with both kinds switched on used to put two cards in the same
// corner, and there was no way to send one of them elsewhere.
//
// Read left to right, top row first, so that an index into the list in the settings page is
// the enumerator with the same value.
enum class ToastPosition {
    TopLeft,
    TopCenter,
    TopRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
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

using DeviceNames = std::map<std::wstring, std::wstring, std::less<>>;

// The longest custom device name kept, in UTF-16 units. A name is a label on a card and a
// line in a tooltip that Windows truncates at 127 characters for every device together.
inline constexpr std::size_t kMaxDeviceNameLength = 40;

// Strips surrounding whitespace and control characters and caps the length, so that what is
// stored is exactly what is shown. An empty result means "no custom name".
std::wstring normaliseDeviceName(std::wstring_view name);

struct Settings {
    // Bumped when a migration is needed. A file left by a newer build is read as the
    // defaults, and its version is kept here rather than dropped -- which is what makes
    // save() refuse to write over it. Both halves are needed: reading defensively only
    // protects the file until the first setting the user changes.
    int version = 1;

    bool startWithWindows = false;
    bool startMinimised = true;
    bool minimiseToTrayOnClose = true;

    // Third-party pads report battery through the same APIs; off by default because the
    // readings are frequently wrong on non-Microsoft hardware.
    bool includeNonXboxGamepads = false;

    int pollIntervalSeconds = 30;
    // Reading the file keeps the critical threshold strictly below the low one, and keeps
    // the low one high enough that there is somewhere below it for the critical one to be.
    // Both alerts on a single reading, and a low alert that can never fire at all, are the
    // two ways this goes wrong.
    int lowThresholdPercent = 20;
    int criticalThresholdPercent = 10;

    // Stops a controller hovering on a threshold from re-notifying every poll.
    int notificationCooldownMinutes = 30;

    // Fires the low-battery event when the projected time left drops below this many
    // minutes, whatever the level. Zero switches it off, which is the default: the
    // projection needs a stretch of discharge history before it says anything at all.
    int lowTimeLeftMinutes = 0;

    // The unplug and charge reminders. Off by default: they are the only events that
    // interrupt about something the user did on purpose, and a controller whose battery
    // manages its own charging does not need the first one at all -- an update should not
    // start nagging people who never asked for it.
    bool remindersEnabled = false;
    int reminderDelayMinutes = 30;

    ThemePreference theme = ThemePreference::System;
    LanguagePreference language = LanguagePreference::System;
    TrayStyle trayStyle = TrayStyle::Battery;
    TrayColor trayColor = TrayColor::Auto;
    // Where the application's own cards appear. The bottom right is where they have always
    // appeared, so an existing installation is not moved by gaining the setting.
    ToastPosition toastPosition = ToastPosition::BottomRight;

    BackdropMode backdrop = BackdropMode::Opaque;
    // Alpha of the window's own background layer, in [kMinimumWindowOpacity, 1]. Fully
    // opaque by default: the backdrop is opt-in and nothing about the shipped window
    // changes until it is chosen.
    float windowOpacity = 1.0f;

    // Holds notifications back while a full-screen game or a presentation is running, or
    // while Focus Assist is on. A critical battery still gets through; notify::decideDelivery
    // has the rules.
    bool quietWhenBusy = true;

    // Scales every notification sound; the per-event volume multiplies into this.
    float masterVolume = 0.8f;

    bool historyEnabled = true;
    int historyRetentionDays = 30;

    std::array<EventSettings, kNotificationEventCount> events = defaultEvents();

    // Names the user gave devices, keyed by DeviceInfo::id -- the identity that survives a
    // reconnect and that duplicate readings have already been merged onto. A device with no
    // entry keeps the name its provider reports. Every stored name is already normalised.
    DeviceNames deviceNames;

    // Stores `name` for the device, or forgets the device's entry when the name normalises to
    // nothing, which is how clearing the field resets the name.
    void setDeviceName(std::wstring const& id, std::wstring_view name);

    EventSettings const& forEvent(NotificationEvent event) const { return events[index(event)]; }
    EventSettings& forEvent(NotificationEvent event) { return events[index(event)]; }

    static std::array<EventSettings, kNotificationEventCount> defaultEvents();

    // A missing or unreadable file yields defaults rather than an error: losing settings
    // must never stop the application from starting.
    static Settings load(std::filesystem::path const& file);

    // Writes through a temporary file and replaces atomically, so a crash mid-write
    // cannot leave a truncated settings file behind. Refuses, and returns false, for
    // settings carrying a version this build does not understand.
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
