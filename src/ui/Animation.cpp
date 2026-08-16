#include "ui/Animation.h"

#include <cmath>

namespace peek::ui {
namespace {

// A millionth of a curve whose whole range is 0 to 1: far below both the precision of the
// float carrying it and the size of the pixel it ends up moving.
//
// The iteration count is a backstop rather than a budget. Bisection alone reaches that
// tolerance in twenty halvings, and Newton takes over long before then, so the loop leaves
// on the tolerance and the count only bounds a curve that misbehaves everywhere.
constexpr float kSolveTolerance = 1e-6f;
constexpr int kSolveIterations = 24;

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

    // Inverts x(t) = progress, then reads y off the t that solved it.
    //
    // Newton alone is not enough here, and the curve that breaks it is the one used most.
    // The Fluent entrance is (0,0,0,1), so its x(t) is t cubed: the slope near zero is flat
    // enough that the first step either lands outside the interval or is abandoned by a
    // small-slope test, and six unguarded iterations then finish nowhere near the root. Its
    // opening frames came out wrong in both directions -- measured against the closed form, a
    // tenth of a percent into the duration the curve stood at 0.056 where it belonged at
    // 0.028, and a hundredth of a percent in it stood at 3e-08 where it belonged at 0.0063.
    // Twice too far along, then nowhere at all: that is the kink, and it sits in the first
    // frames of every entrance the application draws.
    //
    // x(t) is non-decreasing on [0,1] for all three curves, so the root is bracketed from
    // the outset and bisection can always finish what Newton starts. Together they keep
    // Newton's speed everywhere it behaves and its convergence where it does not.
    float operator()(float progress) const noexcept {
        float lower = 0.0f;
        float upper = 1.0f;
        float t = progress;

        for (int i = 0; i < kSolveIterations; ++i) {
            float const error = curve(x1, x2, t) - progress;
            if (std::fabs(error) <= kSolveTolerance) {
                break;
            }
            if (error > 0.0f) {
                upper = t;
            } else {
                lower = t;
            }

            float const d = slope(x1, x2, t);
            float const candidate = t - error / d;
            // The step is taken only when it stays inside the bracket, which is also what
            // rejects the infinity a zero slope produces and the NaN a zero over zero does.
            t = (d > 0.0f && candidate > lower && candidate < upper) ? candidate
                                                                    : 0.5f * (lower + upper);
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
    // Standing on the value already asked for. Without this, the call starts a full
    // animation from a value to itself: every frame of it renders identically, and each one
    // asks the window for another. The window draws on demand and idles at no processor time
    // at all, which is what lets it sit in the tray for weeks -- and hover states retarget on
    // every mouse move, so this is the ordinary call rather than a corner case.
    //
    // The running half matters as much as the idle one and was missed the first time. A
    // widget half way through a fade, told to go to the value it is passing through, met
    // neither guard: the one above wants the destination to match, and this one used to want
    // the animation to be stopped. It fell through to a fresh animation from that value to
    // itself -- the very thing the guard exists to prevent, started from the busier state.
    // Snapping rather than returning is what stops the animation that was in flight.
    if (m_current == target) {
        snapTo(target);
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
