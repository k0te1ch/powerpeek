#include "ui/DesktopWidget.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/Strings.h"
#include "ui/Theme.h"
#include "ui/TrayText.h"

namespace peek::ui {
namespace {

constexpr wchar_t kWidgetClass[] = L"PowerPeek.DesktopWidget";

constexpr UINT kMenuOpen = 1;
constexpr UINT kMenuHide = 2;

constexpr float kBodyWidth = 264.0f;
constexpr float kPadding = 14.0f;
constexpr float kRowHeight = 48.0f;
constexpr float kMoreHeight = 22.0f;
constexpr float kGaugeWidth = 44.0f;
constexpr float kGaugeHeight = 20.0f;
constexpr float kGaugeGap = 12.0f;
constexpr float kLevelWidth = 60.0f;
constexpr float kNameHeight = 20.0f;
constexpr float kStatusHeight = 16.0f;
constexpr float kScreenGap = 16.0f;

// More rows than this and the card would start to cover the desktop it is meant to decorate;
// the rest are summed up in a closing line, as in the tray tooltip.
constexpr std::size_t kMaxRows = 5;

int scaled(float dip, float scale) { return static_cast<int>(std::lround(dip * scale)); }

// The shell window that hosts the desktop icons: Progman, or a WorkerW when the wallpaper
// slideshow has moved them. Sitting directly above it keeps the card over the wallpaper and
// under every other window, which HWND_BOTTOM would not -- that puts it behind the wallpaper.
HWND desktopHost() {
    HWND const progman = FindWindowW(L"Progman", nullptr);
    if (progman != nullptr && FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)) {
        return progman;
    }
    HWND found = nullptr;
    EnumWindows(
        [](HWND window, LPARAM result) -> BOOL {
            if (FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr) {
                *reinterpret_cast<HWND*>(result) = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

std::wstring_view statusText(DeviceInfo const& device) {
    if (device.charge == ChargeState::Unknown && device.source == PowerSource::Wired) {
        return text(Text::StatusWired);
    }
    return toString(device.charge);
}

std::wstring levelText(DeviceInfo const& device) {
    if (!device.hasBattery()) {
        return {};
    }
    std::wstring level;
    // A coarse reading is a stand-in for one of four buckets, and the card says so.
    if (device.fidelity == Fidelity::Coarse) {
        level += L'~';
    }
    level += std::to_wstring(device.percent);
    level += text(Text::UnitPercent);
    return level;
}

}  // namespace

DesktopWidget::DesktopWidget() = default;
DesktopWidget::~DesktopWidget() = default;

bool DesktopWidget::ensureWindow() {
    if (m_hwnd != nullptr) {
        return true;
    }
    CreateParams params;
    params.className = kWidgetClass;
    params.title = std::wstring(text(Text::AppName));
    params.initialSizeDip = SIZE{static_cast<LONG>(kBodyWidth + 2.0f * Metrics::shadowMargin),
                                 static_cast<LONG>(kRowHeight + 2.0f * Metrics::shadowMargin)};
    params.minimumSizeDip = params.initialSizeDip;
    params.resizable = false;
    params.noActivate = true;
    params.appWindow = false;
    if (!createWindow(params)) {
        return false;
    }
    embedInDesktop();
    return true;
}

// Show Desktop (Win+D) and Aero Peek act on top-level windows, so a card that is one of them
// disappears with everything else. A child of the desktop window is not one: it stays where it
// is and is still covered by every window opened over it. If the shell refuses the parenting --
// differing DPI awareness is the usual reason -- the card stays a top-level window sitting just
// above the desktop, which is the next best thing.
void DesktopWidget::embedInDesktop() {
    HWND const progman = FindWindowW(L"Progman", nullptr);
    if (progman == nullptr) {
        return;
    }
    LONG_PTR const style = GetWindowLongPtrW(m_hwnd, GWL_STYLE);
    SetWindowLongPtrW(m_hwnd, GWL_STYLE, (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_CHILD);
    SetParent(m_hwnd, progman);
    if (GetParent(m_hwnd) != progman) {
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, style);
        return;
    }
    m_embedded = true;
    SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void DesktopWidget::moveTo(POINT screen) {
    if (m_embedded) {
        // A child is placed in its parent's client coordinates; everything outside this
        // function, including the saved position, speaks screen coordinates.
        POINT local = screen;
        ScreenToClient(GetParent(m_hwnd), &local);
        SetWindowPos(m_hwnd, HWND_TOP, local.x, local.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    SetWindowPos(m_hwnd, nullptr, screen.x, screen.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

float DesktopWidget::bodyHeightDip() const {
    std::size_t const shown = std::min(m_devices.size(), kMaxRows);
    float height = 2.0f * kPadding + static_cast<float>(std::max<std::size_t>(shown, 1)) * kRowHeight;
    if (m_devices.size() > kMaxRows) {
        height += kMoreHeight;
    }
    return height;
}

void DesktopWidget::resizeToContent() {
    if (m_hwnd == nullptr) {
        return;
    }
    float const s = scale();
    SetWindowPos(m_hwnd, nullptr, 0, 0, scaled(kBodyWidth + 2.0f * Metrics::shadowMargin, s),
                 scaled(bodyHeightDip() + 2.0f * Metrics::shadowMargin, s),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    invalidate();
}

POINT DesktopWidget::defaultPosition() const {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    float const s = scale();
    float const margin = Metrics::shadowMargin;
    // The visible body, not the transparent margin around it, is what keeps `kScreenGap` from
    // the corner.
    return POINT{work.right - scaled(kBodyWidth + kScreenGap, s) - scaled(margin, s),
                 work.top + scaled(kScreenGap, s) - scaled(margin, s)};
}

void DesktopWidget::show(POINT const* position) {
    if (!ensureWindow()) {
        return;
    }
    resizeToContent();

    POINT target = defaultPosition();
    if (position != nullptr) {
        float const s = scale();
        POINT const probe{position->x + scaled(kBodyWidth * 0.5f + Metrics::shadowMargin, s),
                          position->y + scaled(Metrics::shadowMargin + kRowHeight * 0.5f, s)};
        if (MonitorFromPoint(probe, MONITOR_DEFAULTTONULL) != nullptr) {
            target = *position;
        }
    }

    moveTo(target);
    m_shown = true;
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    invalidate();
}

void DesktopWidget::hide() {
    m_shown = false;
    if (m_hwnd != nullptr) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void DesktopWidget::setControllers(std::vector<DeviceInfo> const& devices) {
    m_devices = devices;
    if (m_shown) {
        resizeToContent();
    }
}

void DesktopWidget::languageChanged() {
    if (m_shown) {
        invalidate();
    }
}

// A top-level card drags through the system's own move loop; a child has to do it by hand.
bool DesktopWidget::isCaptionArea(D2D1_POINT_2F) const { return !m_embedded; }

void DesktopWidget::showMenu(POINT screen) {
    HMENU const menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(menu, MF_STRING, kMenuOpen, std::wstring(text(Text::MenuOpen)).c_str());
    AppendMenuW(menu, MF_STRING, kMenuHide, std::wstring(text(Text::WidgetHide)).c_str());

    // The card never activates, and a menu raised from a window that is not foreground does
    // not close when the user clicks elsewhere.
    SetForegroundWindow(m_hwnd);
    UINT const flags = TPM_RETURNCMD | TPM_RIGHTBUTTON |
                       (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
    int const command = TrackPopupMenu(menu, flags, screen.x, screen.y, 0, m_hwnd, nullptr);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (command == kMenuOpen) {
        openRequested();
    } else if (command == kMenuHide) {
        hideRequested();
    }
}

bool DesktopWidget::onMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result) {
    switch (message) {
        case WM_WINDOWPOSCHANGING: {
            // Whatever moves the card -- the user's drag included -- leaves it where it
            // belongs in the stack instead of raising it over the windows the user opened.
            if (m_embedded) {
                return false;
            }
            auto* const position = reinterpret_cast<WINDOWPOS*>(lparam);
            HWND const host = desktopHost();
            if (host != nullptr && host != m_hwnd) {
                position->flags &= ~SWP_NOZORDER;
                position->hwndInsertAfter = host;
            }
            return false;
        }

        case WM_EXITSIZEMOVE: {
            RECT rect{};
            if (GetWindowRect(m_hwnd, &rect)) {
                moved(POINT{rect.left, rect.top});
            }
            result = 0;
            return true;
        }

        case WM_NCLBUTTONDBLCLK:
            if (wparam == HTCAPTION) {
                openRequested();
                result = 0;
                return true;
            }
            return false;

        case WM_NCRBUTTONUP:
            if (wparam == HTCAPTION) {
                showMenu(POINT{static_cast<int>(static_cast<short>(LOWORD(lparam))),
                               static_cast<int>(static_cast<short>(HIWORD(lparam)))});
                result = 0;
                return true;
            }
            return false;

        case WM_LBUTTONDOWN:
            if (m_embedded) {
                RECT rect{};
                GetWindowRect(m_hwnd, &rect);
                GetCursorPos(&m_dragCursor);
                m_dragOrigin = POINT{rect.left, rect.top};
                m_dragging = true;
                SetCapture(m_hwnd);
            }
            result = 0;
            return true;

        case WM_MOUSEMOVE:
            if (m_dragging) {
                POINT cursor{};
                GetCursorPos(&cursor);
                moveTo(POINT{m_dragOrigin.x + cursor.x - m_dragCursor.x,
                             m_dragOrigin.y + cursor.y - m_dragCursor.y});
            }
            result = 0;
            return true;

        case WM_LBUTTONUP:
            if (m_dragging) {
                m_dragging = false;
                ReleaseCapture();
                RECT rect{};
                if (GetWindowRect(m_hwnd, &rect)) {
                    moved(POINT{rect.left, rect.top});
                }
            }
            result = 0;
            return true;

        case WM_CAPTURECHANGED:
            m_dragging = false;
            return false;

        case WM_LBUTTONDBLCLK:
            openRequested();
            result = 0;
            return true;

        case WM_RBUTTONUP: {
            POINT cursor{};
            GetCursorPos(&cursor);
            showMenu(cursor);
            result = 0;
            return true;
        }

        default:
            return false;
    }
}

void DesktopWidget::onPaint(ID2D1DeviceContext& context, D2D1_SIZE_F) {
    Canvas canvas(context);
    Palette const& colors = theme().colors();
    D2D1_RECT_F const body = bodyRect();
    float const radius = Metrics::overlayCornerRadius;

    m_shadow.draw(canvas, body, radius, colors.shadow);
    fillRounded(canvas, body, radius, colors.flyoutBackground);
    strokeRounded(canvas, body, radius, colors.surfaceStroke);

    float const left = body.left + kPadding;
    float const right = body.right - kPadding;

    if (m_devices.empty()) {
        drawText(canvas, text(Text::NoDevices), theme().textFormat(TypeStyle::Body),
                 D2D1::RectF(left, body.top + kPadding, right, body.top + kPadding + kRowHeight),
                 colors.textSecondary, Align::Center, Align::Center);
        return;
    }

    std::vector<DeviceInfo const*> const ordered = trayOrder(m_devices);
    std::size_t const shown = std::min(ordered.size(), kMaxRows);

    for (std::size_t i = 0; i < shown; ++i) {
        DeviceInfo const& device = *ordered[i];
        float const top = body.top + kPadding + static_cast<float>(i) * kRowHeight;

        if (i > 0) {
            drawDivider(canvas, left, right, top, colors.dividerStroke);
        }

        GaugeVisual gauge;
        gauge.percent = device.hasBattery() ? device.percent : -1;
        gauge.fill = gauge.percent >= 0 ? static_cast<float>(gauge.percent) / 100.0f : 0.0f;
        gauge.charging = device.charge == ChargeState::Charging;
        gauge.approximate = device.fidelity == Fidelity::Coarse;
        gauge.level = theme().levelColor(gauge.percent);
        gauge.track = colors.controlFill;
        gauge.outline = colors.controlStrongStroke;
        gauge.text = colors.textPrimary;
        gauge.surface = colors.flyoutBackground;
        float const gaugeTop = top + (kRowHeight - kGaugeHeight) * 0.5f;
        drawBatteryGauge(canvas,
                         D2D1::RectF(left, gaugeTop, left + kGaugeWidth, gaugeTop + kGaugeHeight),
                         gauge);

        float const textLeft = left + kGaugeWidth + kGaugeGap;
        float const textRight = right - kLevelWidth;
        float const textTop = top + (kRowHeight - kNameHeight - kStatusHeight) * 0.5f;
        drawText(canvas, device.name, theme().textFormat(TypeStyle::BodyStrong),
                 D2D1::RectF(textLeft, textTop, textRight, textTop + kNameHeight),
                 colors.textPrimary);
        drawText(canvas, statusText(device), theme().textFormat(TypeStyle::Caption),
                 D2D1::RectF(textLeft, textTop + kNameHeight, textRight,
                             textTop + kNameHeight + kStatusHeight),
                 colors.textSecondary);

        std::wstring const level = levelText(device);
        if (!level.empty()) {
            drawText(canvas, level, theme().textFormat(TypeStyle::Subtitle),
                     D2D1::RectF(textRight, top, right, top + kRowHeight),
                     colors.textPrimary, Align::End, Align::Center);
        }
    }

    if (ordered.size() > shown) {
        float const top = body.top + kPadding + static_cast<float>(shown) * kRowHeight;
        drawText(canvas, formatText(Text::TrayMoreDevices, ordered.size() - shown),
                 theme().textFormat(TypeStyle::Caption),
                 D2D1::RectF(left, top, right, top + kMoreHeight), colors.textTertiary);
    }
}

}  // namespace peek::ui
