#include "ui/Graphics.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <chrono>

#include "core/Logger.h"

namespace peek::ui {
namespace {

// Flip-model swap chains need at least two buffers and refuse multisampling; a composition
// swap chain additionally requires STRETCH scaling and FLIP_SEQUENTIAL.
constexpr UINT kBufferCount = 2;

constexpr auto kLossWindow = std::chrono::seconds{5};
constexpr int kLossesBeforeWarp = 3;

bool isDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR ||
           hr == D2DERR_RECREATE_TARGET;
}

HRESULT createD3D(D3D_DRIVER_TYPE type,
                  UINT flags,
                  com_ptr<ID3D11Device>& device,
                  com_ptr<ID3D11DeviceContext>& context) {
    static constexpr D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };

    HRESULT hr = D3D11CreateDevice(nullptr, type, nullptr, flags, kLevels, ARRAYSIZE(kLevels),
                                   D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    if (hr == E_INVALIDARG) {
        // A stack without the 11.1 runtime rejects the whole feature-level array rather than
        // skipping the level it does not know, so retry without 11_1.
        device = nullptr;
        context = nullptr;
        hr = D3D11CreateDevice(nullptr, type, nullptr, flags, kLevels + 1, ARRAYSIZE(kLevels) - 1,
                               D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    }
    return hr;
}

}  // namespace

struct GraphicsDevice::Impl {
    com_ptr<ID3D11Device> d3d;
    com_ptr<ID3D11DeviceContext> d3dContext;
    com_ptr<IDXGIDevice1> dxgiDevice;
    com_ptr<IDXGIFactory2> dxgiFactory;
    com_ptr<ID2D1Factory1> d2dFactory;
    com_ptr<ID2D1Device> d2dDevice;
    com_ptr<IDWriteFactory3> dwrite;
    com_ptr<IWICImagingFactory2> wic;
    com_ptr<IDCompositionDevice> composition;

    bool warp = false;
    bool rebuilding = false;
    int losses = 0;
    std::chrono::steady_clock::time_point firstLoss{};

    void create(bool forceWarp);
    void reset();
};

void GraphicsDevice::Impl::create(bool forceWarp) {
    // BGRA support is not optional: without it CreateDevice on the D2D factory fails with
    // D2DERR_UNSUPPORTED_OPERATION and says nothing about why.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    HRESULT hr = forceWarp ? E_FAIL : createD3D(D3D_DRIVER_TYPE_HARDWARE, flags, d3d, d3dContext);
    if (FAILED(hr)) {
        d3d = nullptr;
        d3dContext = nullptr;
        // WARP, not REFERENCE: CreateSwapChainForComposition rejects the reference rasteriser.
        // This is the path taken in a bare RDP session or with a broken display driver.
        check_hresult(createD3D(D3D_DRIVER_TYPE_WARP, flags, d3d, d3dContext));
        warp = true;
        log::warning(L"Direct3D hardware device unavailable ({}); rendering through WARP",
                     describeHresult(hr));
    }

    dxgiDevice = d3d.as<IDXGIDevice1>();
    dxgiDevice->SetMaximumFrameLatency(1);

    // Take the factory from our own adapter: on a hybrid-GPU laptop the process default
    // factory can hand back the other adapter, and a swap chain from it will not compose.
    com_ptr<IDXGIAdapter> adapter;
    check_hresult(dxgiDevice->GetAdapter(adapter.put()));
    check_hresult(adapter->GetParent(winrt::guid_of<IDXGIFactory2>(), dxgiFactory.put_void()));

    D2D1_FACTORY_OPTIONS options{};
    check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                    winrt::guid_of<ID2D1Factory1>(), &options,
                                    d2dFactory.put_void()));
    check_hresult(d2dFactory->CreateDevice(dxgiDevice.get(), d2dDevice.put()));

    com_ptr<IDWriteFactory> writeBase;
    check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, winrt::guid_of<IDWriteFactory>(),
                                      reinterpret_cast<IUnknown**>(writeBase.put())));
    dwrite = writeBase.as<IDWriteFactory3>();

    // IDCompositionDevice2 has no CreateTargetForHwnd; only the v1 device and
    // IDCompositionDesktopDevice do, so the v1 entry point is what this window model needs.
    check_hresult(DCompositionCreateDevice(dxgiDevice.get(), winrt::guid_of<IDCompositionDevice>(),
                                           composition.put_void()));

    // WIC only rasterises the tray icon. Losing it costs the icon, not the application, so a
    // failure here is logged rather than thrown.
    HRESULT wicHr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                     winrt::guid_of<IWICImagingFactory2>(), wic.put_void());
    if (FAILED(wicHr)) {
        log::error(L"WIC imaging factory unavailable ({}); the tray icon cannot be rendered",
                   describeHresult(wicHr));
    }
}

void GraphicsDevice::Impl::reset() {
    if (d3dContext) {
        d3dContext->ClearState();
        d3dContext->Flush();
    }
    wic = nullptr;
    composition = nullptr;
    dwrite = nullptr;
    d2dDevice = nullptr;
    d2dFactory = nullptr;
    dxgiFactory = nullptr;
    dxgiDevice = nullptr;
    d3dContext = nullptr;
    d3d = nullptr;
    warp = false;
}

GraphicsDevice::GraphicsDevice() : m_impl(std::make_unique<Impl>()) {
    m_impl->create(false);
}

GraphicsDevice::~GraphicsDevice() = default;

GraphicsDevice& GraphicsDevice::instance() {
    static GraphicsDevice device;
    return device;
}

ID2D1Factory1* GraphicsDevice::d2dFactory() const noexcept { return m_impl->d2dFactory.get(); }
ID2D1Device* GraphicsDevice::d2dDevice() const noexcept { return m_impl->d2dDevice.get(); }
IDWriteFactory3* GraphicsDevice::dwrite() const noexcept { return m_impl->dwrite.get(); }
IWICImagingFactory2* GraphicsDevice::wic() const noexcept { return m_impl->wic.get(); }
IDXGIFactory2* GraphicsDevice::dxgiFactory() const noexcept { return m_impl->dxgiFactory.get(); }
ID3D11Device* GraphicsDevice::d3dDevice() const noexcept { return m_impl->d3d.get(); }

IDCompositionDevice* GraphicsDevice::compositionDevice() const noexcept {
    return m_impl->composition.get();
}

bool GraphicsDevice::isSoftwareRenderer() const noexcept { return m_impl->warp; }

com_ptr<ID2D1DeviceContext> GraphicsDevice::createContext() const {
    com_ptr<ID2D1DeviceContext> context;
    check_hresult(m_impl->d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                         context.put()));
    // ClearType on a per-pixel-alpha surface produces coloured fringes wherever the desktop
    // shows through, so every context in this application renders text greyscale.
    context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    return context;
}

void GraphicsDevice::recreate() {
    // Every window reports the loss it saw, so without this guard one lost device rebuilds the
    // stack once per window and each rebuild invalidates the one before it.
    if (m_impl->rebuilding) {
        return;
    }

    auto const now = std::chrono::steady_clock::now();
    if (m_impl->losses == 0 || now - m_impl->firstLoss > kLossWindow) {
        m_impl->firstLoss = now;
        m_impl->losses = 0;
    }
    ++m_impl->losses;

    bool const forceWarp = m_impl->losses > kLossesBeforeWarp;
    if (forceWarp) {
        log::error(L"Direct3D device lost {} times in {} seconds; falling back to WARP",
                   m_impl->losses, kLossWindow.count());
    } else {
        log::warning(L"Direct3D device lost; rebuilding the rendering stack");
    }

    m_impl->rebuilding = true;
    m_impl->reset();
    try {
        m_impl->create(forceWarp);
    } catch (...) {
        m_impl->rebuilding = false;
        throw;
    }
    m_impl->rebuilding = false;

    recreated();
}

struct CompositionTarget::Impl {
    HWND window = nullptr;
    com_ptr<IDXGISwapChain1> swapChain;
    com_ptr<IDCompositionTarget> target;
    com_ptr<IDCompositionVisual> visual;
    com_ptr<ID2D1DeviceContext> context;
    com_ptr<ID2D1Bitmap1> surface;
    SIZE pixels{0, 0};
    float dpi = 96.0f;
    bool drawing = false;
    Signal<>::Token recreatedToken = 0;

    void build();
    void releaseDeviceResources();
    void bindBackBuffer();
};

void CompositionTarget::Impl::releaseDeviceResources() {
    if (context) {
        context->SetTarget(nullptr);
    }
    surface = nullptr;
    context = nullptr;
    visual = nullptr;
    target = nullptr;
    swapChain = nullptr;
}

void CompositionTarget::Impl::build() {
    if (!window || pixels.cx <= 0 || pixels.cy <= 0) {
        return;
    }

    auto& device = GraphicsDevice::instance();

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(pixels.cx);
    desc.Height = static_cast<UINT>(pixels.cy);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc = {1, 0};
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBufferCount;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    // Premultiplied alpha is what makes the window genuinely translucent, and therefore what
    // lets us draw rounded corners and a soft shadow instead of a rectangle.
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    check_hresult(device.dxgiFactory()->CreateSwapChainForComposition(device.d3dDevice(), &desc,
                                                                      nullptr, swapChain.put()));

    check_hresult(device.compositionDevice()->CreateTargetForHwnd(window, TRUE, target.put()));
    check_hresult(device.compositionDevice()->CreateVisual(visual.put()));
    check_hresult(visual->SetContent(swapChain.get()));
    check_hresult(target->SetRoot(visual.get()));
    check_hresult(device.compositionDevice()->Commit());

    context = device.createContext();
    bindBackBuffer();
}

void CompositionTarget::Impl::bindBackBuffer() {
    com_ptr<IDXGISurface> back;
    check_hresult(swapChain->GetBuffer(0, winrt::guid_of<IDXGISurface>(), back.put_void()));

    // CANNOT_DRAW is mandatory for a flip-model back buffer: it declares the bitmap a target
    // only, which is the one thing a flip-model buffer can be.
    auto const properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);

    surface = nullptr;
    check_hresult(context->CreateBitmapFromDxgiSurface(back.get(), &properties, surface.put()));
}

CompositionTarget::CompositionTarget() : m_impl(std::make_unique<Impl>()) {
    m_impl->recreatedToken = GraphicsDevice::instance().recreated.connect([this] {
        // Everything below is owned by the device that just went away; the next frame rebuilds
        // it against the new one.
        m_impl->releaseDeviceResources();
        m_impl->build();
    });
}

CompositionTarget::~CompositionTarget() {
    GraphicsDevice::instance().recreated.disconnect(m_impl->recreatedToken);
}

void CompositionTarget::attach(HWND window) {
    m_impl->window = window;
    m_impl->build();
}

void CompositionTarget::detach() {
    m_impl->releaseDeviceResources();
    m_impl->window = nullptr;
    m_impl->pixels = {0, 0};
}

void CompositionTarget::resize(SIZE pixelSize, float dpi) {
    if (pixelSize.cx <= 0 || pixelSize.cy <= 0) {
        return;  // minimised: there is nothing to size the buffers to
    }

    bool const dpiChanged = dpi != m_impl->dpi;
    if (!dpiChanged && pixelSize.cx == m_impl->pixels.cx && pixelSize.cy == m_impl->pixels.cy &&
        m_impl->swapChain) {
        return;
    }

    m_impl->pixels = pixelSize;
    m_impl->dpi = dpi;

    if (!m_impl->swapChain) {
        m_impl->build();
        return;
    }

    // ResizeBuffers fails with DXGI_ERROR_INVALID_CALL while any reference to a back buffer
    // survives, and the D2D target bitmap is exactly such a reference.
    m_impl->context->SetTarget(nullptr);
    m_impl->surface = nullptr;

    HRESULT hr = m_impl->swapChain->ResizeBuffers(0, static_cast<UINT>(pixelSize.cx),
                                                 static_cast<UINT>(pixelSize.cy),
                                                 DXGI_FORMAT_UNKNOWN, 0);
    if (isDeviceLost(hr)) {
        GraphicsDevice::instance().recreate();
        return;
    }
    check_hresult(hr);

    m_impl->bindBackBuffer();
}

ID2D1DeviceContext* CompositionTarget::beginDraw() {
    if (!m_impl->surface || !m_impl->context || m_impl->drawing) {
        return nullptr;
    }

    auto* context = m_impl->context.get();
    context->SetTarget(m_impl->surface.get());
    context->SetDpi(m_impl->dpi, m_impl->dpi);
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    context->BeginDraw();
    context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    m_impl->drawing = true;
    return context;
}

bool CompositionTarget::endDraw() {
    if (!m_impl->drawing) {
        return true;
    }
    m_impl->drawing = false;

    HRESULT hr = m_impl->context->EndDraw();
    if (isDeviceLost(hr)) {
        return false;
    }
    if (FAILED(hr)) {
        log::error(L"Direct2D EndDraw failed: {}", describeHresult(hr));
        return true;  // not a device loss; the next frame has no better chance, so do not loop
    }

    DXGI_PRESENT_PARAMETERS present{};
    hr = m_impl->swapChain->Present1(1, 0, &present);
    if (isDeviceLost(hr)) {
        return false;
    }
    if (FAILED(hr)) {
        log::error(L"Present failed: {}", describeHresult(hr));
        return true;
    }

    check_hresult(GraphicsDevice::instance().compositionDevice()->Commit());
    return true;
}

SIZE CompositionTarget::pixelSize() const noexcept { return m_impl->pixels; }

float CompositionTarget::dpi() const noexcept { return m_impl->dpi; }

float CompositionTarget::scale() const noexcept { return m_impl->dpi / 96.0f; }

}  // namespace peek::ui
