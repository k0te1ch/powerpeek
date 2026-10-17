#pragma once

#include <vector>

#include "battery/DeviceInfo.h"
#include "core/Signal.h"
#include "core/Win.h"
#include "ui/Drawing.h"
#include "ui/Window.h"

namespace peek::ui {

// A small card on the desktop listing every connected device with its charge.
//
// It is a child of the desktop window, so it reads as part of the wallpaper: any window opened
// over it covers it, Show Desktop does not hide it, and it never takes focus or a taskbar
// button. The whole body drags it; a double click asks for the main window and the context menu
// hides it again. Where the shell will not adopt it, it is a top-level window kept just above
// the desktop instead, and Show Desktop hides that one.
//
// Painting is on demand like every other window here -- it redraws when the device list, the
// theme or the language changes and costs nothing in between.
class DesktopWidget : public D2DWindow {
public:
    DesktopWidget();
    ~DesktopWidget() override;

    // Shows the card at `position` (the window's top-left corner in virtual-screen pixels), or
    // in the top right corner of the primary work area when `position` is null or no longer on
    // any monitor -- a card saved on a display that has since been unplugged must not open
    // where nobody can reach it.
    void show(POINT const* position);
    void hide();

    void setControllers(std::vector<DeviceInfo> const& devices);

    // The text depends on the language, which the page cannot learn from a theme change.
    void languageChanged();

    // The user finished dragging it; the argument is the window's new top-left corner.
    Signal<POINT> moved;
    Signal<> openRequested;
    Signal<> hideRequested;

protected:
    void onPaint(ID2D1DeviceContext& context, D2D1_SIZE_F size) override;
    bool onMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result) override;
    bool isCaptionArea(D2D1_POINT_2F point) const override;

private:
    bool ensureWindow();
    float bodyHeightDip() const;
    void embedInDesktop();
    void moveTo(POINT screen);
    void resizeToContent();
    void showMenu(POINT screen);
    POINT defaultPosition() const;

    WindowShadow m_shadow;
    std::vector<DeviceInfo> m_devices;
    bool m_shown = false;
    // A child of the desktop window rather than a top-level one, so Show Desktop leaves it be.
    bool m_embedded = false;
    bool m_dragging = false;
    POINT m_dragCursor{};
    POINT m_dragOrigin{};
};

}  // namespace peek::ui
