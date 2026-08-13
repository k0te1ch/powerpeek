#include "platform/Platform.h"

#include <dwmapi.h>

#include <winrt/Windows.UI.ViewManagement.h>

#include <cwchar>
#include <filesystem>
#include <string>

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "platform/PlatformInternal.h"

namespace peek::platform {
namespace {

constexpr wchar_t kPersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"PowerPeek";
constexpr wchar_t kInstanceMutex[] = L"Local\\PowerPeek.SingleInstance";
constexpr wchar_t kActivationMessage[] = L"PowerPeek.ActivateExistingInstance";

// Windows 10 "Blue", the shipping default, so the last-resort accent still looks like a
// system colour rather than an arbitrary one.
constexpr D2D1_COLOR_F kFallbackAccent{0.0f, 0x78 / 255.0f, 0xD4 / 255.0f, 1.0f};

// DWMWA_SYSTEMBACKDROP_TYPE arrived with Windows 11 22H2. Windows 11 21H2 could do Mica
// only through a different, undocumented attribute; rather than carry two undocumented
// routes, that build is treated as having no system backdrop and degrades to blur.
constexpr DWORD kFirstBuildWithSystemBackdrop = 22621;

// The undocumented Windows 10 route.
//
// user32!SetWindowCompositionAttribute has never been documented or declared in any SDK
// header, so it is resolved by name and treated as absent when it does not answer. The
// structures below mirror what the entry point reads; they are named for what they do
// rather than copied from a leaked header.
enum AccentState : int {
    kAccentDisabled = 0,
    kAccentBlurBehind = 3,
    kAccentAcrylicBlurBehind = 4,
};

struct AccentPolicy {
    int state;
    int flags;
    // ABGR. Only the alpha carries: the window paints its own material colour on top, so
    // the system layer is asked for the blur and for almost no tint of its own.
    unsigned int gradientColor;
    int animationId;
};

struct WindowCompositionAttributeData {
    int attribute;
    void* data;
    SIZE_T size;
};

constexpr int kWindowCompositionAccentPolicy = 19;
constexpr unsigned int kUntintedBlur = 0x01000000u;

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*);

SetWindowCompositionAttributeFn accentEntryPoint() {
    static SetWindowCompositionAttributeFn const entry = [] {
        // user32 is loaded in every process that has a window, so this never loads a
        // library and never has one to free.
        HMODULE const user32 = GetModuleHandleW(L"user32.dll");
        auto const found =
            user32 ? reinterpret_cast<SetWindowCompositionAttributeFn>(
                         GetProcAddress(user32, "SetWindowCompositionAttribute"))
                   : nullptr;
        if (!found) {
            log::info(L"This system cannot blur behind a window; the backdrop stays opaque");
        }
        return found;
    }();
    return entry;
}

bool systemBackdropAvailable() { return osBuildNumber() >= kFirstBuildWithSystemBackdrop; }

bool applyAccentPolicy(HWND window, AccentState state) {
    SetWindowCompositionAttributeFn const entry = accentEntryPoint();
    if (!entry) {
        return false;
    }

    bool const enabling = state != kAccentDisabled;
    AccentPolicy policy{state, 0, enabling ? kUntintedBlur : 0u, 0};
    WindowCompositionAttributeData data{kWindowCompositionAccentPolicy, &policy, sizeof(policy)};
    if (entry(window, &data)) {
        return enabling;
    }

    if (enabling) {
        log::warning(L"The system refused to blur behind the window; leaving it opaque");
    }
    return false;
}

// Hands the window frame to the compositor, or takes it back. A backdrop is painted into
// the frame, so it only appears once the frame covers the client area, and the system drop
// shadow comes back with it -- which is what replaces the shadow the window stops drawing.
void setFrameOwnedBySystem(HWND window, bool owned) {
    DWMNCRENDERINGPOLICY const policy = owned ? DWMNCRP_ENABLED : DWMNCRP_DISABLED;
    DwmSetWindowAttribute(window, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));

    MARGINS const margins = owned ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
    HRESULT const hr = DwmExtendFrameIntoClientArea(window, &margins);
    if (FAILED(hr)) {
        log::debug(L"The window frame could not be extended: {}", describeHresult(hr));
    }
}

std::wstring readRegistryString(wchar_t const* subkey, wchar_t const* value) {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, subkey, value, RRF_RT_REG_SZ, nullptr, nullptr,
                     &bytes) != ERROR_SUCCESS) {
        return {};
    }
    std::wstring text(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, subkey, value, RRF_RT_REG_SZ, nullptr, text.data(),
                     &bytes) != ERROR_SUCCESS) {
        return {};
    }
    text.resize(std::wcslen(text.c_str()));
    return text;
}

// A Run entry is a command line, so the path may be quoted and may carry arguments; only
// the program part identifies the entry.
std::wstring_view programFromCommand(std::wstring_view command) {
    if (!command.empty() && command.front() == L'"') {
        std::size_t const end = command.find(L'"', 1);
        return end == std::wstring_view::npos ? command.substr(1) : command.substr(1, end - 1);
    }
    return command.substr(0, command.find(L' '));
}

void wakeExistingInstance() {
    // SetForegroundWindow in the running instance is refused unless a process that owns
    // the foreground hands the right over first. This process has it, having just been
    // launched by the user; ASFW_ANY is the only usable form because the process id of
    // the instance that will answer the broadcast is unknown.
    if (!AllowSetForegroundWindow(ASFW_ANY)) {
        log::debug(L"AllowSetForegroundWindow refused; the running window may only flash");
    }

    UINT const message = SingleInstance::activationMessage();
    if (message == 0) {
        log::error(L"RegisterWindowMessage failed; the running instance cannot be signalled");
        return;
    }

    // SendMessageTimeout rather than PostMessage: this process exits the moment claim()
    // returns, and the foreground grant dies with it, so the first instance has to act
    // while we are still alive. SMTO_ABORTIFHUNG keeps a wedged third-party window from
    // holding the broadcast up.
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, message, 0, 0, SMTO_ABORTIFHUNG, 2000, &result);
}

}  // namespace

bool equalPathsIgnoringCase(std::wstring_view a, std::wstring_view b) {
    if (a.empty() || b.empty()) {
        return false;
    }
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

DWORD osBuildNumber() {
    static DWORD const build = [] {
        // GetVersionEx and VerifyVersionInfo report 6.2 unless the manifest lists every
        // supportedOS GUID, and even then they lie about the build for compatibility
        // shims. RtlGetVersion is not shimmed. It is undocumented for user mode but
        // exported by name from ntdll, which is already loaded in every process.
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        if (HMODULE const ntdll = GetModuleHandleW(L"ntdll.dll")) {
            auto const getVersion =
                reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
            RTL_OSVERSIONINFOW info{sizeof(info)};
            if (getVersion && getVersion(&info) == 0) {
                return info.dwBuildNumber;
            }
        }
        log::warning(L"RtlGetVersion unavailable; assuming the oldest supported Windows 10");
        return DWORD{0};
    }();
    return build;
}

bool isWindows11OrGreater() {
    return osBuildNumber() >= 22000;
}

void setTitleBarDarkMode(HWND window, bool dark) {
    // Attribute 19 was DWMWA_USE_IMMERSIVE_DARK_MODE up to build 18984 and became
    // DWMWA_USE_HOSTBACKDROPBRUSH from 18985 on, where the flag moved to 20. Firing both
    // would set the wrong thing on one of the two, so the build decides.
    constexpr DWORD kDarkModeBefore20H1 = 19;
    DWORD const attribute = osBuildNumber() >= 18985
                                ? static_cast<DWORD>(DWMWA_USE_IMMERSIVE_DARK_MODE)
                                : kDarkModeBefore20H1;

    BOOL const value = dark ? TRUE : FALSE;
    HRESULT const hr = DwmSetWindowAttribute(window, attribute, &value, sizeof(value));
    if (FAILED(hr)) {
        // Expected below build 17763, where the attribute does not exist at all; the
        // window draws its own chrome regardless, so nothing user-visible is lost.
        log::debug(L"Dark title bar attribute {} rejected: {}", attribute, describeHresult(hr));
    }
}

void setRoundedCorners(HWND window, bool rounded) {
    if (!isWindows11OrGreater()) {
        return;
    }

    DWM_WINDOW_CORNER_PREFERENCE const preference = rounded ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
    HRESULT const hr = DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                                             sizeof(preference));
    if (FAILED(hr)) {
        log::debug(L"Corner preference rejected: {}", describeHresult(hr));
    }
}

bool backdropSupported(BackdropMode mode) {
    switch (mode) {
        case BackdropMode::Opaque:
            return true;
        case BackdropMode::Blur:
            return accentEntryPoint() != nullptr;
        case BackdropMode::Acrylic:
            // Windows 11 has a documented acrylic; Windows 10 can only reach it through the
            // undocumented route, and does it badly (see applyWindowBackdrop).
            return systemBackdropAvailable() || accentEntryPoint() != nullptr;
        case BackdropMode::Mica:
            return systemBackdropAvailable();
    }
    return false;
}

BackdropMode effectiveBackdrop(BackdropMode requested) {
    if (backdropSupported(requested)) {
        return requested;
    }
    // One step at a time, so a system that has blur but no material still gets something.
    if (requested != BackdropMode::Blur && backdropSupported(BackdropMode::Blur)) {
        return BackdropMode::Blur;
    }
    return BackdropMode::Opaque;
}

bool backdropRoundsCorners() { return isWindows11OrGreater(); }

bool applyWindowBackdrop(HWND window, BackdropMode mode) {
    BackdropMode const resolved = effectiveBackdrop(mode);

    if (resolved == BackdropMode::Opaque) {
        applyAccentPolicy(window, kAccentDisabled);
        if (systemBackdropAvailable()) {
            DWM_SYSTEMBACKDROP_TYPE const none = DWMSBT_NONE;
            DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &none, sizeof(none));
        }
        setFrameOwnedBySystem(window, false);
        setRoundedCorners(window, false);
        return false;
    }

    // The frame has to belong to the compositor before any of this shows, and the corners
    // have to be rounded before the backdrop stops being a hard-edged rectangle.
    setFrameOwnedBySystem(window, true);
    setRoundedCorners(window, true);

    if (systemBackdropAvailable() && resolved != BackdropMode::Blur) {
        DWM_SYSTEMBACKDROP_TYPE const type =
            resolved == BackdropMode::Mica ? DWMSBT_MAINWINDOW : DWMSBT_TRANSIENTWINDOW;
        HRESULT const hr =
            DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
        if (SUCCEEDED(hr)) {
            return true;
        }
        log::warning(L"The window material was refused ({}); falling back to a plain blur",
                     describeHresult(hr));
    }

    // Windows 10 gets here, and so does Blur everywhere.
    //
    // Acrylic is deliberately not the Windows 10 default: on that generation the acrylic
    // blur is recomputed against the desktop on every move, and dragging a window with it
    // stutters to single-digit frame rates. Plain blur has no such defect.
    return applyAccentPolicy(window, resolved == BackdropMode::Acrylic
                                         ? kAccentAcrylicBlurBehind
                                         : kAccentBlurBehind);
}

bool systemUsesLightTheme() {
    // Absent on a fresh install, where the shipped appearance is light.
    DWORD light = 1;
    DWORD size = sizeof(light);
    LSTATUS const status = RegGetValueW(HKEY_CURRENT_USER, kPersonalizeKey, L"AppsUseLightTheme",
                                        RRF_RT_REG_DWORD, nullptr, &light, &size);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        log::warning(L"Could not read AppsUseLightTheme: {}",
                     describeHresult(HRESULT_FROM_WIN32(static_cast<DWORD>(status))));
    }
    return light != 0;
}

D2D1_COLOR_F systemAccentColor() {
    using namespace winrt::Windows::UI::ViewManagement;
    try {
        // In-box WinRT type; activating it from an unpackaged desktop process works, but
        // only once the thread is in an apartment, so failure here is a real possibility
        // and must not propagate.
        UISettings const settings;
        winrt::Windows::UI::Color const accent = settings.GetColorValue(UIColorType::Accent);
        return D2D1::ColorF(accent.R / 255.0f, accent.G / 255.0f, accent.B / 255.0f, 1.0f);
    } catch (winrt::hresult_error const& error) {
        log::debug(L"UISettings accent unavailable ({}); falling back to DWM",
                   describeHresult(error.code()));
    }

    // The colorization colour is the accent after DWM has blended it with the
    // transparency setting, so it can read greyer than the true accent. Close enough as a
    // fallback, wrong as a primary.
    DWORD colorization = 0;
    BOOL blendedWithOpacity = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&colorization, &blendedWithOpacity))) {
        return D2D1::ColorF(((colorization >> 16) & 0xFF) / 255.0f,
                            ((colorization >> 8) & 0xFF) / 255.0f, (colorization & 0xFF) / 255.0f,
                            1.0f);
    }

    log::warning(L"No accent colour source answered; using the Windows default blue");
    return kFallbackAccent;
}

bool isAutostartEnabled() {
    std::wstring const command = readRegistryString(kRunKey, kRunValue);
    if (command.empty()) {
        return false;
    }
    // Comparing against the running executable means a moved or renamed copy reports
    // itself as disabled, which is the truth: the stale entry starts something else.
    return equalPathsIgnoringCase(programFromCommand(command), paths::executable().native());
}

bool setAutostartEnabled(bool enabled) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                     KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        log::error(L"Could not open the Run key: {}",
                   describeHresult(HRESULT_FROM_WIN32(static_cast<DWORD>(status))));
        return false;
    }

    if (enabled) {
        // Quoted: an unquoted path with a space is read as a program name plus arguments,
        // which is the classic way an autostart entry silently launches the wrong file.
        std::wstring const command = L'"' + paths::executable().wstring() + L'"';
        status = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                reinterpret_cast<BYTE const*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kRunValue);
        if (status == ERROR_FILE_NOT_FOUND) {
            status = ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);

    if (status != ERROR_SUCCESS) {
        log::error(L"Could not {} the autostart entry: {}", enabled ? L"write" : L"remove",
                   describeHresult(HRESULT_FROM_WIN32(static_cast<DWORD>(status))));
        return false;
    }
    return true;
}

void bringToForeground(HWND window) {
    if (!IsWindow(window)) {
        return;
    }
    ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
    if (SetForegroundWindow(window)) {
        return;
    }

    // Refused, because this process does not currently own the foreground. Attaching to
    // the foreground thread's input queue makes the two share foreground state and lifts
    // the restriction for the duration of the attachment.
    HWND const foreground = GetForegroundWindow();
    DWORD const foregroundThread = GetWindowThreadProcessId(foreground, nullptr);
    DWORD const thisThread = GetCurrentThreadId();
    if (foregroundThread != 0 && foregroundThread != thisThread &&
        AttachThreadInput(thisThread, foregroundThread, TRUE)) {
        SetForegroundWindow(window);
        BringWindowToTop(window);
        // Detaching is not optional: attached queues share focus, capture and activation
        // from here on, so a leaked attachment makes both applications misbehave.
        AttachThreadInput(thisThread, foregroundThread, FALSE);
    }

    if (GetForegroundWindow() != window) {
        log::debug(L"Foreground activation refused; flashing the taskbar button instead");
        FLASHWINFO flash{sizeof(flash), window, FLASHW_ALL | FLASHW_TIMERNOFG, 3, 0};
        FlashWindowEx(&flash);
    }
}

SingleInstance::SingleInstance() = default;

SingleInstance::~SingleInstance() {
    if (m_mutex) {
        // Created without ownership, so there is nothing to release and no abandoned-mutex
        // state to worry about if this process is killed.
        CloseHandle(m_mutex);
    }
}

bool SingleInstance::claim() {
    m_mutex = CreateMutexW(nullptr, FALSE, kInstanceMutex);
    DWORD const error = GetLastError();

    if (!m_mutex) {
        if (error == ERROR_ACCESS_DENIED) {
            // The name exists but belongs to a more privileged instance; it is running.
            wakeExistingInstance();
            return false;
        }
        log::error(L"Could not create the single-instance mutex: {}",
                   describeHresult(HRESULT_FROM_WIN32(error)));
        return true;  // Failing open beats refusing to start over a bookkeeping object.
    }

    if (error == ERROR_ALREADY_EXISTS) {
        wakeExistingInstance();
        return false;
    }
    return true;
}

UINT SingleInstance::activationMessage() {
    // The value is per-session and stable for as long as any process holds it, so
    // registering once and caching is both correct and what the sender relies on.
    static UINT const message = RegisterWindowMessageW(kActivationMessage);
    return message;
}

}  // namespace peek::platform
