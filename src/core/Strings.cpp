#include "core/Strings.h"

#include <array>
#include <iterator>

#include "core/Win.h"

namespace peek {
namespace {

struct Entry {
    Text id;
    wchar_t const* en;
    wchar_t const* ru;
};

constexpr Entry kTable[] = {
    {Text::AppName, L"PowerPeek", L"PowerPeek"},
    {Text::AppTagline, L"Controller battery, at a glance", L"Заряд геймпадов Xbox под рукой"},

    {Text::NavDevices, L"Controllers", L"Геймпады"},
    {Text::NavHistory, L"History", L"История"},
    {Text::NavSounds, L"Sounds", L"Звуки"},
    {Text::NavSettings, L"Settings", L"Параметры"},
    {Text::NavAbout, L"About", L"О программе"},

    {Text::DevicesTitle, L"Controllers", L"Геймпады"},
    {Text::NoControllers, L"No controllers connected", L"Нет подключённых геймпадов"},
    {Text::NoControllersHint,
     L"Turn a controller on or plug it in, and it will show up here.",
     L"Включи геймпад или подключи его кабелем — он появится здесь."},
    {Text::Refresh, L"Refresh", L"Обновить"},
    {Text::ChargeLevel, L"Charge", L"Заряд"},
    {Text::StatusCharging, L"Charging", L"Заряжается"},
    {Text::StatusFull, L"Fully charged", L"Заряжен полностью"},
    {Text::StatusOnBattery, L"On battery", L"От батареи"},
    {Text::StatusWired, L"Wired, no battery", L"По кабелю, без батареи"},
    {Text::StatusUnknown, L"Unknown", L"Неизвестно"},
    {Text::ApproximateSuffix, L"approximate", L"примерно"},
    {Text::ConnectionUsb, L"USB", L"USB"},
    {Text::ConnectionWireless, L"Xbox Wireless", L"Xbox Wireless"},
    {Text::ConnectionBluetooth, L"Bluetooth", L"Bluetooth"},
    {Text::EstimatedRemaining, L"About {} left", L"Осталось примерно {}"},
    {Text::UpdatedJustNow, L"Updated just now", L"Обновлено только что"},
    {Text::UpdatedMinutesAgo, L"Updated {} min ago", L"Обновлено {} мин назад"},

    {Text::HistoryTitle, L"Battery history", L"История заряда"},
    {Text::HistoryEmpty, L"Not enough data yet", L"Пока недостаточно данных"},
    {Text::HistoryEmptyHint,
     L"The chart appears once a controller has spent a while running on battery.",
     L"График появится, когда геймпад поработает от батареи."},
    {Text::HistoryController, L"Controller", L"Геймпад"},
    {Text::HistoryRange, L"Time range", L"Период"},
    {Text::HistoryRange24h, L"24 hours", L"24 часа"},
    {Text::HistoryRange7d, L"7 days", L"7 дней"},
    {Text::HistoryRange30d, L"30 days", L"30 дней"},
    {Text::DrainRate, L"{}% per hour", L"{}% в час"},

    {Text::SoundsTitle, L"Sounds and notifications", L"Звуки и уведомления"},
    {Text::SoundsSubtitle, L"Choose what each event does.",
     L"Выбери, что происходит при каждом событии."},
    {Text::EventConnected, L"Controller connected", L"Геймпад подключён"},
    {Text::EventDisconnected, L"Controller disconnected", L"Геймпад отключён"},
    {Text::EventLow, L"Battery low", L"Низкий заряд"},
    {Text::EventCritical, L"Battery critically low", L"Критический заряд"},
    {Text::EventCharged, L"Fully charged", L"Заряжен полностью"},
    {Text::SoundFile, L"Sound", L"Звук"},
    {Text::SoundBuiltIn, L"Built-in", L"Встроенный"},
    {Text::SoundBrowse, L"Choose file", L"Выбрать файл"},
    {Text::SoundReset, L"Reset", L"Сбросить"},
    {Text::SoundTest, L"Test", L"Проверить"},
    {Text::SoundFileFilter, L"Audio files", L"Звуковые файлы"},
    {Text::Volume, L"Volume", L"Громкость"},
    {Text::MasterVolume, L"Master volume", L"Общая громкость"},
    {Text::PlaySound, L"Play sound", L"Проигрывать звук"},
    {Text::ShowFlyout, L"Show notification", L"Показывать уведомление"},
    {Text::ShowSystemToast, L"Windows notification", L"Уведомление Windows"},
    {Text::SystemToastUnavailable,
     L"Unavailable on this system. The app's own notifications still work.",
     L"Недоступно на этой системе. Собственные уведомления приложения работают."},

    {Text::SettingsTitle, L"Settings", L"Параметры"},
    {Text::SettingsGroupGeneral, L"General", L"Общие"},
    {Text::SettingsGroupMonitoring, L"Monitoring", L"Наблюдение"},
    {Text::SettingsGroupAppearance, L"Appearance", L"Оформление"},
    {Text::StartWithWindows, L"Start with Windows", L"Запускать вместе с Windows"},
    {Text::StartWithWindowsDesc,
     L"Launches when you sign in, for your account only.",
     L"Запускается при входе в систему, только для твоей учётной записи."},
    {Text::StartMinimised, L"Start minimised to the tray", L"Запускать свёрнутым в трей"},
    {Text::StartMinimisedDesc, L"The window stays hidden until you open it from the tray.",
     L"Окно не показывается, пока не откроешь его из трея."},
    {Text::MinimiseToTray, L"Close button minimises to the tray",
     L"Кнопка закрытия сворачивает в трей"},
    {Text::MinimiseToTrayDesc, L"Exit from the tray menu still quits for real.",
     L"Выход из меню в трее по-прежнему закрывает программу полностью."},
    {Text::IncludeNonXbox, L"Show non-Xbox gamepads", L"Показывать не-Xbox геймпады"},
    {Text::IncludeNonXboxDesc,
     L"Their battery readings are often wrong, so they are hidden by default.",
     L"Их показания заряда часто неверны, поэтому по умолчанию они скрыты."},
    {Text::PollInterval, L"Check every", L"Проверять каждые"},
    {Text::PollIntervalDesc, L"How often the battery level is read.",
     L"Как часто считывается уровень заряда."},
    {Text::LowThreshold, L"Low battery below", L"Низкий заряд ниже"},
    {Text::LowThresholdDesc, L"Crossing this level downwards fires the low-battery event.",
     L"Пересечение этого уровня вниз запускает событие низкого заряда."},
    {Text::CriticalThreshold, L"Critical below", L"Критический ниже"},
    {Text::CriticalThresholdDesc, L"The last warning before the controller dies.",
     L"Последнее предупреждение перед тем, как геймпад выключится."},
    {Text::Cooldown, L"Repeat no more than once every", L"Повторять не чаще одного раза в"},
    {Text::CooldownDesc,
     L"Keeps a controller resting on a threshold from notifying at every check.",
     L"Не даёт геймпаду, застрявшему на пороге, уведомлять при каждой проверке."},
    {Text::ThemeLabel, L"Theme", L"Тема"},
    {Text::ThemeSystem, L"System", L"Как в системе"},
    {Text::ThemeLight, L"Light", L"Светлая"},
    {Text::ThemeDark, L"Dark", L"Тёмная"},
    {Text::LanguageLabel, L"Language", L"Язык"},
    {Text::LanguageSystem, L"System", L"Как в системе"},
    {Text::LanguageEnglish, L"English", L"English"},
    {Text::LanguageRussian, L"Русский", L"Русский"},
    {Text::TrayStyleLabel, L"Tray icon", L"Значок в трее"},
    {Text::TrayStyleBattery, L"Battery", L"Батарея"},
    {Text::TrayStyleRing, L"Ring", L"Кольцо"},
    {Text::TrayStylePercentage, L"Percentage", L"Проценты"},
    {Text::TrayColorLabel, L"Tray icon colour", L"Цвет значка"},
    {Text::TrayColorAuto, L"Automatic", L"Автоматически"},
    {Text::TrayColorAccent, L"System accent", L"Акцент системы"},
    {Text::TrayColorWhite, L"White", L"Белый"},
    {Text::TrayColorGreen, L"Green", L"Зелёный"},
    {Text::TrayColorBlue, L"Blue", L"Синий"},
    {Text::TrayColorPink, L"Pink", L"Розовый"},
    {Text::ToastPositionLabel, L"Notification position", L"Положение уведомлений"},
    {Text::ToastPositionDesc,
     L"Where this application's own cards appear. Windows places its own notifications "
     L"itself, and this setting does not move those.",
     L"Где появляются собственные карточки приложения. Уведомления Windows расставляет "
     L"сама, и на них эта настройка не влияет."},
    {Text::ToastPositionTopLeft, L"Top left", L"Сверху слева"},
    {Text::ToastPositionTopCenter, L"Top centre", L"Сверху по центру"},
    {Text::ToastPositionTopRight, L"Top right", L"Сверху справа"},
    {Text::ToastPositionBottomLeft, L"Bottom left", L"Снизу слева"},
    {Text::ToastPositionBottomCenter, L"Bottom centre", L"Снизу по центру"},
    {Text::ToastPositionBottomRight, L"Bottom right", L"Снизу справа"},
    {Text::BackdropLabel, L"Window backdrop", L"Фон окна"},
    {Text::BackdropDesc, L"What shows through behind the window. Only what this system can "
                         L"actually draw is offered.",
     L"Что видно за окном. Предлагается только то, что эта система действительно умеет."},
    {Text::BackdropOpaque, L"Solid", L"Сплошной"},
    {Text::BackdropBlur, L"Blur", L"Размытие"},
    {Text::BackdropAcrylic, L"Acrylic", L"Акрил"},
    {Text::BackdropMica, L"Mica", L"Слюда"},
    {Text::WindowOpacity, L"Window opacity", L"Непрозрачность окна"},
    {Text::WindowOpacityDesc,
     L"How much of the backdrop comes through. It stops well short of invisible so that "
     L"text stays readable over any wallpaper.",
     L"Насколько сильно проступает фон. Ползунок не доходит до прозрачности, при которой "
     L"текст перестал бы читаться на любых обоях."},
    {Text::HistoryEnabled, L"Record history", L"Записывать историю"},
    {Text::HistoryEnabledDesc,
     L"Kept on this computer only. Nothing is ever sent anywhere.",
     L"Хранится только на этом компьютере. Никуда не отправляется."},
    {Text::HistoryRetention, L"Keep history for", L"Хранить историю"},
    {Text::HistoryRetentionDesc, L"Older samples are dropped at startup.",
     L"Более старые записи удаляются при запуске."},

    {Text::AboutVersion, L"Version {}", L"Версия {}"},
    {Text::AboutDescription,
     L"Controller battery in the notification area, with the warnings you choose. "
     L"No telemetry, no network access, nothing to install.",
     L"Заряд геймпадов в области уведомлений и предупреждения, которые ты сам настроишь. "
     L"Без телеметрии, без сети, без установки."},
    {Text::OpenDataFolder, L"Open data folder", L"Открыть папку данных"},
    {Text::OpenSourceRepository, L"Source code", L"Исходный код"},
    {Text::AboutAuthor, L"Made by k0te1ch", L"Автор — k0te1ch"},
    {Text::SupportAuthor, L"Support the author", L"Поддержать автора"},

    {Text::MenuOpen, L"Open", L"Открыть"},
    {Text::MenuRefresh, L"Refresh", L"Обновить"},
    {Text::MenuExit, L"Exit", L"Выход"},
    {Text::TrayMoreControllers, L"and {} more", L"и ещё {}"},

    {Text::ToastConnected, L"{} connected", L"{} подключён"},
    {Text::ToastDisconnected, L"{} disconnected", L"{} отключён"},
    {Text::ToastLow, L"Battery low", L"Низкий заряд"},
    {Text::ToastCritical, L"Battery critically low", L"Критический заряд"},
    {Text::ToastCharged, L"Fully charged", L"Заряжен полностью"},
    {Text::ToastBodyLevel, L"{} — {}%", L"{} — {}%"},
    {Text::ToastBodyNoLevel, L"{}", L"{}"},

    {Text::UnitSeconds, L"sec", L"сек"},
    {Text::UnitMinutes, L"min", L"мин"},
    {Text::UnitHours, L"h", L"ч"},
    {Text::UnitDays, L"days", L"дней"},
    {Text::UnitPercent, L"%", L"%"},
    {Text::Never, L"Never", L"Никогда"},
    {Text::Close, L"Close", L"Закрыть"},
    {Text::Minimise, L"Minimise", L"Свернуть"},
    {Text::Maximise, L"Maximise", L"Развернуть"},
    {Text::Restore, L"Restore", L"Восстановить"},
};

constexpr bool tableIsComplete() {
    for (std::size_t i = 0; i < std::size(kTable); ++i) {
        if (static_cast<std::size_t>(kTable[i].id) != i) {
            return false;
        }
    }
    return true;
}

// Entry is an aggregate, so a row written with one language -- {Text::Foo, L"English"} --
// compiles, and leaves the other column null. The size and order checks above both still
// pass, because the row is there and it is in the right place; only text() finds out, by
// handing a null pointer to std::wstring_view, which is undefined behaviour rather than a
// blank label. Both READMEs and CONTRIBUTING promise a missing translation is a compile
// error, and this is the half of that promise that was missing.
constexpr bool everyRowSpeaksBothLanguages() {
    for (Entry const& entry : kTable) {
        if (entry.en == nullptr || entry.ru == nullptr) {
            return false;
        }
        if (*entry.en == L'\0' || *entry.ru == L'\0') {
            return false;
        }
    }
    return true;
}

static_assert(std::size(kTable) == static_cast<std::size_t>(Text::Count),
              "every Text needs exactly one row in kTable");
static_assert(tableIsComplete(),
              "kTable rows must be in Text order, so a lookup is a plain index");
static_assert(everyRowSpeaksBothLanguages(),
              "every kTable row needs a non-empty English and Russian string");

bool g_useRussian = false;

}  // namespace

void setLanguage(LanguagePreference preference) {
    switch (preference) {
        case LanguagePreference::English:
            g_useRussian = false;
            return;
        case LanguagePreference::Russian:
            g_useRussian = true;
            return;
        case LanguagePreference::System:
            break;
    }

    // GetUserDefaultUILanguage reflects the display language the user actually reads,
    // which is what matters here; the locale (GetUserDefaultLCID) tracks number and date
    // formats and is frequently Russian on an English Windows.
    g_useRussian = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN;
}

std::wstring_view text(Text id) {
    Entry const& entry = kTable[static_cast<std::size_t>(id)];
    return g_useRussian ? entry.ru : entry.en;
}

}  // namespace peek
