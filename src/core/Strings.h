#pragma once

#include <format>
#include <string>
#include <string_view>

#include "core/Settings.h"

namespace peek {

// Every piece of user-visible text in the application.
//
// The enum lives in one place rather than being scattered as literals so that adding a
// language means filling one table, and so that a missing translation is a compile error
// instead of an English string leaking into a Russian UI.
enum class Text {
    AppName,
    AppTagline,

    NavDevices,
    NavHistory,
    NavSounds,
    NavSettings,
    NavAbout,

    DevicesTitle,
    NoControllers,
    NoControllersHint,
    Refresh,
    ChargeLevel,
    StatusCharging,
    StatusFull,
    StatusOnBattery,
    StatusWired,
    StatusUnknown,
    ApproximateSuffix,
    ConnectionUsb,
    ConnectionWireless,
    ConnectionBluetooth,
    EstimatedRemaining,
    UpdatedJustNow,
    UpdatedMinutesAgo,

    HistoryTitle,
    HistoryEmpty,
    HistoryEmptyHint,
    HistoryController,
    HistoryRange,
    HistoryRange24h,
    HistoryRange7d,
    HistoryRange30d,
    DrainRate,

    SoundsTitle,
    SoundsSubtitle,
    EventConnected,
    EventDisconnected,
    EventLow,
    EventCritical,
    EventCharged,
    SoundFile,
    SoundBuiltIn,
    SoundBrowse,
    SoundReset,
    SoundTest,
    SoundFileFilter,
    Volume,
    MasterVolume,
    PlaySound,
    ShowFlyout,
    ShowSystemToast,
    SystemToastUnavailable,

    SettingsTitle,
    SettingsGroupGeneral,
    SettingsGroupMonitoring,
    SettingsGroupAppearance,
    StartWithWindows,
    StartWithWindowsDesc,
    StartMinimised,
    StartMinimisedDesc,
    MinimiseToTray,
    MinimiseToTrayDesc,
    IncludeNonXbox,
    IncludeNonXboxDesc,
    PollInterval,
    PollIntervalDesc,
    LowThreshold,
    LowThresholdDesc,
    CriticalThreshold,
    CriticalThresholdDesc,
    Cooldown,
    CooldownDesc,
    ThemeLabel,
    ThemeSystem,
    ThemeLight,
    ThemeDark,
    LanguageLabel,
    LanguageSystem,
    LanguageEnglish,
    LanguageRussian,
    TrayStyleLabel,
    TrayStyleBattery,
    TrayStyleRing,
    TrayStylePercentage,
    TrayColorLabel,
    TrayColorAuto,
    TrayColorAccent,
    TrayColorWhite,
    TrayColorGreen,
    TrayColorBlue,
    TrayColorPink,
    BackdropLabel,
    BackdropDesc,
    BackdropOpaque,
    BackdropBlur,
    BackdropAcrylic,
    BackdropMica,
    WindowOpacity,
    WindowOpacityDesc,
    HistoryEnabled,
    HistoryEnabledDesc,
    HistoryRetention,
    HistoryRetentionDesc,

    AboutVersion,
    AboutDescription,
    OpenDataFolder,
    OpenSourceRepository,
    AboutAuthor,
    SupportAuthor,

    MenuOpen,
    MenuRefresh,
    MenuExit,
    TrayMoreControllers,

    ToastConnected,
    ToastDisconnected,
    ToastLow,
    ToastCritical,
    ToastCharged,
    ToastBodyLevel,
    ToastBodyNoLevel,

    UnitSeconds,
    UnitMinutes,
    UnitHours,
    UnitDays,
    UnitPercent,
    Never,
    Close,
    Minimise,
    Maximise,
    Restore,

    // Not a string: how many there are. The table in Strings.cpp is checked against this at
    // compile time, so nothing depends on remembering which enumerator happens to be last.
    Count,
};

// Resolves LanguagePreference::System against the user's UI language once, then serves
// every lookup from the chosen table.
void setLanguage(LanguagePreference preference);

std::wstring_view text(Text id);

// Substitutes into a string containing {} placeholders. std::format cannot be used
// directly because its format string has to be a compile-time constant, and these come
// out of a runtime table.
template <class... Args>
std::wstring formatText(Text id, Args const&... args) {
    return std::vformat(text(id), std::make_wformat_args(args...));
}

}  // namespace peek
