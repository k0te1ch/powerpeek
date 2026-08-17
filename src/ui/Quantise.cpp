#include "ui/Quantise.h"

#include <algorithm>
#include <cmath>

namespace peek::ui {

float snapToStep(double value, double minimum, double maximum, double step) {
    double const low = minimum;
    double const high = (std::max)(low, maximum);

    if (step > 0.0) {
        // Counted from the minimum rather than from zero, because the grid a user sees starts
        // at the left end of the track: the opacity slider runs from 0.7 in steps of 0.05, and
        // its stops are 0.7, 0.75, 0.8 -- not the multiples of 0.05 nearest to them.
        value = low + std::round((value - low) / step) * step;
    }
    return static_cast<float>(std::clamp(value, low, high));
}

}  // namespace peek::ui
