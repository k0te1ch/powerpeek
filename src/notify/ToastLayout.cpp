#include "notify/ToastLayout.h"

namespace peek::notify {
namespace {

bool anchoredToTop(ToastPosition position) {
    return position == ToastPosition::TopLeft || position == ToastPosition::TopCenter ||
           position == ToastPosition::TopRight;
}

}  // namespace

ToastPlacement placeToast(RECT workArea, SIZE windowPx, int marginPx, int gapPx, int offsetPx,
                          ToastPosition position) {
    ToastPlacement placement;

    switch (position) {
        case ToastPosition::TopLeft:
        case ToastPosition::BottomLeft:
            // The gap is measured from the visible card, and the window begins a shadow
            // margin outside it -- so the margin comes off on this edge exactly as it goes
            // on at the opposite one.
            placement.x = workArea.left + gapPx - marginPx;
            break;

        case ToastPosition::TopCenter:
        case ToastPosition::BottomCenter:
            // The shadow field is symmetric, so it cancels: centring the window centres the
            // card. Nothing is measured from an edge here, and the gap plays no part at all.
            // Written as an offset from `left` rather than as the midpoint of the two edges,
            // because a monitor to the left of the primary one has negative coordinates and
            // integer division truncates towards zero rather than downwards.
            placement.x = workArea.left + (workArea.right - workArea.left - windowPx.cx) / 2;
            break;

        case ToastPosition::TopRight:
        case ToastPosition::BottomRight:
            placement.x = workArea.right - gapPx - windowPx.cx + marginPx;
            break;
    }

    if (anchoredToTop(position)) {
        placement.y = workArea.top + gapPx - marginPx + offsetPx;
        placement.slideDirection = -1.0f;
    } else {
        placement.y = workArea.bottom - gapPx - windowPx.cy + marginPx - offsetPx;
        placement.slideDirection = 1.0f;
    }
    return placement;
}

}  // namespace peek::notify
