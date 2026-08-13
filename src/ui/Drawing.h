#pragma once

#include <d2d1_1.h>
#include <dwrite_3.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/Settings.h"
#include "core/Win.h"
#include "ui/Theme.h"

namespace peek::ui {

// The reusable visual pieces every window in this application is assembled from.
//
// Everything here takes a Canvas, works in DIPs, and reads its colours from the caller rather
// than from the theme, so the same primitive serves the main window, a flyout and the 16-pixel
// tray icon without knowing which it is drawing into.

// A render target plus the small amount of per-frame state drawing needs.
//
// One solid-colour brush is shared by every call: Direct2D solid brushes are cheap to make but
// not free, and a settings page issues a few hundred fills per frame. Construct a Canvas per
// frame around the context from D2DWindow::onPaint and pass it down.
class Canvas {
public:
    explicit Canvas(ID2D1RenderTarget& target);

    Canvas(Canvas const&) = delete;
    Canvas& operator=(Canvas const&) = delete;

    ID2D1RenderTarget& target() const noexcept { return *m_target; }

    // Present for a swap-chain or device context target, null for the WIC bitmap target the
    // tray icon is rasterised on. Effects (and therefore shadows) need this.
    ID2D1DeviceContext* deviceContext() const noexcept { return m_context.get(); }

    ID2D1Factory* factory() const noexcept { return m_factory.get(); }

    // The shared brush, recoloured. The returned pointer is valid until the next call.
    ID2D1SolidColorBrush* brush(D2D1_COLOR_F const& color) const;

    // One physical pixel expressed in DIPs, which is what a hairline stroke should be.
    float pixel() const noexcept { return m_pixel; }

    // Snaps a coordinate to the physical pixel grid, so a 1px line is one crisp pixel instead
    // of two half-lit ones at 125% scale.
    float snap(float dip) const noexcept;

    // Insets a rectangle by half a stroke and snaps it, which is where a stroke of `width`
    // has to sit for its edges to land on pixel boundaries.
    D2D1_RECT_F snapStroke(D2D1_RECT_F rect, float width) const noexcept;

private:
    ID2D1RenderTarget* m_target;
    com_ptr<ID2D1DeviceContext> m_context;
    com_ptr<ID2D1Factory> m_factory;
    mutable com_ptr<ID2D1SolidColorBrush> m_brush;
    float m_pixel = 1.0f;
};

enum class Align { Start, Center, End };

void fillRect(Canvas& canvas, D2D1_RECT_F rect, D2D1_COLOR_F color);
void fillRounded(Canvas& canvas, D2D1_RECT_F rect, float radius, D2D1_COLOR_F color);
void fillCircle(Canvas& canvas, D2D1_POINT_2F center, float radius, D2D1_COLOR_F color);

// `width` <= 0 means a one-physical-pixel hairline.
void strokeRounded(Canvas& canvas,
                   D2D1_RECT_F rect,
                   float radius,
                   D2D1_COLOR_F color,
                   float width = 0.0f);

// Fluent's control border is a two-stop vertical gradient whose darker stop is always the
// bottom pixel, which is what stops a button reading as a flat rectangle. Drawn here as the
// stroke plus a darker bottom edge, which is visually identical and far cheaper.
void strokeControlBorder(Canvas& canvas,
                         D2D1_RECT_F rect,
                         float radius,
                         D2D1_COLOR_F border,
                         D2D1_COLOR_F bottomEdge);

// The keyboard focus visual: a thick outer ring in the high-contrast colour with a thin inner
// ring in the opposite one, so it reads on an accent fill and on a neutral one alike.
void drawFocusRing(Canvas& canvas, D2D1_RECT_F rect, float radius);

void drawDivider(Canvas& canvas, float left, float right, float y, D2D1_COLOR_F color);

// Single-line text, ellipsised if it does not fit. The shared text formats carry no alignment
// of their own, so it is set here on every call.
void drawText(Canvas& canvas,
              std::wstring_view text,
              IDWriteTextFormat* format,
              D2D1_RECT_F rect,
              D2D1_COLOR_F color,
              Align horizontal = Align::Start,
              Align vertical = Align::Center);

// Segoe MDL2 Assets (or Segoe Fluent Icons) glyph, centred in `rect`.
void drawIcon(Canvas& canvas,
              std::wstring_view glyph,
              float sizeDip,
              D2D1_RECT_F rect,
              D2D1_COLOR_F color);

float measureText(std::wstring_view text, IDWriteTextFormat* format);

// A piece of text that owns its DirectWrite layout.
//
// Widgets keep one of these instead of calling DrawText with a format, because a wrapped
// paragraph has to be measured before it can be laid out and rebuilding the layout for that
// on every frame is the difference between a page that lays out in microseconds and one that
// takes milliseconds.
class TextBlock {
public:
    void setText(std::wstring text);
    void setStyle(TypeStyle style);
    void setWrapping(bool wrap);

    std::wstring const& text() const noexcept { return m_text; }
    bool empty() const noexcept { return m_text.empty(); }

    // Lays the text out for `maxWidth` (DIPs) and returns the height it needs.
    float measure(float maxWidth);

    D2D1_SIZE_F size() const noexcept { return m_size; }

    // Draws at the top-left corner of the box last passed to measure().
    void draw(Canvas& canvas, D2D1_POINT_2F origin, D2D1_COLOR_F color);

private:
    void ensureLayout(float maxWidth);
    void relayout();

    std::wstring m_text;
    TypeStyle m_style = TypeStyle::Body;
    bool m_wrap = false;
    float m_maxWidth = 0.0f;
    D2D1_SIZE_F m_size{0.0f, 0.0f};
    com_ptr<IDWriteTextLayout> m_layout;
};

// The window's own drop shadow, drawn into the transparent margin around its rounded body.
//
// The blur is a Direct2D shadow effect run once over an opaque silhouette and kept as a
// bitmap: blurring an 800x560 surface per frame costs about a millisecond, which would defeat
// the point of a render loop that only runs when something moved. The cache is rebuilt when
// the geometry, the DPI or the theme changes.
class WindowShadow {
public:
    WindowShadow();
    ~WindowShadow();

    WindowShadow(WindowShadow const&) = delete;
    WindowShadow& operator=(WindowShadow const&) = delete;

    void draw(Canvas& canvas, D2D1_RECT_F body, float radius, D2D1_COLOR_F color);

private:
    com_ptr<ID2D1Bitmap1> m_cache;
    std::uint64_t m_deviceToken = 0;
    D2D1_RECT_F m_body{};
    float m_radius = 0.0f;
    D2D1_COLOR_F m_color{};
    float m_dpi = 0.0f;
    bool m_unsupported = false;
};

// What the battery visuals need to know. `fill` is separate from `percent` so it can be
// animated: the number changes at once, the bar catches up.
struct GaugeVisual {
    int percent = -1;       // negative when the level is unknown
    float fill = 0.0f;      // 0..1, animated towards percent / 100
    bool charging = false;
    bool approximate = false;
    D2D1_COLOR_F level{};    // Theme::levelColor(percent)
    D2D1_COLOR_F track{};    // the empty part of the gauge
    D2D1_COLOR_F outline{};
    D2D1_COLOR_F text{};
    // What is behind the gauge. The charging bolt is knocked out in this colour so that it
    // reads over the filled part and the empty part alike.
    D2D1_COLOR_F surface{};
};

// A rounded battery body with a nub on the right, an animated fill and, when charging, a bolt
// drawn as geometry so it takes the level colour and scales cleanly to 16 pixels.
void drawBatteryGauge(Canvas& canvas, D2D1_RECT_F bounds, GaugeVisual const& visual);

// An arc sweeping clockwise from twelve o'clock, with the percentage centred inside it.
// `label` may be null, in which case only the ring is drawn.
void drawRingGauge(Canvas& canvas,
                   D2D1_RECT_F bounds,
                   GaugeVisual const& visual,
                   IDWriteTextFormat* label);

// How a pad reached the machine, for the badge in the corner of its portrait. An enum of its
// own rather than the battery layer's types, so Drawing.h stays a leaf of the include graph.
enum class ControllerLink { None, Usb, Wireless, Bluetooth };

// The colours a controller portrait is drawn from. As everywhere else here they come from the
// caller, so the same geometry serves a card today and a flyout tomorrow.
struct ControllerArt {
    D2D1_COLOR_F body{};       // the shell
    D2D1_COLOR_F bodyEdge{};   // its outline, and the seam under the bumpers
    D2D1_COLOR_F recess{};     // the wells the sticks and the d-pad sit in
    D2D1_COLOR_F detail{};     // the sticks, the d-pad and the face buttons
    D2D1_COLOR_F guide{};      // the guide button: the battery level colour
    D2D1_COLOR_F badge{};      // the connection glyph
    D2D1_COLOR_F badgeFill{};
    ControllerLink link = ControllerLink::None;
};

// A pad seen from the front, fitted into `bounds` and centred there.
//
// Details are dropped as the box shrinks -- first the badge, then the sticks, the d-pad and
// the face buttons, and last the guide button -- because below roughly fifty DIPs of height
// they stop reading as controls and start reading as dirt on the shell.
void drawControllerArt(Canvas& canvas, D2D1_RECT_F bounds, ControllerArt const& art);

// Width the portrait occupies when it is given `height` DIPs. The artwork keeps its aspect
// ratio, so a caller reserving a column needs this to place whatever comes after it.
float controllerArtWidth(float height);

struct ChartPoint {
    double x;  // any ascending unit; the chart only cares about the range
    double y;  // percent, 0..100
};

struct ChartStyle {
    D2D1_COLOR_F line{};
    D2D1_COLOR_F fill{};    // the gradient below the line fades this to transparent
    D2D1_COLOR_F grid{};
    D2D1_COLOR_F label{};
    IDWriteTextFormat* labelFormat = nullptr;
    std::wstring startLabel;
    std::wstring middleLabel;
    std::wstring endLabel;
};

// Battery history: grid, axis labels, a monotone-smoothed path and a gradient below it.
// Copes with one point (a level line) and with none (the empty grid).
void drawHistoryChart(Canvas& canvas,
                      D2D1_RECT_F bounds,
                      std::span<ChartPoint const> points,
                      ChartStyle const& style);

// Rasterises `paint` into a WIC bitmap and converts it to an icon the caller owns and must
// DestroyIcon. The canvas is at 96 DPI so one DIP is one pixel.
using IconPainter = std::function<void(Canvas&, D2D1_SIZE_F)>;
HICON renderToIcon(SIZE pixelSize, IconPainter const& paint);

// What the notification-area icon says about the whole set of connected controllers, as
// opposed to the single gauge it draws.
struct TrayVisual {
    // The controller closest to running out. Its `percent` is negative when no connected
    // controller reports a battery at all, and the gauge then shows no reading rather than
    // an empty one.
    GaugeVisual gauge;
    // How many controllers are connected. None draws the "nothing connected" mark instead of
    // a gauge; more than one adds the count badge.
    int connected = 0;
    // The badge disc, in the system accent. The digits on it are picked for contrast against
    // this colour rather than supplied, because a light accent needs dark digits.
    D2D1_COLOR_F badge{};
};

// The notification-area icon in the style the user picked. Rendered at the exact pixel size
// the shell asked for rather than scaled from one bitmap, which is what keeps it crisp at
// 125% and 150%.
HICON renderTrayIcon(SIZE pixelSize, TrayStyle style, TrayVisual const& visual, bool dark);

}  // namespace peek::ui
