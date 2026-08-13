#include "ui/Window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <set>

#include "core/Logger.h"
#include "platform/Platform.h"
#include "ui/Theme.h"

namespace peek::ui {
namespace {

// Posted rather than sent, so a frame never runs inside another window's message handling.
constexpr UINT kMsgFrame = WM_APP + 0x1F0;
constexpr UINT_PTR kFrameTimer = 0x0F1;

// Present blocks on the vertical blank while the window is visible, which is what paces the
// render loop. A fully occluded window presents immediately, so this floor keeps an animation
// running behind another window from spinning a core.
constexpr auto kMinimumFrameInterval = std::chrono::milliseconds{6};

// The band that grabs a window edge, straddling it: a few DIPs into the transparent margin and
// a few into the body, which is what makes a thin border comfortable to grab.
constexpr float kResizeGripOutside = 4.0f;
constexpr float kResizeGripInside = 6.0f;

std::set<std::wstring>& registeredClasses() {
    static std::set<std::wstring> classes;
    return classes;
}

// Reports the edge of an auto-hiding taskbar on this monitor, or -1 when there is none.
int autoHideEdge(HMONITOR monitor) {
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) {
        return -1;
    }
    for (UINT edge : {ABE_BOTTOM, ABE_TOP, ABE_LEFT, ABE_RIGHT}) {
        APPBARDATA data{sizeof(data)};
        data.uEdge = edge;
        data.rc = info.rcMonitor;
        if (reinterpret_cast<HWND>(SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &data))) {
            return static_cast<int>(edge);
        }
    }
    return -1;
}

}  // namespace

struct D2DWindow::Impl {
    CompositionTarget target;
    UINT dpi = 96;
    SIZE minimumSizeDip{360, 280};
    bool resizable = true;
    bool maximised = false;
    bool systemBackdrop = false;
    bool painting = false;
    bool framePending = false;
    bool wantsAnotherFrame = false;
    std::chrono::steady_clock::time_point lastPresent{};
    Signal<>::Token themeToken = 0;
    Signal<>::Token deviceToken = 0;
};

D2DWindow::D2DWindow() : m_impl(std::make_unique<Impl>()) {}

D2DWindow::~D2DWindow() {
    theme().changed.disconnect(m_impl->themeToken);
    GraphicsDevice::instance().recreated.disconnect(m_impl->deviceToken);
    destroyWindow();
}

bool D2DWindow::createWindow(CreateParams const& params) {
    HINSTANCE const instance = GetModuleHandleW(nullptr);

    if (registeredClasses().find(params.className) == registeredClasses().end()) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = &D2DWindow::windowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        // No background brush and deliberately no CS_DROPSHADOW: nothing GDI paints is ever
        // visible on a window with no redirection surface, and the system shadow is square
        // and would fight the one this window draws for itself.
        wc.lpszClassName = params.className.c_str();
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
        wc.hIconSm = wc.hIcon;
        if (!RegisterClassExW(&wc)) {
            log::error(L"RegisterClassEx failed for {}: {}", params.className,
                       describeHresult(HRESULT_FROM_WIN32(GetLastError())));
            return false;
        }
        registeredClasses().insert(params.className);
    }

    m_impl->minimumSizeDip = params.minimumSizeDip;
    m_impl->resizable = params.resizable;

    // WS_THICKFRAME is what gives a popup Aero Snap, Win+Arrow, Shake and the maximise
    // animation; WM_NCHITTEST alone does not. WS_SYSMENU brings Alt+Space and the taskbar
    // jump-list entries with it.
    DWORD style = WS_POPUP | WS_CLIPCHILDREN | WS_SYSMENU;
    if (params.resizable) {
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    }
    if (params.appWindow) {
        style |= WS_MINIMIZEBOX;
    }

    DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP;
    if (params.topmost) {
        exStyle |= WS_EX_TOPMOST;
    }
    if (params.noActivate) {
        exStyle |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    }
    if (params.appWindow) {
        exStyle |= WS_EX_APPWINDOW;  // popups are excluded from the taskbar without this
    }

    m_hwnd = CreateWindowExW(exStyle, params.className.c_str(), params.title.c_str(), style, 0, 0,
                             100, 100, params.owner, nullptr, instance, this);
    if (!m_hwnd) {
        log::error(L"CreateWindowEx failed for {}: {}", params.className,
                   describeHresult(HRESULT_FROM_WIN32(GetLastError())));
        return false;
    }

    // Make it deterministic that DWM draws no frame of its own around us, and that Windows 11
    // does not round the window rect -- rounding it would clip the shadow margin's corners.
    // setBackdrop reverses both once the window stops drawing that margin.
    DWMNCRENDERINGPOLICY const policy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(m_hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    platform::setRoundedCorners(m_hwnd, false);

    m_impl->dpi = GetDpiForWindow(m_hwnd);
    if (m_impl->dpi == 0) {
        m_impl->dpi = 96;
    }
    m_impl->target.attach(m_hwnd);

    int const widthPx = MulDiv(params.initialSizeDip.cx, static_cast<int>(m_impl->dpi), 96);
    int const heightPx = MulDiv(params.initialSizeDip.cy, static_cast<int>(m_impl->dpi), 96);

    int x = 0;
    int y = 0;
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
        x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - widthPx) / 2;
        y = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - heightPx) / 2;
    }

    // SWP_FRAMECHANGED re-sends WM_NCCALCSIZE so the zero-sized frame takes effect.
    SetWindowPos(m_hwnd, nullptr, x, y, widthPx, heightPx,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    m_impl->themeToken = theme().changed.connect([this] {
        onThemeChanged();
        invalidate();
    });
    m_impl->deviceToken =
        GraphicsDevice::instance().recreated.connect([this] { invalidate(); });

    invalidate();
    return true;
}

void D2DWindow::destroyWindow() {
    if (m_hwnd) {
        HWND const window = m_hwnd;
        m_hwnd = nullptr;
        m_impl->target.detach();
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        DestroyWindow(window);
    }
}

bool D2DWindow::isVisible() const {
    return m_hwnd && IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd);
}

float D2DWindow::scale() const { return static_cast<float>(m_impl->dpi) / 96.0f; }

D2D1_SIZE_F D2DWindow::sizeDip() const {
    SIZE const pixels = m_impl->target.pixelSize();
    float const s = scale();
    return D2D1_SIZE_F{static_cast<float>(pixels.cx) / s, static_cast<float>(pixels.cy) / s};
}

bool D2DWindow::isMaximised() const { return m_impl->maximised; }

D2D1_RECT_F D2DWindow::bodyRect() const {
    D2D1_SIZE_F const size = sizeDip();
    float const margin =
        m_impl->maximised || m_impl->systemBackdrop ? 0.0f : Metrics::shadowMargin;
    return D2D1_RECT_F{margin, margin, size.width - margin, size.height - margin};
}

void D2DWindow::setBackdrop(BackdropMode mode) {
    if (!m_hwnd) {
        return;
    }
    bool const active = platform::applyWindowBackdrop(m_hwnd, mode);
    if (active == m_impl->systemBackdrop) {
        invalidate();
        return;
    }
    m_impl->systemBackdrop = active;

    // The body just grew or shrank by the whole margin on every side, and the frame the
    // compositor draws around it changed with it. Nothing moves the window, so only a
    // frame change tells the system to re-ask for the non-client size.
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    invalidate();
}

bool D2DWindow::hasSystemBackdrop() const { return m_impl->systemBackdrop; }

float D2DWindow::cornerRadius() const {
    if (m_impl->maximised) {
        return 0.0f;
    }
    if (m_impl->systemBackdrop) {
        return platform::backdropRoundsCorners() ? Metrics::windowCornerRadius : 0.0f;
    }
    return Metrics::windowCornerRadius;
}

bool D2DWindow::isCaptionArea(D2D1_POINT_2F) const { return false; }

void D2DWindow::invalidate() {
    if (m_impl->painting) {
        m_impl->wantsAnotherFrame = true;
        return;
    }
    if (!m_hwnd || m_impl->framePending) {
        return;
    }

    auto const since = std::chrono::steady_clock::now() - m_impl->lastPresent;
    if (since < kMinimumFrameInterval) {
        m_impl->framePending = SetTimer(m_hwnd, kFrameTimer, 1, nullptr) != 0;
    } else {
        m_impl->framePending = PostMessageW(m_hwnd, kMsgFrame, 0, 0) != FALSE;
    }
}

void D2DWindow::requestFrame() {
    if (m_impl->painting) {
        m_impl->wantsAnotherFrame = true;
        return;
    }
    invalidate();
}

void D2DWindow::render() {
    if (!isVisible()) {
        return;
    }

    auto* context = m_impl->target.beginDraw();
    if (!context) {
        return;
    }

    m_frameTime = std::chrono::steady_clock::now();
    m_impl->wantsAnotherFrame = false;
    m_impl->painting = true;

    try {
        onPaint(*context, sizeDip());
    } catch (winrt::hresult_error const& error) {
        log::error(L"Painting failed: {}", describeHresult(error.code()));
    }

    m_impl->painting = false;

    if (!m_impl->target.endDraw()) {
        GraphicsDevice::instance().recreate();
        invalidate();
        return;
    }

    m_impl->lastPresent = std::chrono::steady_clock::now();
    if (m_impl->wantsAnotherFrame) {
        invalidate();
    }
}

bool D2DWindow::onMessage(UINT, WPARAM, LPARAM, LRESULT&) { return false; }

LRESULT CALLBACK D2DWindow::windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    D2DWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto const* create = reinterpret_cast<CREATESTRUCTW const*>(lparam);
        self = static_cast<D2DWindow*>(create->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<D2DWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    LRESULT result = 0;
    if (self->onMessage(message, wparam, lparam, result)) {
        return result;
    }

    auto& impl = *self->m_impl;
    switch (message) {
        case WM_NCCALCSIZE:
            if (wparam) {
                // Returning zero with the proposed rectangle untouched makes the client area
                // exactly the window rectangle: no caption, no borders, no non-client area.
                // The usual "maximised window overhangs the monitor" bug does not follow
                // because WM_GETMINMAXINFO computes the maximised rectangle below.
                return 0;
            }
            break;

        case kMsgFrame:
            impl.framePending = false;
            self->render();
            return 0;

        case WM_TIMER:
            if (wparam == kFrameTimer) {
                KillTimer(hwnd, kFrameTimer);
                impl.framePending = false;
                self->render();
                return 0;
            }
            break;

        case WM_PAINT:
            // There is no WM_PAINT-driven painting here, but the system still sends this after
            // an unminimise and the surface has to be refreshed for it.
            ValidateRect(hwnd, nullptr);
            self->render();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_WINDOWPOSCHANGED: {
            auto const* position = reinterpret_cast<WINDOWPOS const*>(lparam);
            if (!(position->flags & SWP_NOSIZE)) {
                RECT client{};
                GetClientRect(hwnd, &client);
                impl.target.resize(SIZE{client.right - client.left, client.bottom - client.top},
                                   static_cast<float>(impl.dpi));
                // Synchronously, so dragging an edge does not trail an unpainted frame.
                self->render();
            }
            break;  // let DefWindowProc raise WM_SIZE and WM_MOVE
        }

        case WM_SIZE:
            impl.maximised = wparam == SIZE_MAXIMIZED;
            break;

        case WM_DPICHANGED: {
            impl.dpi = HIWORD(wparam);
            auto const* suggested = reinterpret_cast<RECT const*>(lparam);
            self->onDpiChanged(self->scale());
            // Ignoring the suggested rectangle leaves the window half off the new monitor.
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            HMONITOR const monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{sizeof(info)};
            if (GetMonitorInfoW(monitor, &info)) {
                RECT work = info.rcWork;
                // With an auto-hiding taskbar the work area is the whole monitor, and a window
                // covering every pixel of it stops the bar sliding out. Windows' own maximised
                // windows give back one pixel on that edge; so does this one.
                switch (autoHideEdge(monitor)) {
                    case ABE_BOTTOM: work.bottom -= 1; break;
                    case ABE_TOP: work.top += 1; break;
                    case ABE_LEFT: work.left += 1; break;
                    case ABE_RIGHT: work.right -= 1; break;
                    default: break;
                }
                // ptMaxPosition is relative to the monitor, not to the desktop; getting that
                // wrong is why "maximise" sometimes lands on the wrong screen.
                limits->ptMaxPosition.x = work.left - info.rcMonitor.left;
                limits->ptMaxPosition.y = work.top - info.rcMonitor.top;
                limits->ptMaxSize.x = work.right - work.left;
                limits->ptMaxSize.y = work.bottom - work.top;
            }
            limits->ptMinTrackSize.x =
                MulDiv(impl.minimumSizeDip.cx, static_cast<int>(impl.dpi), 96);
            limits->ptMinTrackSize.y =
                MulDiv(impl.minimumSizeDip.cy, static_cast<int>(impl.dpi), 96);
            return 0;
        }

        case WM_NCHITTEST:
            return self->hitTest(POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});

        case WM_NCRBUTTONUP:
            if (wparam == HTCAPTION || wparam == HTSYSMENU) {
                self->showSystemMenu(POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                return 0;
            }
            break;

        case WM_SYSCOMMAND:
            if ((wparam & 0xFFF0) == SC_KEYMENU || (wparam & 0xFFF0) == SC_MOUSEMENU) {
                RECT window{};
                GetWindowRect(hwnd, &window);
                D2D1_RECT_F const body = self->bodyRect();
                float const s = self->scale();
                self->showSystemMenu(
                    POINT{window.left + static_cast<int>(body.left * s),
                          window.top + static_cast<int>((body.top + Metrics::titleBarHeight) * s)});
                return 0;
            }
            break;

        case WM_MOUSEACTIVATE:
            // A flyout must never take activation, not even on the first click.
            if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_NOACTIVATE) {
                return MA_NOACTIVATE;
            }
            break;

        case WM_DESTROY:
            impl.target.detach();
            self->m_hwnd = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT D2DWindow::hitTest(POINT screen) const {
    RECT window{};
    GetWindowRect(m_hwnd, &window);

    float const s = scale();
    D2D1_POINT_2F const point{static_cast<float>(screen.x - window.left) / s,
                              static_cast<float>(screen.y - window.top) / s};
    D2D1_RECT_F const body = bodyRect();

    if (m_impl->resizable && !m_impl->maximised) {
        bool const left = point.x >= body.left - kResizeGripOutside &&
                          point.x < body.left + kResizeGripInside;
        bool const right = point.x > body.right - kResizeGripInside &&
                           point.x <= body.right + kResizeGripOutside;
        bool const top =
            point.y >= body.top - kResizeGripOutside && point.y < body.top + kResizeGripInside;
        bool const bottom = point.y > body.bottom - kResizeGripInside &&
                            point.y <= body.bottom + kResizeGripOutside;
        bool const insideX = point.x >= body.left - kResizeGripOutside &&
                             point.x <= body.right + kResizeGripOutside;
        bool const insideY = point.y >= body.top - kResizeGripOutside &&
                             point.y <= body.bottom + kResizeGripOutside;

        if (insideX && insideY) {
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
        }
    }

    // The shadow margin is transparent but the window is not layered, so without this it eats
    // every click in an invisible frame around itself. HTTRANSPARENT passes the hit through to
    // whatever is underneath, across process boundaries.
    if (point.x < body.left || point.x >= body.right || point.y < body.top ||
        point.y >= body.bottom) {
        return HTTRANSPARENT;
    }

    return isCaptionArea(point) ? HTCAPTION : HTCLIENT;
}

void D2DWindow::showSystemMenu(POINT screen) const {
    HMENU const menu = GetSystemMenu(m_hwnd, FALSE);
    if (!menu) {
        return;
    }

    bool const maximised = IsZoomed(m_hwnd) != FALSE;
    bool const minimised = IsIconic(m_hwnd) != FALSE;
    auto enable = [&](UINT id, bool on) {
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_STATE;
        item.fState = on ? MFS_ENABLED : MFS_DISABLED;
        SetMenuItemInfoW(menu, id, FALSE, &item);
    };
    enable(SC_RESTORE, minimised || maximised);
    enable(SC_MOVE, !maximised && !minimised);
    enable(SC_SIZE, m_impl->resizable && !maximised && !minimised);
    enable(SC_MINIMIZE, !minimised);
    enable(SC_MAXIMIZE, m_impl->resizable && !maximised);
    enable(SC_CLOSE, true);
    SetMenuDefaultItem(menu, UINT(-1), FALSE);

    // TPM_RETURNCMD keeps this out of the menu's own modal message pump.
    UINT const flags = TPM_RETURNCMD | TPM_RIGHTBUTTON |
                       (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
    int const command = TrackPopupMenu(menu, flags, screen.x, screen.y, 0, m_hwnd, nullptr);
    if (command) {
        PostMessageW(m_hwnd, WM_SYSCOMMAND, static_cast<WPARAM>(command), 0);
    }
}

}  // namespace peek::ui
