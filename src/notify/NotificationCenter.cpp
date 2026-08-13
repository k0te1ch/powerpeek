#include "notify/NotificationCenter.h"

#include <cstdio>
#include <functional>
#include <map>
#include <string>

#include "audio/AudioEngine.h"
#include "core/Logger.h"
#include "core/Strings.h"
#include "notify/SystemToast.h"
#include "notify/ToastWindow.h"
#include "ui/Theme.h"

// The resources directory is not on the include path -- only src is -- and the built-in
// sound identifiers have to agree with app.rc, so the one authoritative header is reached
// by relative path rather than copied as literals.
#include "../../resources/resource.h"

namespace peek::notify {
namespace {

// A product name rather than a translated string: the test button is showing what a real
// notification looks like, and hardware model names are not localised.
constexpr wchar_t kPreviewController[] = L"Xbox Wireless Controller";
constexpr int kPreviewLevel = 78;

Text titleTextFor(NotificationEvent event) {
    switch (event) {
        case NotificationEvent::Connected: return Text::ToastConnected;
        case NotificationEvent::Disconnected: return Text::ToastDisconnected;
        case NotificationEvent::BatteryLow: return Text::ToastLow;
        case NotificationEvent::BatteryCritical: return Text::ToastCritical;
        case NotificationEvent::FullyCharged: return Text::ToastCharged;
    }
    return Text::AppName;
}

std::wstring makeTitle(NotificationEvent event, ControllerInfo const& controller) {
    Text const id = titleTextFor(event);
    // Only the connection events name the controller in the title; for the rest the
    // headline is the condition and the name belongs in the body.
    if (id == Text::ToastConnected || id == Text::ToastDisconnected) {
        return formatText(id, controller.name);
    }
    return std::wstring(text(id));
}

std::wstring makeBody(ControllerInfo const& controller) {
    if (controller.percent < 0) {
        return formatText(Text::ToastBodyNoLevel, controller.name);
    }
    return formatText(Text::ToastBodyLevel, controller.name, controller.percent);
}

struct Appearance {
    std::wstring glyph;
    D2D1_COLOR_F color;
};

Appearance appearanceFor(NotificationEvent event) {
    ui::Palette const& colors = ui::theme().colors();
    switch (event) {
        case NotificationEvent::Connected:
            return {ui::glyph::kGamepad, colors.accent};
        case NotificationEvent::Disconnected:
            return {ui::glyph::kGamepad, colors.textSecondary};
        case NotificationEvent::BatteryLow:
            return {ui::glyph::kWarning, colors.caution};
        case NotificationEvent::BatteryCritical:
            return {ui::glyph::kWarning, colors.critical};
        case NotificationEvent::FullyCharged:
            return {ui::glyph::kCompleted, colors.success};
    }
    return {ui::glyph::kInfo, colors.accent};
}

// One tag per event per controller, so a controller sitting on the low threshold replaces
// its own card while a second controller still gets one of its own.
std::wstring toastTag(NotificationEvent event, ControllerInfo const& controller) {
    auto const hash = static_cast<unsigned>(std::hash<std::wstring>{}(controller.id));
    wchar_t buffer[24]{};
    std::swprintf(buffer, std::size(buffer), L"%zu-%08x", index(event), hash);
    return buffer;
}

ToastContent makeContent(NotificationEvent event, ControllerInfo const& controller) {
    ToastContent content;
    content.title = makeTitle(event, controller);
    content.body = makeBody(controller);

    Appearance const appearance = appearanceFor(event);
    content.glyph = appearance.glyph;
    content.badge = appearance.color;

    content.gauge.percent = controller.percent;
    content.gauge.fill =
        controller.percent > 0 ? static_cast<float>(controller.percent) / 100.0f : 0.0f;
    content.gauge.charging = controller.charge == ChargeState::Charging;
    content.gauge.approximate = controller.fidelity == Fidelity::Coarse;
    // A pad that has just gone is not reporting a level any more; showing its last one
    // next to "disconnected" reads as though it were still there.
    content.showGauge = controller.percent >= 0 && event != NotificationEvent::Disconnected;
    return content;
}

ControllerInfo previewController(NotificationEvent event, Settings const& settings) {
    ControllerInfo controller;
    controller.id = L"preview";
    controller.name = kPreviewController;
    controller.fidelity = Fidelity::Exact;
    controller.source = PowerSource::Battery;
    controller.charge = ChargeState::Discharging;
    controller.isXboxController = true;

    switch (event) {
        case NotificationEvent::BatteryLow:
            controller.percent = settings.lowThresholdPercent;
            break;
        case NotificationEvent::BatteryCritical:
            controller.percent = settings.criticalThresholdPercent;
            break;
        case NotificationEvent::FullyCharged:
            controller.percent = 100;
            controller.charge = ChargeState::Full;
            break;
        case NotificationEvent::Connected:
        case NotificationEvent::Disconnected:
            controller.percent = kPreviewLevel;
            break;
    }
    return controller;
}

}  // namespace

struct NotificationCenter::Impl {
    HWND owner = nullptr;
    Settings settings;
    // Keyed by the resolved sound, so pointing two events at the same file decodes it
    // once and changing the path is picked up without any invalidation at all.
    std::map<std::wstring, audio::PcmClip> clips;
    Signal<>::Token clickToken = 0;

    audio::PcmClip const& clipFor(NotificationEvent event);
    void playFor(NotificationEvent event);
    void deliver(NotificationEvent event, ControllerInfo const& controller, bool ignoreEnabled);
};

audio::PcmClip const& NotificationCenter::Impl::clipFor(NotificationEvent event) {
    EventSettings const& config = settings.forEvent(event);
    int const resource = IDW_SOUND_FIRST + static_cast<int>(index(event));

    std::wstring key = config.soundFile.empty() ? L"builtin:" + std::to_wstring(resource)
                                                : config.soundFile;
    if (auto const found = clips.find(key); found != clips.end()) {
        return found->second;
    }

    audio::PcmClip clip;
    if (config.soundFile.empty()) {
        clip = audio::decodeResource(resource);
    } else {
        clip = audio::decodeFile(config.soundFile);
        if (!clip.valid()) {
            // decodeFile already said why; the point here is that the user still hears
            // something rather than silently losing the notification.
            log::warning(L"Falling back to the built-in sound for {}", displayName(event));
            clip = audio::decodeResource(resource);
        }
    }

    // Cached even when invalid, so a broken file is not re-decoded on every event.
    return clips.emplace(std::move(key), std::move(clip)).first->second;
}

void NotificationCenter::Impl::playFor(NotificationEvent event) {
    audio::PcmClip const& clip = clipFor(event);
    if (!clip.valid()) {
        return;
    }
    audio::AudioEngine::instance().play(clip, settings.forEvent(event).volume);
}

void NotificationCenter::Impl::deliver(NotificationEvent event,
                                       ControllerInfo const& controller,
                                       bool ignoreEnabled) {
    EventSettings const& config = settings.forEvent(event);
    if (!config.enabled && !ignoreEnabled) {
        return;
    }

    if (config.playSound) {
        playFor(event);
    }

    bool flyoutShown = false;
    if (config.showFlyout) {
        ToastStack::instance().show(makeContent(event, controller));
        flyoutShown = true;
    }

    if (config.showSystemToast) {
        bool const raised = systemToast::show(makeTitle(event, controller), makeBody(controller),
                                              toastTag(event, controller));
        if (!raised && !flyoutShown) {
            // Windows would not take it and the user asked for no flyout, which would
            // otherwise leave the event with no visible trace at all.
            ToastStack::instance().show(makeContent(event, controller));
        }
    }
}

NotificationCenter::NotificationCenter(HWND owner) : m_impl(std::make_unique<Impl>()) {
    m_impl->owner = owner;
    m_impl->settings = SettingsStore::instance().get();
    audio::AudioEngine::instance().setMasterVolume(m_impl->settings.masterVolume);

    m_impl->clickToken = ToastStack::instance().clicked.connect([owner] {
        PostMessageW(owner, kFlyoutClickedMessage, 0, 0);
    });
}

NotificationCenter::~NotificationCenter() {
    ToastStack::instance().clicked.disconnect(m_impl->clickToken);
    ToastStack::instance().dismissAll();
}

void NotificationCenter::post(DetectedEvent const& event) {
    m_impl->deliver(event.event, event.controller, false);
}

void NotificationCenter::preview(NotificationEvent event) {
    // Ignoring `enabled` deliberately: the test button has to demonstrate the event even
    // while the user still has it switched off.
    m_impl->deliver(event, previewController(event, m_impl->settings), true);
}

void NotificationCenter::previewSound(NotificationEvent event) {
    // Cuts whatever the previous press started, so holding down the test button does not
    // pile clips on top of each other.
    audio::AudioEngine::instance().stopAll();
    m_impl->playFor(event);
}

void NotificationCenter::applySettings(Settings const& settings) {
    m_impl->settings = settings;
    audio::AudioEngine::instance().setMasterVolume(settings.masterVolume);
}

void NotificationCenter::invalidateSoundCache() { m_impl->clips.clear(); }

}  // namespace peek::notify
