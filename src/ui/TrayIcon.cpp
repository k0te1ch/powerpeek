#include "ui/TrayIcon.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <map>
#include <string>

#include "core/Logger.h"
#include "core/Strings.h"
#include "ui/Drawing.h"
#include "ui/Theme.h"

namespace peek::ui {
namespace {

// uID rather than NIF_GUID: a GUID binds to the executable's full path on the first
// NIM_ADD and the shell then refuses to add the icon ever again from a different path,
// which for a portable single-exe application means the icon vanishes the moment the user
// moves it out of Downloads.
constexpr UINT kIconId = 1;

constexpr UINT kMenuOpen = 1;
constexpr UINT kMenuRefresh = 2;
constexpr UINT kMenuExit = 3;

// Started from Run at logon, this process regularly beats Explorer's notification area to
// the punch and NIM_ADD fails with ERROR_TIMEOUT. Retrying for a minute covers that and
// the transient busy-shell case; after that the shell is not coming.
constexpr UINT kRetryIntervalMs = 2000;
constexpr int kAddAttempts = 30;

// szTip is WCHAR[128] under version 4 (it was 64 under version 1). The shell truncates
// without telling anyone, so the text is assembled to fit rather than discovered to be too
// long; the capacity is the array minus its terminator.
constexpr std::size_t kTipCapacity = 127;

// The notification-area tooltip breaks lines on CRLF, not on a bare LF.
constexpr std::wstring_view kLineBreak = L"\r\n";

SIZE requestedIconSize(HWND owner) {
    // The notification area lives on the taskbar, which on a mixed-DPI desktop is not
    // necessarily on the monitor the owner window sits on.
    UINT dpi = 0;
    if (HWND const taskbar = FindWindowW(L"Shell_TrayWnd", nullptr)) {
        dpi = GetDpiForWindow(taskbar);
    }
    if (dpi == 0 && owner != nullptr) {
        dpi = GetDpiForWindow(owner);
    }
    if (dpi == 0) {
        dpi = GetDpiForSystem();
    }
    return SIZE{GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi)};
}

// The taskbar follows SystemUsesLightTheme, which the user can set independently of the
// AppsUseLightTheme value the rest of the application reads; taking the app value would
// put a black icon on a black taskbar for anyone running the common light-apps/dark-shell
// combination.
bool taskbarIsDark() {
    DWORD light = 1;
    DWORD size = sizeof(light);
    LSTATUS const status =
        RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        log::warning(L"Could not read SystemUsesLightTheme: {}",
                     describeHresult(HRESULT_FROM_WIN32(static_cast<DWORD>(status))));
    }
    return light == 0;
}

// Dark context menus have no public API. uxtheme exports the two functions that switch
// them by ordinal only -- they carry no name in the export table -- so they can only be
// reached through MAKEINTRESOURCEA. Missing before build 17763, where the menu stays
// light and nothing else is affected.
struct MenuThemeApi {
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();

    SetPreferredAppModeFn setPreferredAppMode = nullptr;
    FlushMenuThemesFn flushMenuThemes = nullptr;
};

MenuThemeApi const& menuThemeApi() {
    static MenuThemeApi const api = [] {
        MenuThemeApi resolved;
        // Deliberately never freed: uxtheme stays loaded for the life of the process and
        // the function pointers below outlive any scope that could free it.
        HMODULE const uxtheme =
            LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (uxtheme == nullptr) {
            log::warning(L"uxtheme.dll could not be loaded: {}",
                         describeHresult(HRESULT_FROM_WIN32(GetLastError())));
            return resolved;
        }
        resolved.setPreferredAppMode = reinterpret_cast<MenuThemeApi::SetPreferredAppModeFn>(
            reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135))));
        resolved.flushMenuThemes = reinterpret_cast<MenuThemeApi::FlushMenuThemesFn>(
            reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136))));
        if (resolved.setPreferredAppMode == nullptr || resolved.flushMenuThemes == nullptr) {
            log::info(L"This Windows build has no dark-menu support; the tray menu stays light");
        }
        return resolved;
    }();
    return api;
}

void applyMenuTheme() {
    MenuThemeApi const& api = menuThemeApi();
    if (api.setPreferredAppMode == nullptr || api.flushMenuThemes == nullptr) {
        return;
    }
    // 1 is AllowDark, which makes menus follow the system setting instead of forcing a
    // colour. It is also the value that reads correctly as TRUE on build 17763, where this
    // ordinal was AllowDarkModeForApp(BOOL) rather than SetPreferredAppMode(mode).
    api.setPreferredAppMode(1);
    // Without this the change only reaches menus created after the next theme broadcast.
    api.flushMenuThemes();
}

// The controller the icon speaks for: the lowest of the connected batteries, because that is
// the one that will interrupt play. A pad with nothing to report -- wired, or a level the
// source never gave -- is not a candidate: counting it would let a plugged-in pad pull the
// icon to nothing while a half-empty wireless one sat next to it.
DeviceInfo const* lowestBattery(std::vector<DeviceInfo> const& controllers) {
    DeviceInfo const* lowest = nullptr;
    for (DeviceInfo const& controller : controllers) {
        if (!controller.hasBattery()) {
            continue;
        }
        if (lowest == nullptr || controller.percent < lowest->percent) {
            lowest = &controller;
        }
    }
    return lowest;
}

// The badge follows the system accent, but the notification area follows the system theme
// rather than the application's: on a dark taskbar the base accent is too dark for a digit to
// come out of it, so the lighter shade Windows itself uses on dark surfaces is taken instead.
D2D1_COLOR_F badgeColor(bool darkTaskbar) { return theme().accentShade(darkTaskbar ? 1 : 0); }

bool sameColor(D2D1_COLOR_F const& a, D2D1_COLOR_F const& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

std::wstring describeLevel(DeviceInfo const& controller) {
    if (controller.percent < 0) {
        return std::wstring(toString(controller.source));
    }
    std::wstring level = std::to_wstring(controller.percent);
    level += text(Text::UnitPercent);
    if (controller.fidelity == Fidelity::Coarse) {
        level += L" (";
        level += text(Text::ApproximateSuffix);
        level += L')';
    }
    if (controller.charge == ChargeState::Charging || controller.charge == ChargeState::Full) {
        level += L", ";
        level += toString(controller.charge);
    }
    return level;
}

std::wstring describeController(DeviceInfo const& controller) {
    std::wstring line = controller.name;
    line += L" \x2014 ";
    line += describeLevel(controller);
    return line;
}

// Whole lines only: a name cut in half tells the user less than an honest count of what was
// left out. Returns false when the line did not fit, which ends the list.
bool appendLine(std::wstring& tip, std::wstring const& line, std::size_t capacity) {
    if (tip.size() + kLineBreak.size() + line.size() > capacity) {
        return false;
    }
    tip += kLineBreak;
    tip += line;
    return true;
}

std::wstring omittedNotice(std::size_t omitted) {
    return formatText(Text::TrayMoreControllers, omitted);
}

std::wstring buildTooltip(std::vector<DeviceInfo> const& controllers) {
    std::wstring tip(text(Text::AppName));
    if (controllers.empty()) {
        appendLine(tip, std::wstring(text(Text::NoControllers)), kTipCapacity);
        return tip;
    }

    std::size_t shown = 0;
    for (DeviceInfo const& controller : controllers) {
        // Room for the notice is reserved before the line goes in. Discovering afterwards
        // that it no longer fits would leave a list that is short without saying so.
        std::size_t const rest = controllers.size() - shown - 1;
        std::size_t budget = kTipCapacity;
        if (rest > 0) {
            budget -= std::min(budget, kLineBreak.size() + omittedNotice(rest).size());
        }
        if (!appendLine(tip, describeController(controller), budget)) {
            break;
        }
        ++shown;
    }

    if (shown < controllers.size()) {
        appendLine(tip, omittedNotice(controllers.size() - shown), kTipCapacity);
    }
    return tip;
}

}  // namespace

struct TrayIcon::Impl {
    TrayIcon* self = nullptr;
    HWND owner = nullptr;
    HICON icon = nullptr;
    bool added = false;
    int attemptsLeft = kAddAttempts;
    UINT_PTR retryTimer = 0;
    std::wstring tip;

    // Everything the bitmap is a function of, so that a poll which changed nothing does
    // not rebuild it. This runs every poll for weeks; a redundant render is a GDI object
    // and a WIC surface each time.
    bool rendered = false;
    SIZE size{0, 0};
    TrayStyle style = TrayStyle::Battery;
    int percent = -1;
    int connected = 0;
    float fill = 0.0f;
    bool charging = false;
    bool approximate = false;
    bool dark = false;
    D2D1_COLOR_F badge{};

    bool install();
    void fillCommon(NOTIFYICONDATAW& data) const;
    void applyIcon(HICON next);
    // True when a new bitmap was installed, which also carries the current tooltip.
    bool refreshIcon(std::vector<DeviceInfo> const& controllers, Settings const& settings);
    void showMenu(POINT screen);
    void scheduleRetry();
    void cancelRetry();

    // WM_TIMER for a thread timer is dispatched straight to this callback, which keeps the
    // retry off the owner window and out of the way of whatever timer ids it uses.
    static void CALLBACK retryProc(HWND, UINT, UINT_PTR timer, DWORD);
    static std::map<UINT_PTR, Impl*>& pendingRetries();
};

std::map<UINT_PTR, TrayIcon::Impl*>& TrayIcon::Impl::pendingRetries() {
    static std::map<UINT_PTR, Impl*> timers;
    return timers;
}

void CALLBACK TrayIcon::Impl::retryProc(HWND, UINT, UINT_PTR timer, DWORD) {
    auto& timers = pendingRetries();
    auto const found = timers.find(timer);
    if (found == timers.end()) {
        KillTimer(nullptr, timer);
        return;
    }
    Impl* const impl = found->second;
    impl->cancelRetry();
    impl->install();
}

void TrayIcon::Impl::scheduleRetry() {
    if (retryTimer != 0 || attemptsLeft <= 0) {
        return;
    }
    retryTimer = SetTimer(nullptr, 0, kRetryIntervalMs, &Impl::retryProc);
    if (retryTimer == 0) {
        log::error(L"Could not start the tray icon retry timer: {}",
                   describeHresult(HRESULT_FROM_WIN32(GetLastError())));
        return;
    }
    pendingRetries().emplace(retryTimer, this);
}

void TrayIcon::Impl::cancelRetry() {
    if (retryTimer == 0) {
        return;
    }
    KillTimer(nullptr, retryTimer);
    pendingRetries().erase(retryTimer);
    retryTimer = 0;
}

void TrayIcon::Impl::fillCommon(NOTIFYICONDATAW& data) const {
    data.cbSize = sizeof(data);
    data.hWnd = owner;
    data.uID = kIconId;
}

bool TrayIcon::Impl::install() {
    if (added) {
        return true;
    }
    if (icon == nullptr) {
        // Nothing has been polled yet; an empty gauge is still better than no icon, and it
        // is replaced by the first update().
        refreshIcon({}, SettingsStore::instance().get());
    }

    NOTIFYICONDATAW data{};
    fillCommon(data);
    // NIF_SHOWTIP because version 4 suppresses the standard tooltip by default and instead
    // expects the application to draw its own from NIN_POPUPOPEN.
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kCallbackMessage;
    data.hIcon = icon;
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);

    --attemptsLeft;
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        DWORD const error = GetLastError();
        if (attemptsLeft > 0) {
            log::info(L"The notification area is not ready yet ({}); retrying",
                      describeHresult(HRESULT_FROM_WIN32(error)));
            scheduleRetry();
        } else {
            log::error(L"Giving up on the notification area icon: {}",
                       describeHresult(HRESULT_FROM_WIN32(error)));
        }
        return false;
    }

    // uVersion shares a union with uTimeout, so it is only ever set for NIM_SETVERSION.
    data.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) {
        // Version 1 packs the callback's wParam and lParam differently, so every click
        // would be misread. Better no icon than one that does the wrong thing.
        log::error(L"NIM_SETVERSION failed: {}",
                   describeHresult(HRESULT_FROM_WIN32(GetLastError())));
        Shell_NotifyIconW(NIM_DELETE, &data);
        return false;
    }

    added = true;
    attemptsLeft = kAddAttempts;
    return true;
}

void TrayIcon::Impl::applyIcon(HICON next) {
    if (next == nullptr) {
        return;
    }
    HICON const previous = icon;
    icon = next;
    if (added) {
        NOTIFYICONDATAW data{};
        fillCommon(data);
        data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.hIcon = icon;
        wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
        if (!Shell_NotifyIconW(NIM_MODIFY, &data)) {
            // Explorer restarted without us seeing TaskbarCreated, or the shell dropped
            // the icon. Put it back rather than leaving a stale bitmap.
            added = false;
            install();
        }
    }
    // The shell holds the handle it was given, so the previous one can only be destroyed
    // once the new one is installed.
    if (previous != nullptr) {
        DestroyIcon(previous);
    }
}

bool TrayIcon::Impl::refreshIcon(std::vector<DeviceInfo> const& controllers,
                                 Settings const& settings) {
    DeviceInfo const* const subject = lowestBattery(controllers);

    SIZE const wanted = requestedIconSize(owner);
    bool const wantDark = taskbarIsDark();
    auto const wantConnected = static_cast<int>(controllers.size());
    int const wantPercent = subject != nullptr ? subject->percent : -1;
    bool const wantCharging = subject != nullptr && (subject->charge == ChargeState::Charging ||
                                                     subject->charge == ChargeState::Full);
    bool const wantApproximate = subject != nullptr && subject->fidelity == Fidelity::Coarse;
    D2D1_COLOR_F const wantBadge = badgeColor(wantDark);

    bool const unchanged = rendered && wanted.cx == size.cx && wanted.cy == size.cy &&
                           settings.trayStyle == style && wantPercent == percent &&
                           wantConnected == connected && wantCharging == charging &&
                           wantApproximate == approximate && wantDark == dark &&
                           sameColor(wantBadge, badge);
    if (unchanged) {
        return false;
    }

    size = wanted;
    style = settings.trayStyle;
    percent = wantPercent;
    connected = wantConnected;
    charging = wantCharging;
    approximate = wantApproximate;
    dark = wantDark;
    badge = wantBadge;
    fill = percent > 0 ? static_cast<float>(percent) / 100.0f : 0.0f;
    rendered = true;

    TrayVisual visual;
    visual.gauge.percent = percent;
    visual.gauge.fill = fill;
    visual.gauge.charging = charging;
    visual.gauge.approximate = approximate;
    visual.gauge.level = theme().trayLevelColor(percent, dark);
    visual.connected = connected;
    visual.badge = badge;

    HICON const next = renderTrayIcon(size, style, visual, dark);
    if (next == nullptr) {
        // renderTrayIcon has already logged; keep whatever is installed rather than
        // dropping to no icon at all, and try again on the next poll.
        rendered = false;
        return false;
    }
    applyIcon(next);
    return true;
}

void TrayIcon::Impl::showMenu(POINT screen) {
    applyMenuTheme();

    HMENU const menu = CreatePopupMenu();
    if (menu == nullptr) {
        log::error(L"CreatePopupMenu failed: {}",
                   describeHresult(HRESULT_FROM_WIN32(GetLastError())));
        return;
    }

    AppendMenuW(menu, MF_STRING, kMenuOpen, std::wstring(text(Text::MenuOpen)).c_str());
    AppendMenuW(menu, MF_STRING, kMenuRefresh, std::wstring(text(Text::MenuRefresh)).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, std::wstring(text(Text::MenuExit)).c_str());
    SetMenuDefaultItem(menu, kMenuOpen, FALSE);

    // A menu whose owner is not the foreground window never dismisses when the user clicks
    // elsewhere; a tray click does not hand this process the foreground on its own.
    SetForegroundWindow(owner);

    UINT const flags = TPM_RETURNCMD | TPM_RIGHTBUTTON |
                       (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0 ? TPM_RIGHTALIGN
                                                                    : TPM_LEFTALIGN);
    int const command = TrackPopupMenuEx(menu, flags, screen.x, screen.y, owner, nullptr);
    DestroyMenu(menu);

    // KB135788: without a message posted to the owner the menu can stay stuck open after
    // the first dismissal.
    PostMessageW(owner, WM_NULL, 0, 0);

    switch (command) {
        case kMenuOpen:
            self->openRequested();
            break;
        case kMenuRefresh:
            self->refreshRequested();
            break;
        case kMenuExit:
            self->exitRequested();
            break;
        default:
            break;
    }
}

UINT TrayIcon::taskbarCreatedMessage() {
    static UINT const message = RegisterWindowMessageW(L"TaskbarCreated");
    return message;
}

TrayIcon::TrayIcon(HWND owner) : m_impl(std::make_unique<Impl>()) {
    m_impl->self = this;
    m_impl->owner = owner;
    // The icon can be installed before the first poll returns, so it starts out saying that
    // nothing is connected rather than saying nothing at all.
    m_impl->tip = buildTooltip({});

    // Explorer broadcasts TaskbarCreated at its own integrity level; UIPI drops it on the
    // floor if this process ever runs elevated, and the icon then never comes back after a
    // shell restart.
    ChangeWindowMessageFilterEx(owner, taskbarCreatedMessage(), MSGFLT_ALLOW, nullptr);
}

TrayIcon::~TrayIcon() {
    m_impl->cancelRetry();
    remove();
    if (m_impl->icon != nullptr) {
        DestroyIcon(m_impl->icon);
        m_impl->icon = nullptr;
    }
}

bool TrayIcon::add() { return m_impl->install(); }

void TrayIcon::remove() {
    m_impl->cancelRetry();
    if (!m_impl->added) {
        return;
    }
    NOTIFYICONDATAW data{};
    m_impl->fillCommon(data);
    if (!Shell_NotifyIconW(NIM_DELETE, &data)) {
        // The shell has already forgotten the icon, which is what happens when Explorer
        // went away between the last poll and here. Nothing is left to clean up.
        log::debug(L"NIM_DELETE was refused: {}",
                   describeHresult(HRESULT_FROM_WIN32(GetLastError())));
    }
    m_impl->added = false;
}

void TrayIcon::reAdd() {
    // Every icon Explorer knew about died with it, so NIM_MODIFY would fail: the only way
    // back is a fresh NIM_ADD.
    m_impl->added = false;
    m_impl->attemptsLeft = kAddAttempts;
    m_impl->install();
}

void TrayIcon::update(std::vector<DeviceInfo> const& controllers, Settings const& settings) {
    std::wstring tip = buildTooltip(controllers);
    bool const tipChanged = tip != m_impl->tip;
    m_impl->tip = std::move(tip);

    // Renders and installs in one step when anything the bitmap depends on moved; that
    // NIM_MODIFY carries the tooltip too, so only an unaccompanied tooltip needs its own.
    bool const iconInstalled = m_impl->refreshIcon(controllers, settings);

    if (!m_impl->added) {
        m_impl->install();
        return;
    }
    if (!tipChanged || iconInstalled) {
        return;
    }

    NOTIFYICONDATAW data{};
    m_impl->fillCommon(data);
    data.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(data.szTip, m_impl->tip.c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_MODIFY, &data)) {
        m_impl->added = false;
        m_impl->install();
    }
}

void TrayIcon::handleCallback(WPARAM wparam, LPARAM lparam) {
    // GET_X_LPARAM rather than LOWORD: on a desktop whose secondary monitor sits left of
    // or above the primary the anchor coordinates go negative, and the unsigned LOWORD
    // turns -20 into 65516. The Microsoft sample gets this wrong.
    POINT const anchor{GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)};

    switch (LOWORD(lparam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            activated();
            break;
        case WM_LBUTTONDBLCLK:
            openRequested();
            break;
        case WM_CONTEXTMENU:
            // Version 4 delivers this for the right button and for Shift+F10 alike.
            m_impl->showMenu(anchor);
            break;
        default:
            break;
    }
}

RECT TrayIcon::iconRect() const {
    RECT rect{};
    if (!m_impl->added) {
        return rect;
    }

    NOTIFYICONIDENTIFIER identifier{};
    identifier.cbSize = sizeof(identifier);
    identifier.hWnd = m_impl->owner;
    identifier.uID = kIconId;

    // S_FALSE means the icon is hidden in the overflow flyout and the rectangle describes
    // the "show hidden icons" chevron instead, which is exactly where a flyout anchored to
    // this should go, so SUCCEEDED is the right test.
    if (FAILED(Shell_NotifyIconGetRect(&identifier, &rect))) {
        return RECT{};
    }
    return rect;
}

}  // namespace peek::ui
