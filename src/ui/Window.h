#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "core/Win.h"
#include "ui/Graphics.h"

namespace peek::ui {

// Base for every window the application draws itself.
//
// Rendering is on demand: nothing is painted until something calls invalidate(), and a
// window that is animating calls requestFrame() from onPaint to ask for another one.
// An idle window therefore costs no CPU at all, which matters for something that lives
// in the notification area for weeks at a time.
//
// The window has no non-client area (WS_POPUP, WS_EX_NOREDIRECTIONBITMAP) so the
// derived class draws its own title bar, corners and shadow, and answers WM_NCHITTEST to
// say which parts drag and which parts resize.
class D2DWindow {
public:
    virtual ~D2DWindow();

    D2DWindow(D2DWindow const&) = delete;
    D2DWindow& operator=(D2DWindow const&) = delete;

    HWND handle() const noexcept { return m_hwnd; }
    bool isVisible() const;

    // Physical pixels per DIP for the monitor the window is on.
    float scale() const;

    // Client size in DIPs, excluding nothing -- the shadow margin is part of it, because
    // the window really is that big; derived classes inset by Metrics::shadowMargin.
    D2D1_SIZE_F sizeDip() const;

    void invalidate();

protected:
    D2DWindow();

    struct CreateParams {
        std::wstring className;
        std::wstring title;
        // The whole window including the transparent shadow margin, matching sizeDip().
        SIZE initialSizeDip{800, 560};
        SIZE minimumSizeDip{360, 280};
        // WS_THICKFRAME, which is also what enables Aero Snap and the maximise animation;
        // a flyout wants it off.
        bool resizable = true;
        // Notification flyouts want topmost, no activation and no taskbar button.
        bool topmost = false;
        bool noActivate = false;
        bool appWindow = true;
        HWND owner = nullptr;
    };

    bool createWindow(CreateParams const& params);
    void destroyWindow();

    // Draw one frame. The context is inside BeginDraw with the DPI transform applied, so
    // all coordinates are DIPs. Call requestFrame() from here to keep animating.
    virtual void onPaint(ID2D1DeviceContext& ctx, D2D1_SIZE_F size) = 0;

    // Return true if the message was handled and `result` should be returned.
    virtual bool onMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result);

    virtual void onThemeChanged() {}
    virtual void onDpiChanged(float newScale) {}

    // Which part of the window drags it. `point` is in DIPs relative to the window's top-left
    // corner, so it is directly comparable with what onPaint drew. Returning true for the
    // title bar is all a derived class has to do: the base answers WM_NCHITTEST with
    // HTCAPTION there, and DefWindowProc then gives it dragging, Aero Snap, Aero Shake and
    // double-click-to-maximise for free.
    virtual bool isCaptionArea(D2D1_POINT_2F point) const;

    // True while the window is maximised, which collapses the shadow margin and the corner
    // radius to zero exactly as WinUI does.
    bool isMaximised() const;

    // The visible body: the window rect inset by the shadow margin, or the whole window when
    // maximised. Derived classes draw inside this and hit-test against it.
    D2D1_RECT_F bodyRect() const;

    // Asks for another frame after the compositor has shown this one; the render loop
    // keeps going while any frame requests a successor.
    void requestFrame();

    std::chrono::steady_clock::time_point frameTime() const noexcept { return m_frameTime; }

    HWND m_hwnd = nullptr;

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void render();
    LRESULT hitTest(POINT screen) const;
    void showSystemMenu(POINT screen) const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::chrono::steady_clock::time_point m_frameTime{};
};

}  // namespace peek::ui
