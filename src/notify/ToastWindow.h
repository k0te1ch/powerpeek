#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "core/Signal.h"
#include "core/Win.h"
#include "ui/Animation.h"
#include "ui/Drawing.h"
#include "ui/Window.h"

namespace peek::notify {

// What one notification card says.
struct ToastContent {
    std::wstring title;
    std::wstring body;
    // A Segoe MDL2 Assets codepoint from ui::glyph, drawn in a tinted badge whose colour
    // is what tells "connected" from "critically low" at a glance.
    std::wstring glyph;
    D2D1_COLOR_F badge{};
    ui::GaugeVisual gauge{};
    bool showGauge = false;
};

// The application's own notification card.
//
// It exists instead of a message box or a system toast because it must be able to appear
// over a running game without taking the keyboard away from it: WS_EX_NOACTIVATE plus
// SW_SHOWNOACTIVATE plus MA_NOACTIVATE, and never a call to SetForegroundWindow. Anything
// that activates a window here would drop the player out of their match.
//
// A card is never destroyed while it is on screen. ToastStack keeps a fixed pool and
// re-shows an idle one, which also removes the whole question of deleting a window from
// inside its own message handling.
class ToastWindow : public ui::D2DWindow {
public:
    ToastWindow();
    ~ToastWindow() override;

    // Lays the content out, sizes the window to fit it, places it in the bottom-right of
    // `workArea` raised by `offsetDip`, and starts the entrance animation.
    bool show(ToastContent content,
              std::chrono::milliseconds lifetime,
              RECT workArea,
              float offsetDip);

    // Starts the exit animation; the card reports `finished` once it is off screen.
    void dismiss();

    // Moves an already-visible card, which is what happens when the one below it closes.
    void restack(float offsetDip);

    bool idle() const noexcept;

    // The whole window in DIPs, transparent shadow margin included, which is what the
    // stack has to add up to place the next card.
    float heightDip() const noexcept;

    Signal<> clicked;
    Signal<ToastWindow*> finished;

protected:
    void onPaint(ID2D1DeviceContext& context, D2D1_SIZE_F size) override;
    bool onMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result) override;
    void onDpiChanged(float newScale) override;

private:
    enum class Phase { Idle, Entering, Holding, Leaving };

    float layoutContent();
    void place();
    void beginLeaving();
    void advance(std::chrono::steady_clock::time_point now);

    ToastContent m_content;
    ui::TextBlock m_title;
    ui::TextBlock m_body;
    ui::WindowShadow m_shadow;
    ui::Animated m_reveal{0.0f};

    Phase m_phase = Phase::Idle;
    std::chrono::milliseconds m_lifetime{0};
    std::chrono::milliseconds m_remaining{0};
    std::chrono::steady_clock::time_point m_lastTick{};
    // A card the pointer is resting on stops counting down, so reading a long name never
    // races the timeout.
    bool m_hovered = false;
    bool m_tracking = false;

    RECT m_workArea{};
    float m_offsetDip = 0.0f;
    float m_cardHeightDip = 0.0f;
    float m_titleHeightDip = 0.0f;
    float m_textWidthDip = 0.0f;
};

// The visible cards, stacked upward from the corner of the work area the system's own
// notifications use.
//
// The pool is deliberately small: five controllers all going flat at once must not bury
// the screen, so a sixth card recycles the oldest rather than adding to the pile.
class ToastStack {
public:
    static ToastStack& instance();

    void show(ToastContent content);
    void dismissAll();

    // Raised when the user clicks any card. The application treats it as "open the
    // window": that is the only thing a battery notification can usefully lead to.
    Signal<> clicked;

private:
    ToastStack();
    ~ToastStack();

    static constexpr std::size_t kSlots = 3;

    ToastWindow* acquire();
    void onFinished(ToastWindow* card);
    void restack();

    std::array<std::unique_ptr<ToastWindow>, kSlots> m_slots;
    // Visible cards, oldest first, so recycling takes the one that has been up longest and
    // restacking knows which card sits closest to the corner.
    std::vector<ToastWindow*> m_order;
};

}  // namespace peek::notify
