#include "notify/ToastWindow.h"

#include <windowsx.h>

#include <algorithm>
#include <utility>

#include "core/Logger.h"
#include "core/Strings.h"
#include "ui/Theme.h"

namespace peek::notify {
namespace {

using namespace std::chrono_literals;

constexpr wchar_t kToastClass[] = L"PowerPeek.ToastWindow";

// Posted to the card after a monitor change so that it re-places itself: the base window
// answers WM_DPICHANGED by moving to the rectangle the system suggests, which is not
// where a toast belongs, and that move happens after onDpiChanged returns.
constexpr UINT kMsgReplace = WM_APP + 0x1F1;

constexpr float kCardWidth = 340.0f;
constexpr float kPadding = 16.0f;
constexpr float kBadgeSize = 36.0f;
constexpr float kBadgeGap = 12.0f;
constexpr float kGaugeWidth = 56.0f;
constexpr float kGaugeHeight = 24.0f;
constexpr float kProgressHeight = 2.0f;
constexpr float kTitleGap = 2.0f;

// Distance from the work-area corner to the visible card, and between stacked cards.
constexpr float kEdgeGap = 12.0f;
constexpr float kStackGap = 8.0f;

// The card slides in from below. It must stay within the shadow margin, otherwise the
// content is drawn outside the window and the first frames are clipped.
constexpr float kSlideDip = 20.0f;
static_assert(kSlideDip <= ui::Metrics::shadowMargin,
              "the entrance slide is drawn inside the window and cannot exceed its margin");

constexpr auto kLifetime = 6000ms;

D2D1_COLOR_F withAlpha(D2D1_COLOR_F color, float alpha) {
    color.a = alpha;
    return color;
}

RECT taskbarWorkArea() {
    // The monitor the taskbar is on, because that is where the user expects notifications
    // and where the system puts its own.
    HMONITOR const monitor =
        MonitorFromWindow(FindWindowW(L"Shell_TrayWnd", nullptr), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) {
        log::warning(L"GetMonitorInfo failed for the taskbar monitor: {}",
                     describeHresult(HRESULT_FROM_WIN32(GetLastError())));
        RECT fallback{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback, 0);
        return fallback;
    }
    // rcWork already excludes the taskbar on whichever edge it sits, so a left, top or
    // right taskbar needs no special case.
    return info.rcWork;
}

int scaled(float dip, float scale) { return static_cast<int>(std::lround(dip * scale)); }

}  // namespace

ToastWindow::ToastWindow() {
    m_title.setStyle(ui::TypeStyle::BodyStrong);
    m_body.setStyle(ui::TypeStyle::Body);
    m_body.setWrapping(true);
}

ToastWindow::~ToastWindow() = default;

bool ToastWindow::idle() const noexcept { return m_phase == Phase::Idle; }

float ToastWindow::heightDip() const noexcept {
    return m_cardHeightDip + 2.0f * ui::Metrics::shadowMargin;
}

float ToastWindow::layoutContent() {
    float const textLeft = kPadding + kBadgeSize + kBadgeGap;
    float const gaugeSpace = m_content.showGauge ? kGaugeWidth + kBadgeGap : 0.0f;
    m_textWidthDip = std::max(80.0f, kCardWidth - textLeft - kPadding - gaugeSpace);

    m_titleHeightDip = m_title.measure(m_textWidthDip);
    float const bodyHeight = m_body.measure(m_textWidthDip);
    float const textHeight =
        m_titleHeightDip + (m_body.empty() ? 0.0f : kTitleGap + bodyHeight);

    return std::max(kBadgeSize + 2.0f * kPadding,
                    textHeight + 2.0f * kPadding + kProgressHeight);
}

void ToastWindow::place() {
    if (m_hwnd == nullptr) {
        return;
    }
    float const s = scale();
    float const margin = ui::Metrics::shadowMargin;

    int const width = scaled(kCardWidth + 2.0f * margin, s);
    int const height = scaled(m_cardHeightDip + 2.0f * margin, s);
    int const marginPx = scaled(margin, s);
    int const gap = scaled(kEdgeGap, s);

    // The window is larger than the card by the transparent margin on every side, so the
    // margin is added back to leave the *visible* card `kEdgeGap` from the corner.
    int const x = m_workArea.right - gap - width + marginPx;
    int const y = m_workArea.bottom - gap - height + marginPx - scaled(m_offsetDip, s);

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

bool ToastWindow::show(ToastContent content,
                       std::chrono::milliseconds lifetime,
                       RECT workArea,
                       float offsetDip) {
    if (m_hwnd == nullptr) {
        CreateParams params;
        params.className = kToastClass;
        params.title = std::wstring(text(Text::AppName));
        params.initialSizeDip =
            SIZE{static_cast<LONG>(kCardWidth + 2.0f * ui::Metrics::shadowMargin), 140};
        params.minimumSizeDip = params.initialSizeDip;
        params.resizable = false;
        params.topmost = true;
        params.noActivate = true;
        params.appWindow = false;
        if (!createWindow(params)) {
            return false;
        }
    }

    m_content = std::move(content);
    m_title.setText(m_content.title);
    m_body.setText(m_content.body);

    m_workArea = workArea;
    m_offsetDip = offsetDip;
    m_cardHeightDip = layoutContent();
    place();

    m_lifetime = lifetime;
    m_remaining = lifetime;
    m_lastTick = std::chrono::steady_clock::now();
    m_hovered = false;
    m_phase = Phase::Entering;
    m_reveal.snapTo(0.0f);
    m_reveal.animateTo(1.0f, ui::kDurationSlow, ui::Easing::Entrance);

    // SW_SHOWNOACTIVATE, never SW_SHOW: taking activation here would minimise a fullscreen
    // borderless game.
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    invalidate();
    return true;
}

void ToastWindow::restack(float offsetDip) {
    if (m_phase == Phase::Idle) {
        return;
    }
    m_offsetDip = offsetDip;
    place();
}

void ToastWindow::dismiss() {
    if (m_phase == Phase::Idle || m_phase == Phase::Leaving) {
        return;
    }
    beginLeaving();
}

void ToastWindow::beginLeaving() {
    m_phase = Phase::Leaving;
    m_reveal.animateTo(0.0f, ui::kDurationNormal, ui::Easing::Exit);
    invalidate();
}

void ToastWindow::advance(std::chrono::steady_clock::time_point now) {
    bool const animating = m_reveal.tick(now);
    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastTick);
    m_lastTick = now;

    switch (m_phase) {
        case Phase::Entering:
            if (!animating) {
                m_phase = Phase::Holding;
            }
            break;

        case Phase::Holding:
            if (!m_hovered) {
                m_remaining -= elapsed;
                if (m_remaining <= 0ms) {
                    m_remaining = 0ms;
                    beginLeaving();
                }
            }
            break;

        case Phase::Leaving:
            if (!animating) {
                ShowWindow(m_hwnd, SW_HIDE);
                m_phase = Phase::Idle;
                finished(this);
            }
            break;

        case Phase::Idle:
            break;
    }
}

void ToastWindow::onPaint(ID2D1DeviceContext& context, D2D1_SIZE_F) {
    advance(frameTime());
    if (m_phase == Phase::Idle) {
        return;
    }

    ui::Canvas canvas(context);
    ui::Palette const& colors = ui::theme().colors();
    D2D1_RECT_F const body = bodyRect();
    float const reveal = m_reveal.value();
    float const radius = ui::Metrics::overlayCornerRadius;

    context.SetTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - reveal) * kSlideDip));
    // One layer for the whole card is what makes the fade a fade rather than a pile of
    // independently translucent shapes showing through each other.
    context.PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr,
                                             D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                             D2D1::Matrix3x2F::Identity(), reveal),
                      nullptr);

    m_shadow.draw(canvas, body, radius, colors.shadow);
    ui::fillRounded(canvas, body, radius, colors.flyoutBackground);
    ui::strokeRounded(canvas, body, radius, colors.surfaceStroke);

    D2D1_RECT_F const badge{body.left + kPadding, body.top + kPadding,
                            body.left + kPadding + kBadgeSize, body.top + kPadding + kBadgeSize};
    ui::fillRounded(canvas, badge, radius, withAlpha(m_content.badge, 0.18f));
    ui::drawIcon(canvas, m_content.glyph, kBadgeSize * 0.5f, badge, m_content.badge);

    float const textLeft = body.left + kPadding + kBadgeSize + kBadgeGap;
    m_title.draw(canvas, D2D1::Point2F(textLeft, body.top + kPadding), colors.textPrimary);
    if (!m_body.empty()) {
        m_body.draw(canvas,
                    D2D1::Point2F(textLeft, body.top + kPadding + m_titleHeightDip + kTitleGap),
                    colors.textSecondary);
    }

    if (m_content.showGauge) {
        ui::GaugeVisual gauge = m_content.gauge;
        gauge.level = ui::theme().levelColor(gauge.percent);
        gauge.track = colors.controlFill;
        gauge.outline = colors.controlStrongStroke;
        gauge.text = colors.textPrimary;
        gauge.surface = colors.flyoutBackground;
        float const gaugeTop = body.top + kPadding + (kBadgeSize - kGaugeHeight) * 0.5f;
        ui::drawBatteryGauge(canvas,
                             D2D1::RectF(body.right - kPadding - kGaugeWidth, gaugeTop,
                                         body.right - kPadding, gaugeTop + kGaugeHeight),
                             gauge);
    }

    if (m_lifetime > 0ms) {
        float const left = static_cast<float>(m_remaining.count()) /
                           static_cast<float>(m_lifetime.count());
        float const railLeft = body.left + radius;
        float const railRight = body.right - radius;
        ui::fillRounded(canvas,
                        D2D1::RectF(railLeft, body.bottom - kProgressHeight - 1.0f,
                                    railLeft + (railRight - railLeft) * std::clamp(left, 0.0f, 1.0f),
                                    body.bottom - 1.0f),
                        kProgressHeight * 0.5f, withAlpha(colors.accent, 0.7f));
    }

    context.PopLayer();
    context.SetTransform(D2D1::Matrix3x2F::Identity());

    if (m_phase != Phase::Idle) {
        // The countdown hairline moves every frame, so the card asks for the next one for
        // as long as it is on screen and then stops entirely.
        requestFrame();
    }
}

bool ToastWindow::onMessage(UINT message, WPARAM, LPARAM, LRESULT& result) {
    switch (message) {
        case WM_LBUTTONUP:
            clicked();
            dismiss();
            result = 0;
            return true;

        case WM_RBUTTONUP:
            dismiss();
            result = 0;
            return true;

        case WM_MOUSEMOVE:
            if (!m_tracking) {
                TRACKMOUSEEVENT track{sizeof(track)};
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = m_hwnd;
                m_tracking = TrackMouseEvent(&track) != FALSE;
            }
            m_hovered = true;
            result = 0;
            return true;

        case WM_MOUSELEAVE:
            m_tracking = false;
            m_hovered = false;
            invalidate();
            result = 0;
            return true;

        case kMsgReplace:
            place();
            result = 0;
            return true;

        default:
            return false;
    }
}

void ToastWindow::onDpiChanged(float) {
    // The base is about to move the window to the rectangle the system suggested, so the
    // correction has to happen after this returns rather than in it.
    PostMessageW(m_hwnd, kMsgReplace, 0, 0);
}

ToastStack& ToastStack::instance() {
    static ToastStack stack;
    return stack;
}

ToastStack::ToastStack() = default;
ToastStack::~ToastStack() = default;

ToastWindow* ToastStack::acquire() {
    for (auto& slot : m_slots) {
        if (slot && slot->idle()) {
            return slot.get();
        }
    }
    for (auto& slot : m_slots) {
        if (!slot) {
            slot = std::make_unique<ToastWindow>();
            slot->clicked.connect([this] { clicked(); });
            slot->finished.connect([this](ToastWindow* card) { onFinished(card); });
            return slot.get();
        }
    }
    // Every card is busy: the oldest one has had its turn.
    return m_order.empty() ? nullptr : m_order.front();
}

void ToastStack::show(ToastContent content) {
    ToastWindow* const card = acquire();
    if (card == nullptr) {
        return;
    }
    std::erase(m_order, card);

    // Placed at the corner; the cards already up are pushed away from it below.
    if (!card->show(std::move(content), kLifetime, taskbarWorkArea(), 0.0f)) {
        return;
    }
    m_order.push_back(card);
    restack();
}

void ToastStack::dismissAll() {
    for (auto& slot : m_slots) {
        if (slot) {
            slot->dismiss();
        }
    }
}

void ToastStack::onFinished(ToastWindow* card) {
    std::erase(m_order, card);
    restack();
}

void ToastStack::restack() {
    float offset = 0.0f;
    // Newest first: it takes the corner and everything older moves up by exactly its
    // height, which keeps the gaps even whatever the cards' contents made them.
    for (auto card = m_order.rbegin(); card != m_order.rend(); ++card) {
        (*card)->restack(offset);
        offset += (*card)->heightDip() - 2.0f * ui::Metrics::shadowMargin + kStackGap;
    }
}

}  // namespace peek::notify
