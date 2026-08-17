#include "ui/pages/SoundsPage.h"

#include <shobjidl.h>

#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <utility>

#include "core/Logger.h"
#include "core/Strings.h"
#include "ui/pages/PageWidgets.h"

namespace peek::ui {
namespace {

std::wstring_view eventGlyph(NotificationEvent event) {
    switch (event) {
        case NotificationEvent::Connected: return glyph::kGamepad;
        case NotificationEvent::Disconnected: return glyph::kClose;
        case NotificationEvent::BatteryLow: return glyph::kBatteryUnknown;
        case NotificationEvent::BatteryCritical: return glyph::kWarning;
        case NotificationEvent::FullyCharged: break;
    }
    return glyph::kCompleted;
}

std::wstring percentLabel(float fraction) {
    return std::format(L"{} {}", static_cast<int>(fraction * 100.0f + 0.5f),
                       text(Text::UnitPercent));
}

// Disabling a container only stops the container itself from taking input; the buttons inside
// the sound row are hit-tested in their own right and have to be told separately.
void setControlEnabled(Widget* control, bool enabled) {
    if (!control) {
        return;
    }
    control->setEnabled(enabled);
    if (auto* container = dynamic_cast<Container*>(control)) {
        for (auto const& child : container->children()) {
            child->setEnabled(enabled);
        }
    }
}

// The name shown for the configured sound: the file's own name, or the built-in one.
std::wstring soundLabel(EventSettings const& event) {
    if (event.soundFile.empty()) {
        return std::wstring(text(Text::SoundBuiltIn));
    }
    return std::filesystem::path(event.soundFile).filename().wstring();
}

}  // namespace

SoundsPage::SoundsPage(PageContext context) : Page(std::move(context)) {}

void SoundsPage::build(StackPanel& column) {
    m_rows = {};

    column.emplace<PageHeader>(std::wstring(text(Text::SoundsTitle)),
                               std::wstring(text(Text::SoundsSubtitle)));

    auto* master = column.emplace<SettingsCard>(glyph::kVolume,
                                                std::wstring(text(Text::MasterVolume)));
    auto masterSlider = std::make_unique<Slider>(0.0f, 1.0f,
                                                 SettingsStore::instance().get().masterVolume,
                                                 [this](float value) {
                                                     Settings next = SettingsStore::instance().get();
                                                     next.masterVolume = value;
                                                     m_context.applySettings(std::move(next));
                                                 });
    masterSlider->setStep(0.05);
    masterSlider->setFormatter(percentLabel);
    master->setControl(std::move(masterSlider));

    for (std::size_t i = 0; i < kNotificationEventCount; ++i) {
        addEvent(column, static_cast<NotificationEvent>(i));
    }
}

void SoundsPage::addEvent(StackPanel& column, NotificationEvent event) {
    EventSettings const& current = SettingsStore::instance().get().forEvent(event);
    EventRows& rows = m_rows[index(event)];

    rows.expander = column.emplace<Expander>(std::wstring(eventGlyph(event)),
                                             std::wstring(displayName(event)));
    rows.expander->setHeaderControl(std::make_unique<ToggleSwitch>(
        current.enabled, [this, event](bool on) {
            EventSettings next = SettingsStore::instance().get().forEvent(event);
            next.enabled = on;
            writeEvent(event, next);
            setEventEnabled(m_rows[index(event)], on);
        }));

    rows.sound = rows.expander->addRow(std::wstring(text(Text::PlaySound)));
    rows.sound->setControl(std::make_unique<ToggleSwitch>(current.playSound, [this, event](bool on) {
        EventSettings next = SettingsStore::instance().get().forEvent(event);
        next.playSound = on;
        writeEvent(event, next);
    }));

    rows.file = rows.expander->addRow(std::wstring(text(Text::SoundFile)));
    rows.file->setDescription(soundLabel(current));
    auto buttons = std::make_unique<ButtonRow>();
    auto* browse = buttons->emplace<Button>(std::wstring(text(Text::SoundBrowse)),
                                            [this, event] { chooseFile(event); });
    browse->setGlyph(glyph::kFolder);
    buttons->emplace<Button>(std::wstring(text(Text::SoundReset)), [this, event] {
        EventSettings next = SettingsStore::instance().get().forEvent(event);
        next.soundFile.clear();
        writeEvent(event, next);
        m_rows[index(event)].file->setDescription(soundLabel(next));
    });
    auto* test = buttons->emplace<Button>(std::wstring(text(Text::SoundTest)), [this, event] {
        if (m_context.notifications) {
            m_context.notifications->previewSound(event);
        }
    });
    test->setGlyph(glyph::kPlay);
    rows.file->setControl(std::move(buttons));

    rows.volume = rows.expander->addRow(std::wstring(text(Text::Volume)));
    auto volume = std::make_unique<Slider>(0.0f, 1.0f, current.volume, [this, event](float value) {
        EventSettings next = SettingsStore::instance().get().forEvent(event);
        next.volume = value;
        writeEvent(event, next);
    });
    volume->setStep(0.05);
    volume->setFormatter(percentLabel);
    rows.volume->setControl(std::move(volume));

    rows.flyout = rows.expander->addRow(std::wstring(text(Text::ShowFlyout)));
    rows.flyout->setControl(std::make_unique<ToggleSwitch>(current.showFlyout,
                                                           [this, event](bool on) {
                                                               EventSettings next =
                                                                   SettingsStore::instance()
                                                                       .get()
                                                                       .forEvent(event);
                                                               next.showFlyout = on;
                                                               writeEvent(event, next);
                                                           }));

    rows.toast = rows.expander->addRow(std::wstring(text(Text::ShowSystemToast)));
    rows.toast->setControl(std::make_unique<ToggleSwitch>(
        current.showSystemToast && m_context.systemToastsAvailable, [this, event](bool on) {
            EventSettings next = SettingsStore::instance().get().forEvent(event);
            next.showSystemToast = on;
            writeEvent(event, next);
        }));
    if (!m_context.systemToastsAvailable) {
        rows.toast->setDescription(std::wstring(text(Text::SystemToastUnavailable)));
    }

    setEventEnabled(rows, current.enabled);
}

void SoundsPage::setEventEnabled(EventRows const& rows, bool enabled) {
    for (SettingsCard* row : {rows.sound, rows.file, rows.volume, rows.flyout}) {
        if (row) {
            row->setEnabled(enabled);
            setControlEnabled(row->control(), enabled);
        }
    }
    if (rows.toast) {
        // The Windows toast switch has a second reason to stay off, and it outranks this one.
        bool const usable = enabled && m_context.systemToastsAvailable;
        rows.toast->setEnabled(usable);
        setControlEnabled(rows.toast->control(), usable);
    }
}

void SoundsPage::writeEvent(NotificationEvent event, EventSettings const& value) {
    Settings next = SettingsStore::instance().get();
    next.forEvent(event) = value;
    m_context.applySettings(std::move(next));
}

void SoundsPage::chooseFile(NotificationEvent event) {
    com_ptr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(dialog.put()));
    if (FAILED(hr)) {
        log::error(L"Could not create the file dialog: {}", describeHresult(hr));
        return;
    }

    std::wstring const filterName(text(Text::SoundFileFilter));
    COMDLG_FILTERSPEC const filters[]{
        {filterName.c_str(), L"*.wav;*.mp3;*.flac;*.m4a;*.ogg"}};
    dialog->SetFileTypes(ARRAYSIZE(filters), filters);
    dialog->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM |
                       FOS_NOCHANGEDIR);

    hr = dialog->Show(m_context.owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return;
    }
    if (FAILED(hr)) {
        log::warning(L"The file dialog failed: {}", describeHresult(hr));
        return;
    }

    com_ptr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put()))) {
        return;
    }
    wchar_t* path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) {
        return;
    }

    EventSettings next = SettingsStore::instance().get().forEvent(event);
    next.soundFile = path;
    CoTaskMemFree(path);

    writeEvent(event, next);
    m_rows[index(event)].file->setDescription(soundLabel(next));
    if (m_context.notifications) {
        m_context.notifications->invalidateSoundCache();
    }
}

}  // namespace peek::ui
