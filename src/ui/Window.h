#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "core/Settings.h"
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
    // maximised or while a system backdrop is painted. Derived classes draw inside this and
    // hit-test against it.
    D2D1_RECT_F bodyRect() const;

    // Asks the system for a backdrop behind the window, degrading per the running build.
    //
    // The two frames are mutually exclusive and this is the switch between them. Without a
    // backdrop the window owns its frame: it reserves a transparent margin, draws its own
    // rounded corners into it and blurs its own shadow there. A system backdrop is painted
    // across the entire window rectangle, underneath every pixel this window draws, and no
    // API exists to give the compositor the real silhouette -- so that margin would come
    // back as a hard-edged rectangle of blurred desktop with the shadow buried under it.
    // The only clip on offer is a window region, which is aliased and would saw the corners
    // and the shadow off anyway. So a backdrop collapses the margin to zero, the window
    // rectangle becomes the body, and the compositor supplies the corners and the shadow it
    // is already drawing for the frame.
    void setBackdrop(BackdropMode mode);

    // True while the system is painting a backdrop, in which case there is no shadow margin
    // and the derived class must not draw a shadow of its own.
    bool hasSystemBackdrop() const;

    // The radius the body should be drawn at: zero when maximised, and zero under a backdrop
    // the system leaves square, so the drawn body and the backdrop rectangle agree.
    float cornerRadius() const;

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
