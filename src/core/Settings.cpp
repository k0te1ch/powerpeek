#include "core/Settings.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

#include "core/AppPaths.h"
#include "core/Json.h"
#include "core/Logger.h"
#include "core/Strings.h"
#include "core/Win.h"

namespace peek {
namespace {

constexpr int kCurrentVersion = 1;

constexpr int kMinPollSeconds = 5;
constexpr int kMaxPollSeconds = 3600;
constexpr int kMinThresholdPercent = 1;
constexpr int kMaxThresholdPercent = 95;
// The low threshold has to leave a percent underneath it for the critical one to sit in.
// Without that floor the two collapse onto each other at the bottom of the band: a
// hand-edited low of 1 drags critical down to 1 as well, and the low-battery event then
// never fires for anyone -- the critical alert reaches every reading first.
constexpr int kMinLowThresholdPercent = kMinThresholdPercent + 1;
constexpr int kMaxCooldownMinutes = 24 * 60;
constexpr int kMinRetentionDays = 1;
constexpr int kMaxRetentionDays = 365;

constexpr std::array<char const*, kNotificationEventCount> kEventKeys = {
    "connected", "disconnected", "batteryLow", "batteryCritical", "fullyCharged",
};

constexpr std::array<std::pair<ThemePreference, char const*>, 3> kThemeNames = {{
    {ThemePreference::System, "system"},
    {ThemePreference::Light, "light"},
    {ThemePreference::Dark, "dark"},
}};

constexpr std::array<std::pair<LanguagePreference, char const*>, 3> kLanguageNames = {{
    {LanguagePreference::System, "system"},
    {LanguagePreference::English, "english"},
    {LanguagePreference::Russian, "russian"},
}};

constexpr std::array<std::pair<TrayStyle, char const*>, 3> kTrayStyleNames = {{
    {TrayStyle::Battery, "battery"},
    {TrayStyle::Ring, "ring"},
    {TrayStyle::Percentage, "percentage"},
}};

constexpr std::array<std::pair<TrayColor, char const*>, 6> kTrayColorNames = {{
    {TrayColor::Auto, "auto"},
    {TrayColor::Accent, "accent"},
    {TrayColor::White, "white"},
    {TrayColor::Green, "green"},
    {TrayColor::Blue, "blue"},
    {TrayColor::Pink, "pink"},
}};

constexpr std::array<std::pair<ToastPosition, char const*>, 6> kToastPositionNames = {{
    {ToastPosition::TopLeft, "top-left"},
    {ToastPosition::TopCenter, "top-center"},
    {ToastPosition::TopRight, "top-right"},
    {ToastPosition::BottomLeft, "bottom-left"},
    {ToastPosition::BottomCenter, "bottom-center"},
    {ToastPosition::BottomRight, "bottom-right"},
}};

constexpr std::array<std::pair<BackdropMode, char const*>, 4> kBackdropNames = {{
    {BackdropMode::Opaque, "opaque"},
    {BackdropMode::Blur, "blur"},
    {BackdropMode::Acrylic, "acrylic"},
    {BackdropMode::Mica, "mica"},
}};

// Enumerations are stored by name rather than by ordinal so that the file stays readable
// and so that reordering an enum cannot silently change a user's setting.
template <class T, std::size_t N>
T parseEnum(std::array<std::pair<T, char const*>, N> const& table, json::Value const& node,
            T fallback) {
    std::string const name = node.asString();
    for (auto const& [value, label] : table) {
        if (name == label) {
            return value;
        }
    }
    return fallback;
}

template <class T, std::size_t N>
char const* enumName(std::array<std::pair<T, char const*>, N> const& table, T value) {
    for (auto const& [candidate, label] : table) {
        if (candidate == value) {
            return label;
        }
    }
    return table.front().second;
}

std::wstring win32Error(DWORD error) {
    return describeHresult(HRESULT_FROM_WIN32(error));
}

json::Value toJson(Settings const& settings) {
    json::Value root;
    root.set("version", kCurrentVersion);

    root.set("startWithWindows", settings.startWithWindows);
    root.set("startMinimised", settings.startMinimised);
    root.set("minimiseToTrayOnClose", settings.minimiseToTrayOnClose);
    root.set("includeNonXboxGamepads", settings.includeNonXboxGamepads);

    root.set("pollIntervalSeconds", settings.pollIntervalSeconds);
    root.set("lowThresholdPercent", settings.lowThresholdPercent);
    root.set("criticalThresholdPercent", settings.criticalThresholdPercent);
    root.set("notificationCooldownMinutes", settings.notificationCooldownMinutes);

    root.set("theme", enumName(kThemeNames, settings.theme));
    root.set("language", enumName(kLanguageNames, settings.language));
    root.set("trayStyle", enumName(kTrayStyleNames, settings.trayStyle));
    root.set("trayColor", enumName(kTrayColorNames, settings.trayColor));
    root.set("toastPosition", enumName(kToastPositionNames, settings.toastPosition));
    root.set("backdrop", enumName(kBackdropNames, settings.backdrop));
    root.set("windowOpacity", settings.windowOpacity);

    root.set("masterVolume", settings.masterVolume);

    root.set("historyEnabled", settings.historyEnabled);
    root.set("historyRetentionDays", settings.historyRetentionDays);

    json::Value events;
    for (std::size_t i = 0; i < kNotificationEventCount; ++i) {
        EventSettings const& event = settings.events[i];

        json::Value node;
        node.set("enabled", event.enabled);
        node.set("playSound", event.playSound);
        node.set("showFlyout", event.showFlyout);
        node.set("showSystemToast", event.showSystemToast);
        node.set("soundFile", narrow(event.soundFile));
        node.set("volume", event.volume);

        events.set(kEventKeys[i], std::move(node));
    }
    root.set("events", std::move(events));

    return root;
}

void readEvents(Settings& settings, json::Value const& root) {
    json::Value const& events = root["events"];
    for (std::size_t i = 0; i < kNotificationEventCount; ++i) {
        json::Value const& node = events[kEventKeys[i]];
        if (node.kind() != json::Value::Kind::Object) {
            continue;
        }

        EventSettings& event = settings.events[i];
        event.enabled = node["enabled"].asBool(event.enabled);
        event.playSound = node["playSound"].asBool(event.playSound);
        event.showFlyout = node["showFlyout"].asBool(event.showFlyout);
        event.showSystemToast = node["showSystemToast"].asBool(event.showSystemToast);
        event.soundFile = node["soundFile"].asWide(event.soundFile);
        event.volume = std::clamp(node["volume"].asFloat(event.volume), 0.0f, 1.0f);
    }
}

std::string readWholeFile(std::filesystem::path const& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool writeTempFile(std::filesystem::path const& temp, std::string const& text) {
    HANDLE const handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        log::error(L"Cannot create {}: {}", temp.wstring(), win32Error(GetLastError()));
        return false;
    }

    DWORD written = 0;
    bool ok = WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) !=
                  FALSE &&
              written == text.size();
    if (!ok) {
        log::error(L"Cannot write {}: {}", temp.wstring(), win32Error(GetLastError()));
    } else if (!FlushFileBuffers(handle)) {
        // The rename below is only crash-safe once the bytes have reached the disk;
        // otherwise a power cut can leave the new name pointing at an empty file.
        log::error(L"Cannot flush {}: {}", temp.wstring(), win32Error(GetLastError()));
        ok = false;
    }

    CloseHandle(handle);
    return ok;
}

}  // namespace

std::wstring_view displayName(NotificationEvent event) {
    constexpr std::array<Text, kNotificationEventCount> kNames = {
        Text::EventConnected, Text::EventDisconnected, Text::EventLow,
        Text::EventCritical,  Text::EventCharged,
    };
    return text(kNames[index(event)]);
}

std::array<EventSettings, kNotificationEventCount> Settings::defaultEvents() {
    std::array<EventSettings, kNotificationEventCount> events{};

    events[index(NotificationEvent::Connected)].volume = 0.7f;
    events[index(NotificationEvent::Disconnected)].volume = 0.7f;
    events[index(NotificationEvent::BatteryLow)].volume = 0.9f;
    events[index(NotificationEvent::FullyCharged)].volume = 0.6f;

    // The one event a user must not miss because the flyout appeared while they were away
    // from the screen; the Action Center keeps it.
    events[index(NotificationEvent::BatteryCritical)].showSystemToast = true;

    return events;
}

Settings Settings::load(std::filesystem::path const& file) {
    Settings settings;

    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        log::info(L"No settings file at {} yet; starting from the defaults", file.wstring());
        return settings;
    }

    std::string const document = readWholeFile(file);
    if (document.empty()) {
        log::warning(L"Settings file {} is empty or unreadable; using the defaults",
                     file.wstring());
        return settings;
    }

    std::string error;
    json::Value const root = json::parse(document, &error);
    if (!error.empty()) {
        log::error(L"Settings file {} is malformed ({}); using the defaults", file.wstring(),
                   widen(error));
        return settings;
    }
    if (root.kind() != json::Value::Kind::Object) {
        log::error(L"Settings file {} does not hold a JSON object; using the defaults",
                   file.wstring());
        return settings;
    }

    int const fileVersion = root["version"].asInt(kCurrentVersion);
    if (fileVersion > kCurrentVersion) {
        // The version is carried on the returned settings rather than dropped, and save()
        // refuses to write anything holding one. Without that, this branch protects the file
        // only until the user changes a single setting, at which point the store saves and
        // every key this build does not know about is gone for good.
        settings.version = fileVersion;
        log::warning(L"Settings file version {} is newer than this build understands ({}); "
                     L"using the defaults, and leaving the file alone",
                     fileVersion, kCurrentVersion);
        return settings;
    }

    settings.startWithWindows = root["startWithWindows"].asBool(settings.startWithWindows);
    settings.startMinimised = root["startMinimised"].asBool(settings.startMinimised);
    settings.minimiseToTrayOnClose =
        root["minimiseToTrayOnClose"].asBool(settings.minimiseToTrayOnClose);
    settings.includeNonXboxGamepads =
        root["includeNonXboxGamepads"].asBool(settings.includeNonXboxGamepads);

    settings.pollIntervalSeconds =
        std::clamp(root["pollIntervalSeconds"].asInt(settings.pollIntervalSeconds),
                   kMinPollSeconds, kMaxPollSeconds);
    settings.lowThresholdPercent =
        std::clamp(root["lowThresholdPercent"].asInt(settings.lowThresholdPercent),
                   kMinLowThresholdPercent, kMaxThresholdPercent);
    settings.criticalThresholdPercent =
        std::clamp(root["criticalThresholdPercent"].asInt(settings.criticalThresholdPercent),
                   kMinThresholdPercent, kMaxThresholdPercent);
    // A critical threshold at or above the low one would either never fire or would fire
    // both alerts on the same reading.
    if (settings.criticalThresholdPercent >= settings.lowThresholdPercent) {
        settings.criticalThresholdPercent =
            std::max(kMinThresholdPercent, settings.lowThresholdPercent - 1);
    }
    settings.notificationCooldownMinutes =
        std::clamp(root["notificationCooldownMinutes"].asInt(settings.notificationCooldownMinutes),
                   0, kMaxCooldownMinutes);

    settings.theme = parseEnum(kThemeNames, root["theme"], settings.theme);
    settings.language = parseEnum(kLanguageNames, root["language"], settings.language);
    settings.trayStyle = parseEnum(kTrayStyleNames, root["trayStyle"], settings.trayStyle);
    settings.trayColor = parseEnum(kTrayColorNames, root["trayColor"], settings.trayColor);
    settings.toastPosition =
        parseEnum(kToastPositionNames, root["toastPosition"], settings.toastPosition);

    settings.backdrop = parseEnum(kBackdropNames, root["backdrop"], settings.backdrop);
    settings.windowOpacity =
        std::clamp(root["windowOpacity"].asFloat(settings.windowOpacity),
                   static_cast<float>(kMinimumWindowOpacity), 1.0f);

    settings.masterVolume =
        std::clamp(root["masterVolume"].asFloat(settings.masterVolume), 0.0f, 1.0f);

    settings.historyEnabled = root["historyEnabled"].asBool(settings.historyEnabled);
    settings.historyRetentionDays =
        std::clamp(root["historyRetentionDays"].asInt(settings.historyRetentionDays),
                   kMinRetentionDays, kMaxRetentionDays);

    readEvents(settings, root);
    return settings;
}

bool Settings::save(std::filesystem::path const& file) const {
    // These are the defaults standing in for a file this build could not read, and writing
    // them would replace a newer configuration with them. A user who runs an old build once
    // -- from a portable copy, say -- would lose everything the newer one had stored.
    if (version > kCurrentVersion) {
        log::warning(L"Not writing {}: it was left by version {}, which this build ({}) does "
                     L"not understand",
                     file.wstring(), version, kCurrentVersion);
        return false;
    }

    if (!file.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        if (ec) {
            log::error(L"Cannot create {}: {}", file.parent_path().wstring(),
                       win32Error(static_cast<DWORD>(ec.value())));
            return false;
        }
    }

    std::string text = json::dump(toJson(*this), 2);
    text.push_back('\n');

    std::filesystem::path temp = file;
    temp += L".tmp";
    if (!writeTempFile(temp, text)) {
        DeleteFileW(temp.c_str());
        return false;
    }

    // ReplaceFileW preserves the ACLs, the creation time and the file id of the settings
    // file, but it insists the destination already exists -- which it does not the first
    // time anything is saved.
    if (ReplaceFileW(file.c_str(), temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr,
                     nullptr)) {
        return true;
    }

    DWORD const replaceError = GetLastError();
    if (replaceError == ERROR_FILE_NOT_FOUND &&
        MoveFileExW(temp.c_str(), file.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }

    log::error(L"Cannot replace {}: {}", file.wstring(), win32Error(replaceError));
    DeleteFileW(temp.c_str());
    return false;
}

SettingsStore& SettingsStore::instance() {
    static SettingsStore store;
    return store;
}

void SettingsStore::load() {
    m_settings = Settings::load(paths::settingsFile());
}

void SettingsStore::apply(Settings next) {
    Settings const previous = std::move(m_settings);
    m_settings = std::move(next);

    if (!m_settings.save(paths::settingsFile())) {
        log::warning(L"The new settings are in effect but could not be written to disk");
    }

    changed(m_settings, previous);
}

}  // namespace peek
