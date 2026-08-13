#pragma once

#include <memory>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_3.h>
#include <wincodec.h>

#include "core/Signal.h"
#include "core/Win.h"

namespace peek::ui {

// The process-wide rendering device stack.
//
// One D3D11 device backs one Direct2D device, shared by every window: the main window,
// each notification flyout, and the off-screen surface the tray icon is rasterised on.
// Sharing matters because a composition swap chain and a WIC render target both have to
// come from the same D2D device to reuse brushes and cached geometry.
class GraphicsDevice {
public:
    static GraphicsDevice& instance();

    ID2D1Factory1* d2dFactory() const noexcept;
    ID2D1Device* d2dDevice() const noexcept;
    IDWriteFactory3* dwrite() const noexcept;
    IWICImagingFactory2* wic() const noexcept;
    IDXGIFactory2* dxgiFactory() const noexcept;
    ID3D11Device* d3dDevice() const noexcept;
    IDCompositionDevice* compositionDevice() const noexcept;

    com_ptr<ID2D1DeviceContext> createContext() const;

    // True when the device was created with the WARP adapter because hardware
    // acceleration was unavailable. The UI drops its blur effects in that case.
    bool isSoftwareRenderer() const noexcept;

    // Rebuilds everything after DXGI_ERROR_DEVICE_REMOVED, then raises `recreated` so
    // each window can re-attach its swap chain and drop cached device resources.
    void recreate();

    Signal<> recreated;

private:
    GraphicsDevice();
    ~GraphicsDevice();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// A DirectComposition-backed drawing surface bound to one window.
//
// The window is created WS_EX_NOREDIRECTIONBITMAP so it has no GDI surface at all; the
// swap chain is composed by DWM through a DirectComposition visual, which is what gives
// genuine per-pixel alpha and therefore rounded corners and a soft shadow the application
// draws itself. That is the only route to those on Windows 10, where the Win11 DWM
// corner and backdrop attributes do not exist.
class CompositionTarget {
public:
    CompositionTarget();
    ~CompositionTarget();

    CompositionTarget(CompositionTarget const&) = delete;
    CompositionTarget& operator=(CompositionTarget const&) = delete;

    void attach(HWND window);
    void detach();

    // `pixelSize` is physical pixels; `dpi` sets the DIP-to-pixel scale used for drawing.
    void resize(SIZE pixelSize, float dpi);

    // Returns nullptr when the surface has no size yet or the device is being rebuilt,
    // in which case the caller must skip the frame. The context is already inside
    // BeginDraw with the transform and DPI applied.
    ID2D1DeviceContext* beginDraw();

    // Presents and commits. Returns false if the device was lost and the frame should be
    // retried after GraphicsDevice::recreate().
    bool endDraw();

    SIZE pixelSize() const noexcept;
    float dpi() const noexcept;
    float scale() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace peek::ui
