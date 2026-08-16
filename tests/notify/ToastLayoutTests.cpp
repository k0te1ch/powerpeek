// Where every notification card lands on screen -- the arithmetic ToastWindow hands its work
// area to before it moves a window.
//
// The window is not the card. It carries a transparent shadow field on all four sides, so what
// the user sees is the window inset by that much, and the placement has to take the margin off
// on the edge it anchors to and put it back on at the opposite one. That leaves almost every
// term here as a sign, and these signs are not subtle when they go wrong: the wrong one on the
// horizontal rule hangs a card half off the side of the screen, and the wrong one on the stack
// offset grows the second and third cards into the edge of the work area instead of away from
// it, piling them on top of one another. slideDirection is the same kind of term -- it is the
// difference between a card rising onto the screen and one that reads as falling off it.
//
// None of that is reachable from a test on ToastWindow itself: that wants a device, a message
// loop and a monitor to put the window on, and the build agent has none of the three. It is
// reachable here only because placeToast takes the work area as an argument and touches no Win32
// at all, which is the whole reason the arithmetic was lifted out of the window.
//
// Every assertion below is written against the visible card, rebuilt from the returned
// coordinates by insetting the window, because the visible card is what the function promises.
// Assertions on the raw window coordinates would restate the implementation, margin signs and
// all, and would pass just as happily with the margin applied to the wrong edge.

#include "TestSupport.h"

#include "core/Settings.h"
#include "notify/ToastLayout.h"

namespace {

using peek::ToastPosition;
using peek::notify::ToastPlacement;
using peek::notify::placeToast;

// A primary monitor at the origin with a taskbar along the bottom, and the window the
// application really asks for: a 340 by 96 card wrapped in a 24-pixel shadow field, kept 12
// pixels clear of the work area.
constexpr RECT kWorkArea{0, 0, 1920, 1040};
constexpr SIZE kWindow{388, 144};
constexpr int kMargin = 24;
constexpr int kGap = 12;

// What the stack would hand the second card: one card's height plus the gap between cards. It
// is a distance and nothing more -- the stack has no idea which way the cards grow.
constexpr int kOffset = 104;

// Two windows the application asks for just as readily as the one above, and neither of which
// holds a 340 by 96 card: a notification whose body wraps to a second line is taller, and a
// controller with a long name is wider. Both keep the shipped shadow field, so what differs
// between them and kWindow is the card the user sees.
constexpr SIZE kTallWindow{388, 240};
constexpr SIZE kWideWindow{520, 144};

struct NamedPosition {
    ToastPosition position;
    char const* name;
    // A property of the enumerator rather than of the code under test: these are the three
    // settings a user chooses when they want cards at the top of the screen.
    bool anchoredToTop;
};

constexpr NamedPosition kAllPositions[] = {
    {ToastPosition::TopLeft, "TopLeft", true},
    {ToastPosition::TopCenter, "TopCenter", true},
    {ToastPosition::TopRight, "TopRight", true},
    {ToastPosition::BottomLeft, "BottomLeft", false},
    {ToastPosition::BottomCenter, "BottomCenter", false},
    {ToastPosition::BottomRight, "BottomRight", false},
};

// The rectangle the user sees: the window less the shadow field it carries on every side.
struct VisibleCard {
    long left;
    long top;
    long right;
    long bottom;
};

VisibleCard visibleCard(ToastPlacement const& placement, SIZE windowPx, int marginPx) {
    return VisibleCard{placement.x + marginPx, placement.y + marginPx,
                       placement.x + windowPx.cx - marginPx,
                       placement.y + windowPx.cy - marginPx};
}

// Places one card of any size and hands back the rectangle that would end up in front of the
// user.
VisibleCard placedCardOfSize(RECT workArea, SIZE windowPx, int marginPx, int gapPx, int offsetPx,
                             ToastPosition position) {
    return visibleCard(placeToast(workArea, windowPx, marginPx, gapPx, offsetPx, position),
                       windowPx, marginPx);
}

// The same, with the window and shadow field the application ships.
VisibleCard placedCard(RECT workArea, int gapPx, int offsetPx, ToastPosition position) {
    return placedCardOfSize(workArea, kWindow, kMargin, gapPx, offsetPx, position);
}

}  // namespace

TEST_CASE("placeToast: every position keeps the visible card a gap from the edges it anchors to") {
    // The one rule the whole feature exists for, and the one the shadow field makes easy to get
    // wrong: the gap is measured from the card, not from the window, so an implementation that
    // forgot the margin would leave every card sitting 24 pixels further into the screen than
    // asked on one edge and 24 pixels off the screen on the other. The corner cases pin both
    // axes at once, which is what proves the two rules are applied to the right coordinate.
    SUBCASE("TopLeft") {
        VisibleCard const card = placedCard(kWorkArea, kGap, 0, ToastPosition::TopLeft);
        CHECK(card.left == kWorkArea.left + kGap);
        CHECK(card.top == kWorkArea.top + kGap);
    }

    SUBCASE("TopRight") {
        VisibleCard const card = placedCard(kWorkArea, kGap, 0, ToastPosition::TopRight);
        CHECK(card.right == kWorkArea.right - kGap);
        CHECK(card.top == kWorkArea.top + kGap);
    }

    SUBCASE("BottomLeft") {
        VisibleCard const card = placedCard(kWorkArea, kGap, 0, ToastPosition::BottomLeft);
        CHECK(card.left == kWorkArea.left + kGap);
        CHECK(card.bottom == kWorkArea.bottom - kGap);
    }

    SUBCASE("BottomRight") {
        VisibleCard const card = placedCard(kWorkArea, kGap, 0, ToastPosition::BottomRight);
        CHECK(card.right == kWorkArea.right - kGap);
        CHECK(card.bottom == kWorkArea.bottom - kGap);
    }

    // The centred pair is anchored on one axis only; where the other axis puts them is the
    // subject of the centring case below.
    SUBCASE("TopCenter") {
        VisibleCard const card = placedCard(kWorkArea, kGap, 0, ToastPosition::TopCenter);
        CHECK(card.top == kWorkArea.top + kGap);
    }

    SUBCASE("BottomCenter") {
        VisibleCard const card = placedCard(kWorkArea, kGap, 0, ToastPosition::BottomCenter);
        CHECK(card.bottom == kWorkArea.bottom - kGap);
    }
}

TEST_CASE("placeToast: an offset past the far edge is passed straight through") {
    // What keeps a stack on screen is the three slots ToastStack has, not anything here, and
    // clamping an over-large offset back into the work area would be the worse behaviour by
    // some way: every card past the edge would then land on the same coordinates as the one
    // before it, so instead of a stack that visibly ran out of room the user would get cards
    // stacked one exactly on top of another with only the newest legible. The offset is a
    // distance the stack accumulated, and applying a sign to it is this function's whole job.
    // As far from the anchored edge as the work area is tall, so wherever the card ends up it
    // is not on the screen.
    constexpr int beyond = static_cast<int>(kWorkArea.bottom - kWorkArea.top);

    VisibleCard const fromBottom = placedCard(kWorkArea, kGap, beyond, ToastPosition::BottomRight);
    CHECK(fromBottom.bottom == kWorkArea.bottom - kGap - beyond);
    CHECK(fromBottom.bottom < kWorkArea.top);

    VisibleCard const fromTop = placedCard(kWorkArea, kGap, beyond, ToastPosition::TopRight);
    CHECK(fromTop.top == kWorkArea.top + kGap + beyond);
    CHECK(fromTop.top > kWorkArea.bottom);
}

TEST_CASE("placeToast: the centred positions sit on the work area's centre line") {
    // Centring is the one horizontal rule with no edge to measure from, so it is also the one
    // that keeps working when the margin handling is wrong -- the shadow field is symmetric and
    // cancels itself. That makes it worth pinning separately, and pinning it against the visible
    // card: an implementation that centred the card rather than the window would be off by a
    // shadow margin, which looks deliberate enough that nobody would report it.
    //
    // The two centres are compared as sums rather than by halving either of them, so the test
    // never has to invent a rounding rule of its own.
    SUBCASE("both centred positions") {
        VisibleCard const top = placedCard(kWorkArea, kGap, 0, ToastPosition::TopCenter);
        CHECK(top.left + top.right == kWorkArea.left + kWorkArea.right);

        VisibleCard const bottom = placedCard(kWorkArea, kGap, 0, ToastPosition::BottomCenter);
        CHECK(bottom.left + bottom.right == kWorkArea.left + kWorkArea.right);
    }

    // Nothing is measured from a side edge here, so the gap has no business in the answer. A
    // centred card that drifted with the gap would be a sign the centred labels had fallen into
    // the left or right arm of the switch, which is exactly the mistake a switch with three
    // grouped pairs invites.
    SUBCASE("the gap plays no part") {
        VisibleCard const tight = placedCard(kWorkArea, 0, 0, ToastPosition::BottomCenter);
        VisibleCard const generous = placedCard(kWorkArea, 200, 0, ToastPosition::BottomCenter);
        CHECK(tight.left == generous.left);
        CHECK(tight.right == generous.right);

        // The vertical rule is still measured from an edge, so this pair genuinely differs
        // somewhere -- otherwise the comparison above would prove nothing.
        CHECK(tight.bottom != generous.bottom);
    }

    // A work area of odd width cannot be centred exactly and one pixel has to fall to one side.
    // Which side is arbitrary; that it is the same side on every monitor is not. Integer
    // division truncates towards zero, so a centring written as the midpoint of the two edges
    // hands that pixel to the other side once the coordinates go negative, and a card that sits
    // a pixel differently depending on which monitor it opens on is the visible end of an
    // arithmetic that is reading absolute coordinates where it should be reading a width.
    SUBCASE("an odd remainder falls the same way wherever the monitor is") {
        constexpr RECT atOrigin{0, 0, 1921, 1040};
        constexpr RECT toTheLeft{-1921, 0, 0, 1040};

        VisibleCard const primary = placedCard(atOrigin, kGap, 0, ToastPosition::TopCenter);
        VisibleCard const secondary = placedCard(toTheLeft, kGap, 0, ToastPosition::TopCenter);

        CHECK(primary.left - atOrigin.left == secondary.left - toTheLeft.left);
        CHECK(atOrigin.right - primary.right == toTheLeft.right - secondary.right);

        // And the card really is centred, to the one pixel that cannot be divided. Which side
        // that pixel falls to is deliberately not pinned: rounding it the other way would be
        // just as consistent from monitor to monitor and no user could tell the two apart, so
        // a test that named a side would fail on a change that costs nothing.
        long const leftGap = primary.left - atOrigin.left;
        long const rightGap = atOrigin.right - primary.right;
        CHECK(leftGap - rightGap <= 1);
        CHECK(rightGap - leftGap <= 1);
    }
}

TEST_CASE("placeToast: a stack offset grows the stack away from the anchored edge") {
    // The stack accumulates the heights of the cards already on screen and hands the total over
    // as a plain distance -- it knows nothing about direction, by design. So the sign applied
    // here is the only thing deciding whether the second card sits clear of the first or on top
    // of it, and the wrong sign drives the stack straight into the edge of the work area, where
    // every card after the first is clipped or lands exactly where the previous one is.
    SUBCASE("a bottom-anchored card moves up") {
        for (auto const& entry : kAllPositions) {
            if (entry.anchoredToTop) {
                continue;
            }
            CAPTURE(entry.name);
            VisibleCard const first = placedCard(kWorkArea, kGap, 0, entry.position);
            VisibleCard const second = placedCard(kWorkArea, kGap, kOffset, entry.position);

            CHECK(second.bottom == first.bottom - kOffset);
            CHECK(second.top == first.top - kOffset);
            CHECK(second.bottom == kWorkArea.bottom - kGap - kOffset);

            // The offset is a vertical quantity only; a card that also drifted sideways as the
            // stack grew would fan out across the screen.
            CHECK(second.left == first.left);
            CHECK(second.right == first.right);
        }
    }

    SUBCASE("a top-anchored card moves down") {
        for (auto const& entry : kAllPositions) {
            if (!entry.anchoredToTop) {
                continue;
            }
            CAPTURE(entry.name);
            VisibleCard const first = placedCard(kWorkArea, kGap, 0, entry.position);
            VisibleCard const second = placedCard(kWorkArea, kGap, kOffset, entry.position);

            CHECK(second.top == first.top + kOffset);
            CHECK(second.bottom == first.bottom + kOffset);
            CHECK(second.top == kWorkArea.top + kGap + kOffset);

            CHECK(second.left == first.left);
            CHECK(second.right == first.right);
        }
    }
}

TEST_CASE("placeToast: a card arrives from the edge it is anchored to") {
    // The window multiplies this by its slide distance, so the sign is what decides whether a
    // card at the bottom of the screen rises into place or starts above its resting position and
    // drops onto it. Both animate smoothly and only one looks like a notification arriving; the
    // other reads as a card sliding off the display. Exact equality, because these are two
    // literals in the function rather than anything computed.
    for (auto const& entry : kAllPositions) {
        CAPTURE(entry.name);
        float const expected = entry.anchoredToTop ? -1.0f : 1.0f;

        CHECK(placeToast(kWorkArea, kWindow, kMargin, kGap, 0, entry.position).slideDirection ==
              expected);

        // The direction belongs to the anchor alone: a card further up a stack still enters the
        // same way as the first one, or the stack would appear to come apart as it fills.
        CHECK(placeToast(kWorkArea, kWindow, kMargin, kGap, kOffset, entry.position)
                  .slideDirection == expected);
    }
}

TEST_CASE("placeToast: a work area away from the origin still places the card inside it") {
    // The work area is whatever monitor the pointer or the main window happens to be on, and on
    // a multi-monitor desktop only one of them starts at the origin. Arithmetic that treated a
    // coordinate as a size -- or a width as a right edge -- gives the same answers on the
    // primary monitor and puts the card on the wrong display everywhere else, which is the one
    // arrangement a developer with a single screen never sees.
    SUBCASE("a monitor below and to the right of the primary") {
        constexpr RECT area{1920, 0, 3840, 1080};

        VisibleCard const topLeft = placedCard(area, kGap, 0, ToastPosition::TopLeft);
        CHECK(topLeft.left == area.left + kGap);
        CHECK(topLeft.top == area.top + kGap);

        VisibleCard const bottomRight = placedCard(area, kGap, 0, ToastPosition::BottomRight);
        CHECK(bottomRight.right == area.right - kGap);
        CHECK(bottomRight.bottom == area.bottom - kGap);

        VisibleCard const centred = placedCard(area, kGap, 0, ToastPosition::BottomCenter);
        CHECK(centred.left + centred.right == area.left + area.right);
    }

    // Windows gives a monitor to the left of, or above, the primary one negative coordinates,
    // and those are the ones where a subtraction written the wrong way round stops being
    // harmless: the card is not merely misplaced by the gap but thrown a screen width away.
    SUBCASE("a monitor above and to the left of the primary") {
        constexpr RECT area{-1600, -300, 0, 600};

        VisibleCard const topLeft = placedCard(area, kGap, 0, ToastPosition::TopLeft);
        CHECK(topLeft.left == area.left + kGap);
        CHECK(topLeft.top == area.top + kGap);

        VisibleCard const bottomRight = placedCard(area, kGap, 0, ToastPosition::BottomRight);
        CHECK(bottomRight.right == area.right - kGap);
        CHECK(bottomRight.bottom == area.bottom - kGap);

        VisibleCard const centred = placedCard(area, kGap, 0, ToastPosition::TopCenter);
        CHECK(centred.left + centred.right == area.left + area.right);

        // A stack on a monitor with negative coordinates still grows away from its own edge.
        VisibleCard const second = placedCard(area, kGap, kOffset, ToastPosition::BottomLeft);
        CHECK(second.bottom == area.bottom - kGap - kOffset);
        CHECK(second.left == area.left + kGap);
    }
}

TEST_CASE("placeToast: growing the shadow field does not move the visible card") {
    // The margin is the term with a sign on both ends -- taken off the anchored edge, added back
    // at the far one -- and this is the only case that can tell the two apart, because it holds
    // the visible card fixed while the window around it changes size. The shadow field is a
    // drawing detail that varies with the theme's shadow and with nothing the user chose, so a
    // card that shifted when it changed would be a card whose position depends on how heavy its
    // shadow is.
    for (auto const& entry : kAllPositions) {
        CAPTURE(entry.name);
        VisibleCard const shipped = placedCard(kWorkArea, kGap, kOffset, entry.position);

        // The same 340 by 96 card with no shadow field at all, and with twice the shipped one.
        SIZE const bare{kWindow.cx - 2 * kMargin, kWindow.cy - 2 * kMargin};
        VisibleCard const without =
            placedCardOfSize(kWorkArea, bare, 0, kGap, kOffset, entry.position);

        SIZE const padded{kWindow.cx + 2 * kMargin, kWindow.cy + 2 * kMargin};
        VisibleCard const doubled =
            placedCardOfSize(kWorkArea, padded, 2 * kMargin, kGap, kOffset, entry.position);

        CHECK(without.left == shipped.left);
        CHECK(without.top == shipped.top);
        CHECK(without.right == shipped.right);
        CHECK(without.bottom == shipped.bottom);

        CHECK(doubled.left == shipped.left);
        CHECK(doubled.top == shipped.top);
        CHECK(doubled.right == shipped.right);
        CHECK(doubled.bottom == shipped.bottom);
    }
}

TEST_CASE("placeToast: the anchored edges hold whatever size the card is") {
    // Every other case here hands over a window holding the same 340 by 96 card, which is what
    // one line of text on a 100% monitor comes to and nothing else. The card ToastWindow builds
    // is measured from the notification's own text -- a body that wraps makes it taller -- and
    // the whole window is then multiplied by the monitor's scale, so the size is a real
    // variable. A rule written with those two numbers baked in, or a bottom edge that forgot to
    // subtract the window height, would place the developer's own notifications perfectly and
    // hang everyone else's over the edge of the screen.
    SUBCASE("a taller card and a wider one anchor to the same edges") {
        for (SIZE const window : {kTallWindow, kWideWindow}) {
            CAPTURE(window.cx);
            CAPTURE(window.cy);

            VisibleCard const bottomRight =
                placedCardOfSize(kWorkArea, window, kMargin, kGap, 0, ToastPosition::BottomRight);
            CHECK(bottomRight.right == kWorkArea.right - kGap);
            CHECK(bottomRight.bottom == kWorkArea.bottom - kGap);

            VisibleCard const topLeft =
                placedCardOfSize(kWorkArea, window, kMargin, kGap, 0, ToastPosition::TopLeft);
            CHECK(topLeft.left == kWorkArea.left + kGap);
            CHECK(topLeft.top == kWorkArea.top + kGap);

            VisibleCard const centred =
                placedCardOfSize(kWorkArea, window, kMargin, kGap, 0, ToastPosition::TopCenter);
            CHECK(centred.left + centred.right == kWorkArea.left + kWorkArea.right);
        }
    }

    // The scale reaches every term at once -- the card, the shadow field it carries and the gap
    // it keeps from the edge are all multiplied before they arrive here. An expression that
    // mixed a scaled length with an unscaled one would still be right at 100% and wrong at
    // every other setting, which is the half of the range a developer on one 100% display never
    // looks at.
    SUBCASE("a window scaled for a 150% monitor") {
        constexpr SIZE window{582, 246};
        constexpr int marginPx = 36;
        constexpr int gapPx = 18;

        VisibleCard const card =
            placedCardOfSize(kWorkArea, window, marginPx, gapPx, 0, ToastPosition::BottomRight);
        CHECK(card.right == kWorkArea.right - gapPx);
        CHECK(card.bottom == kWorkArea.bottom - gapPx);
    }
}
