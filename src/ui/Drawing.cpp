#include "ui/Drawing.h"

#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <unordered_map>

#include "core/Logger.h"
#include "ui/Graphics.h"

namespace peek::ui {
namespace {

constexpr float kPi = 3.14159265358979f;

float width(D2D1_RECT_F const& rect) { return rect.right - rect.left; }
float height(D2D1_RECT_F const& rect) { return rect.bottom - rect.top; }

D2D1_RECT_F inflate(D2D1_RECT_F rect, float by) {
    return D2D1::RectF(rect.left - by, rect.top - by, rect.right + by, rect.bottom + by);
}

com_ptr<ID2D1PathGeometry> beginPath(Canvas& canvas, com_ptr<ID2D1GeometrySink>& sink) {
    com_ptr<ID2D1PathGeometry> path;
    if (FAILED(canvas.factory()->CreatePathGeometry(path.put())) ||
        FAILED(path->Open(sink.put()))) {
        log::error(L"Failed to open a Direct2D path geometry");
        return nullptr;
    }
    return path;
}

// Segoe UI at an arbitrary size, for the numbers in the gauges. The type ramp has fixed
// sizes; a 16-pixel tray icon needs whatever fits.
IDWriteTextFormat* numberFormat(float size,
                                DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_SEMI_BOLD) {
    // The key packs the size in quarter-points with the weight: sizes are asked for as
    // fractions of a pixel height and would otherwise almost never hit the cache.
    static std::unordered_map<std::uint32_t, com_ptr<IDWriteTextFormat>> cache;
    auto const quarters = static_cast<std::uint32_t>(std::lround(size * 4.0f));
    std::uint32_t const key = (quarters << 16) | static_cast<std::uint32_t>(weight);
    if (auto const found = cache.find(key); found != cache.end()) {
        return found->second.get();
    }

    com_ptr<IDWriteTextFormat> format;
    HRESULT hr = GraphicsDevice::instance().dwrite()->CreateTextFormat(
        L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        static_cast<float>(quarters) / 4.0f, L"", format.put());
    if (FAILED(hr)) {
        log::error(L"CreateTextFormat failed for the gauge number: {}", describeHresult(hr));
        return nullptr;
    }
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return cache.emplace(key, std::move(format)).first->second.get();
}

// A lightning bolt in a unit box, so it can be scaled into any gauge.
constexpr D2D1_POINT_2F kBolt[] = {
    {0.60f, 0.00f}, {0.20f, 0.56f}, {0.46f, 0.56f},
    {0.36f, 1.00f}, {0.80f, 0.42f}, {0.54f, 0.42f},
};

com_ptr<ID2D1PathGeometry> boltGeometry(Canvas& canvas, D2D1_RECT_F box) {
    com_ptr<ID2D1GeometrySink> sink;
    auto path = beginPath(canvas, sink);
    if (!path) {
        return nullptr;
    }

    auto place = [&](D2D1_POINT_2F unit) {
        return D2D1::Point2F(box.left + unit.x * width(box), box.top + unit.y * height(box));
    };

    sink->BeginFigure(place(kBolt[0]), D2D1_FIGURE_BEGIN_FILLED);
    for (std::size_t i = 1; i < std::size(kBolt); ++i) {
        sink->AddLine(place(kBolt[i]));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return path;
}

// ---- The controller portrait -------------------------------------------------------------
//
// Everything below is designed in a space that is one unit tall, with x measured out from the
// centre line, so a circle stays a circle whatever box the drawing is fitted into and every
// position can be read as a fraction of the pad's height.

// A little wider than the silhouette actually reaches, so the outline stroke has somewhere to
// go and a caller reserving controllerArtWidth() never gets ink outside the box it reserved.
constexpr float kArtAspect = 1.50f;

// Fitted design space: where x = 0, y = 0.5 landed, and what one design unit is in DIPs.
struct ArtSpace {
    D2D1_POINT_2F center{};
    float unit = 0.0f;

    D2D1_POINT_2F at(float x, float y) const {
        return D2D1::Point2F(center.x + x * unit, center.y + (y - 0.5f) * unit);
    }
    D2D1_POINT_2F at(D2D1_POINT_2F p) const { return at(p.x, p.y); }
    D2D1_RECT_F box(D2D1_POINT_2F around, float halfWidth, float halfHeight) const {
        auto const topLeft = at(around.x - halfWidth, around.y - halfHeight);
        auto const bottomRight = at(around.x + halfWidth, around.y + halfHeight);
        return D2D1::RectF(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y);
    }
};

struct ArtSegment {
    D2D1_POINT_2F c1;
    D2D1_POINT_2F c2;
    D2D1_POINT_2F end;
};

// The right half of the silhouette, running clockwise from the dip between the bumpers to the
// apex of the notch between the grips. The left half is this mirrored, which is both half the
// numbers and the only way to be sure the two sides cannot drift apart.
constexpr D2D1_POINT_2F kBodyStart{0.000f, 0.075f};
constexpr ArtSegment kBodyRight[] = {
    {{0.075f, 0.075f}, {0.145f, 0.014f}, {0.320f, 0.006f}},   // over the bumper
    {{0.515f, -0.006f}, {0.685f, 0.040f}, {0.705f, 0.195f}},  // the outer corner
    {{0.722f, 0.300f}, {0.680f, 0.380f}, {0.664f, 0.505f}},   // the waist
    {{0.652f, 0.650f}, {0.742f, 0.812f}, {0.645f, 0.940f}},   // the grip, splaying outwards
    {{0.590f, 1.000f}, {0.468f, 1.000f}, {0.400f, 0.930f}},   // its rounded tip
    {{0.352f, 0.878f}, {0.324f, 0.806f}, {0.268f, 0.745f}},   // the grip's inner edge
    {{0.205f, 0.685f}, {0.100f, 0.660f}, {0.000f, 0.660f}},   // the notch between the grips
};

constexpr D2D1_POINT_2F kLeftStick{-0.405f, 0.300f};
constexpr D2D1_POINT_2F kRightStick{0.175f, 0.490f};
constexpr D2D1_POINT_2F kDpad{-0.218f, 0.505f};
constexpr D2D1_POINT_2F kFaceCluster{0.405f, 0.285f};
constexpr D2D1_POINT_2F kGuide{0.000f, 0.215f};
constexpr D2D1_POINT_2F kBadge{0.000f, 0.840f};

constexpr float kStickWell = 0.116f;
constexpr float kStickCap = 0.090f;
constexpr float kDpadArm = 0.120f;
constexpr float kDpadWaist = 0.040f;
constexpr float kFaceButton = 0.058f;
constexpr float kFaceSpread = 0.115f;
constexpr float kGuideRadius = 0.060f;
constexpr float kGuideRing = 0.079f;  // the recess the guide button sits in
constexpr float kBadgeRadius = 0.135f;

// Art heights, in DIPs, below which a layer of detail is left out.
constexpr float kGuideThreshold = 26.0f;
constexpr float kBadgeThreshold = 40.0f;
constexpr float kDetailThreshold = 50.0f;

com_ptr<ID2D1PathGeometry> controllerBody(Canvas& canvas, ArtSpace const& space) {
    com_ptr<ID2D1GeometrySink> sink;
    auto path = beginPath(canvas, sink);
    if (!path) {
        return nullptr;
    }

    auto mirror = [](D2D1_POINT_2F p) { return D2D1::Point2F(-p.x, p.y); };

    sink->BeginFigure(space.at(kBodyStart), D2D1_FIGURE_BEGIN_FILLED);
    for (auto const& segment : kBodyRight) {
        sink->AddBezier(D2D1::BezierSegment(space.at(segment.c1), space.at(segment.c2),
                                            space.at(segment.end)));
    }
    for (std::size_t i = std::size(kBodyRight); i-- > 0;) {
        auto const& segment = kBodyRight[i];
        D2D1_POINT_2F const start = i == 0 ? kBodyStart : kBodyRight[i - 1].end;
        sink->AddBezier(D2D1::BezierSegment(space.at(mirror(segment.c2)),
                                            space.at(mirror(segment.c1)),
                                            space.at(mirror(start))));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return path;
}

// The stick as two concentric discs: the well it moves in, and the cap on top of it. Two flat
// circles read as a thumbstick where one does not.
void drawStick(Canvas& canvas, ArtSpace const& space, D2D1_POINT_2F at, ControllerArt const& art) {
    fillCircle(canvas, space.at(at), kStickWell * space.unit, art.recess);
    fillCircle(canvas, space.at(at), kStickCap * space.unit, art.detail);
}

// A plus sign, drawn as two rounded bars crossing. The waist is deliberately narrow: at card
// size a fatter cross turns into a square.
void drawDpad(Canvas& canvas, ArtSpace const& space, ControllerArt const& art) {
    float const radius = kDpadWaist * space.unit * 0.6f;
    fillRounded(canvas, space.box(kDpad, kDpadArm, kDpadWaist), radius, art.detail);
    fillRounded(canvas, space.box(kDpad, kDpadWaist, kDpadArm), radius, art.detail);
}

void drawFaceButtons(Canvas& canvas, ArtSpace const& space, ControllerArt const& art) {
    for (auto const& offset : {D2D1::Point2F(0.0f, -kFaceSpread), D2D1::Point2F(0.0f, kFaceSpread),
                               D2D1::Point2F(-kFaceSpread, 0.0f),
                               D2D1::Point2F(kFaceSpread, 0.0f)}) {
        auto const center = space.at(kFaceCluster.x + offset.x, kFaceCluster.y + offset.y);
        fillCircle(canvas, center, kFaceButton * space.unit, art.detail);
    }
}

std::wstring_view linkGlyph(ControllerLink link) {
    switch (link) {
        case ControllerLink::Usb:
            return glyph::kUsb;
        case ControllerLink::Wireless:
            return glyph::kWireless;
        case ControllerLink::Bluetooth:
            return glyph::kBluetooth;
        case ControllerLink::None:
            break;
    }
    return {};
}

// Monotone cubic Hermite tangents (Fritsch-Carlson). A plain Catmull-Rom spline overshoots
// between samples, which on a battery chart draws levels the battery never had.
std::vector<float> monotoneTangents(std::span<D2D1_POINT_2F const> points) {
    std::size_t const n = points.size();
    std::vector<float> slopes(n - 1);
    std::vector<float> tangents(n);

    for (std::size_t i = 0; i + 1 < n; ++i) {
        float const dx = points[i + 1].x - points[i].x;
        slopes[i] = dx > 0.0f ? (points[i + 1].y - points[i].y) / dx : 0.0f;
    }

    tangents.front() = slopes.front();
    tangents.back() = slopes.back();
    for (std::size_t i = 1; i + 1 < n; ++i) {
        tangents[i] = (slopes[i - 1] + slopes[i]) * 0.5f;
    }

    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (slopes[i] == 0.0f) {
            tangents[i] = 0.0f;
            tangents[i + 1] = 0.0f;
            continue;
        }
        float const alpha = tangents[i] / slopes[i];
        float const beta = tangents[i + 1] / slopes[i];
        float const magnitude = alpha * alpha + beta * beta;
        if (magnitude > 9.0f) {
            float const scale = 3.0f / std::sqrt(magnitude);
            tangents[i] = scale * alpha * slopes[i];
            tangents[i + 1] = scale * beta * slopes[i];
        }
    }
    return tangents;
}

void addSmoothedFigure(ID2D1GeometrySink& sink, std::span<D2D1_POINT_2F const> points) {
    auto const tangents = monotoneTangents(points);
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        float const dx = points[i + 1].x - points[i].x;
        sink.AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(points[i].x + dx / 3.0f, points[i].y + tangents[i] * dx / 3.0f),
            D2D1::Point2F(points[i + 1].x - dx / 3.0f,
                          points[i + 1].y - tangents[i + 1] * dx / 3.0f),
            points[i + 1]));
    }
}

HBITMAP copyToDib(IWICBitmap& bitmap, SIZE size) {
    com_ptr<IWICBitmapLock> lock;
    WICRect const all{0, 0, size.cx, size.cy};
    if (FAILED(bitmap.Lock(&all, WICBitmapLockRead, lock.put()))) {
        return nullptr;
    }

    UINT stride = 0;
    UINT bufferSize = 0;
    BYTE* source = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&bufferSize, &source)) ||
        !source) {
        return nullptr;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size.cx;
    info.bmiHeader.biHeight = -size.cy;  // negative: top-down, matching WIC's row order
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) {
        return nullptr;
    }

    auto const rowBytes = static_cast<std::size_t>(size.cx) * 4;
    for (int y = 0; y < size.cy; ++y) {
        std::memcpy(static_cast<BYTE*>(bits) + static_cast<std::size_t>(y) * rowBytes,
                    source + static_cast<std::size_t>(y) * stride, rowBytes);
    }
    return dib;
}

}  // namespace

Canvas::Canvas(ID2D1RenderTarget& target) : m_target(&target) {
    // A render target created straight from the factory -- the WIC one the tray icon uses --
    // may not be a device context, and then effects are unavailable. Everything else in this
    // file works on either.
    if (FAILED(target.QueryInterface(winrt::guid_of<ID2D1DeviceContext>(), m_context.put_void()))) {
        m_context = nullptr;
    }
    target.GetFactory(m_factory.put());

    float dpiX = 96.0f;
    float dpiY = 96.0f;
    target.GetDpi(&dpiX, &dpiY);
    m_pixel = dpiX > 0.0f ? 96.0f / dpiX : 1.0f;
}

ID2D1SolidColorBrush* Canvas::brush(D2D1_COLOR_F const& color) const {
    if (!m_brush) {
        if (FAILED(m_target->CreateSolidColorBrush(color, m_brush.put()))) {
            log::error(L"CreateSolidColorBrush failed; this frame will be blank");
            return nullptr;
        }
        return m_brush.get();
    }
    m_brush->SetColor(color);
    m_brush->SetOpacity(1.0f);
    return m_brush.get();
}

float Canvas::snap(float dip) const noexcept {
    return m_pixel > 0.0f ? std::round(dip / m_pixel) * m_pixel : dip;
}

D2D1_RECT_F Canvas::snapStroke(D2D1_RECT_F rect, float strokeWidth) const noexcept {
    float const half = (strokeWidth <= 0.0f ? m_pixel : strokeWidth) * 0.5f;
    return D2D1::RectF(snap(rect.left) + half, snap(rect.top) + half, snap(rect.right) - half,
                       snap(rect.bottom) - half);
}

void fillRect(Canvas& canvas, D2D1_RECT_F rect, D2D1_COLOR_F color) {
    if (auto* brush = canvas.brush(color)) {
        canvas.target().FillRectangle(rect, brush);
    }
}

void fillRounded(Canvas& canvas, D2D1_RECT_F rect, float radius, D2D1_COLOR_F color) {
    auto* brush = canvas.brush(color);
    if (!brush) {
        return;
    }
    if (radius <= 0.0f) {
        canvas.target().FillRectangle(rect, brush);
        return;
    }
    // A radius larger than half the shorter side draws a lens rather than a pill.
    float const limit = std::min(width(rect), height(rect)) * 0.5f;
    float const r = std::min(radius, limit);
    canvas.target().FillRoundedRectangle(D2D1::RoundedRect(rect, r, r), brush);
}

void fillCircle(Canvas& canvas, D2D1_POINT_2F center, float radius, D2D1_COLOR_F color) {
    if (auto* brush = canvas.brush(color)) {
        canvas.target().FillEllipse(D2D1::Ellipse(center, radius, radius), brush);
    }
}

void strokeRounded(Canvas& canvas,
                   D2D1_RECT_F rect,
                   float radius,
                   D2D1_COLOR_F color,
                   float strokeWidth) {
    auto* brush = canvas.brush(color);
    if (!brush) {
        return;
    }

    float const w = strokeWidth <= 0.0f ? canvas.pixel() : strokeWidth;
    D2D1_RECT_F const aligned = canvas.snapStroke(rect, w);
    if (width(aligned) <= 0.0f || height(aligned) <= 0.0f) {
        return;
    }

    if (radius <= 0.0f) {
        canvas.target().DrawRectangle(aligned, brush, w);
        return;
    }
    // The stroke sits on the geometry edge, so its radius is the fill radius less the inset.
    float const r = std::max(0.0f, std::min(radius - w * 0.5f,
                                            std::min(width(aligned), height(aligned)) * 0.5f));
    canvas.target().DrawRoundedRectangle(D2D1::RoundedRect(aligned, r, r), brush, w);
}

void strokeControlBorder(Canvas& canvas,
                         D2D1_RECT_F rect,
                         float radius,
                         D2D1_COLOR_F border,
                         D2D1_COLOR_F bottomEdge) {
    strokeRounded(canvas, rect, radius, border);

    auto* brush = canvas.brush(bottomEdge);
    if (!brush) {
        return;
    }
    float const w = canvas.pixel();
    float const y = canvas.snap(rect.bottom) - w * 0.5f;
    float const inset = std::max(radius, w);
    canvas.target().DrawLine(D2D1::Point2F(rect.left + inset, y),
                             D2D1::Point2F(rect.right - inset, y), brush, w);
}

void drawFocusRing(Canvas& canvas, D2D1_RECT_F rect, float radius) {
    auto const& palette = theme().colors();
    float const outer = 2.0f;
    strokeRounded(canvas, inflate(rect, outer * 0.5f), radius + outer, palette.focusStrokeOuter,
                  outer);
    strokeRounded(canvas, inflate(rect, -canvas.pixel() * 0.5f), radius, palette.focusStrokeInner);
}

void drawDivider(Canvas& canvas, float left, float right, float y, D2D1_COLOR_F color) {
    auto* brush = canvas.brush(color);
    if (!brush) {
        return;
    }
    float const w = canvas.pixel();
    float const snapped = canvas.snap(y) + w * 0.5f;
    canvas.target().DrawLine(D2D1::Point2F(left, snapped), D2D1::Point2F(right, snapped), brush, w);
}

void drawText(Canvas& canvas,
              std::wstring_view text,
              IDWriteTextFormat* format,
              D2D1_RECT_F rect,
              D2D1_COLOR_F color,
              Align horizontal,
              Align vertical) {
    if (!format || text.empty()) {
        return;
    }
    auto* brush = canvas.brush(color);
    if (!brush) {
        return;
    }

    static constexpr DWRITE_TEXT_ALIGNMENT kHorizontal[]{DWRITE_TEXT_ALIGNMENT_LEADING,
                                                         DWRITE_TEXT_ALIGNMENT_CENTER,
                                                         DWRITE_TEXT_ALIGNMENT_TRAILING};
    static constexpr DWRITE_PARAGRAPH_ALIGNMENT kVertical[]{DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                                                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                                                            DWRITE_PARAGRAPH_ALIGNMENT_FAR};
    format->SetTextAlignment(kHorizontal[static_cast<int>(horizontal)]);
    format->SetParagraphAlignment(kVertical[static_cast<int>(vertical)]);

    canvas.target().DrawText(text.data(), static_cast<UINT32>(text.size()), format, rect, brush,
                             D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}

void drawIcon(Canvas& canvas,
              std::wstring_view glyph,
              float sizeDip,
              D2D1_RECT_F rect,
              D2D1_COLOR_F color) {
    drawText(canvas, glyph, theme().iconFormat(sizeDip), rect, color, Align::Center, Align::Center);
}

float measureText(std::wstring_view text, IDWriteTextFormat* format) {
    if (!format || text.empty()) {
        return 0.0f;
    }
    com_ptr<IDWriteTextLayout> layout;
    if (FAILED(GraphicsDevice::instance().dwrite()->CreateTextLayout(
            text.data(), static_cast<UINT32>(text.size()), format, 1e6f, 1e6f, layout.put()))) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
}

// Discards the layout and, once a width is known, rebuilds it at once.
//
// Rebuilding eagerly is what keeps a block that changes between layout passes visible: a
// slider readout is retexted while the user drags it, and paint() runs long before the next
// measure(). Deferring would leave draw() with nothing to draw and the text would vanish
// mid-gesture. It also keeps size() truthful, which the callers position against.
void TextBlock::relayout() {
    m_layout = nullptr;
    if (m_maxWidth > 0.0f) {
        float const width = m_maxWidth;
        m_maxWidth = 0.0f;  // force ensureLayout past its early-out
        ensureLayout(width);
    }
}

void TextBlock::setText(std::wstring text) {
    if (m_text == text) {
        return;
    }
    m_text = std::move(text);
    relayout();
}

void TextBlock::setStyle(TypeStyle style) {
    if (m_style == style) {
        return;
    }
    m_style = style;
    relayout();
}

void TextBlock::setWrapping(bool wrap) {
    if (m_wrap == wrap) {
        return;
    }
    m_wrap = wrap;
    relayout();
}

void TextBlock::ensureLayout(float maxWidth) {
    if (m_layout && m_maxWidth == maxWidth) {
        return;
    }
    m_maxWidth = maxWidth;
    m_layout = nullptr;
    m_size = {0.0f, 0.0f};

    if (m_text.empty() || maxWidth <= 0.0f) {
        return;
    }

    auto* format = theme().textFormat(m_style);
    if (!format) {
        return;
    }
    if (FAILED(GraphicsDevice::instance().dwrite()->CreateTextLayout(
            m_text.c_str(), static_cast<UINT32>(m_text.size()), format, maxWidth, 1e6f,
            m_layout.put()))) {
        return;
    }

    // Wrapping is a per-layout override so the shared no-wrap format stays untouched.
    m_layout->SetWordWrapping(m_wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    // A layout inherits the format's alignment at the moment it is created, and drawText sets
    // that per call, so pin it here rather than inheriting whatever the last caller wanted.
    m_layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    DWRITE_TEXT_METRICS metrics{};
    m_layout->GetMetrics(&metrics);
    m_size = D2D1::SizeF(std::min(metrics.widthIncludingTrailingWhitespace, maxWidth),
                         metrics.height);
}

float TextBlock::measure(float maxWidth) {
    ensureLayout(maxWidth);
    return m_size.height;
}

void TextBlock::draw(Canvas& canvas, D2D1_POINT_2F origin, D2D1_COLOR_F color) {
    if (!m_layout) {
        return;
    }
    if (auto* brush = canvas.brush(color)) {
        canvas.target().DrawTextLayout(origin, m_layout.get(), brush,
                                       D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
}

WindowShadow::WindowShadow() {
    // The cached bitmap belongs to the Direct2D device that made it, so it has to go when that
    // device does; nothing else would notice, and the stale bitmap would draw as garbage.
    m_deviceToken = GraphicsDevice::instance().recreated.connect([this] {
        m_cache = nullptr;
        m_dpi = 0.0f;
    });
}

WindowShadow::~WindowShadow() {
    GraphicsDevice::instance().recreated.disconnect(m_deviceToken);
}

void WindowShadow::draw(Canvas& canvas, D2D1_RECT_F body, float radius, D2D1_COLOR_F color) {
    auto* context = canvas.deviceContext();
    if (!context) {
        if (!m_unsupported) {
            m_unsupported = true;
            log::warning(L"No Direct2D device context on this target; the window shadow is off");
        }
        return;
    }

    float dpiX = 96.0f;
    float dpiY = 96.0f;
    context->GetDpi(&dpiX, &dpiY);

    bool const stale = !m_cache || m_dpi != dpiX || m_radius != radius ||
                       std::memcmp(&m_body, &body, sizeof(body)) != 0 ||
                       std::memcmp(&m_color, &color, sizeof(color)) != 0;

    if (stale) {
        m_cache = nullptr;
        m_body = body;
        m_radius = radius;
        m_color = color;
        m_dpi = dpiX;

        D2D1_SIZE_F const surface = context->GetSize();
        float const bodyWidth = width(body);
        float const bodyHeight = height(body);
        if (bodyWidth <= 0.0f || bodyHeight <= 0.0f) {
            return;
        }

        auto toPixels = [dpiX](float dip) {
            return static_cast<UINT32>(std::ceil(std::max(1.0f, dip) * dpiX / 96.0f));
        };
        auto const properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpiX,
            dpiY);

        // The cache is built on a context of its own. The caller's context is inside
        // BeginDraw for the current frame, and Direct2D forbids retargeting a context between
        // BeginDraw and EndDraw; bitmaps belong to the device, not to the context, so the
        // result is still drawable by the caller.
        com_ptr<ID2D1DeviceContext> builder;
        try {
            builder = GraphicsDevice::instance().createContext();
        } catch (winrt::hresult_error const& error) {
            log::warning(L"No context for the window shadow ({}); drawing without one",
                         describeHresult(error.code()));
            return;
        }

        com_ptr<ID2D1Bitmap1> silhouette;
        com_ptr<ID2D1Bitmap1> cache;
        if (FAILED(builder->CreateBitmap(D2D1::SizeU(toPixels(bodyWidth), toPixels(bodyHeight)),
                                         nullptr, 0, &properties, silhouette.put())) ||
            FAILED(builder->CreateBitmap(D2D1::SizeU(toPixels(surface.width),
                                                     toPixels(surface.height)),
                                         nullptr, 0, &properties, cache.put()))) {
            log::warning(L"Could not allocate the shadow bitmaps; drawing without a shadow");
            return;
        }

        com_ptr<ID2D1SolidColorBrush> black;
        builder->SetTarget(silhouette.get());
        builder->BeginDraw();
        builder->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        if (SUCCEEDED(builder->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 1.0f),
                                                     black.put()))) {
            auto const shape = D2D1::RectF(0.0f, 0.0f, bodyWidth, bodyHeight);
            builder->FillRoundedRectangle(D2D1::RoundedRect(shape, radius, radius), black.get());
        }
        HRESULT hr = builder->EndDraw();

        if (SUCCEEDED(hr)) {
            com_ptr<ID2D1Effect> shadow;
            hr = builder->CreateEffect(CLSID_D2D1Shadow, shadow.put());
            if (SUCCEEDED(hr)) {
                builder->SetTarget(cache.get());
                builder->BeginDraw();
                builder->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
                shadow->SetInput(0, silhouette.get());

                // Two passes, as Fluent specifies: a tight ambient shadow that defines the
                // edge and an offset key shadow that gives the window its height. Direct2D
                // truncates the Gaussian at three standard deviations, so sigma is clamped to
                // what the transparent margin can actually hold -- an over-large blur ends in
                // a visible hard edge, which is the usual way this technique looks cheap.
                float const margin = std::min({body.left, body.top, surface.width - body.right,
                                               surface.height - body.bottom});
                float const available = std::max(1.0f, margin - Metrics::shadowOffsetY - 1.0f);
                float const sigma = std::min(Metrics::shadowBlur * 0.5f, available / 3.0f);

                struct Pass {
                    float sigma;
                    float offsetY;
                    float alphaScale;
                };
                Pass const passes[]{{1.0f, 0.0f, 0.45f}, {sigma, Metrics::shadowOffsetY, 1.0f}};
                for (auto const& pass : passes) {
                    shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, pass.sigma);
                    shadow->SetValue(
                        D2D1_SHADOW_PROP_COLOR,
                        D2D1::Vector4F(color.r, color.g, color.b, color.a * pass.alphaScale));
                    shadow->SetValue(D2D1_SHADOW_PROP_OPTIMIZATION,
                                     D2D1_SHADOW_OPTIMIZATION_QUALITY);
                    // DrawImage places the input's origin here; the blur legitimately spills
                    // outside it, which is exactly what the transparent margin is for.
                    D2D1_POINT_2F const origin = D2D1::Point2F(body.left, body.top + pass.offsetY);
                    builder->DrawImage(shadow.get(), &origin, nullptr,
                                       D2D1_INTERPOLATION_MODE_LINEAR,
                                       D2D1_COMPOSITE_MODE_SOURCE_OVER);
                }
                hr = builder->EndDraw();
            }
        }

        builder->SetTarget(nullptr);
        if (FAILED(hr)) {
            log::warning(L"Rendering the window shadow failed: {}", describeHresult(hr));
            return;
        }
        m_cache = std::move(cache);
    }

    if (m_cache) {
        context->DrawBitmap(m_cache.get(), nullptr, 1.0f,
                            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    }
}

void drawBatteryGauge(Canvas& canvas, D2D1_RECT_F bounds, GaugeVisual const& visual) {
    float const nub = std::max(canvas.pixel(), width(bounds) * 0.075f);
    float const stroke = std::max(canvas.pixel(), height(bounds) * 0.07f);

    // Snapped up front, because every rectangle below is derived from this one. At tray sizes a
    // half-pixel offset spreads a one-pixel edge across two rows, which reads as blur rather than
    // as a thin line; strokeRounded aligns its own stroke but the fills have no such chance.
    D2D1_RECT_F const body =
        D2D1::RectF(canvas.snap(bounds.left), canvas.snap(bounds.top),
                    canvas.snap(bounds.right - nub - stroke), canvas.snap(bounds.bottom));
    float const radius = std::min(height(body) * 0.28f, width(body) * 0.2f);

    strokeRounded(canvas, body, radius, visual.outline, stroke);

    // The nub is a small rounded cap on the positive terminal, vertically centred.
    float const nubHeight = canvas.snap(height(body) * 0.38f);
    float const nubTop = canvas.snap(body.top + (height(body) - nubHeight) * 0.5f);
    fillRounded(canvas,
                D2D1::RectF(canvas.snap(body.right + stroke * 0.5f), nubTop,
                            canvas.snap(bounds.right), nubTop + nubHeight),
                nub * 0.5f, visual.outline);

    D2D1_RECT_F const well = D2D1::RectF(
        canvas.snap(body.left + stroke * 1.5f), canvas.snap(body.top + stroke * 1.5f),
        canvas.snap(body.right - stroke * 1.5f), canvas.snap(body.bottom - stroke * 1.5f));
    if (width(well) <= 0.0f || height(well) <= 0.0f) {
        return;
    }

    float const wellRadius = std::max(0.0f, radius - stroke);
    fillRounded(canvas, well, wellRadius, visual.track);

    float const fraction = std::clamp(visual.fill, 0.0f, 1.0f);
    if (visual.percent >= 0 && fraction > 0.0f) {
        // Below one radius wide the rounded fill degenerates into a lens, so the fill keeps a
        // minimum width and lets the well's own rounding clip it.
        float const filled = std::max(width(well) * fraction, wellRadius * 2.0f);
        fillRounded(canvas, D2D1::RectF(well.left, well.top, well.left + filled, well.bottom),
                    wellRadius, visual.level);
    }

    if (visual.charging) {
        float const boltHeight = height(well) * 1.05f;
        float const boltWidth = boltHeight * 0.62f;
        float const cx = (well.left + well.right) * 0.5f;
        float const cy = (well.top + well.bottom) * 0.5f;
        auto const box = D2D1::RectF(cx - boltWidth * 0.5f, cy - boltHeight * 0.5f,
                                     cx + boltWidth * 0.5f, cy + boltHeight * 0.5f);
        if (auto bolt = boltGeometry(canvas, box)) {
            // Knocked out in the surface colour first: the bolt straddles the filled and empty
            // halves of the well, and a single flat colour would disappear into one of them.
            if (auto* halo = canvas.brush(visual.surface)) {
                canvas.target().DrawGeometry(bolt.get(), halo, stroke * 1.6f);
            }
            if (auto* fill = canvas.brush(visual.level)) {
                canvas.target().FillGeometry(bolt.get(), fill);
            }
        }
    }
}

void drawRingGauge(Canvas& canvas,
                   D2D1_RECT_F bounds,
                   GaugeVisual const& visual,
                   IDWriteTextFormat* label) {
    float const side = std::min(width(bounds), height(bounds));
    float const stroke = std::max(canvas.pixel(), side * 0.1f);
    float const radius = (side - stroke) * 0.5f;
    D2D1_POINT_2F const center =
        D2D1::Point2F((bounds.left + bounds.right) * 0.5f, (bounds.top + bounds.bottom) * 0.5f);
    if (radius <= 0.0f) {
        return;
    }

    if (auto* brush = canvas.brush(visual.track)) {
        canvas.target().DrawEllipse(D2D1::Ellipse(center, radius, radius), brush, stroke);
    }

    float const fraction = std::clamp(visual.fill, 0.0f, 1.0f);
    if (visual.percent >= 0 && fraction > 0.0f) {
        com_ptr<ID2D1GeometrySink> sink;
        auto arc = beginPath(canvas, sink);
        if (arc) {
            float const sweep = 2.0f * kPi * fraction;
            sink->BeginFigure(D2D1::Point2F(center.x, center.y - radius),
                              D2D1_FIGURE_BEGIN_HOLLOW);
            if (fraction >= 1.0f) {
                // A full circle cannot be one arc segment: the end point would equal the start
                // and Direct2D would draw nothing.
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(center.x, center.y + radius),
                                              D2D1::SizeF(radius, radius), 0.0f,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                              D2D1_ARC_SIZE_SMALL));
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(center.x, center.y - radius),
                                              D2D1::SizeF(radius, radius), 0.0f,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                              D2D1_ARC_SIZE_SMALL));
            } else {
                sink->AddArc(D2D1::ArcSegment(
                    D2D1::Point2F(center.x + radius * std::sin(sweep),
                                  center.y - radius * std::cos(sweep)),
                    D2D1::SizeF(radius, radius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                    fraction > 0.5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
            }
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();

            com_ptr<ID2D1StrokeStyle> caps;
            D2D1_STROKE_STYLE_PROPERTIES const style{
                D2D1_CAP_STYLE_ROUND,  D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                D2D1_LINE_JOIN_ROUND,  10.0f,                D2D1_DASH_STYLE_SOLID,
                0.0f};
            canvas.factory()->CreateStrokeStyle(style, nullptr, 0, caps.put());
            if (auto* brush = canvas.brush(visual.level)) {
                canvas.target().DrawGeometry(arc.get(), brush, stroke, caps.get());
            }
        }
    }

    if (visual.charging) {
        float const boltHeight = side * 0.44f;
        auto const box = D2D1::RectF(center.x - boltHeight * 0.31f, center.y - boltHeight * 0.5f,
                                     center.x + boltHeight * 0.31f, center.y + boltHeight * 0.5f);
        if (auto bolt = boltGeometry(canvas, box)) {
            if (auto* brush = canvas.brush(visual.level)) {
                canvas.target().FillGeometry(bolt.get(), brush);
            }
        }
        return;
    }

    if (label && visual.percent >= 0) {
        auto const number = std::to_wstring(visual.percent);
        drawText(canvas, number, label, bounds, visual.text, Align::Center, Align::Center);
    }
}

float controllerArtWidth(float height) { return std::max(0.0f, height) * kArtAspect; }

void drawControllerArt(Canvas& canvas, D2D1_RECT_F bounds, ControllerArt const& art) {
    ArtSpace space;
    space.unit = std::min(width(bounds) / kArtAspect, height(bounds));
    space.center = D2D1::Point2F((bounds.left + bounds.right) * 0.5f,
                                 (bounds.top + bounds.bottom) * 0.5f);
    if (space.unit <= 0.0f) {
        return;
    }

    auto body = controllerBody(canvas, space);
    if (!body) {
        return;
    }
    if (auto* fill = canvas.brush(art.body)) {
        canvas.target().FillGeometry(body.get(), fill);
    }
    if (auto* edge = canvas.brush(art.bodyEdge)) {
        canvas.target().DrawGeometry(body.get(), edge, std::max(canvas.pixel(),
                                                                space.unit * 0.018f));
    }

    if (space.unit >= kDetailThreshold) {
        drawStick(canvas, space, kLeftStick, art);
        drawStick(canvas, space, kRightStick, art);
        drawDpad(canvas, space, art);
        drawFaceButtons(canvas, space, art);
    }

    if (space.unit >= kGuideThreshold) {
        // The level colour appears here and nowhere else in the portrait, which is what makes
        // the pad and its gauge read as one thing without a legend explaining that they do.
        fillCircle(canvas, space.at(kGuide), kGuideRing * space.unit, art.recess);
        fillCircle(canvas, space.at(kGuide), kGuideRadius * space.unit, art.guide);
    }

    auto const badge = linkGlyph(art.link);
    if (badge.empty() || space.unit < kBadgeThreshold) {
        return;
    }
    // In the notch between the grips: the one hole in the silhouette big enough to hold it,
    // so the badge costs no layout width and covers none of the pad.
    float const radius = kBadgeRadius * space.unit;
    fillCircle(canvas, space.at(kBadge), radius, art.badgeFill);
    drawIcon(canvas, badge, radius * 1.15f,
             space.box(kBadge, kBadgeRadius, kBadgeRadius), art.badge);
}

void drawHistoryChart(Canvas& canvas,
                      D2D1_RECT_F bounds,
                      std::span<ChartPoint const> points,
                      ChartStyle const& style) {
    float const labelWidth = std::max(20.0f, measureText(L"100", style.labelFormat) + 6.0f);
    float const labelHeight = style.labelFormat ? style.labelFormat->GetFontSize() * 1.6f : 0.0f;
    D2D1_RECT_F const plot = D2D1::RectF(bounds.left + labelWidth, bounds.top + 4.0f,
                                         bounds.right - 4.0f, bounds.bottom - labelHeight);
    if (width(plot) <= 0.0f || height(plot) <= 0.0f) {
        return;
    }

    auto toY = [&](double percent) {
        return plot.bottom - static_cast<float>(percent / 100.0) * height(plot);
    };

    for (int percent : {0, 25, 50, 75, 100}) {
        float const y = toY(percent);
        drawDivider(canvas, plot.left, plot.right, y, style.grid);
        if (percent % 50 == 0) {
            drawText(canvas, std::to_wstring(percent), style.labelFormat,
                     D2D1::RectF(bounds.left, y - labelHeight * 0.5f, plot.left - 6.0f,
                                 y + labelHeight * 0.5f),
                     style.label, Align::End, Align::Center);
        }
    }

    if (style.labelFormat && labelHeight > 0.0f) {
        D2D1_RECT_F const axis =
            D2D1::RectF(plot.left, plot.bottom, plot.right, plot.bottom + labelHeight);
        drawText(canvas, style.startLabel, style.labelFormat, axis, style.label, Align::Start);
        drawText(canvas, style.middleLabel, style.labelFormat, axis, style.label, Align::Center);
        drawText(canvas, style.endLabel, style.labelFormat, axis, style.label, Align::End);
    }

    if (points.empty()) {
        return;
    }

    double const firstX = points.front().x;
    double const lastX = points.back().x;
    double const span = lastX - firstX;

    std::vector<D2D1_POINT_2F> screen;
    screen.reserve(points.size());
    for (auto const& point : points) {
        float const x = span > 0.0
                            ? plot.left + static_cast<float>((point.x - firstX) / span) * width(plot)
                            : plot.left;
        screen.push_back(D2D1::Point2F(x, toY(std::clamp(point.y, 0.0, 100.0))));
    }

    if (screen.size() == 1) {
        // One reading is still worth showing: a level line across the plot plus the sample.
        drawDivider(canvas, plot.left, plot.right, screen.front().y, style.line);
        fillCircle(canvas, D2D1::Point2F(plot.left + width(plot) * 0.5f, screen.front().y),
                   std::max(2.0f, canvas.pixel() * 3.0f), style.line);
        return;
    }

    com_ptr<ID2D1GeometrySink> areaSink;
    auto area = beginPath(canvas, areaSink);
    if (area) {
        areaSink->BeginFigure(screen.front(), D2D1_FIGURE_BEGIN_FILLED);
        addSmoothedFigure(*areaSink, screen);
        areaSink->AddLine(D2D1::Point2F(screen.back().x, plot.bottom));
        areaSink->AddLine(D2D1::Point2F(screen.front().x, plot.bottom));
        areaSink->EndFigure(D2D1_FIGURE_END_CLOSED);
        areaSink->Close();

        D2D1_GRADIENT_STOP const stops[]{
            {0.0f, style.fill},
            {1.0f, D2D1::ColorF(style.fill.r, style.fill.g, style.fill.b, 0.0f)}};
        com_ptr<ID2D1GradientStopCollection> collection;
        com_ptr<ID2D1LinearGradientBrush> gradient;
        if (SUCCEEDED(canvas.target().CreateGradientStopCollection(
                stops, ARRAYSIZE(stops), D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                collection.put())) &&
            SUCCEEDED(canvas.target().CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(plot.left, plot.top),
                                                    D2D1::Point2F(plot.left, plot.bottom)),
                collection.get(), gradient.put()))) {
            canvas.target().FillGeometry(area.get(), gradient.get());
        }
    }

    com_ptr<ID2D1GeometrySink> lineSink;
    auto line = beginPath(canvas, lineSink);
    if (line) {
        lineSink->BeginFigure(screen.front(), D2D1_FIGURE_BEGIN_HOLLOW);
        addSmoothedFigure(*lineSink, screen);
        lineSink->EndFigure(D2D1_FIGURE_END_OPEN);
        lineSink->Close();

        com_ptr<ID2D1StrokeStyle> caps;
        D2D1_STROKE_STYLE_PROPERTIES const properties{
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,  D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_ROUND, 10.0f,                 D2D1_DASH_STYLE_SOLID,
            0.0f};
        canvas.factory()->CreateStrokeStyle(properties, nullptr, 0, caps.put());
        if (auto* brush = canvas.brush(style.line)) {
            canvas.target().DrawGeometry(line.get(), brush, 2.0f, caps.get());
        }
    }
}

HICON renderToIcon(SIZE pixelSize, IconPainter const& paint) {
    if (pixelSize.cx <= 0 || pixelSize.cy <= 0 || !paint) {
        return nullptr;
    }
    auto* wic = GraphicsDevice::instance().wic();
    if (!wic) {
        return nullptr;
    }

    // PBGRA, not BGRA: Direct2D renders premultiplied and the shell composites a 32bpp icon
    // with AlphaBlend, which also wants premultiplied, so the pixels copy across untouched.
    com_ptr<IWICBitmap> bitmap;
    if (FAILED(wic->CreateBitmap(static_cast<UINT>(pixelSize.cx), static_cast<UINT>(pixelSize.cy),
                                 GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                 bitmap.put()))) {
        return nullptr;
    }

    // 96 DPI so one DIP is one pixel: the size was already chosen in physical pixels.
    // The feature level must be DEFAULT here, which CreateWicBitmapRenderTarget requires.
    auto const properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f,
        D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);

    com_ptr<ID2D1RenderTarget> target;
    if (FAILED(GraphicsDevice::instance().d2dFactory()->CreateWicBitmapRenderTarget(
            bitmap.get(), properties, target.put()))) {
        return nullptr;
    }
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    target->BeginDraw();
    target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    {
        Canvas canvas(*target);
        paint(canvas, D2D1::SizeF(static_cast<float>(pixelSize.cx),
                                  static_cast<float>(pixelSize.cy)));
    }
    if (HRESULT hr = target->EndDraw(); FAILED(hr)) {
        log::warning(L"Rendering the tray icon failed: {}", describeHresult(hr));
        return nullptr;
    }

    HBITMAP color = copyToDib(*bitmap, pixelSize);
    if (!color) {
        return nullptr;
    }

    // CreateIconIndirect insists on a mask even for a 32bpp alpha icon, and CreateBitmap does
    // not zero its bits: passing null there yields an icon that is randomly invisible.
    std::size_t const maskStride = ((static_cast<std::size_t>(pixelSize.cx) + 31) / 32) * 4;
    std::vector<BYTE> mask(maskStride * static_cast<std::size_t>(pixelSize.cy), 0);
    HBITMAP maskBitmap = CreateBitmap(pixelSize.cx, pixelSize.cy, 1, 1, mask.data());
    if (!maskBitmap) {
        DeleteObject(color);
        return nullptr;
    }

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = maskBitmap;
    info.hbmColor = color;
    HICON icon = CreateIconIndirect(&info);

    // CreateIconIndirect copies both bitmaps, so ours leak two GDI objects per redraw if they
    // are not freed here.
    DeleteObject(maskBitmap);
    DeleteObject(color);
    return icon;
}

namespace {

// The count badge has to survive a 16-pixel icon, where the digit inside it ends up about five
// pixels tall. Below roughly nine pixels of disc the digit stops being a digit, so the badge
// claims that as a floor -- more than half the icon at 16 pixels, which is the price of a
// readable count -- and grows gently after that rather than staying proportional to it.
float badgeDiameter(float iconHeight) { return std::max(10.0f, iconHeight * 0.44f); }

// Windows lets the user pick a bright accent -- the yellows and light greens make white digits
// vanish -- so the badge text is whichever of black and white stands off the disc. The weights
// are the sRGB luminance coefficients used unlinearised, which is close enough for a two-way
// choice and avoids a pow() per digit.
D2D1_COLOR_F readableOn(D2D1_COLOR_F fill) {
    float const luminance = 0.2126f * fill.r + 0.7152f * fill.g + 0.0722f * fill.b;
    return luminance > 0.55f ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f)
                             : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
}

// The number of connected controllers, on a filled accent disc in the bottom-right corner.
//
// Bold rather than the gauges' semibold: at the size a 16-pixel icon allows, semibold counters
// fill in and the digit turns into a blob. Counts that need two digits stretch the disc into a
// stadium instead of shrinking the text, because a badge nobody can read is worse than none.
void drawTrayCountBadge(Canvas& canvas, D2D1_RECT_F bounds, int count, D2D1_COLOR_F fill) {
    float const diameter = badgeDiameter(height(bounds));
    std::wstring const number = std::to_wstring(count);
    IDWriteTextFormat* const format = numberFormat(diameter * 0.84f, DWRITE_FONT_WEIGHT_BOLD);
    float const badgeWidth = std::max(diameter, measureText(number, format) + diameter * 0.3f);

    D2D1_RECT_F const disc =
        D2D1::RectF(canvas.snap(bounds.right - badgeWidth), canvas.snap(bounds.bottom - diameter),
                    bounds.right, bounds.bottom);
    fillRounded(canvas, disc, height(disc) * 0.5f, fill);

    auto* brush = canvas.brush(readableOn(fill));
    if (!format || !brush) {
        return;
    }
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // GDI_CLASSIC rather than the natural metrics the rest of the UI draws with: at the seven
    // pixels a 16-pixel icon leaves, an unhinted digit spreads across three grey rows and a 3
    // stops being tellable from an 8. Gridfitting puts the stems back on whole pixels.
    canvas.target().DrawText(number.c_str(), static_cast<UINT32>(number.size()), format, disc,
                             brush, D2D1_DRAW_TEXT_OPTIONS_NONE,
                             DWRITE_MEASURING_MODE_GDI_CLASSIC);
}

// Nothing connected: a slashed circle, not an empty gauge. An empty gauge would say "flat",
// which is the one thing the icon must not claim when there is no controller to be flat.
void drawTrayEmptyMark(Canvas& canvas, D2D1_RECT_F bounds, D2D1_COLOR_F color) {
    float const side = std::min(width(bounds), height(bounds));
    float const stroke = std::max(canvas.pixel(), side * 0.1f);
    float const radius = (side - stroke) * 0.5f;
    if (radius <= 0.0f) {
        return;
    }
    auto* brush = canvas.brush(color);
    if (!brush) {
        return;
    }

    D2D1_POINT_2F const center =
        D2D1::Point2F((bounds.left + bounds.right) * 0.5f, (bounds.top + bounds.bottom) * 0.5f);
    canvas.target().DrawEllipse(D2D1::Ellipse(center, radius, radius), brush, stroke);

    // The slash meets the circle at 45 degrees, so both ends land on the ring rather than
    // stopping short of it.
    float const reach = radius * 0.70710678f;
    canvas.target().DrawLine(D2D1::Point2F(center.x - reach, center.y + reach),
                             D2D1::Point2F(center.x + reach, center.y - reach), brush, stroke);
}

// A connected controller that reports no level: a dash where the reading would be. Without it
// the gauge draws its empty state and the user reads "flat" instead of "no reading".
void drawTrayUnknownLevel(Canvas& canvas, D2D1_RECT_F bounds, D2D1_COLOR_F color) {
    float const thickness = std::max(canvas.pixel(), height(bounds) * 0.1f);
    float const half = width(bounds) * 0.18f;
    float const cx = (bounds.left + bounds.right) * 0.5f;
    float const cy = (bounds.top + bounds.bottom) * 0.5f;
    fillRounded(
        canvas,
        D2D1::RectF(cx - half, cy - thickness * 0.5f, cx + half, cy + thickness * 0.5f),
        thickness * 0.5f, color);
}

}  // namespace

namespace {

// The gauge's own number, drawn the way the count badge already is.
//
// The shared drawText uses DirectWrite's natural metrics, which is right everywhere the text is
// 12 dip or larger. At the eight or nine pixels a tray icon leaves, an unhinted digit spreads its
// stems across grey rows and the number stops being readable at all; gridfitting puts them back on
// whole pixels. Local to the tray rather than a flag on drawText, because nothing else wants it.
void drawTrayNumber(Canvas& canvas,
                    std::wstring_view number,
                    IDWriteTextFormat* format,
                    D2D1_RECT_F rect,
                    D2D1_COLOR_F color) {
    auto* brush = canvas.brush(color);
    if (!format || !brush) {
        return;
    }
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    canvas.target().DrawText(number.data(), static_cast<UINT32>(number.size()), format, rect, brush,
                             D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_GDI_CLASSIC);
}

}  // namespace

HICON renderTrayIcon(SIZE pixelSize, TrayStyle style, TrayVisual const& visual, bool dark) {
    GaugeVisual local = visual.gauge;
    // The notification area follows the system theme, which the user can set independently of
    // the application's, so these come from the caller's flag and not from the palette.
    local.outline = dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.9f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.9f);
    local.text = local.outline;
    local.track = dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.16f);
    local.surface = dark ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f);

    return renderToIcon(pixelSize, [&](Canvas& canvas, D2D1_SIZE_F size) {
        D2D1_RECT_F const full = D2D1::RectF(0.0f, 0.0f, size.width, size.height);
        if (visual.connected <= 0) {
            drawTrayEmptyMark(canvas, inflate(full, -std::max(1.0f, size.width * 0.09f)),
                              local.outline);
            return;
        }

        // The badge overlaps the gauge's corner rather than being handed a clear square of its
        // own: at 16 pixels a gauge that stepped fully aside would have nothing left to draw.
        // Ceding two fifths of the badge is as much as the gauge can give up and stay legible.
        float const badge = visual.connected > 1 ? badgeDiameter(size.height) : 0.0f;
        D2D1_RECT_F const content =
            D2D1::RectF(0.0f, 0.0f, size.width - badge * 0.4f, size.height - badge * 0.4f);
        bool const unknown = local.percent < 0;

        switch (style) {
            case TrayStyle::Ring: {
                float const box = width(content);
                // The badge sits over the lower corner of the ring, and what is left inside it
                // at these sizes is not enough for a two-digit number: the arc carries the
                // level on its own rather than the two numbers fighting for the same pixels.
                bool const crowded = badge > 0.0f && box < 24.0f;
                drawRingGauge(canvas, inflate(content, -0.5f), local,
                              crowded ? nullptr
                                      : numberFormat(box * (box >= 24.0f ? 0.42f : 0.5f)));
                if (unknown) {
                    drawTrayUnknownLevel(canvas, content, local.text);
                }
                break;
            }
            case TrayStyle::Percentage: {
                std::wstring const number = unknown ? L"?" : std::to_wstring(local.percent);
                float const fontSize = width(content) * (number.size() > 2 ? 0.5f : 0.62f);
                drawTrayNumber(canvas, number, numberFormat(fontSize, DWRITE_FONT_WEIGHT_BOLD),
                               content, unknown ? local.text : local.level);
                break;
            }
            case TrayStyle::Battery: {
                // A 16-pixel battery is only legible lying down, with a margin the shell's own
                // icons leave too -- but a whole-pixel one, and a small one: the mark competes
                // with hand-hinted system glyphs beside it and loses every row it gives away.
                float const inset = std::round(std::max(1.0f, height(content) * 0.08f));
                D2D1_RECT_F const box =
                    D2D1::RectF(std::round(content.left), std::round(content.top) + inset,
                                std::round(content.right), std::round(content.bottom) - inset);
                drawBatteryGauge(canvas, box, local);
                if (unknown) {
                    // Centred on the well, which stops short of the nub on the right.
                    drawTrayUnknownLevel(canvas,
                                         D2D1::RectF(box.left, box.top,
                                                     box.right - width(box) * 0.12f, box.bottom),
                                         local.text);
                }
                break;
            }
        }

        if (badge > 0.0f) {
            drawTrayCountBadge(canvas, full, visual.connected, visual.badge);
        }
    });
}

}  // namespace peek::ui
