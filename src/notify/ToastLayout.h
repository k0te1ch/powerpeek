#pragma once

#include "core/Settings.h"
#include "core/Win.h"

namespace peek::notify {

// Where one notification card goes, and which way it arrives.
struct ToastPlacement {
    int x = 0;
    int y = 0;
    // +1 when the card rises into place from below, -1 when it comes down from above. A
    // card always enters from the edge it is anchored to; sliding the other way reads as
    // the card falling out of the screen rather than onto it.
    float slideDirection = 1.0f;
};

// Places one card inside `workArea`.
//
// Free of Win32 on purpose. This is the arithmetic where a sign costs a card half off the
// screen or a stack that grows into the edge instead of away from it, and it is far cheaper
// to catch that here than on a screen with a controller connected.
//
// `windowPx` is the whole window; `marginPx` is the transparent shadow field it carries on
// each side, so the visible card is inset from the window by that much. `gapPx` is what the
// *visible* card keeps between itself and the work area. `offsetPx` is how far this card
// sits from the anchored edge -- the running total of the heights of the cards already up,
// which the stack accumulates and which knows nothing about direction.
ToastPlacement placeToast(RECT workArea, SIZE windowPx, int marginPx, int gapPx, int offsetPx,
                          ToastPosition position);

}  // namespace peek::notify
