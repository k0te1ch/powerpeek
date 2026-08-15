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
// The cases below are the app's three sliders and nothing invented: volume from 0 to 1 in
// twentieths, window opacity from 0.7 to 1 in the same, and the two thresholds from 10 to 50 in
// fives. What they assert is that every stop is the float nearest the decimal it is drawn as --
// not that the arithmetic was done one particular way.

#include "TestSupport.h"

#include "core/Json.h"
#include "core/Settings.h"
#include "ui/Quantise.h"

#include <string>

namespace {

using peek::ui::snapToStep;

// The stop a slider is meant to land on, as the decimal a label would print, narrowed once.
// Deliberately not the expression snapToStep uses: this is where the value should be, worked
// out from the description of the slider rather than from the code under test.
float stopAt(double minimum, double step, int index) {
    return static_cast<float>(minimum + step * index);
}

// What the settings file would hold for a value. The two units meet here on purpose -- the
// grid exists to keep this readable, and neither half demonstrates that on its own.
std::string written(float value) {
    return peek::json::dump(peek::json::Value(value), 0);
}

}  // namespace

TEST_CASE("quantise: every stop on the volume slider is the float that spells it") {
    // Three of these twenty-one used to miss. The volume slider is where it showed, because it
    // is the one a person moves often and the one with a step -- 0.05 -- that no float holds:
    // nine of them summed in float is the float above the one that says 0.45.
    for (int i = 0; i <= 20; ++i) {
        CAPTURE(i);
        float const expected = stopAt(0.0, 0.05, i);

        // Landing exactly on a stop, and arriving from either side of it, all give the stop.
        CHECK(snapToStep(0.05 * i, 0.0, 1.0, 0.05) == expected);
        CHECK(snapToStep(0.05 * i + 0.02, 0.0, 1.0, 0.05) == expected);
        CHECK(snapToStep(0.05 * i - 0.02, 0.0, 1.0, 0.05) == expected);
    }
}

TEST_CASE("quantise: a stop is written to the settings file the way it is read off the slider") {
    // The point of the whole exercise, stated where both halves of it are visible. json::Value
    // prints the shortest decimal that names the float it was given, so a stop that is the right
    // float is a short line in the file and a stop that is one float out is a nine-digit one.
    CHECK(written(snapToStep(0.45, 0.0, 1.0, 0.05)) == "0.45");
    CHECK(written(snapToStep(0.65, 0.0, 1.0, 0.05)) == "0.65");
    CHECK(written(snapToStep(0.9, 0.0, 1.0, 0.05)) == "0.9");
    CHECK(written(snapToStep(0.8, 0.0, 1.0, 0.05)) == "0.8");

    // And the opacity slider, whose grid starts at 0.7 rather than at zero. Counted from the
    // float nearest 0.7 instead of from the decimal, its fourth stop reads 0.84999996.
    double const floor = peek::kMinimumWindowOpacity;
    CHECK(written(snapToStep(0.85, floor, 1.0, 0.05)) == "0.85");
    CHECK(written(snapToStep(0.75, floor, 1.0, 0.05)) == "0.75");
    CHECK(written(snapToStep(0.7, floor, 1.0, 0.05)) == "0.7");
}

TEST_CASE("quantise: a grid that does not start at zero counts from where it does start") {
    // The opacity slider runs 0.7 to 1. Its stops are 0.7, 0.75, 0.8 and so on -- which happen
    // to be multiples of the step as well, so the case that tells the two rules apart is the
    // threshold slider: from 10 in fives, the stops are 10, 15, 20, and a grid counted from zero
    // would agree. From 12 in fives they are 12, 17, 22, and only one of the two rules says so.
    double const floor = peek::kMinimumWindowOpacity;
    for (int i = 0; i <= 6; ++i) {
        CAPTURE(i);
        CHECK(snapToStep(floor + 0.05 * i, floor, 1.0, 0.05) == stopAt(floor, 0.05, i));
    }

    for (int i = 0; i <= 8; ++i) {
        CAPTURE(i);
        CHECK(snapToStep(10.0 + 5.0 * i, 10.0, 50.0, 5.0) == stopAt(10.0, 5.0, i));
    }

    CHECK(snapToStep(14.0, 12.0, 50.0, 5.0) == 12.0f);
    CHECK(snapToStep(15.0, 12.0, 50.0, 5.0) == 17.0f);
}

TEST_CASE("quantise: stepping along the grid does not walk off it") {
    // What the arrow keys do. They used to add the step to whatever the value already was, and
    // since neither the value nor the step is exactly the decimal it stands for, every press
    // added a little error to the last one -- twenty presses of a key wrote twenty different
    // shapes of number into the file. Going back through the grid each time is what fixes it,
    // and the test walks the whole slider twice to say so.
    float value = 0.0f;
    for (int i = 1; i <= 20; ++i) {
        value = snapToStep(static_cast<double>(value) + 0.05, 0.0, 1.0, 0.05);
        CHECK(value == stopAt(0.0, 0.05, i));
    }
    for (int i = 19; i >= 0; --i) {
        value = snapToStep(static_cast<double>(value) - 0.05, 0.0, 1.0, 0.05);
        CHECK(value == stopAt(0.0, 0.05, i));
    }
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
}

TEST_CASE("quantise: a slider with no step keeps the value it is given") {
    // Not every slider has a grid, and one without a step has to stay continuous rather than
    // collapsing onto its minimum -- which is what a step of zero would do to the division.
    CHECK(snapToStep(0.371, 0.0, 1.0, 0.0) == 0.371f);
    CHECK(snapToStep(0.371, 0.0, 1.0, -1.0) == 0.371f);
    CHECK(snapToStep(2.5, 0.0, 1.0, 0.0) == 1.0f);
}
