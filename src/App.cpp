#include "App.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>
#include <vector>

#include "audio/AudioEngine.h"
#include "battery/BatteryHistory.h"
#include "battery/ControllerInfo.h"
#include "battery/ControllerMonitor.h"
#include "battery/EventDetector.h"
#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/Settings.h"
#include "core/Signal.h"
#include "core/Strings.h"
#include "notify/NotificationCenter.h"
#include "notify/SystemToast.h"
#include "platform/Platform.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/TrayIcon.h"

namespace peek {
namespace {

constexpr wchar_t kHubClassName[] = L"PowerPeek.HubWindow";

// The controller thread's "something moved" ping. WM_APP+0x10 and +0x11 already belong to
// the tray icon and the flyouts, and WM_APP+0x1F0 to D2DWindow's frame pump.
constexpr UINT kControllersChangedMessage = WM_APP + 0x20;

// One physical plug produces a burst of WM_DEVICECHANGE, and the pad is not enumerable at
// the first of them: the poll is worth doing once, after the burst has died down.
constexpr UINT_PTR kDeviceSettleTimer = 1;
constexpr UINT kDeviceSettleMs = 750;

std::chrono::seconds pollInterval(Settings const& settings) {
    return std::chrono::seconds{settings.pollIntervalSeconds};
}

std::chrono::days retention(Settings const& settings) {
    return std::chrono::days{std::max(1, settings.historyRetentionDays)};
}

bool wantsHiddenStart(Settings const& settings, int showCommand) {
    return settings.startMinimised || showCommand == SW_SHOWMINIMIZED ||
           showCommand == SW_SHOWMINNOACTIVE || showCommand == SW_MINIMIZE;
}

}  // namespace

struct App::Impl {
    HINSTANCE instance = nullptr;
    HWND hub = nullptr;

    platform::SingleInstance singleInstance;
    EventDetector detector;
    std::vector<ControllerInfo> controllers;

    std::unique_ptr<BatteryHistory> history;
    std::unique_ptr<notify::NotificationCenter> notifications;
    std::unique_ptr<ControllerMonitor> monitor;
    std::unique_ptr<ui::TrayIcon> tray;
    std::unique_ptr<ui::MainWindow> window;

    Signal<Settings const&, Settings const&>::Token settingsToken = 0;

    bool createHub();
    bool createSubsystems();
    void connectSignals();
    void shutdown();

    void onControllersChanged();
    void onSettingsChanged(Settings const& current, Settings const& previous);
    void onSystemColorsChanged();
    void toggleMainWindow();
    void showMainWindow();
    void requestExit();
    void saveSettings() const;

    static LRESULT CALLBACK hubProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
};

bool App::Impl::createHub() {
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &Impl::hubProc;
    description.hInstance = instance;
    description.lpszClassName = kHubClassName;
    if (RegisterClassExW(&description) == 0) {
        log::error(L"Could not register the hub window class: {}",
                   describeHresult(HRESULT_FROM_WIN32(GetLastError())));
        return false;
    }

    // Deliberately not HWND_MESSAGE: a message-only window receives neither HWND_BROADCAST
    // (the single-instance handshake) nor the shell's TaskbarCreated broadcast.
    hub = CreateWindowExW(WS_EX_TOOLWINDOW, kHubClassName, L"PowerPeek", WS_POPUP, 0, 0,
                          0, 0, nullptr, nullptr, instance, this);
    if (hub == nullptr) {
        log::error(L"Could not create the hub window: {}",
                   describeHresult(HRESULT_FROM_WIN32(GetLastError())));
        return false;
    }

    // SetForegroundWindow -- which TrackPopupMenu needs before the tray menu will dismiss
    // on an outside click -- is refused for a window that has never been shown. The window
    // is zero by zero and WS_EX_TOOLWINDOW, so showing it puts nothing on screen and
    // nothing in the taskbar or Alt+Tab.
    ShowWindow(hub, SW_SHOWNA);

    // An elevated instance would otherwise never hear the broadcast from a second,
    // non-elevated launch: UIPI drops it before the window procedure runs.
    ChangeWindowMessageFilterEx(hub, platform::SingleInstance::activationMessage(), MSGFLT_ALLOW,
                                nullptr);
    return true;
}

bool App::Impl::createSubsystems() {
    Settings const& settings = SettingsStore::instance().get();

    history = std::make_unique<BatteryHistory>(paths::historyFile());
    history->setEnabled(settings.historyEnabled);
    history->setRetention(retention(settings));
    history->prune();

    notifications = std::make_unique<notify::NotificationCenter>(hub);
    notifications->applySettings(settings);

    tray = std::make_unique<ui::TrayIcon>(hub);
    if (!tray->add()) {
        log::warning(L"The notification area icon is not up; the window is the only way in");
    }

    window = std::make_unique<ui::MainWindow>();
    ui::MainWindow::Dependencies dependencies;
    dependencies.history = history.get();
    dependencies.notifications = notifications.get();
    dependencies.refreshControllers = [this] { monitor->refreshNow(); };
    dependencies.exitApplication = [this] { requestExit(); };
    dependencies.systemToastsAvailable = notify::systemToast::available();
    if (!window->create(std::move(dependencies))) {
        log::error(L"The main window could not be created");
        return false;
    }

    monitor = std::make_unique<ControllerMonitor>(hub, kControllersChangedMessage);
    monitor->setIncludeNonXbox(settings.includeNonXboxGamepads);
    return true;
}

void App::Impl::connectSignals() {
    tray->activated.connect([this] { toggleMainWindow(); });
    tray->openRequested.connect([this] { showMainWindow(); });
    tray->refreshRequested.connect([this] { monitor->refreshNow(); });
    tray->exitRequested.connect([this] { requestExit(); });

    settingsToken = SettingsStore::instance().changed.connect(
        [this](Settings const& current, Settings const& previous) {
            onSettingsChanged(current, previous);
        });
}

void App::Impl::onControllersChanged() {
    std::vector<ControllerInfo> snapshot = monitor->snapshot();
    Settings const& settings = SettingsStore::instance().get();

    // Announced before anything is drawn: the sound is the part the user notices, and it
    // should not wait behind a window that may not even be visible.
    for (DetectedEvent const& event : detector.update(snapshot, settings,
                                                      std::chrono::system_clock::now())) {
        notifications->post(event);
    }
    for (ControllerInfo const& controller : snapshot) {
        history->record(controller);
    }

    controllers = std::move(snapshot);
    tray->update(controllers, settings);
    window->setControllers(controllers);
}

void App::Impl::onSettingsChanged(Settings const& current, Settings const& previous) {
    // The language and the palette come first: the window rebuilds itself from them at the
    // end of this function, and a rebuild against the old table would show the old strings.
    if (current.language != previous.language) {
        setLanguage(current.language);
    }
    if (current.theme != previous.theme) {
        ui::Theme::instance().setPreference(current.theme);
    }

    if (current.pollIntervalSeconds != previous.pollIntervalSeconds) {
        monitor->setInterval(pollInterval(current));
    }
    if (current.includeNonXboxGamepads != previous.includeNonXboxGamepads) {
        monitor->setIncludeNonXbox(current.includeNonXboxGamepads);
    }

    // The settings page writes the registry itself before it applies the change, so this
    // only has to catch a value that arrived from anywhere else.
    if (current.startWithWindows != previous.startWithWindows &&
        platform::isAutostartEnabled() != current.startWithWindows &&
        !platform::setAutostartEnabled(current.startWithWindows)) {
        log::error(L"Could not bring the autostart entry in line with the setting");
    }

    if (current.historyEnabled != previous.historyEnabled) {
        history->setEnabled(current.historyEnabled);
    }
    if (current.historyRetentionDays != previous.historyRetentionDays) {
        history->setRetention(retention(current));
        history->prune();
    }

    // A moved threshold redefines what counts as low, and the detector suppresses an event
    // it believes it has already reported at the old one.
    if (current.lowThresholdPercent != previous.lowThresholdPercent ||
        current.criticalThresholdPercent != previous.criticalThresholdPercent) {
        detector.reset();
    }

    notifications->applySettings(current);
    tray->update(controllers, current);
    window->settingsChanged(current, previous);
}

void App::Impl::onSystemColorsChanged() {
    ui::Theme::instance().refreshFromSystem();
    // The tray bitmap follows the taskbar's own theme, which is a different registry value
    // from the one the palette reads, so it has to be re-rendered rather than invalidated.
    tray->update(controllers, SettingsStore::instance().get());
}

void App::Impl::toggleMainWindow() {
    if (window->isVisible()) {
        window->hide();
        return;
    }
    showMainWindow();
}

void App::Impl::showMainWindow() { window->showAndActivate(); }

void App::Impl::requestExit() {
    // Posted rather than acted on here: this arrives from inside a tray menu callback or a
    // caption button's handler, and tearing the window down underneath either is a crash.
    PostMessageW(hub, WM_CLOSE, 0, 0);
}

void App::Impl::saveSettings() const {
    if (!SettingsStore::instance().get().save(paths::settingsFile())) {
        log::warning(L"The settings could not be written on the way out");
    }
}

void App::Impl::shutdown() {
    SettingsStore::instance().changed.disconnect(settingsToken);
    settingsToken = 0;

    // Stopped first: it is the only thing still posting to the hub window.
    if (monitor) {
        monitor->stop();
    }
    tray.reset();
    notifications.reset();
    window.reset();
    monitor.reset();
    history.reset();

    saveSettings();
    audio::AudioEngine::instance().shutdown();

    if (hub != nullptr && IsWindow(hub)) {
        DestroyWindow(hub);
    }
    hub = nullptr;
}

LRESULT CALLBACK App::Impl::hubProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto const* const create = reinterpret_cast<CREATESTRUCTW const*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    auto* const self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return self->handleMessage(hwnd, message, wparam, lparam);
}

LRESULT App::Impl::handleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    // Registered messages are allocated at run time from 0xC000 upwards and cannot be case
    // labels, so they have to be compared before the switch.
    if (message == ui::TrayIcon::taskbarCreatedMessage()) {
        if (tray) {
            tray->reAdd();
        }
        return 0;
    }
    if (message == platform::SingleInstance::activationMessage()) {
        // The second process is blocked in SendMessageTimeout until this returns, so
        // nothing slow belongs here.
        if (window) {
            showMainWindow();
        }
        return 0;
    }

    switch (message) {
        case ui::TrayIcon::kCallbackMessage:
            if (tray) {
                tray->handleCallback(wparam, lparam);
            }
            return 0;

        case kControllersChangedMessage:
            if (monitor) {
                onControllersChanged();
            }
            return 0;

        case notify::NotificationCenter::kFlyoutClickedMessage:
            if (window) {
                showMainWindow();
            }
            return 0;

        case WM_SETTINGCHANGE:
            if (lparam != 0 &&
                CompareStringOrdinal(reinterpret_cast<wchar_t const*>(lparam), -1,
                                     L"ImmersiveColorSet", -1, FALSE) == CSTR_EQUAL) {
                onSystemColorsChanged();
            }
            return 0;

        case WM_DEVICECHANGE:
            SetTimer(hwnd, kDeviceSettleTimer, kDeviceSettleMs, nullptr);
            return TRUE;

        case WM_TIMER:
            if (wparam == kDeviceSettleTimer) {
                KillTimer(hwnd, kDeviceSettleTimer);
                if (monitor) {
                    monitor->refreshNow();
                }
            }
            return 0;

        case WM_QUERYENDSESSION:
            saveSettings();
            return TRUE;

        case WM_ENDSESSION:
            if (wparam != 0) {
                saveSettings();
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

App::App() : m_impl(std::make_unique<Impl>()) {}

App::~App() = default;

int App::run(HINSTANCE instance, int showCommand) {
    m_impl->instance = instance;

    // Before the log is opened, so that a second launch does not rotate the running
    // instance's log file out from under it.
    if (!m_impl->singleInstance.claim()) {
        return 0;
    }

    log::open(paths::logFile());
    log::info(L"PowerPeek {} starting", widen(PP_VERSION_STRING));

    try {
        // Single-threaded: the shell's file dialog, the tray menu and DirectComposition all
        // want an STA on the thread that pumps messages.
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    } catch (winrt::hresult_error const& error) {
        log::error(L"The UI thread could not enter an apartment: {}", describeHresult(error.code()));
        return 1;
    }

    platform::shell::applyAppUserModelId();

    SettingsStore::instance().load();
    Settings const& settings = SettingsStore::instance().get();
    setLanguage(settings.language);
    ui::Theme::instance().setPreference(settings.theme);
    audio::AudioEngine::instance().setMasterVolume(settings.masterVolume);

    int exitCode = 0;
    try {
        if (!m_impl->createHub() || !m_impl->createSubsystems()) {
            m_impl->shutdown();
            log::close();
            return 1;
        }
        m_impl->connectSignals();

        // Built lazily on the first notification otherwise, which puts several milliseconds
        // of device enumeration in front of the sound the user is waiting for.
        audio::AudioEngine::instance().ensureStarted();

        m_impl->monitor->start(pollInterval(settings));
        if (!wantsHiddenStart(settings, showCommand)) {
            m_impl->showMainWindow();
        }

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        exitCode = static_cast<int>(message.wParam);
    } catch (winrt::hresult_error const& error) {
        log::error(L"Fatal: {}", describeHresult(error.code()));
        exitCode = 1;
    } catch (std::exception const& error) {
        log::error(L"Fatal: {}", widen(error.what()));
        exitCode = 1;
    }

    m_impl->shutdown();
    log::info(L"PowerPeek exiting with code {}", exitCode);
    log::close();

    // winrt::uninit_apartment() is deliberately not called. GraphicsDevice, Theme and the
    // flyout pool are function-local statics destroyed after this returns, and releasing
    // their Direct2D and DirectComposition objects after COM has been torn down faults
    // inside the SDK. The process is about to end; Windows reclaims all of it.
    return exitCode;
}

}  // namespace peek
