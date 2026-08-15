#pragma once

namespace peek::ui {

// Snaps a slider value onto its step grid and into its range.
//
// The arithmetic happens in double and only the answer is narrowed, and that is the point of
// the function rather than a detail of it. A step of 0.05 is not a float: multiply the float
// nearest to it by nine and the result is the float next door to the one that spells 0.45.
// The slider then reads "45 %" while the settings file records 0.45000002, and both are
// telling the truth about different numbers. Staying in double until the last step makes the
// value the float that names what the label says.
//
// It also puts every route to a value through one rule. Dragging used to snap and the arrow
// keys did not, so holding an arrow down walked the value off the grid a rounding error at a
// time and wrote the result to the file.
//
// A `step` of zero or less means a continuous slider, and the value is only clamped.
float snapToStep(double value, double minimum, double maximum, double step);

}  // namespace peek::ui
