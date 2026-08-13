#pragma once

#include <chrono>

namespace peek::ui {

// WinUI's standard easing curves, as cubic Bezier curves. Motion is the cheapest way for a
// custom-drawn window to feel like a system one, and getting the curves right matters
// more than getting the durations right.
enum class Easing {
    Linear,
    // Point-to-point movement: things that travel across the screen.
    Standard,
    // Something arriving: fast out of the gate, settles gently.
    Entrance,
    // Something leaving: eases in, then accelerates away.
    Exit,
    FadeIn,
    FadeOut,
};

float ease(Easing curve, float t);

inline constexpr std::chrono::milliseconds kDurationFast{83};
inline constexpr std::chrono::milliseconds kDurationNormal{167};
inline constexpr std::chrono::milliseconds kDurationSlow{333};
inline constexpr std::chrono::milliseconds kDurationFlyout{250};

// One animated scalar: hover opacity, a gauge sweep, a flyout's slide offset.
//
// Nothing here owns a timer. The window's render loop ticks every animation it knows
// about and keeps rendering while any of them reports that it is still running, which is
// what lets the application idle at zero CPU when the UI is static.
class Animated {
public:
    Animated() = default;
    explicit Animated(float initial) : m_current(initial), m_from(initial), m_to(initial) {}

    // Retargets from wherever the value currently is, so interrupting a running
    // animation does not jump.
    void animateTo(float target, std::chrono::milliseconds duration, Easing curve = Easing::Standard);

    // Jumps without animating; used when a value changes while the window is hidden.
    void snapTo(float value);

    // Returns true while the animation still has frames left to produce.
    bool tick(std::chrono::steady_clock::time_point now);

    float value() const noexcept { return m_current; }
    float target() const noexcept { return m_to; }
    bool running() const noexcept { return m_running; }

private:
    float m_current = 0.0f;
    float m_from = 0.0f;
    float m_to = 0.0f;
    std::chrono::steady_clock::time_point m_start{};
    std::chrono::milliseconds m_duration{0};
    Easing m_curve = Easing::Standard;
    bool m_running = false;
};

}  // namespace peek::ui
