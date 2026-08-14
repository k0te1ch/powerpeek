#include "ui/pages/SettingsPage.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <initializer_list>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/Logger.h"
#include "core/Strings.h"
#include "platform/Platform.h"
#include "ui/pages/PageWidgets.h"

namespace peek::ui {
namespace {

constexpr int kPollOptions[]{10, 15, 30, 60, 120, 300};
constexpr int kCooldownOptions[]{5, 15, 30, 60, 120, 240};
constexpr int kRetentionOptions[]{7, 14, 30, 90, 365};

constexpr float kThresholdStep = 5.0f;
constexpr float kLowMinimum = 10.0f;
constexpr float kLowMaximum = 50.0f;
constexpr float kCriticalMinimum = 5.0f;

constexpr float kOpacityStep = 0.05f;

// Where the opacity lands the first time a backdrop is chosen. A backdrop behind a fully
// opaque window is invisible, and a control that appears to do nothing reads as broken.
constexpr float kBackdropOpacity = 0.85f;

// Listed worst-to-best, which is also the order they degrade in.
constexpr std::array<std::pair<BackdropMode, Text>, 4> kBackdropModes = {{
    {BackdropMode::Opaque, Text::BackdropOpaque},
    {BackdropMode::Blur, Text::BackdropBlur},
    {BackdropMode::Acrylic, Text::BackdropAcrylic},
    {BackdropMode::Mica, Text::BackdropMica},
}};

// The stored value need not be one of the offered ones -- an older build or a hand-edited
// file can hold anything -- so the nearest option is selected rather than the first.
int nearestIndex(std::span<int const> options, int value) {
    int best = 0;
    for (std::size_t i = 1; i < options.size(); ++i) {
        if (std::abs(options[i] - value) < std::abs(options[best] - value)) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::wstring secondsLabel(int seconds) {
    if (seconds < 60) {
        return std::format(L"{} {}", seconds, text(Text::UnitSeconds));
    }
    return std::format(L"{} {}", seconds / 60, text(Text::UnitMinutes));
}

std::wstring minutesLabel(int minutes) {
    if (minutes < 60) {
        return std::format(L"{} {}", minutes, text(Text::UnitMinutes));
    }
    return std::format(L"{} {}", minutes / 60, text(Text::UnitHours));
}

std::wstring daysLabel(int days) { return std::format(L"{} {}", days, text(Text::UnitDays)); }

std::wstring percentLabel(float value) {
    return std::format(L"{} {}", static_cast<int>(value + 0.5f), text(Text::UnitPercent));
}

std::wstring fractionLabel(float value) { return percentLabel(value * 100.0f); }

template <class Labeller>
std::vector<std::wstring> labelsFor(std::span<int const> options, Labeller labeller) {
    std::vector<std::wstring> labels;
    labels.reserve(options.size());
    for (int option : options) {
        labels.push_back(labeller(option));
    }
    return labels;
}

std::vector<std::wstring> textsFor(std::initializer_list<Text> ids) {
    std::vector<std::wstring> labels;
    labels.reserve(ids.size());
    for (Text id : ids) {
        labels.emplace_back(text(id));
    }
    return labels;
}

}  // namespace

SettingsPage::SettingsPage(PageContext context) : Page(std::move(context)) {}

void SettingsPage::build(StackPanel& column) {
    m_low = nullptr;
    m_critical = nullptr;
    m_opacity = nullptr;

    column.emplace<PageHeader>(std::wstring(text(Text::SettingsTitle)));
    addGeneral(column);
    addMonitoring(column);
    addAppearance(column);
    addHistory(column);
}

void SettingsPage::addGeneral(StackPanel& column) {
    auto* group = column.emplace<SettingsGroup>(std::wstring(text(Text::SettingsGroupGeneral)));

    auto* autostart = group->addCard(glyph::kRefresh, std::wstring(text(Text::StartWithWindows)));
    autostart->setDescription(std::wstring(text(Text::StartWithWindowsDesc)));
    // The registry is the truth here, not the settings file: the user may have deleted the Run
    // entry by hand since the last launch.
    autostart->setControl(std::make_unique<ToggleSwitch>(
        platform::isAutostartEnabled(), [this, autostart](bool on) {
            if (!platform::setAutostartEnabled(on)) {
                log::error(L"Could not write the autostart entry; leaving the switch as it was");
                static_cast<ToggleSwitch*>(autostart->control())->setValue(!on);
                return;
            }
            Settings next = SettingsStore::instance().get();
            next.startWithWindows = on;
            m_context.applySettings(std::move(next));
        }));

    auto* minimised = group->addCard(glyph::kChevronDown, std::wstring(text(Text::StartMinimised)));
    minimised->setDescription(std::wstring(text(Text::StartMinimisedDesc)));
    minimised->setControl(std::make_unique<ToggleSwitch>(
        SettingsStore::instance().get().startMinimised, [this](bool on) {
            Settings next = SettingsStore::instance().get();
            next.startMinimised = on;
            m_context.applySettings(std::move(next));
        }));

    auto* tray = group->addCard(glyph::kClose, std::wstring(text(Text::MinimiseToTray)));
    tray->setDescription(std::wstring(text(Text::MinimiseToTrayDesc)));
    tray->setControl(std::make_unique<ToggleSwitch>(
        SettingsStore::instance().get().minimiseToTrayOnClose, [this](bool on) {
            Settings next = SettingsStore::instance().get();
            next.minimiseToTrayOnClose = on;
            m_context.applySettings(std::move(next));
        }));
}

void SettingsPage::addMonitoring(StackPanel& column) {
    auto* group = column.emplace<SettingsGroup>(std::wstring(text(Text::SettingsGroupMonitoring)));

    auto* nonXbox = group->addCard(glyph::kGamepad, std::wstring(text(Text::IncludeNonXbox)));
    nonXbox->setDescription(std::wstring(text(Text::IncludeNonXboxDesc)));
    nonXbox->setControl(std::make_unique<ToggleSwitch>(
        SettingsStore::instance().get().includeNonXboxGamepads, [this](bool on) {
            Settings next = SettingsStore::instance().get();
            next.includeNonXboxGamepads = on;
            m_context.applySettings(std::move(next));
        }));

    auto* poll = group->addCard(glyph::kRefresh, std::wstring(text(Text::PollInterval)));
    poll->setDescription(std::wstring(text(Text::PollIntervalDesc)));
    poll->setControl(std::make_unique<ComboBox>(
        labelsFor(kPollOptions, secondsLabel),
        nearestIndex(kPollOptions, SettingsStore::instance().get().pollIntervalSeconds),
        [this](int picked) {
            Settings next = SettingsStore::instance().get();
            next.pollIntervalSeconds = kPollOptions[static_cast<std::size_t>(picked)];
            m_context.applySettings(std::move(next));
        }));

    addThresholds(*group);

    auto* cooldown = group->addCard(glyph::kWarning, std::wstring(text(Text::Cooldown)));
    cooldown->setDescription(std::wstring(text(Text::CooldownDesc)));
    cooldown->setControl(std::make_unique<ComboBox>(
        labelsFor(kCooldownOptions, minutesLabel),
        nearestIndex(kCooldownOptions, SettingsStore::instance().get().notificationCooldownMinutes),
        [this](int picked) {
            Settings next = SettingsStore::instance().get();
            next.notificationCooldownMinutes = kCooldownOptions[static_cast<std::size_t>(picked)];
            m_context.applySettings(std::move(next));
        }));
}

void SettingsPage::addThresholds(SettingsGroup& group) {
    Settings const& current = SettingsStore::instance().get();

    auto* low = group.addCard(glyph::kBatteryUnknown, std::wstring(text(Text::LowThreshold)));
    low->setDescription(std::wstring(text(Text::LowThresholdDesc)));
    auto lowSlider = std::make_unique<Slider>(
        kLowMinimum, kLowMaximum, static_cast<float>(current.lowThresholdPercent),
        [this](float value) {
            Settings next = SettingsStore::instance().get();
            next.lowThresholdPercent = static_cast<int>(value);
            // Critical below low is the whole point of having two of them; pushing the other
            // slider is honest about it, where a silent clamp on save is not.
            if (next.criticalThresholdPercent >= next.lowThresholdPercent) {
                next.criticalThresholdPercent =
                    std::max(static_cast<int>(kCriticalMinimum),
                             next.lowThresholdPercent - static_cast<int>(kThresholdStep));
                m_critical->setValue(static_cast<float>(next.criticalThresholdPercent));
            }
            m_context.applySettings(std::move(next));
        });
    lowSlider->setStep(kThresholdStep);
    lowSlider->setFormatter(percentLabel);
    m_low = static_cast<Slider*>(low->setControl(std::move(lowSlider)));

    auto* critical =
        group.addCard(glyph::kWarning, std::wstring(text(Text::CriticalThreshold)));
    critical->setDescription(std::wstring(text(Text::CriticalThresholdDesc)));
    auto criticalSlider = std::make_unique<Slider>(
        kCriticalMinimum, kLowMaximum - kThresholdStep,
        static_cast<float>(current.criticalThresholdPercent), [this](float value) {
            Settings next = SettingsStore::instance().get();
            next.criticalThresholdPercent = static_cast<int>(value);
            if (next.criticalThresholdPercent >= next.lowThresholdPercent) {
                next.lowThresholdPercent =
                    std::min(static_cast<int>(kLowMaximum),
                             next.criticalThresholdPercent + static_cast<int>(kThresholdStep));
                m_low->setValue(static_cast<float>(next.lowThresholdPercent));
            }
            m_context.applySettings(std::move(next));
        });
    criticalSlider->setStep(kThresholdStep);
    criticalSlider->setFormatter(percentLabel);
    m_critical = static_cast<Slider*>(critical->setControl(std::move(criticalSlider)));
}

void SettingsPage::addAppearance(StackPanel& column) {
    auto* group = column.emplace<SettingsGroup>(std::wstring(text(Text::SettingsGroupAppearance)));
    Settings const& current = SettingsStore::instance().get();

    auto* themeCard = group->addCard(glyph::kSettings, std::wstring(text(Text::ThemeLabel)));
    themeCard->setControl(std::make_unique<ComboBox>(
        textsFor({Text::ThemeSystem, Text::ThemeLight, Text::ThemeDark}),
        static_cast<int>(current.theme), [this](int picked) {
            Settings next = SettingsStore::instance().get();
            next.theme = static_cast<ThemePreference>(picked);
            m_context.applySettings(std::move(next));
        }));

    auto* languageCard = group->addCard(glyph::kInfo, std::wstring(text(Text::LanguageLabel)));
    languageCard->setControl(std::make_unique<ComboBox>(
        textsFor({Text::LanguageSystem, Text::LanguageEnglish, Text::LanguageRussian}),
        static_cast<int>(current.language), [this](int picked) {
            Settings next = SettingsStore::instance().get();
            next.language = static_cast<LanguagePreference>(picked);
            m_context.applySettings(std::move(next));
        }));

    auto* trayCard = group->addCard(glyph::kBatteryUnknown, std::wstring(text(Text::TrayStyleLabel)));
    trayCard->setControl(std::make_unique<ComboBox>(
        textsFor({Text::TrayStyleBattery, Text::TrayStyleRing, Text::TrayStylePercentage}),
        static_cast<int>(current.trayStyle), [this](int picked) {
            Settings next = SettingsStore::instance().get();
            next.trayStyle = static_cast<TrayStyle>(picked);
            m_context.applySettings(std::move(next));
        }));

    auto* trayColourCard = group->addCard(glyph::kSettings, std::wstring(text(Text::TrayColorLabel)));
    trayColourCard->setControl(std::make_unique<ComboBox>(
        textsFor({Text::TrayColorAuto, Text::TrayColorAccent, Text::TrayColorWhite,
                  Text::TrayColorGreen, Text::TrayColorBlue, Text::TrayColorPink}),
        static_cast<int>(current.trayColor), [this](int picked) {
            Settings next = SettingsStore::instance().get();
            next.trayColor = static_cast<TrayColor>(picked);
            m_context.applySettings(std::move(next));
        }));

    addBackdrop(*group);
}

void SettingsPage::addBackdrop(SettingsGroup& group) {
    Settings const& current = SettingsStore::instance().get();

    // Only the modes this Windows can really paint go into the list. Offering the rest and
    // quietly substituting something else would be a lie told in the one place the user
    // goes to find out what their machine can do.
    std::vector<BackdropMode> offered;
    std::vector<std::wstring> labels;
    for (auto const& [mode, label] : kBackdropModes) {
        if (platform::backdropSupported(mode)) {
            offered.push_back(mode);
            labels.emplace_back(text(label));
        }
    }

    // What is in effect, not what is stored: a file carrying Mica opened on Windows 10 has
    // to show the blur it actually got.
    BackdropMode const active = platform::effectiveBackdrop(current.backdrop);
    int selected = 0;
    for (std::size_t i = 0; i < offered.size(); ++i) {
        if (offered[i] == active) {
            selected = static_cast<int>(i);
        }
    }

    auto* card = group.addCard(glyph::kMaximise, std::wstring(text(Text::BackdropLabel)));
    card->setDescription(std::wstring(text(Text::BackdropDesc)));
    card->setControl(std::make_unique<ComboBox>(
        std::move(labels), selected, [this, offered](int picked) {
            Settings next = SettingsStore::instance().get();
            next.backdrop = offered[static_cast<std::size_t>(picked)];
            // A backdrop behind an opaque window shows nothing at all, so the first one
            // chosen takes the opacity off its ceiling and moves the slider with it -- the
            // same push the two threshold sliders give each other.
            if (next.backdrop != BackdropMode::Opaque && next.windowOpacity >= 1.0f) {
                next.windowOpacity = kBackdropOpacity;
                m_opacity->setValue(kBackdropOpacity);
            }
            m_context.applySettings(std::move(next));
        }));

    auto* opacityCard = group.addCard(glyph::kRestore, std::wstring(text(Text::WindowOpacity)));
    opacityCard->setDescription(std::wstring(text(Text::WindowOpacityDesc)));
    auto slider = std::make_unique<Slider>(kMinimumWindowOpacity, 1.0f, current.windowOpacity,
                                           [this](float value) {
                                               Settings next = SettingsStore::instance().get();
                                               next.windowOpacity = value;
                                               m_context.applySettings(std::move(next));
                                           });
    slider->setStep(kOpacityStep);
    slider->setFormatter(fractionLabel);
    m_opacity = static_cast<Slider*>(opacityCard->setControl(std::move(slider)));
}

void SettingsPage::addHistory(StackPanel& column) {
    auto* group = column.emplace<SettingsGroup>(std::wstring(text(Text::HistoryTitle)));

    auto* enabled = group->addCard(glyph::kChart, std::wstring(text(Text::HistoryEnabled)));
    enabled->setDescription(std::wstring(text(Text::HistoryEnabledDesc)));
    enabled->setControl(std::make_unique<ToggleSwitch>(
        SettingsStore::instance().get().historyEnabled, [this](bool on) {
            Settings next = SettingsStore::instance().get();
            next.historyEnabled = on;
            m_context.applySettings(std::move(next));
        }));

    auto* retention = group->addCard(glyph::kFolder, std::wstring(text(Text::HistoryRetention)));
    retention->setDescription(std::wstring(text(Text::HistoryRetentionDesc)));
    retention->setControl(std::make_unique<ComboBox>(
        labelsFor(kRetentionOptions, daysLabel),
        nearestIndex(kRetentionOptions, SettingsStore::instance().get().historyRetentionDays),
        [this](int picked) {
            Settings next = SettingsStore::instance().get();
            next.historyRetentionDays = kRetentionOptions[static_cast<std::size_t>(picked)];
            m_context.applySettings(std::move(next));
        }));
}

}  // namespace peek::ui
