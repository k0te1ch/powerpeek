// The easing curves and the animated scalar behind every fade, slide and sweep in the window.
//
// Two things earn this file. `ease` is the mapping table between the six names the UI code uses
// and the three curves that actually exist, and it is a switch with no `default` in which three
// of those names reach their curve only by falling out of the bottom -- so one inserted case
// silently rewires the rest, and every hover, press and chevron in the tree asks for a name from
// that table. `Animated` is the whole contract with the render loop: the window keeps painting
// for exactly as long as some animation reports it is still running, so a value that settles a
// hair short of its target leaves a permanent artefact, and a flag that disagrees with the tick
// it came from costs either a frozen animation or a window that never idles.
//
// `animateTo` reads the clock itself and there is no way to inject a start instant, so no test
// here can pin a mid-flight value to an exact number. Every timing case instead brackets the
// call between two steady_clock readings, which traps the start instant in [before, after] and
// makes three families of tick argument exact on any machine and under any scheduling:
// tick(before) always reads as zero progress, tick(after + duration) always reads as finished,
// and tick(before + d) for a d below the duration is always still running. Mid-flight cases
// animate over seconds so that the microseconds animateTo itself costs cannot move the sampled
// progress. Nothing here sleeps, and nothing reads the clock to produce an expected value.

#include "TestSupport.h"

#include "ui/Animation.h"

#include <chrono>
#include <cmath>

namespace {

using peek::ui::Animated;
using peek::ui::Easing;
using peek::ui::ease;
using peek::ui::kDurationNormal;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

struct NamedCurve {
    Easing curve;
    char const* name;
};

// All six names the UI code can ask for. Several are aliases for the same curve, which is
// precisely why the whole set is walked rather than the three distinct curves.
constexpr NamedCurve kAllCurves[] = {
    {Easing::Linear, "Linear"},
    {Easing::Standard, "Standard"},
    {Easing::Entrance, "Entrance"},
    {Easing::Exit, "Exit"},
    {Easing::FadeIn, "FadeIn"},
    {Easing::FadeOut, "FadeOut"},
};

// Away from both ends, where the curves are furthest apart in value and so easiest to tell
// from one another. The bisection-guarded solve behind them converges across the whole
// interval, so this is about picking the curves apart rather than about avoiding the solve.
constexpr float kMidSamples[] = {0.1f, 0.25f, 0.5f, 0.75f, 0.9f};
constexpr float kMirrorSamples[] = {0.25f, 0.5f, 0.75f};
constexpr float kLinearSamples[] = {0.05f, 0.1f, 0.25f, 0.4f, 0.5f, 0.6f, 0.75f, 0.9f, 0.95f};

// The closed forms behind the two Bezier curves the application ships. Both share
// y(t) = 3t^2 - 2t^3; the entrance control points give x(t) = t^3 and the exit points give
// x(t) = 1 - (1 - t)^3, so inverting either is one cube root rather than an iteration. Read the
// control points off Animation.cpp and these fall out of the same two polynomials, which is what
// makes them an expectation and not a second copy of the thing under test.
double entranceExact(double progress) {
    double const t = std::cbrt(progress);
    return t * t * (3.0 - 2.0 * t);
}

double exitExact(double progress) {
    double const t = 1.0 - std::cbrt(1.0 - progress);
    return t * t * (3.0 - 2.0 * t);
}

// Both ends and the middle in one set, because the two curves are difficult in opposite
// places: the entrance curve's x(t) is flat at the origin and the exit curve's is flat at one,
// and inverting a function whose slope has gone is where a solve fails. The set stops at 1e-4
// rather than reaching further down -- the solve leaves on an absolute tolerance of 1e-6 in x,
// so a progress of about that size is already satisfied by the starting guess and would pin the
// tolerance rather than the curve.
constexpr float kClosedFormSamples[] = {1e-4f, 1e-3f, 0.01f, 0.05f, 0.2f, 0.5f, 0.8f,
                                        0.95f, 0.99f, 0.999f, 0.9999f};

// The solve leaves when x is within 1e-6 of the progress asked for, and that residue reaches y
// multiplied by dy/dx -- which the entrance curve pushes past forty near the origin and the exit
// curve past forty near one. Hence an expectation pinned at 1e-4 rather than at the solve's own
// tolerance. doctest adds one to the compared magnitude before scaling, so this reads as roughly
// 1e-4 absolute for the values near zero and 2e-4 for those near one; the shipped solve meets
// both with about a factor of two in hand.
constexpr double kClosedFormEpsilon = 1e-4;

// Hard against both ends, where linear's own x(t) = 3t^2 - 2t^3 is flat and an inversion has
// least to work with.
constexpr float kLinearEndSamples[] = {1e-5f, 1e-4f, 1e-3f, 0.01f,
                                       0.99f, 0.999f, 0.9999f, 0.99999f};

// Linear needs none of that headroom: its x and y are the same polynomial, so the value handed
// back is the x the solve stopped on and the identity holds to the solve's own tolerance
// wherever it is sampled -- an order of magnitude tighter than the Bezier inversions can promise.
constexpr double kIdentityEpsilon = 1e-5;

// A finer sweep than the interval and direction cases use, because what this one looks for is a
// step rather than a wrong value. The largest step any of the three curves genuinely takes at
// this resolution is the entrance curve's first, a shade under 0.028, and the exit curve's last
// is the mirror of it. The bound leaves close to half as much again on top of that, and still
// sits well under the jump an abandoned iteration leaves behind.
constexpr int kSweepSteps = 1024;
constexpr float kMaxSweepStep = 0.04f;

// Long enough that the animation is unambiguously still in flight at the sampled frame times
// whatever the scheduler does between two adjacent statements.
constexpr milliseconds kLongDuration{10000};

}  // namespace

TEST_CASE("ease: every curve pins both endpoints exactly") {
    // Exact equality rather than Approx, because both endpoints are returned as literals before
    // any Bezier work happens. An alpha that came back as 0.004 instead of 0 leaves a supposedly
    // hidden highlight permanently on screen, and 0.998 instead of 1 leaves a fully hovered
    // control dimmer than the theme intends; neither has a later frame to correct it.
    for (auto const& entry : kAllCurves) {
        CAPTURE(entry.name);
        CHECK(ease(entry.curve, 0.0f) == 0.0f);
        CHECK(ease(entry.curve, 1.0f) == 1.0f);
    }
}

TEST_CASE("ease: progress outside the unit interval is clamped") {
    // The solve behind ease clamps its own Newton parameter but would still evaluate the
    // polynomial for an out-of-range argument, so without these two guards a caller handing
    // over an unclamped ratio would get an alpha above 1 or a negative slide offset -- a widget
    // drawn outside its own bounds, or a colour that saturates instead of fading.
    for (auto const& entry : kAllCurves) {
        CAPTURE(entry.name);
        CHECK(ease(entry.curve, -0.5f) == 0.0f);
        CHECK(ease(entry.curve, -1000.0f) == 0.0f);

        // Negative zero satisfies `t <= 0.0f`, so it takes the early return like any other
        // non-positive progress rather than reaching the solve.
        CHECK(ease(entry.curve, -0.0f) == 0.0f);

        CHECK(ease(entry.curve, 1.5f) == 1.0f);
        CHECK(ease(entry.curve, 1000.0f) == 1.0f);
    }
}

TEST_CASE("ease: linear is the identity") {
    // kLinear's control points make the x and y polynomials the same expression, so the solve
    // drives one onto the requested progress and then returns the other unchanged. Linear is
    // the curve chosen when a value must track time exactly, and pointing its switch label at
    // the entrance curve is a one-line mistake that still looks like plausible motion.
    //
    // The samples deliberately avoid the extremes: below roughly progress 0.001 the six fixed
    // Newton iterations have not converged, so an identity assertion down there would be
    // pinning the residual rather than the contract.
    for (float const sample : kLinearSamples) {
        CAPTURE(sample);
        CHECK(ease(Easing::Linear, sample) == doctest::Approx(sample));
    }
}

TEST_CASE("ease: standard, entrance and fadeIn share one curve") {
    // These three labels reach the entrance curve only by falling out of the bottom of a switch
    // that has no default, so inserting a case above the break, or adding a default that
    // returns, silently rewires whichever names sit below. Every hover, press, focus, knob,
    // chevron and gauge asks for Entrance, so that edit would flatten or reverse essentially
    // all motion in the window at once.
    for (float const sample : kMidSamples) {
        CAPTURE(sample);
        float const entrance = ease(Easing::Entrance, sample);
        CHECK(ease(Easing::Standard, sample) == entrance);
        CHECK(ease(Easing::FadeIn, sample) == entrance);
    }
}

TEST_CASE("ease: exit and fadeOut share one curve") {
    // The same mapping-table guard on the other branch: FadeOut having quietly become the
    // entrance curve would make a toast's dismissal accelerate into nothing instead of easing
    // away, which reads as a glitch rather than as motion.
    for (float const sample : kMidSamples) {
        CAPTURE(sample);
        CHECK(ease(Easing::FadeOut, sample) == ease(Easing::Exit, sample));
    }

    // The two branches are genuinely different curves, which is what makes the aliasing checks
    // above worth anything at all.
    CHECK(ease(Easing::Exit, 0.5f) != ease(Easing::Entrance, 0.5f));
}

TEST_CASE("ease: entrance front-loads the travel, exit back-loads it") {
    // Closed-form values rather than numbers scraped off a run. The entrance control points map
    // x = t^3 against y = 3t^2 - 2t^3, hence y = 3*x^(2/3) - 2x; the exit points map
    // x = 1 - (1 - t)^3 against the same y. The two constants are declared one line apart and
    // differ only in where the zeros sit, so transposing them is completely silent -- the
    // animation still runs, still starts at 0 and ends at 1, and only the feel is inverted.
    // The epsilon is headroom against a compiler reassociating the polynomial; the shipped
    // algorithm reproduces all six to within 3e-7.
    CHECK(ease(Easing::Entrance, 0.25f) == doctest::Approx(0.690551).epsilon(0.001));
    CHECK(ease(Easing::Entrance, 0.5f) == doctest::Approx(0.889882).epsilon(0.001));
    CHECK(ease(Easing::Entrance, 0.75f) == doctest::Approx(0.976445).epsilon(0.001));

    CHECK(ease(Easing::Exit, 0.25f) == doctest::Approx(0.023555).epsilon(0.001));
    CHECK(ease(Easing::Exit, 0.5f) == doctest::Approx(0.110118).epsilon(0.001));
    CHECK(ease(Easing::Exit, 0.75f) == doctest::Approx(0.309449).epsilon(0.001));

    CHECK(ease(Easing::Entrance, 0.5f) > 0.5f);
    CHECK(ease(Easing::Exit, 0.5f) < 0.5f);
}

TEST_CASE("ease: exit is the mirror image of entrance") {
    // Both curves share y(t) = 3t^2 - 2t^3 and their x mappings are reflections of one another,
    // so y_exit(x) = 1 - y_entrance(1 - x) is an exact identity for the shipped control points.
    // It catches a typo in either quadruple that the pinned values might survive -- the exit
    // points written as {1,1,1,0} still give a monotonic curve from 0 to 1 with plausible
    // numbers -- and it encodes the intent that a thing leaving is the time-reverse of the same
    // thing arriving, which is what makes a toast dismissed mid-entrance look like one
    // continuous movement. Samples stay in the middle because Approx is relative and both sides
    // are near zero at the ends.
    for (float const sample : kMirrorSamples) {
        CAPTURE(sample);
        CHECK(ease(Easing::Exit, sample) ==
              doctest::Approx(1.0f - ease(Easing::Entrance, 1.0f - sample)));
    }
}

TEST_CASE("ease: no curve ever leaves the unit interval") {
    // Control points off the unit square give the familiar bouncy easing. Here that would be an
    // alpha above 1 or a slide offset past its own end position, so the widget visibly pops past
    // its target and comes back -- and Fluent motion is meant to be non-overshooting. The clamp
    // on the Newton parameter and the choice of control points are what prevent it.
    for (auto const& entry : kAllCurves) {
        CAPTURE(entry.name);
        for (int i = 0; i <= 128; ++i) {
            CAPTURE(i);
            float const y = ease(entry.curve, static_cast<float>(i) / 128.0f);
            CHECK(y >= 0.0f);
            CHECK(y <= 1.0f);
        }
    }
}

TEST_CASE("ease: no curve ever goes backwards") {
    // A curve that dips is the one easing defect users spot immediately without being able to
    // name it: the highlight brightens, falls back a shade, then brightens again. It is also
    // what a bad solve produces, since the solve is re-run from scratch for every sampled
    // progress and nothing forces consistency between neighbouring frames. Non-decreasing
    // rather than strictly increasing, so a float tie in the flat tails cannot flake.
    for (auto const& entry : kAllCurves) {
        CAPTURE(entry.name);
        float previous = 0.0f;
        for (int i = 0; i <= 128; ++i) {
            CAPTURE(i);
            float const y = ease(entry.curve, static_cast<float>(i) / 128.0f);
            CHECK(y >= previous);
            previous = y;
        }
    }
}

TEST_CASE("ease: every curve has all but reached 1 just before the end") {
    // The `t >= 1.0f` early return hands back a perfect 1 whatever the curve underneath does, so
    // a curve that had only climbed to 0.7 by then would look correct at both endpoints and
    // simply jump on the final frame -- which is exactly the frame the render loop paints as it
    // stops asking for more. The threshold is loose on purpose, and there is deliberately no
    // mirrored assertion at the bottom of the domain: the solve is only trustworthy away from
    // zero, so one there would be pinning a numerical artefact.
    for (auto const& entry : kAllCurves) {
        CAPTURE(entry.name);
        CHECK(ease(entry.curve, 0.9999f) > 0.99f);
    }
}

TEST_CASE("ease: the entrance curve matches its closed form in the first frames") {
    // This is the curve behind Standard, Entrance and FadeIn, which is to say behind nearly
    // every animation the window runs, and its x(t) is t cubed: the function the solve has to
    // invert is at its flattest exactly where an animation begins. A solve that cannot get a
    // foothold there hands back a value from further along the curve than the frame has earned,
    // and what that looks like is a fade or a slide that opens with a jump and only then eases.
    // It is in the first frames, on every entrance, so it is the most-watched motion there is.
    //
    // Small progress values carry the case; the mid-range ones are here so a failure says
    // whether the whole curve moved or only its opening.
    for (float const sample : kClosedFormSamples) {
        CAPTURE(sample);
        CHECK(ease(Easing::Entrance, sample) ==
              doctest::Approx(entranceExact(sample)).epsilon(kClosedFormEpsilon));
    }
}

TEST_CASE("ease: the exit curve matches its closed form in the last frames") {
    // The same failure reflected. The exit control points give x(t) = 1 - (1 - t)^3, flat as the
    // progress approaches one, so this curve's difficult frames are the ones that finish a
    // dismissal rather than the ones that start it -- and a toast disappearing is a motion the
    // user is looking straight at. The mirror case above ties the two curves to each other,
    // which a solve wrong in matching ways at both ends would still satisfy; this ties this one
    // to arithmetic instead.
    for (float const sample : kClosedFormSamples) {
        CAPTURE(sample);
        CHECK(ease(Easing::Exit, sample) ==
              doctest::Approx(exitExact(sample)).epsilon(kClosedFormEpsilon));
    }
}

TEST_CASE("ease: linear is the identity at the ends as well as the middle") {
    // Linear's x and y are one polynomial, so the number returned is literally the x the solve
    // settled on: the identity is not an approximation of a curve but a direct readout of how
    // close the solve got, and it therefore holds to the solve's own tolerance wherever it is
    // sampled. That makes both ends assertable rather than a region to keep clear of, and the
    // ends are the point -- x(t) is flat at each of them, which is where an inversion that runs
    // out of room lands furthest from the root. Linear is what the code asks for when a value
    // has to track time exactly, so drift here is motion running ahead of its own clock.
    for (float const sample : kLinearEndSamples) {
        CAPTURE(sample);
        CHECK(ease(Easing::Linear, sample) == doctest::Approx(sample).epsilon(kIdentityEpsilon));
    }
}

TEST_CASE("ease: no curve steps between adjacent frames") {
    // The shape of a bad solve is a discontinuity rather than a bias, and this is the case that
    // names it. Every sampled progress is inverted from scratch with nothing tying one frame's
    // answer to the next, so an inversion that converges for some inputs and gives up for others
    // draws a curve smooth in places and stepped in between. The step is what the eye catches:
    // half a percent of error either side of it goes unnoticed and the jump does not.
    //
    // Sampling eight times as finely as the interval and direction cases do is what makes the
    // bound meaningful, and it also reports the progress a failure happened at closely enough to
    // say which part of the curve broke. Those two checks are repeated here for that reason
    // rather than because they are missing.
    for (auto const& entry : kAllCurves) {
        CAPTURE(entry.name);
        float previous = 0.0f;
        for (int i = 0; i <= kSweepSteps; ++i) {
            float const progress = static_cast<float>(i) / static_cast<float>(kSweepSteps);
            CAPTURE(progress);
            float const y = ease(entry.curve, progress);
            CHECK(y >= 0.0f);
            CHECK(y <= 1.0f);
            CHECK(y >= previous);
            CHECK(y - previous <= kMaxSweepStep);
            previous = y;
        }
    }
}

TEST_CASE("animated: a default-constructed value is zero and idle") {
    Animated a;
    CHECK(a.value() == 0.0f);
    CHECK(a.target() == 0.0f);
    CHECK_FALSE(a.running());

    // The render loop ORs the tick result of every widget in the tree and asks for another
    // frame if anything returns true, so a value nobody has animated must report false or the
    // window repaints for ever from its very first frame. The same early return is also the
    // only thing between an untouched Animated and a division by its zero duration.
    CHECK_FALSE(a.tick(steady_clock::now()));
    CHECK(a.value() == 0.0f);
}

TEST_CASE("animated: the explicit constructor seeds both the value and the target") {
    Animated a(0.75f);
    CHECK(a.value() == 0.75f);
    CHECK(a.target() == 0.75f);
    CHECK_FALSE(a.running());

    // Widgets declare their animations with a brace-initialised seed; the nav pill's stretch is
    // `Animated m_pillStretch{1.0f}`. A seed that reached the value but not the target would
    // have the pill asked to shrink to nothing, and one that reached the target but not the
    // value would draw it collapsed -- both on the first paint, before any input arrives.
    Animated b{1.0f};
    CHECK(b.value() == 1.0f);
    CHECK(b.target() == 1.0f);
    CHECK_FALSE(b.running());
}

TEST_CASE("animated: animateTo does not move the value until the first tick") {
    Animated a(0.0f);
    a.animateTo(1.0f, kDurationNormal);

    // The window ticks and then paints within one frame, but the call that starts an animation
    // usually arrives from an input message, before that frame. If animateTo moved the value at
    // all, the frame that begins a fade would already be drawn part-way through it and the
    // motion would start with a visible step instead of from rest.
    CHECK(a.value() == 0.0f);
    CHECK(a.target() == 1.0f);
    CHECK(a.running());
}

TEST_CASE("animated: a frame stamped before the start reads as zero progress") {
    Animated a(0.25f);

    steady_clock::time_point const before = steady_clock::now();
    a.animateTo(1.0f, kDurationNormal);

    // A window rendering for the first time passes the timestamp it captured before ticking, so
    // a frame genuinely can predate the animation it is ticking. Without the guard the elapsed
    // duration would be negative and the value would be driven the wrong side of where it
    // started -- a fade that begins by going darker than its rest state.
    CHECK(a.tick(before));
    CHECK(a.value() == 0.25f);
    CHECK(a.running());
}

TEST_CASE("animated: ticking past the end settles exactly on the target") {
    Animated a(0.0f);

    a.animateTo(1.0f, kDurationNormal);
    steady_clock::time_point const after = steady_clock::now();

    // Exact equality, not Approx: the final frame assigns the target rather than computing it
    // through the lerp, and Approx would hide the bug this guards. The render loop stops asking
    // for frames the moment tick reports false, so whatever this leaves behind is what the user
    // looks at until something else wakes the window -- a highlight stuck at 97% opacity, or a
    // gauge stopping a hair short of the real battery level. Note that this frame changed the
    // value while reporting false: no more frames must never mean nothing happened.
    CHECK_FALSE(a.tick(after + kDurationNormal));
    CHECK(a.value() == 1.0f);
    CHECK(a.target() == 1.0f);
    CHECK_FALSE(a.running());
}

TEST_CASE("animated: a settled animation stays settled and keeps reporting false") {
    Animated a(0.0f);

    a.animateTo(1.0f, kDurationNormal);
    steady_clock::time_point const after = steady_clock::now();
    CHECK_FALSE(a.tick(after + kDurationNormal));

    // Widget trees are ticked on every paint, including paints caused by something else
    // entirely -- a theme change, a resize, another widget animating. A settled animation that
    // recomputed itself on those ticks would be at the mercy of the progress arithmetic for
    // ever, and one that reported true would keep the whole window repainting.
    CHECK_FALSE(a.tick(after + kDurationNormal * 10));
    CHECK(a.value() == 1.0f);
    CHECK_FALSE(a.tick(after + kDurationNormal * 10));
    CHECK(a.value() == 1.0f);
    CHECK_FALSE(a.running());
}

TEST_CASE("animated: running() and tick() agree at every boundary") {
    // These two are the whole contract with the render loop, which keeps rendering while any
    // animation reports it is still running. If the flag and the return value could disagree, a
    // widget would either pin the window at full frame rate with nothing moving -- constant GPU
    // and battery drain on a laptop -- or stop being drawn part-way through its motion.
    Animated a(0.0f);
    CHECK_FALSE(a.running());
    CHECK_FALSE(a.tick(steady_clock::now()));

    steady_clock::time_point const before = steady_clock::now();
    a.animateTo(1.0f, kLongDuration);
    steady_clock::time_point const after = steady_clock::now();
    CHECK(a.running());

    bool const midFrame = a.tick(before + milliseconds{5000});
    CHECK(midFrame);
    CHECK(a.running() == midFrame);

    bool const lastFrame = a.tick(after + kLongDuration);
    CHECK_FALSE(lastFrame);
    CHECK(a.running() == lastFrame);
}

TEST_CASE("animated: a zero or negative duration lands immediately without animating") {
    // tick divides by the duration to get progress, so without this guard a zero duration is
    // 0/0: a NaN progress fails the `>= 1.0` test, reaches the lerp and leaves a NaN in the
    // value -- and a NaN alpha or coordinate handed to Direct2D takes the drawing with it, not
    // just the animation.
    SUBCASE("zero") {
        Animated a(0.0f);
        a.animateTo(1.0f, milliseconds::zero());
        CHECK(a.value() == 1.0f);
        CHECK(a.target() == 1.0f);
        CHECK_FALSE(a.running());
        CHECK_FALSE(a.tick(steady_clock::now()));
        CHECK(a.value() == 1.0f);
    }

    // The comparison is `<=`, so a negative duration takes the same branch rather than running
    // backwards through the curve.
    SUBCASE("negative") {
        Animated b(0.5f);
        b.animateTo(0.25f, milliseconds{-50});
        CHECK(b.value() == 0.25f);
        CHECK(b.target() == 0.25f);
        CHECK_FALSE(b.running());
    }
}

TEST_CASE("animated: snapTo jumps and abandons the animation in flight") {
    Animated a(0.0f);

    steady_clock::time_point const before = steady_clock::now();
    a.animateTo(1.0f, kLongDuration);
    steady_clock::time_point const after = steady_clock::now();
    CHECK(a.tick(before + milliseconds{5000}));
    CHECK(a.running());

    // snapTo is what the app calls when a value changes while nobody can see it: the toast
    // resets its reveal before showing, a new controller card seeds its gauge at the real
    // battery level. If it left the animation running, the card would open its first frame
    // mid-sweep and then carry on from the wrong place.
    a.snapTo(0.25f);
    CHECK(a.value() == 0.25f);
    CHECK(a.target() == 0.25f);
    CHECK_FALSE(a.running());

    // snapTo leaves the old start time and duration behind, so the early return on a stopped
    // animation is the only thing keeping a later frame from resurrecting them.
    CHECK_FALSE(a.tick(after + kLongDuration));
    CHECK(a.value() == 0.25f);
}

TEST_CASE("animated: retargeting continues from the current value instead of jumping") {
    // The everyday case is a pointer that crosses a button and leaves again before the 83ms
    // hover fade has finished. If the fade out started from the old target rather than from the
    // live value, the highlight would snap to fully lit at the instant the pointer left and only
    // then fade -- a flash exactly where the user expects things to calm down. The same applies
    // to a battery gauge re-targeted by a poll that lands during the previous sweep.
    SUBCASE("interrupted mid-flight") {
        Animated a(0.0f);

        steady_clock::time_point const before = steady_clock::now();
        a.animateTo(1.0f, kLongDuration, Easing::Entrance);
        CHECK(a.tick(before + milliseconds{5000}));

        // Progress here is essentially half, where the entrance curve has already covered 89%
        // of the travel; it would take a multi-second stall between two adjacent statements to
        // bring this below half.
        float const midway = a.value();
        CHECK(midway > 0.5f);
        CHECK(midway < 1.0f);

        steady_clock::time_point const restart = steady_clock::now();
        a.animateTo(0.0f, kDurationNormal, Easing::Exit);
        steady_clock::time_point const restarted = steady_clock::now();
        CHECK(a.value() == midway);
        CHECK(a.target() == 0.0f);
        CHECK(a.running());

        // The exact equality is the real proof: a zero-progress frame reproduces the value the
        // retarget started from, which can only be the live value if animateTo captured it
        // rather than leaving the old start value in place.
        CHECK(a.tick(restart));
        CHECK(a.value() == midway);

        CHECK_FALSE(a.tick(restarted + kDurationNormal));
        CHECK(a.value() == 0.0f);
    }

    // The same capture on the path taken when nothing is running, which reaches those
    // assignments past a different early return.
    SUBCASE("restarted after it settled") {
        Animated a(0.0f);
        a.animateTo(1.0f, kDurationNormal);
        steady_clock::time_point const settled = steady_clock::now();
        CHECK_FALSE(a.tick(settled + kDurationNormal));
        CHECK(a.value() == 1.0f);

        steady_clock::time_point const restart = steady_clock::now();
        a.animateTo(0.25f, kDurationNormal);
        CHECK(a.tick(restart));
        CHECK(a.value() == 1.0f);
        CHECK(a.target() == 0.25f);
        CHECK(a.running());
    }
}

TEST_CASE("animated: retargeting to the value it already rests at starts nothing") {
    Animated a(0.5f);
    REQUIRE_FALSE(a.running());

    // The widget is at rest on this value already, so there is nothing to animate. Starting one
    // anyway costs a whole duration of identical frames, each asking the window to draw the
    // next -- and a tray application that wakes for no reason is the failure this window's
    // draw-on-demand loop exists to avoid.
    a.animateTo(0.5f, kDurationNormal);
    CHECK_FALSE(a.running());

    // The value is untouched either way; running() is what says whether the loop was woken.
    CHECK(a.value() == doctest::Approx(0.5f));
}

TEST_CASE("animated: retargeting to the destination it is already heading for is ignored") {
    Animated a(0.0f);

    a.animateTo(1.0f, kDurationNormal);
    steady_clock::time_point const after = steady_clock::now();

    // Same target, sixty times the duration and a different curve, while the first animation is
    // still in flight. Requests like this arrive repeatedly from event streams -- an expander
    // re-driven by a layout pass, a scrollbar re-shown while it is already appearing, a gauge
    // re-targeted by a poll. If each restarted the clock, the animation would have its remaining
    // time reset for as long as events keep coming and would visibly never arrive, and the
    // window would never be allowed to idle.
    a.animateTo(1.0f, kLongDuration, Easing::Exit);

    // The long second duration is what makes this a discriminator rather than a tautology: had
    // the call been honoured, this same frame would still be running and short of the target.
    CHECK_FALSE(a.tick(after + kDurationNormal));
    CHECK(a.value() == 1.0f);
    CHECK_FALSE(a.running());
}

TEST_CASE("animated: the value only depends on the frame time, not on the tick count") {
    Animated a(0.0f);

    steady_clock::time_point const before = steady_clock::now();
    a.animateTo(1.0f, kLongDuration);

    CHECK(a.tick(before + milliseconds{2000}));
    float const early = a.value();
    CHECK(a.tick(before + milliseconds{2000}));
    CHECK(a.value() == early);

    CHECK(a.tick(before + milliseconds{6000}));
    CHECK(a.value() > early);

    // Back to the earlier timestamp. An implementation that integrated per-frame deltas would
    // carry the later frame's progress with it, and would also make every animation's speed
    // depend on the refresh rate of the panel it happens to be drawn on and drift permanently
    // after a stall. Recomputing from the start instant is what makes the duration constants
    // mean wall-clock milliseconds.
    CHECK(a.tick(before + milliseconds{2000}));
    CHECK(a.value() == early);
}

TEST_CASE("animated: the value never leaves the interval between its endpoints") {
    Animated a(0.0f);

    steady_clock::time_point const before = steady_clock::now();
    a.animateTo(1.0f, milliseconds{8000});

    // Frame times arrive at irregular intervals and these values feed alphas and DIP offsets
    // straight into Direct2D, where a sample outside the endpoints or one that steps backwards
    // shows as a flicker or as a widget drawn past where it is meant to stop -- and only ever
    // mid-motion, which is where it is hardest to catch by eye.
    float previous = 0.0f;
    for (int k = 1; k <= 7; ++k) {
        CAPTURE(k);
        CHECK(a.tick(before + milliseconds{k * 1000}));
        float const sampled = a.value();
        CHECK(sampled >= 0.0f);
        CHECK(sampled <= 1.0f);
        CHECK(sampled >= previous);
        previous = sampled;
    }
}

TEST_CASE("animated: a target outside the unit interval is honoured") {
    // Not every Animated is an opacity. The nav indicator animates a position in DIPs across the
    // whole strip, and a stretch factor that is greater than one by construction. Anything that
    // clamped the lerp to the unit interval would stop the indicator travelling past the first
    // item and would collapse the pill instead of elongating it.
    Animated a(1.0f);

    steady_clock::time_point const before = steady_clock::now();
    a.animateTo(2.5f, milliseconds{8000});
    steady_clock::time_point const after = steady_clock::now();

    CHECK(a.tick(before + milliseconds{4000}));
    CHECK(a.value() > 1.0f);
    CHECK(a.value() <= 2.5f);

    CHECK_FALSE(a.tick(after + milliseconds{8000}));
    CHECK(a.value() == 2.5f);
    CHECK_FALSE(a.running());
}

TEST_CASE("animated: a downward animation settles exactly on the lower target") {
    // This is the toast's dismissal, which animates its reveal to zero on the exit curve and
    // feeds the tick result straight into its phase machine. A value that stopped at 0.0001
    // would leave a ghost of the notification painted over the desktop, and because tick had
    // already reported it was finished the window would never be asked to redraw and clear it.
    Animated a(1.0f);

    steady_clock::time_point const before = steady_clock::now();
    a.animateTo(0.0f, kDurationNormal, Easing::Exit);
    steady_clock::time_point const after = steady_clock::now();

    CHECK(a.tick(before));
    CHECK(a.value() == 1.0f);

    CHECK(a.tick(before + kDurationNormal / 2));
    CHECK(a.value() >= 0.0f);
    CHECK(a.value() <= 1.0f);

    // The lerp runs with a negative span here, but the settling frame assigns the target
    // directly, so landing on exactly zero does not depend on that arithmetic.
    CHECK_FALSE(a.tick(after + kDurationNormal));
    CHECK(a.value() == 0.0f);
    CHECK_FALSE(a.running());
}
