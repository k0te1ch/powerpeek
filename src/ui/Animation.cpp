#include "ui/Animation.h"

#include <algorithm>

namespace peek::ui {
namespace {

// A cubic Bezier with P0 = (0,0) and P3 = (1,1), solved for y at a given x.
struct CubicBezier {
    float x1, y1, x2, y2;

    static constexpr float curve(float a, float b, float t) noexcept {
        float const mt = 1.0f - t;
        return 3.0f * mt * mt * t * a + 3.0f * mt * t * t * b + t * t * t;
    }

    static constexpr float slope(float a, float b, float t) noexcept {
        float const mt = 1.0f - t;
        return 3.0f * mt * mt * a + 6.0f * mt * t * (b - a) + 3.0f * t * t * (1.0f - b);
    }

    float operator()(float progress) const noexcept {
        float t = progress;
        for (int i = 0; i < 6; ++i) {
            float const error = curve(x1, x2, t) - progress;
            float const d = slope(x1, x2, t);
            if (d < 1e-6f) {
                break;
            }
            t = std::clamp(t - error / d, 0.0f, 1.0f);
        }
        return curve(y1, y2, t);
    }
};

// Fluent ships exactly two curves plus linear, and they are far more extreme than the web's
// ease-in-out: (0,0,0,1) covers 63% of the travel in the first 10% of the duration. Softening
// them is the fastest way to make a custom-drawn window feel unlike the rest of the system.
constexpr CubicBezier kEntrance{0.0f, 0.0f, 0.0f, 1.0f};
constexpr CubicBezier kExit{1.0f, 0.0f, 1.0f, 1.0f};
constexpr CubicBezier kLinear{0.0f, 0.0f, 1.0f, 1.0f};

}  // namespace

float ease(Easing curve, float t) {
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    switch (curve) {
        case Easing::Linear:
            return kLinear(t);
        case Easing::Exit:
        case Easing::FadeOut:
            return kExit(t);
        case Easing::Standard:
        case Easing::Entrance:
        case Easing::FadeIn:
            break;
    }
    return kEntrance(t);
}

void Animated::animateTo(float target, std::chrono::milliseconds duration, Easing curve) {
    if (m_running && m_to == target) {
        return;
    }
    if (duration <= std::chrono::milliseconds::zero()) {
        snapTo(target);
        return;
    }

    m_from = m_current;
    m_to = target;
    m_duration = duration;
    m_curve = curve;
    m_start = std::chrono::steady_clock::now();
    m_running = true;
}

void Animated::snapTo(float value) {
    m_current = value;
    m_from = value;
    m_to = value;
    m_running = false;
}

bool Animated::tick(std::chrono::steady_clock::time_point now) {
    if (!m_running) {
        return false;
    }

    // A frame that arrives before the animation was started (a window rendering for the first
    // time uses the timestamp it captured before ticking) must not read as negative progress.
    auto const elapsed = now > m_start ? now - m_start : std::chrono::steady_clock::duration::zero();
    double const progress =
        std::chrono::duration<double>(elapsed) / std::chrono::duration<double>(m_duration);

    if (progress >= 1.0) {
        m_current = m_to;
        m_running = false;
        return false;
    }

    m_current = m_from + (m_to - m_from) * ease(m_curve, static_cast<float>(progress));
    return true;
}

}  // namespace peek::ui
