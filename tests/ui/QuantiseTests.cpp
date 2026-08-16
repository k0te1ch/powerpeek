// The grid a slider snaps to, and the reason it is arithmetic in a library rather than three
// lines inside a widget.
//
// Every number in the settings file that a person chose came through here. The values are
// decimals a label spells out -- "45 %", "85 %", "20 %" -- and the settings file is expected to
// read the same way, because the whole reason its writer emits the shortest form of a number is
// that a person may open it. A stop that lands on the float next door to the one that spells
// 0.45 costs exactly that: the file records 0.45000002, which is true about the value and
// useless about the intent.
//
// The grids below are the app's sliders and nothing invented: volume from 0 to 1 in twentieths,
// window opacity from 0.70 to 1 in the same, and the thresholds in fives -- low from 10 to 50,
// critical from 5 to 45.
//
// What the cases assert is that every stop is the float nearest the decimal it is drawn as, and
// they assert it against decimals written out by hand. An expected value computed from the step
// is the arithmetic of the code under test spelled a second time, and would follow that
// arithmetic wherever it went wrong; the text below is what the file has to say, and there is
// only one float per line of it.

#include "TestSupport.h"

#include "core/Json.h"
#include "core/Settings.h"
#include "ui/Quantise.h"

#include <string>

namespace {

using peek::ui::snapToStep;

// What the settings file would hold for a value. The two units meet here on purpose -- the
// grid exists to keep this readable, and neither half demonstrates that on its own. json::Value
// prints the shortest decimal that names the float it was given, so a stop that is the right
// float is a short line in the file and a stop one float out is a nine-digit one.
std::string written(float value) {
    return peek::json::dump(peek::json::Value(value), 0);
}

// The twenty-one stops of a slider running from 0 to 1 in twentieths, as the label draws them.
// Both volume sliders -- the master and the one on every notification event -- are this grid.
constexpr char const* kVolumeStops[] = {"0",    "0.05", "0.1",  "0.15", "0.2",  "0.25", "0.3",
                                        "0.35", "0.4",  "0.45", "0.5",  "0.55", "0.6",  "0.65",
                                        "0.7",  "0.75", "0.8",  "0.85", "0.9",  "0.95", "1"};

// The window opacity slider, the same twentieths counted from kMinimumWindowOpacity.
constexpr char const* kOpacityStops[] = {"0.7", "0.75", "0.8", "0.85", "0.9", "0.95", "1"};

}  // namespace

TEST_CASE("quantise: every stop on the volume slider is the float that spells it") {
    // Three of these twenty-one used to miss. The volume slider is where it showed, because it
    // is the one a person moves often and the one with a step -- 0.05 -- that no float holds:
    // nine of them summed in float is the float above the one that says 0.45.
    for (int i = 0; i <= 20; ++i) {
        CAPTURE(i);

        // Landing exactly on a stop, and arriving from either side of it, all give the stop.
        CHECK(written(snapToStep(0.05 * i, 0.0, 1.0, 0.05)) == kVolumeStops[i]);
        CHECK(written(snapToStep(0.05 * i + 0.02, 0.0, 1.0, 0.05)) == kVolumeStops[i]);
        CHECK(written(snapToStep(0.05 * i - 0.02, 0.0, 1.0, 0.05)) == kVolumeStops[i]);
    }
}

TEST_CASE("quantise: a grid that does not start at zero counts from where it does start") {
    // The opacity slider starts at 0.70, which is not a float either. Counted from the float
    // nearest 0.7 rather than in double from the decimal, its fourth stop reads 0.84999996 in
    // the file while the slider says 85 %.
    double const floor = peek::kMinimumWindowOpacity;
    for (int i = 0; i <= 6; ++i) {
        CAPTURE(i);
        CHECK(written(snapToStep(floor + 0.05 * i, floor, 1.0, 0.05)) == kOpacityStops[i]);
        CHECK(written(snapToStep(floor + 0.05 * i + 0.02, floor, 1.0, 0.05)) == kOpacityStops[i]);
        CHECK(written(snapToStep(floor + 0.05 * i - 0.02, floor, 1.0, 0.05)) == kOpacityStops[i]);
    }

    // Every stop of that grid is also a multiple of its step, so it cannot tell counting from
    // the minimum apart from counting from zero. No slider in the app is off its own step
    // today, and a page sets its minimum and its step independently -- so the rule is pinned on
    // a grid that does differ: from 12 in fives, counting from zero puts the stops at 15, 20,
    // 25 and the slider can never sit on the value it was built with, not even by being dragged
    // to the left end of its own track.
    float const expected[] = {12.0f, 17.0f, 22.0f, 27.0f, 32.0f, 37.0f, 42.0f};
    for (int i = 0; i <= 6; ++i) {
        CAPTURE(i);
        double const stop = 12.0 + 5.0 * i;
        CHECK(snapToStep(stop, 12.0, 50.0, 5.0) == expected[i]);
        CHECK(snapToStep(stop + 2.0, 12.0, 50.0, 5.0) == expected[i]);
        CHECK(snapToStep(stop - 2.0, 12.0, 50.0, 5.0) == expected[i]);
    }
}

TEST_CASE("quantise: stepping along the grid does not walk off it") {
    // What the arrow keys do. They used to add the step to whatever the value already was, and
    // since neither the value nor the step is exactly the decimal it stands for, every press
    // added a little error to the last one -- twenty presses of a key wrote twenty different
    // shapes of number into the file. Going back through the grid on every press is what fixes
    // it, and the walk starts between two stops so the first press has to correct as well as
    // advance.
    float value = 0.02f;
    for (int press = 1; press <= 20; ++press) {
        CAPTURE(press);
        value = snapToStep(static_cast<double>(value) + 0.05, 0.0, 1.0, 0.05);
        CHECK(written(value) == kVolumeStops[press]);
    }
}

TEST_CASE("quantise: a value that arrived off the grid is pulled back onto it") {
    // Nothing snaps a value on the way in -- the slider's constructor and setValue both only
    // clamp -- so a settings file edited by hand, or one written by a build from before the
    // grid existed, seats the thumb between two stops. If a press only added the step to that,
    // the slider would run its own parallel grid for the life of the install: from 0.83 the
    // file would record 0.88, then 0.93, then 0.98, and none of them is a stop the slider can
    // be dragged to.
    float volume = 0.83f;
    volume = snapToStep(static_cast<double>(volume) + 0.05, 0.0, 1.0, 0.05);
    CHECK(written(volume) == "0.9");
    volume = snapToStep(static_cast<double>(volume) + 0.05, 0.0, 1.0, 0.05);
    CHECK(written(volume) == "0.95");
    volume = snapToStep(static_cast<double>(volume) + 0.05, 0.0, 1.0, 0.05);
    CHECK(written(volume) == "1");

    // The opacity grid recovers the same value from its own floor rather than from zero.
    float const opacity = 0.83f;
    CHECK(written(snapToStep(static_cast<double>(opacity) + 0.05, peek::kMinimumWindowOpacity,
                             1.0, 0.05)) == "0.9");

    // And the one that is not hand-edited at all: 0.45000002 is what a build from before the
    // grid wrote for a slider reading 45 %, and it is in the settings file of every install
    // that ever ran one. The first press has to leave the file readable again.
    float const legacy = 0.45000002f;
    CHECK(written(snapToStep(static_cast<double>(legacy) + 0.05, 0.0, 1.0, 0.05)) == "0.5");
}

TEST_CASE("quantise: the value never leaves the range whatever is asked for") {
    // The pointer can be dragged past either end of the track, and the keyboard can step past
    // them; a value outside the range would fail the settings clamp on the way to the file and
    // silently become something else again.
    CHECK(snapToStep(-5.0, 0.0, 1.0, 0.05) == 0.0f);
    CHECK(snapToStep(5.0, 0.0, 1.0, 0.05) == 1.0f);
    CHECK(snapToStep(0.0, peek::kMinimumWindowOpacity, 1.0, 0.05) ==
          static_cast<float>(peek::kMinimumWindowOpacity));

    // Including when the last stop does not fall on the far end: from 10 in sevens, the stop
    // above 45 is 52, and the slider stops at 50.
    CHECK(snapToStep(49.0, 10.0, 50.0, 7.0) == 50.0f);

    // A range with nothing in it cannot produce a value outside itself either.
    CHECK(snapToStep(3.0, 1.0, 1.0, 0.05) == 1.0f);
    CHECK(snapToStep(-3.0, 1.0, 1.0, 0.05) == 1.0f);

    // Nor can a range handed over back to front. The result is the minimum, but the reason to
    // pin it is the step before that one: without the guard that orders the two ends, clamp is
    // called with its bounds crossed, and that is undefined behaviour rather than a wrong
    // number -- a defect no later CHECK would report.
    CHECK(snapToStep(0.5, 1.0, 0.0, 0.05) == 1.0f);
    CHECK(snapToStep(-0.5, 1.0, 0.0, 0.0) == 1.0f);
}

TEST_CASE("quantise: a slider with no step keeps the value it is given") {
    // Not every slider has a grid, and one without a step has to stay continuous rather than
    // collapsing onto its minimum -- which is what a step of zero would do to the division.
    CHECK(snapToStep(0.371, 0.0, 1.0, 0.0) == 0.371f);
    CHECK(snapToStep(0.371, 0.0, 1.0, -1.0) == 0.371f);
    CHECK(snapToStep(2.5, 0.0, 1.0, 0.0) == 1.0f);
}
