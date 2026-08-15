#include "ui/Widgets.h"

#include "ui/Quantise.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace peek::ui {
namespace {

constexpr float kButtonPaddingX = 11.0f;
constexpr float kButtonMinWidth = 64.0f;
constexpr float kButtonGlyphSize = 14.0f;

constexpr float kCardPadding = Metrics::settingsCardPadding;
constexpr float kCardIconLead = 2.0f;
constexpr float kCardIconSize = 20.0f;
constexpr float kCardIconGap = 20.0f;
constexpr float kCardContentMinWidth = 120.0f;
constexpr float kCardColumnGap = 12.0f;
constexpr float kCardWrapThreshold = 476.0f;
constexpr float kCardWrapThresholdNoIcon = 286.0f;
constexpr float kCardVerticalSpacing = 8.0f;

constexpr float kToggleTrackWidth = 40.0f;
constexpr float kToggleTrackHeight = 20.0f;
constexpr float kToggleKnobInset = 4.0f;
constexpr float kToggleTravel = 20.0f;

constexpr float kSliderTrackHeight = 4.0f;
constexpr float kSliderThumbRadius = 9.0f;
constexpr float kSliderInnerRadius = 6.0f;
constexpr float kSliderEdgeMargin = 14.0f;
constexpr float kSliderReadoutWidth = 56.0f;
constexpr float kSliderWidth = 200.0f;

constexpr float kComboItemHeight = 32.0f;
constexpr float kComboMinWidth = 120.0f;
constexpr float kChevronGlyphSize = 12.0f;
constexpr float kComboPopupPadding = 4.0f;

constexpr float kExpanderRowInset = 58.0f;
constexpr float kExpanderRowMinHeight = 52.0f;
constexpr float kExpanderChevronColumn = 40.0f;

constexpr float kScrollbarGutter = 12.0f;
constexpr float kScrollbarRailWidth = 2.0f;
constexpr float kScrollbarThumbWidth = 6.0f;
constexpr float kScrollbarMinimumThumb = 32.0f;
constexpr float kWheelStep = 56.0f;

constexpr float kNavIconSize = 16.0f;
constexpr float kNavItemInset = 4.0f;

// WinUI moves the navigation pill over 600 ms, far slower than any other transition in the
// system; it is the one animation the eye is meant to follow.
constexpr std::chrono::milliseconds kPillDuration{600};

bool contains(D2D1_RECT_F const& rect, D2D1_POINT_2F point) {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top &&
           point.y < rect.bottom;
}

float widthOf(D2D1_RECT_F const& rect) { return rect.right - rect.left; }
float heightOf(D2D1_RECT_F const& rect) { return rect.bottom - rect.top; }

D2D1_COLOR_F mix(D2D1_COLOR_F const& from, D2D1_COLOR_F const& to, float t) {
    float const k = std::clamp(t, 0.0f, 1.0f);
    return D2D1_COLOR_F{from.r + (to.r - from.r) * k, from.g + (to.g - from.g) * k,
                        from.b + (to.b - from.b) * k, from.a + (to.a - from.a) * k};
}

D2D1_COLOR_F roleColor(TextRole role, bool enabled) {
    auto const& palette = theme().colors();
    if (!enabled) {
        return palette.textDisabled;
    }
    switch (role) {
        case TextRole::Secondary: return palette.textSecondary;
        case TextRole::Tertiary: return palette.textTertiary;
        case TextRole::Accent: return palette.accent;
        case TextRole::OnAccent: return palette.textOnAccent;
        case TextRole::Primary: break;
    }
    return palette.textPrimary;
}

D2D1_RECT_F rectOf(float x, float y, float w, float h) {
    return D2D1_RECT_F{x, y, x + w, y + h};
}

D2D1_RECT_F centredRow(D2D1_RECT_F const& bounds, float h) {
    float const top = bounds.top + (heightOf(bounds) - h) * 0.5f;
    return D2D1_RECT_F{bounds.left, top, bounds.right, top + h};
}

bool activationKey(WPARAM key) { return key == VK_SPACE || key == VK_RETURN; }

// AccentFillColorDisabled, which is a neutral wash rather than a faded accent.
D2D1_COLOR_F accentDisabled() {
    return theme().isDark() ? D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 0x28 / 255.0f}
                            : D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0x37 / 255.0f};
}

}  // namespace

void Widget::arrange(D2D1_RECT_F bounds) { m_bounds = bounds; }

bool Widget::tick(std::chrono::steady_clock::time_point now) {
    bool running = m_hoverFade.tick(now);
    running |= m_pressFade.tick(now);
    running |= m_focusFade.tick(now);
    return running;
}

Widget* Widget::hitTest(D2D1_POINT_2F point) {
    return m_visible && contains(m_bounds, point) ? this : nullptr;
}

void Widget::collectFocusable(std::vector<Widget*>& out) {
    if (m_visible && m_enabled && focusable()) {
        out.push_back(this);
    }
}

void Widget::attach(WidgetHost* host) { m_host = host; }

void Widget::invalidate() {
    if (m_host) {
        m_host->invalidate();
    }
}

void Widget::invalidateLayout() {
    if (m_host) {
        m_host->invalidateLayout();
    }
}

void Widget::setEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (!enabled) {
        setHovered(false);
        setPressed(false);
    }
    invalidate();
}

void Widget::setVisible(bool visible) {
    if (m_visible == visible) {
        return;
    }
    m_visible = visible;
    invalidateLayout();
    invalidate();
}

void Widget::setHovered(bool hovered) {
    hovered = hovered && m_enabled;
    if (m_hovered == hovered) {
        return;
    }
    m_hovered = hovered;
    m_hoverFade.animateTo(hovered ? 1.0f : 0.0f, kDurationFast, Easing::Entrance);
    invalidate();
}

void Widget::setPressed(bool pressed) {
    if (m_pressed == pressed) {
        return;
    }
    m_pressed = pressed;
    m_pressFade.animateTo(pressed ? 1.0f : 0.0f, kDurationFast, Easing::Entrance);
    invalidate();
}

void Widget::setFocused(bool focused) {
    if (m_focused == focused) {
        return;
    }
    m_focused = focused;
    m_focusFade.animateTo(focused ? 1.0f : 0.0f, kDurationNormal, Easing::Entrance);
    invalidate();
}

Widget* Container::add(std::unique_ptr<Widget> child) {
    Widget* raw = child.get();
    raw->setParent(this);
    raw->attach(host());
    m_children.push_back(std::move(child));
    invalidateLayout();
    return raw;
}

void Container::clear() {
    m_children.clear();
    invalidateLayout();
    invalidate();
}

void Container::paint(Canvas& canvas) {
    for (auto const& child : m_children) {
        if (child->visible()) {
            child->paint(canvas);
        }
    }
}

bool Container::tick(std::chrono::steady_clock::time_point now) {
    bool running = Widget::tick(now);
    for (auto const& child : m_children) {
        running |= child->tick(now);
    }
    return running;
}

Widget* Container::hitTest(D2D1_POINT_2F point) {
    if (!visible()) {
        return nullptr;
    }
    // Reverse order: the last child painted is the one on top.
    for (auto child = m_children.rbegin(); child != m_children.rend(); ++child) {
        if (Widget* hit = (*child)->hitTest(point)) {
            return hit;
        }
    }
    return nullptr;
}

void Container::collectFocusable(std::vector<Widget*>& out) {
    if (!visible()) {
        return;
    }
    Widget::collectFocusable(out);
    for (auto const& child : m_children) {
        child->collectFocusable(out);
    }
}

void Container::attach(WidgetHost* host) {
    Widget::attach(host);
    for (auto const& child : m_children) {
        child->attach(host);
    }
}

float StackPanel::measure(float availableWidth) {
    float const inner = std::max(0.0f, availableWidth - m_padding.left - m_padding.right);
    float total = m_padding.top + m_padding.bottom;
    bool first = true;
    for (auto const& child : m_children) {
        if (!child->visible()) {
            continue;
        }
        if (!first) {
            total += m_spacing;
        }
        first = false;
        total += child->measure(inner);
    }
    return total;
}

void StackPanel::arrange(D2D1_RECT_F bounds) {
    Widget::arrange(bounds);
    float const left = bounds.left + m_padding.left;
    float const inner = std::max(0.0f, widthOf(bounds) - m_padding.left - m_padding.right);
    float y = bounds.top + m_padding.top;
    bool first = true;
    for (auto const& child : m_children) {
        if (!child->visible()) {
            continue;
        }
        if (!first) {
            y += m_spacing;
        }
        first = false;
        float const h = child->measure(inner);
        child->arrange(rectOf(left, y, inner, h));
        y += h;
    }
}

Label::Label(std::wstring text, TypeStyle style, TextRole role) : m_role(role) {
    m_block.setStyle(style);
    m_block.setText(std::move(text));
}

void Label::setText(std::wstring text) {
    m_block.setText(std::move(text));
    invalidateLayout();
}

void Label::setStyle(TypeStyle style) {
    m_block.setStyle(style);
    invalidateLayout();
}

void Label::setWrapping(bool wrap) {
    m_block.setWrapping(wrap);
    invalidateLayout();
}

float Label::measure(float availableWidth) {
    float const inner = std::max(0.0f, availableWidth - m_margin.left - m_margin.right);
    return m_block.measure(inner) + m_margin.top + m_margin.bottom;
}

void Label::paint(Canvas& canvas) {
    m_block.draw(canvas, D2D1::Point2F(m_bounds.left + m_margin.left, m_bounds.top + m_margin.top),
                 roleColor(m_role, enabled()));
}

Button::Button(std::wstring text, Handler onClick, bool accent)
    : m_onClick(std::move(onClick)), m_accent(accent) {
    m_label.setStyle(TypeStyle::Body);
    m_label.setText(std::move(text));
}

void Button::setText(std::wstring text) {
    m_label.setText(std::move(text));
    invalidateLayout();
}

float Button::desiredWidth() const {
    // Measured from the text rather than from the cached layout: a settings card asks for the
    // column width before it has given the button a chance to lay itself out.
    float const glyph = m_glyph.empty() ? 0.0f : kButtonGlyphSize + 8.0f;
    float const text = measureText(m_label.text(), theme().textFormat(TypeStyle::Body));
    return std::max({kButtonMinWidth, m_minimumWidth, kButtonPaddingX * 2.0f + glyph + text});
}

float Button::measure(float availableWidth) {
    m_label.measure(std::max(0.0f, availableWidth - kButtonPaddingX * 2.0f));
    return Metrics::controlHeight;
}

void Button::paint(Canvas& canvas) {
    auto const& palette = theme().colors();
    float const hover = m_hoverFade.value();
    float const press = m_pressFade.value();

    D2D1_COLOR_F fill{};
    D2D1_COLOR_F border{};
    D2D1_COLOR_F bottomEdge{};
    D2D1_COLOR_F text{};

    if (!enabled()) {
        fill = m_accent ? accentDisabled() : palette.controlFillDisabled;
        border = palette.controlStroke;
        bottomEdge = border;
        text = palette.textDisabled;
    } else if (m_accent) {
        fill = mix(mix(palette.accent, palette.accentSecondary, hover), palette.accentTertiary,
                   press);
        border = palette.controlStroke;
        // The accent button's elevation border is inverted: the dark stop sits at the bottom
        // of a light fill.
        bottomEdge = D2D1::ColorF(0.0f, 0.0f, 0.0f, theme().isDark() ? 0.14f : 0.4f);
        text = palette.textOnAccent;
    } else {
        fill = mix(mix(palette.controlFill, palette.controlFillSecondary, hover),
                   palette.controlFillTertiary, press);
        border = palette.controlStroke;
        bottomEdge = mix(palette.controlStrokeSecondary, palette.controlStroke, press);
        text = mix(palette.textPrimary, palette.textSecondary, press);
    }

    fillRounded(canvas, m_bounds, Metrics::controlCornerRadius, fill);
    strokeControlBorder(canvas, m_bounds, Metrics::controlCornerRadius, border, bottomEdge);

    float const glyphWidth = m_glyph.empty() ? 0.0f : kButtonGlyphSize + 8.0f;
    float const contentWidth = glyphWidth + m_label.size().width;
    float x = m_bounds.left + (widthOf(m_bounds) - contentWidth) * 0.5f;
    if (!m_glyph.empty()) {
        drawIcon(canvas, m_glyph, kButtonGlyphSize,
                 rectOf(x, m_bounds.top, kButtonGlyphSize, heightOf(m_bounds)), text);
        x += glyphWidth;
    }
    m_label.draw(canvas,
                 D2D1::Point2F(x, m_bounds.top + (heightOf(m_bounds) - m_label.size().height) * 0.5f),
                 text);

    if (focused() && host() && host()->focusVisible()) {
        drawFocusRing(canvas, m_bounds, Metrics::controlCornerRadius);
    }
}

void Button::onPointerUp(D2D1_POINT_2F, bool insideBounds) {
    if (insideBounds) {
        fire();
    }
}

bool Button::onKey(WPARAM key) {
    if (!activationKey(key)) {
        return false;
    }
    fire();
    return true;
}

void Button::fire() {
    if (enabled() && m_onClick) {
        m_onClick();
    }
}

ToggleSwitch::ToggleSwitch(bool value, Handler onChanged)
    : m_value(value), m_onChanged(std::move(onChanged)) {
    m_knob.snapTo(value ? 1.0f : 0.0f);
}

void ToggleSwitch::setValue(bool value, bool animate) {
    if (m_value == value) {
        return;
    }
    m_value = value;
    if (animate) {
        m_knob.animateTo(value ? 1.0f : 0.0f, kDurationNormal, Easing::Entrance);
    } else {
        m_knob.snapTo(value ? 1.0f : 0.0f);
    }
    invalidate();
}

float ToggleSwitch::desiredWidth() const { return kToggleTrackWidth; }

float ToggleSwitch::measure(float) { return Metrics::controlHeight; }

bool ToggleSwitch::tick(std::chrono::steady_clock::time_point now) {
    bool running = Widget::tick(now);
    running |= m_knob.tick(now);
    return running;
}

void ToggleSwitch::paint(Canvas& canvas) {
    auto const& palette = theme().colors();
    float const hover = m_hoverFade.value();
    float const press = m_pressFade.value();
    float const on = m_knob.value();

    D2D1_RECT_F const track = centredRow(
        rectOf(m_bounds.left, m_bounds.top, kToggleTrackWidth, heightOf(m_bounds)),
        kToggleTrackHeight);
    float const radius = kToggleTrackHeight * 0.5f;

    // The off state uses the inverted-polarity ControlAlt set, which is the one place in the
    // palette where light theme uses black alphas and dark theme white ones.
    bool const dark = theme().isDark();
    D2D1_COLOR_F const offRest = dark ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f)
                                      : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.024f);
    D2D1_COLOR_F const offHover = dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.043f)
                                       : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.06f);
    D2D1_COLOR_F const offPress = dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f)
                                       : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.094f);

    D2D1_COLOR_F offFill = mix(mix(offRest, offHover, hover), offPress, press);
    D2D1_COLOR_F onFill =
        mix(mix(palette.accent, palette.accentSecondary, hover), palette.accentTertiary, press);
    D2D1_COLOR_F knob = mix(palette.textSecondary, palette.textOnAccent, on);

    if (!enabled()) {
        offFill = palette.controlFillDisabled;
        onFill = accentDisabled();
        knob = palette.textDisabled;
    }

    fillRounded(canvas, track, radius, mix(offFill, onFill, on));
    if (on < 1.0f) {
        // The outer stroke belongs to the off state only; WinUI drops it entirely when on.
        D2D1_COLOR_F stroke = enabled() ? palette.controlStrongStroke : palette.controlStroke;
        stroke.a *= 1.0f - on;
        strokeRounded(canvas, track, radius, stroke);
    }

    // 12 at rest, 14 hovered, 17 wide when pressed: the knob squashes in the travel direction.
    float const knobWidth = 12.0f + 2.0f * hover + 3.0f * press;
    float const knobHeight = 12.0f + 2.0f * std::max(hover, press);
    float const centreX = track.left + kToggleKnobInset + 6.0f + on * kToggleTravel;
    float const clampedX = std::clamp(centreX, track.left + knobWidth * 0.5f + 2.0f,
                                      track.right - knobWidth * 0.5f - 2.0f);
    float const centreY = (track.top + track.bottom) * 0.5f;
    fillRounded(canvas,
                D2D1::RectF(clampedX - knobWidth * 0.5f, centreY - knobHeight * 0.5f,
                            clampedX + knobWidth * 0.5f, centreY + knobHeight * 0.5f),
                knobHeight * 0.5f, knob);

    if (focused() && host() && host()->focusVisible()) {
        drawFocusRing(canvas, track, radius);
    }
}

void ToggleSwitch::onPointerUp(D2D1_POINT_2F, bool insideBounds) {
    if (insideBounds) {
        toggle();
    }
}

bool ToggleSwitch::onKey(WPARAM key) {
    if (!activationKey(key)) {
        return false;
    }
    toggle();
    return true;
}

void ToggleSwitch::toggle() {
    if (!enabled()) {
        return;
    }
    setValue(!m_value);
    if (m_onChanged) {
        m_onChanged(m_value);
    }
}

Slider::Slider(double minimum, double maximum, float value, Handler onChanged)
    : m_minimum(minimum),
      m_maximum(std::max(maximum, minimum + 1e-6)),
      m_value(static_cast<float>(std::clamp(static_cast<double>(value), minimum, maximum))),
      m_onChanged(std::move(onChanged)) {
    m_position.snapTo(static_cast<float>((m_value - m_minimum) / (m_maximum - m_minimum)));
    m_readout.setStyle(TypeStyle::Body);
}

void Slider::setFormatter(std::function<std::wstring(float)> formatter) {
    m_formatter = std::move(formatter);
    if (m_formatter) {
        m_readout.setText(m_formatter(m_value));
    }
    invalidate();
}

void Slider::setValue(float value, bool animate) {
    float const clamped =
        static_cast<float>(std::clamp(static_cast<double>(value), m_minimum, m_maximum));
    if (m_value == clamped) {
        return;
    }
    m_value = clamped;
    float const fraction = static_cast<float>((m_value - m_minimum) / (m_maximum - m_minimum));
    if (animate) {
        m_position.animateTo(fraction, kDurationNormal, Easing::Entrance);
    } else {
        m_position.snapTo(fraction);
    }
    if (m_formatter) {
        m_readout.setText(m_formatter(m_value));
    }
    invalidate();
}

float Slider::desiredWidth() const {
    return kSliderWidth + (m_formatter ? kSliderReadoutWidth : 0.0f);
}

float Slider::measure(float availableWidth) {
    if (m_formatter) {
        m_readout.measure(kSliderReadoutWidth);
    }
    (void)availableWidth;
    return Metrics::controlHeight;
}

bool Slider::tick(std::chrono::steady_clock::time_point now) {
    bool running = Widget::tick(now);
    running |= m_position.tick(now);
    return running;
}

D2D1_RECT_F Slider::trackRect() const {
    float const right = m_bounds.right - (m_formatter ? kSliderReadoutWidth : 0.0f);
    float const centre = (m_bounds.top + m_bounds.bottom) * 0.5f;
    return D2D1::RectF(m_bounds.left + kSliderEdgeMargin, centre - kSliderTrackHeight * 0.5f,
                       right - kSliderEdgeMargin, centre + kSliderTrackHeight * 0.5f);
}

void Slider::paint(Canvas& canvas) {
    auto const& palette = theme().colors();
    float const hover = m_hoverFade.value();
    float const press = m_pressFade.value();

    D2D1_RECT_F const track = trackRect();
    D2D1_COLOR_F const rail = enabled() ? palette.controlStrongFill : palette.controlStroke;
    D2D1_COLOR_F fill = mix(mix(palette.accent, palette.accentSecondary, hover),
                            palette.accentTertiary, press);
    if (!enabled()) {
        fill = palette.controlStrongFill;
    }

    fillRounded(canvas, track, kSliderTrackHeight * 0.5f, rail);

    float const fraction = std::clamp(m_position.value(), 0.0f, 1.0f);
    float const thumbX = track.left + fraction * widthOf(track);
    fillRounded(canvas, D2D1::RectF(track.left, track.top, thumbX, track.bottom),
                kSliderTrackHeight * 0.5f, fill);

    float const centreY = (track.top + track.bottom) * 0.5f;
    D2D1_POINT_2F const centre = D2D1::Point2F(thumbX, centreY);
    fillCircle(canvas, centre, kSliderThumbRadius,
               enabled() ? D2D1::ColorF(theme().isDark() ? 0x454545 : 0xFFFFFF, 1.0f)
                         : palette.controlFillDisabled);
    if (auto* brush = canvas.brush(palette.controlStroke)) {
        canvas.target().DrawEllipse(D2D1::Ellipse(centre, kSliderThumbRadius, kSliderThumbRadius),
                                    brush, canvas.pixel());
    }

    // The inner dot is 0.86 of its 12 dip box at rest, 1.167 hovered and 0.71 pressed.
    float const scale = 0.86f + 0.307f * hover - 0.457f * press;
    fillCircle(canvas, centre, kSliderInnerRadius * std::max(0.2f, scale), fill);

    if (m_formatter) {
        m_readout.draw(canvas,
                       D2D1::Point2F(m_bounds.right - kSliderReadoutWidth + 12.0f,
                                     centreY - m_readout.size().height * 0.5f),
                       roleColor(TextRole::Secondary, enabled()));
    }

    if (focused() && host() && host()->focusVisible()) {
        drawFocusRing(canvas, D2D1::RectF(track.left - kSliderThumbRadius - 2.0f, m_bounds.top,
                                          track.right + kSliderThumbRadius + 2.0f, m_bounds.bottom),
                      Metrics::controlCornerRadius);
    }
}

void Slider::setFromPoint(D2D1_POINT_2F point) {
    D2D1_RECT_F const track = trackRect();
    float const span = widthOf(track);
    if (span <= 0.0f) {
        return;
    }
    double const fraction = std::clamp((point.x - track.left) / span, 0.0f, 1.0f);
    double const value = m_minimum + fraction * (m_maximum - m_minimum);
    // Dragging must track the pointer exactly, so the position snaps rather than eases.
    setValue(snapToStep(value, m_minimum, m_maximum, m_step), false);
    commit(m_value);
}

void Slider::commit(float value) {
    if (m_onChanged) {
        m_onChanged(value);
    }
}

void Slider::onPointerDown(D2D1_POINT_2F point) {
    if (enabled()) {
        setFromPoint(point);
    }
}

void Slider::onPointerMove(D2D1_POINT_2F point) {
    if (enabled() && pressed()) {
        setFromPoint(point);
    }
}

void Slider::onPointerUp(D2D1_POINT_2F point, bool) {
    if (enabled()) {
        setFromPoint(point);
    }
}

bool Slider::onKey(WPARAM key) {
    if (!enabled()) {
        return false;
    }
    double const step = m_step > 0.0 ? m_step : (m_maximum - m_minimum) / 20.0;
    // Every arrow goes back through the grid rather than adding to wherever the value already
    // was. Stepping repeatedly from a value that is a rounding error off the grid walks
    // further off it with each press, and the settings file keeps every one of those errors.
    auto const stepped = [this](double by) {
        return snapToStep(static_cast<double>(m_value) + by, m_minimum, m_maximum, m_step);
    };
    switch (key) {
        case VK_LEFT:
        case VK_DOWN: setValue(stepped(-step)); break;
        case VK_RIGHT:
        case VK_UP: setValue(stepped(step)); break;
        case VK_PRIOR: setValue(stepped(step * 5.0)); break;
        case VK_NEXT: setValue(stepped(-step * 5.0)); break;
        case VK_HOME: setValue(static_cast<float>(m_minimum)); break;
        case VK_END: setValue(static_cast<float>(m_maximum)); break;
        default: return false;
    }
    commit(m_value);
    return true;
}

ComboPopup::ComboPopup(std::vector<std::wstring> const& items, int selected, Handler onPick)
    : m_selected(selected), m_highlighted(selected), m_onPick(std::move(onPick)) {
    m_items.resize(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        m_items[i].setStyle(TypeStyle::Body);
        m_items[i].setText(items[i]);
    }
}

float ComboPopup::measure(float availableWidth) {
    for (auto& item : m_items) {
        item.measure(std::max(0.0f, availableWidth - 2.0f * kButtonPaddingX));
    }
    return kComboPopupPadding * 2.0f + static_cast<float>(m_items.size()) * kComboItemHeight;
}

int ComboPopup::indexAt(D2D1_POINT_2F point) const {
    if (!contains(m_bounds, point)) {
        return -1;
    }
    int const index =
        static_cast<int>((point.y - m_bounds.top - kComboPopupPadding) / kComboItemHeight);
    return index >= 0 && index < static_cast<int>(m_items.size()) ? index : -1;
}

void ComboPopup::paint(Canvas& canvas) {
    auto const& palette = theme().colors();
    fillRounded(canvas, m_bounds, Metrics::overlayCornerRadius, palette.flyoutBackground);
    strokeRounded(canvas, m_bounds, Metrics::overlayCornerRadius, palette.surfaceStroke);

    for (std::size_t i = 0; i < m_items.size(); ++i) {
        D2D1_RECT_F const row =
            rectOf(m_bounds.left + kComboPopupPadding,
                   m_bounds.top + kComboPopupPadding + static_cast<float>(i) * kComboItemHeight,
                   widthOf(m_bounds) - kComboPopupPadding * 2.0f, kComboItemHeight);

        bool const selected = static_cast<int>(i) == m_selected;
        bool const highlighted = static_cast<int>(i) == m_highlighted;
        if (selected || highlighted) {
            fillRounded(canvas, row, Metrics::controlCornerRadius,
                        highlighted ? palette.subtleFillTertiary : palette.subtleFillSecondary);
        }
        if (selected) {
            float const centre = (row.top + row.bottom) * 0.5f;
            fillRounded(canvas,
                        D2D1::RectF(row.left + 1.0f, centre - Metrics::navIndicatorLength * 0.5f,
                                    row.left + 1.0f + Metrics::navIndicatorWidth,
                                    centre + Metrics::navIndicatorLength * 0.5f),
                        Metrics::navIndicatorWidth * 0.5f, palette.accent);
        }
        m_items[i].draw(canvas,
                        D2D1::Point2F(row.left + kButtonPaddingX,
                                      (row.top + row.bottom - m_items[i].size().height) * 0.5f),
                        palette.textPrimary);
    }
}

void ComboPopup::onPointerMove(D2D1_POINT_2F point) {
    int const index = indexAt(point);
    if (index >= 0 && index != m_highlighted) {
        m_highlighted = index;
        invalidate();
    }
}

void ComboPopup::onPointerUp(D2D1_POINT_2F point, bool) {
    int const index = indexAt(point);
    if (index >= 0 && m_onPick) {
        m_onPick(index);
    }
}

bool ComboPopup::onKey(WPARAM key) {
    int const count = static_cast<int>(m_items.size());
    switch (key) {
        case VK_UP:
            m_highlighted = std::max(0, m_highlighted - 1);
            invalidate();
            return true;
        case VK_DOWN:
            m_highlighted = std::min(count - 1, m_highlighted + 1);
            invalidate();
            return true;
        case VK_RETURN:
        case VK_SPACE:
            if (m_onPick) {
                m_onPick(m_highlighted);
            }
            return true;
        default:
            return false;
    }
}

ComboBox::ComboBox(std::vector<std::wstring> items, int selected, Handler onChanged)
    : m_items(std::move(items)), m_onChanged(std::move(onChanged)) {
    m_label.setStyle(TypeStyle::Body);
    setSelected(selected);
}

void ComboBox::attach(WidgetHost* host) {
    Widget::attach(host);
    if (m_popup) {
        m_popup->attach(host);
    }
}

void ComboBox::setSelected(int index) {
    m_selected = std::clamp(index, 0, std::max(0, static_cast<int>(m_items.size()) - 1));
    m_label.setText(m_items.empty() ? std::wstring{} : m_items[static_cast<std::size_t>(m_selected)]);
    invalidate();
}

float ComboBox::desiredWidth() const {
    float widest = 0.0f;
    if (auto* format = theme().textFormat(TypeStyle::Body)) {
        for (auto const& item : m_items) {
            widest = std::max(widest, measureText(item, format));
        }
    }
    return std::max(kComboMinWidth, widest + kButtonPaddingX * 2.0f + 30.0f);
}

float ComboBox::measure(float availableWidth) {
    m_label.measure(std::max(0.0f, availableWidth - kButtonPaddingX - 38.0f));
    return Metrics::controlHeight;
}

bool ComboBox::tick(std::chrono::steady_clock::time_point now) {
    bool running = Widget::tick(now);
    running |= m_chevron.tick(now);
    if (m_popup) {
        running |= m_popup->tick(now);
    }
    return running;
}

void ComboBox::paint(Canvas& canvas) {
    auto const& palette = theme().colors();
    float const hover = m_hoverFade.value();
    float const press = m_pressFade.value();

    D2D1_COLOR_F fill = mix(mix(palette.controlFill, palette.controlFillSecondary, hover),
                            palette.controlFillTertiary, press);
    D2D1_COLOR_F text = palette.textPrimary;
    if (!enabled()) {
        fill = palette.controlFillDisabled;
        text = palette.textDisabled;
    }

    fillRounded(canvas, m_bounds, Metrics::controlCornerRadius, fill);
    strokeControlBorder(canvas, m_bounds, Metrics::controlCornerRadius, palette.controlStroke,
                        press > 0.5f ? palette.controlStroke : palette.controlStrokeSecondary);

    m_label.draw(canvas,
                 D2D1::Point2F(m_bounds.left + kButtonPaddingX,
                               (m_bounds.top + m_bounds.bottom - m_label.size().height) * 0.5f),
                 text);

    // The icon font already carries both directions, so the chevron swaps glyph rather than
    // being rotated by a transform that would have to be pushed and popped for one character.
    wchar_t const* chevron = m_chevron.value() > 0.5f ? glyph::kChevronUp : glyph::kChevronDown;
    drawIcon(canvas, chevron, kChevronGlyphSize,
             D2D1::RectF(m_bounds.right - 38.0f, m_bounds.top, m_bounds.right - 8.0f,
                         m_bounds.bottom),
             enabled() ? palette.textSecondary : palette.textDisabled);

    if (focused() && host() && host()->focusVisible()) {
        drawFocusRing(canvas, m_bounds, Metrics::controlCornerRadius);
    }
}

void ComboBox::open() {
    if (m_open || m_items.empty() || !host()) {
        return;
    }
    m_popup = std::make_unique<ComboPopup>(m_items, m_selected,
                                           [this](int index) { pick(index); });
    m_popup->setParent(this);
    m_popup->attach(host());

    float const width = std::max(widthOf(m_bounds), desiredWidth());
    float const height = m_popup->measure(width);
    D2D1_RECT_F const area = host()->bounds();

    float left = std::min(m_bounds.left, area.right - width - 4.0f);
    left = std::max(left, area.left + 4.0f);
    // Opening downwards is the default; near the bottom edge it flips, because the drop-down
    // is drawn inside the window and cannot escape it.
    float top = m_bounds.bottom + 4.0f;
    if (top + height > area.bottom - 4.0f) {
        top = m_bounds.top - 4.0f - height;
    }
    top = std::clamp(top, area.top + 4.0f, std::max(area.top + 4.0f, area.bottom - height - 4.0f));

    m_popup->arrange(rectOf(left, top, width, height));
    m_open = true;
    m_chevron.animateTo(1.0f, kDurationNormal, Easing::Entrance);
    host()->openOverlay(m_popup.get());
}

void ComboBox::close() {
    if (!m_open) {
        return;
    }
    m_open = false;
    m_chevron.animateTo(0.0f, kDurationNormal, Easing::Exit);
    if (host()) {
        host()->closeOverlay(m_popup.get());
    }
    // The popup outlives close(): picking an item calls this from inside the popup's own
    // event handler, and open() replaces it safely later.
    invalidate();
}

void ComboBox::pick(int index) {
    bool const changed = index != m_selected;
    setSelected(index);
    close();
    if (changed && m_onChanged) {
        m_onChanged(m_selected);
    }
}

void ComboBox::onPointerUp(D2D1_POINT_2F, bool insideBounds) {
    if (!insideBounds || !enabled()) {
        return;
    }
    if (m_open) {
        close();
    } else {
        open();
    }
}

bool ComboBox::onKey(WPARAM key) {
    if (!enabled()) {
        return false;
    }
    if (m_open) {
        if (key == VK_ESCAPE) {
            close();
            return true;
        }
        return m_popup && m_popup->onKey(key);
    }
    switch (key) {
        case VK_SPACE:
        case VK_RETURN:
        case VK_DOWN:
            open();
            return true;
        case VK_UP:
            if (m_selected > 0) {
                pick(m_selected - 1);
            }
            return true;
        default:
            return false;
    }
}

SettingsCard::SettingsCard(std::wstring glyph, std::wstring title) : m_glyph(std::move(glyph)) {
    m_title.setStyle(TypeStyle::Body);
    m_title.setText(std::move(title));
    m_description.setStyle(TypeStyle::Caption);
    m_description.setWrapping(true);
}

void SettingsCard::setTitle(std::wstring title) {
    m_title.setText(std::move(title));
    invalidateLayout();
}

void SettingsCard::setDescription(std::wstring description) {
    m_description.setText(std::move(description));
    invalidateLayout();
}

Widget* SettingsCard::setControl(std::unique_ptr<Widget> control) {
    m_control = std::move(control);
    if (m_control) {
        m_control->setParent(this);
        m_control->attach(host());
    }
    invalidateLayout();
    return m_control.get();
}

void SettingsCard::setOnClick(std::function<void()> handler) { m_onClick = std::move(handler); }

void SettingsCard::setCornerRadii(float top, float bottom) {
    m_topRadius = top;
    m_bottomRadius = bottom;
}

void SettingsCard::attach(WidgetHost* host) {
    Widget::attach(host);
    if (m_control) {
        m_control->attach(host);
    }
}

void SettingsCard::collectFocusable(std::vector<Widget*>& out) {
    Widget::collectFocusable(out);
    if (m_control && visible()) {
        m_control->collectFocusable(out);
    }
}

float SettingsCard::textColumnLeft() const {
    float const icon = m_glyph.empty() ? 0.0f : kCardIconLead + kCardIconSize + kCardIconGap;
    return kCardPadding + m_contentInset + icon;
}

float SettingsCard::measure(float availableWidth) {
    float const threshold = m_glyph.empty() ? kCardWrapThresholdNoIcon : kCardWrapThreshold;
    m_wrapped = m_control && availableWidth < threshold;

    float const textLeft = textColumnLeft();
    float controlWidth = 0.0f;
    if (m_control) {
        float const desired = m_control->desiredWidth();
        controlWidth = desired > 0.0f ? desired : kCardContentMinWidth;
    }

    float textWidth = availableWidth - textLeft - kCardPadding - m_trailingInset;
    if (m_control && !m_wrapped) {
        textWidth -= controlWidth + kCardColumnGap;
    }
    textWidth = std::max(40.0f, textWidth);

    float text = m_title.measure(textWidth);
    if (!m_description.empty()) {
        text += m_description.measure(textWidth);
    }

    float controlHeight = 0.0f;
    if (m_control) {
        float const width = m_wrapped ? std::max(40.0f, availableWidth - textLeft - kCardPadding -
                                                            m_trailingInset)
                                      : controlWidth;
        controlHeight = m_control->measure(width);
    }

    if (m_wrapped) {
        return kCardPadding * 2.0f + text + kCardVerticalSpacing + controlHeight;
    }
    return std::max(Metrics::settingsCardHeight,
                    kCardPadding * 2.0f + std::max(text, controlHeight));
}

void SettingsCard::arrange(D2D1_RECT_F bounds) {
    Widget::arrange(bounds);
    if (!m_control) {
        return;
    }

    float const textLeft = bounds.left + textColumnLeft();
    float const desired = m_control->desiredWidth();
    float const controlWidth = desired > 0.0f ? desired : kCardContentMinWidth;

    if (m_wrapped) {
        float const width =
            std::max(40.0f, bounds.right - kCardPadding - m_trailingInset - textLeft);
        float const height = m_control->measure(width);
        m_control->arrange(rectOf(textLeft, bounds.bottom - kCardPadding - height, width, height));
        return;
    }

    float const height = m_control->measure(controlWidth);
    m_control->arrange(rectOf(bounds.right - kCardPadding - m_trailingInset - controlWidth,
                              (bounds.top + bounds.bottom - height) * 0.5f, controlWidth, height));
}

bool SettingsCard::tick(std::chrono::steady_clock::time_point now) {
    bool running = Widget::tick(now);
    if (m_control) {
        running |= m_control->tick(now);
    }
    return running;
}

Widget* SettingsCard::hitTest(D2D1_POINT_2F point) {
    if (!visible() || !contains(m_bounds, point)) {
        return nullptr;
    }
    if (m_control && m_control->visible()) {
        if (Widget* hit = m_control->hitTest(point)) {
            return hit;
        }
    }
    return m_onClick ? this : nullptr;
}

void SettingsCard::onPointerUp(D2D1_POINT_2F, bool insideBounds) {
    if (insideBounds && enabled() && m_onClick) {
        m_onClick();
    }
}

bool SettingsCard::onKey(WPARAM key) {
    if (!m_onClick || !activationKey(key)) {
        return false;
    }
    m_onClick();
    return true;
}

void SettingsCard::paint(Canvas& canvas) {
    auto const& palette = theme().colors();

    if (m_chrome) {
        D2D1_COLOR_F fill = palette.cardFill;
        if (m_onClick && enabled()) {
            fill = mix(mix(palette.cardFill, palette.controlFillSecondary, m_hoverFade.value()),
                       palette.controlFillTertiary, m_pressFade.value());
        } else if (!enabled()) {
            fill = palette.controlFillDisabled;
        }

        // Rounded rectangles take one radius, so a card that is rounded at the top only is
        // drawn as the rounded shape plus a square patch over the other end.
        float const radius = std::max(m_topRadius, m_bottomRadius);
        fillRounded(canvas, m_bounds, radius, fill);
        if (m_topRadius < radius) {
            fillRect(canvas,
                     D2D1::RectF(m_bounds.left, m_bounds.top, m_bounds.right,
                                 m_bounds.top + radius),
                     fill);
        }
        if (m_bottomRadius < radius) {
            fillRect(canvas,
                     D2D1::RectF(m_bounds.left, m_bounds.bottom - radius, m_bounds.right,
                                 m_bounds.bottom),
                     fill);
        }
        strokeRounded(canvas, m_bounds, radius, palette.cardStroke);
    }

    D2D1_COLOR_F const titleColor =
        enabled() ? palette.textPrimary : palette.textDisabled;
    D2D1_COLOR_F const descriptionColor =
        enabled() ? palette.textSecondary : palette.textDisabled;

    float const textLeft = m_bounds.left + textColumnLeft();
    if (!m_glyph.empty()) {
        drawIcon(canvas, m_glyph, kCardIconSize,
                 rectOf(m_bounds.left + kCardPadding + m_contentInset + kCardIconLead,
                        m_bounds.top, kCardIconSize, heightOf(m_bounds)),
                 titleColor);
    }

    float textHeight = m_title.size().height;
    if (!m_description.empty()) {
        textHeight += m_description.size().height;
    }
    float y = m_wrapped ? m_bounds.top + kCardPadding
                        : m_bounds.top + (heightOf(m_bounds) - textHeight) * 0.5f;

    m_title.draw(canvas, D2D1::Point2F(textLeft, y), titleColor);
    y += m_title.size().height;
    if (!m_description.empty()) {
        m_description.draw(canvas, D2D1::Point2F(textLeft, y), descriptionColor);
    }

    if (m_control && m_control->visible()) {
        m_control->paint(canvas);
    }

    if (focused() && host() && host()->focusVisible()) {
        drawFocusRing(canvas, m_bounds, std::max(m_topRadius, m_bottomRadius));
    }
}

SettingsGroup::SettingsGroup(std::wstring header, std::wstring description) {
    m_header.setStyle(TypeStyle::BodyStrong);
    m_header.setText(std::move(header));
    m_description.setStyle(TypeStyle::Caption);
    m_description.setWrapping(true);
    m_description.setText(std::move(description));
}

SettingsCard* SettingsGroup::addCard(std::wstring glyph, std::wstring title) {
    return emplace<SettingsCard>(std::move(glyph), std::move(title));
}

float SettingsGroup::measure(float availableWidth) {
    m_headerHeight = m_header.measure(availableWidth);
    if (!m_description.empty()) {
        m_headerHeight += m_description.measure(availableWidth);
    }
    // PowerToys puts eight DIPs between a group's text and its first card, and four between
    // the cards.
    m_headerHeight += kCardVerticalSpacing;

    float total = m_headerHeight;
    bool first = true;
    for (auto const& child : m_children) {
        if (!child->visible()) {
            continue;
        }
        if (!first) {
            total += Metrics::cardGap;
        }
        first = false;
        total += child->measure(availableWidth);
    }
    return total;
}

void SettingsGroup::arrange(D2D1_RECT_F bounds) {
    Widget::arrange(bounds);
    float y = bounds.top + m_headerHeight;
    bool first = true;
    for (auto const& child : m_children) {
        if (!child->visible()) {
            continue;
        }
        if (!first) {
            y += Metrics::cardGap;
        }
        first = false;
        float const h = child->measure(widthOf(bounds));
        child->arrange(rectOf(bounds.left, y, widthOf(bounds), h));
        y += h;
    }
}

void SettingsGroup::paint(Canvas& canvas) {
    auto const& palette = theme().colors();
    m_header.draw(canvas, D2D1::Point2F(m_bounds.left, m_bounds.top), palette.textPrimary);
    if (!m_description.empty()) {
        m_description.draw(canvas,
                           D2D1::Point2F(m_bounds.left, m_bounds.top + m_header.size().height),
                           palette.textSecondary);
    }
    Container::paint(canvas);
}

Expander::Expander(std::wstring glyph, std::wstring title) {
    m_header = emplace<SettingsCard>(std::move(glyph), std::move(title));
    m_header->setOnClick([this] { setExpanded(!m_expanded); });
    m_header->setTrailingInset(kExpanderChevronColumn);
}

void Expander::setDescription(std::wstring description) {
    m_header->setDescription(std::move(description));
}

Widget* Expander::setHeaderControl(std::unique_ptr<Widget> control) {
    return m_header->setControl(std::move(control));
}

SettingsCard* Expander::addRow(std::wstring title) {
    auto* row = emplace<SettingsCard>(std::wstring{}, std::move(title));
    row->setChrome(false);
    row->setContentInset(kExpanderRowInset - kCardPadding);
    return row;
}

void Expander::setExpanded(bool expanded, bool animate) {
    if (m_expanded == expanded) {
        return;
    }
    m_expanded = expanded;
    if (animate) {
        m_reveal.animateTo(expanded ? 1.0f : 0.0f, kDurationFlyout, Easing::Entrance);
    } else {
        m_reveal.snapTo(expanded ? 1.0f : 0.0f);
    }
    invalidateLayout();
    invalidate();
}

float Expander::measure(float availableWidth) {
    float const headerHeight = m_header->measure(availableWidth);

    m_contentHeight = 0.0f;
    for (std::size_t i = 1; i < m_children.size(); ++i) {
        if (!m_children[i]->visible()) {
            continue;
        }
        m_contentHeight +=
            std::max(kExpanderRowMinHeight, m_children[i]->measure(availableWidth));
    }
    return headerHeight + m_contentHeight * std::clamp(m_reveal.value(), 0.0f, 1.0f);
}

void Expander::arrange(D2D1_RECT_F bounds) {
    Widget::arrange(bounds);
    float const headerHeight = m_header->measure(widthOf(bounds));
    m_header->arrange(rectOf(bounds.left, bounds.top, widthOf(bounds), headerHeight));
    m_header->setCornerRadii(Metrics::controlCornerRadius,
                             m_reveal.value() > 0.01f ? 0.0f : Metrics::controlCornerRadius);

    float y = bounds.top + headerHeight;
    for (std::size_t i = 1; i < m_children.size(); ++i) {
        if (!m_children[i]->visible()) {
            continue;
        }
        float const h =
            std::max(kExpanderRowMinHeight, m_children[i]->measure(widthOf(bounds)));
        m_children[i]->arrange(rectOf(bounds.left, y, widthOf(bounds), h));
        y += h;
    }
}

bool Expander::tick(std::chrono::steady_clock::time_point now) {
    bool running = Container::tick(now);
    if (m_reveal.tick(now)) {
        running = true;
        invalidateLayout();
    }
    return running;
}

Widget* Expander::hitTest(D2D1_POINT_2F point) {
    // While collapsing, rows are still arranged below the clip; they must not take clicks.
    if (!visible() || !contains(m_bounds, point)) {
        return nullptr;
    }
    return Container::hitTest(point);
}

void Expander::paint(Canvas& canvas) {
    auto const& palette = theme().colors();
    float const reveal = std::clamp(m_reveal.value(), 0.0f, 1.0f);
    D2D1_RECT_F const header = m_header->bounds();

    if (reveal > 0.01f) {
        D2D1_RECT_F const content =
            D2D1::RectF(m_bounds.left, header.bottom, m_bounds.right, m_bounds.bottom);
        fillRounded(canvas, content, Metrics::controlCornerRadius, palette.cardFillSecondary);
        fillRect(canvas,
                 D2D1::RectF(content.left, content.top, content.right,
                             std::min(content.bottom, content.top + Metrics::controlCornerRadius)),
                 palette.cardFillSecondary);
        strokeRounded(canvas, content, Metrics::controlCornerRadius, palette.cardStroke);
    }

    m_header->paint(canvas);

    // The chevron lives in the header's right-hand column, which the header was measured
    // without so its own control cannot collide with it.
    drawIcon(canvas, reveal > 0.5f ? glyph::kChevronDown : glyph::kChevronRight, kChevronGlyphSize,
             D2D1::RectF(header.right - kExpanderChevronColumn, header.top, header.right - 12.0f,
                         header.bottom),
             palette.textSecondary);

    if (reveal <= 0.01f) {
        return;
    }

    auto* context = canvas.deviceContext();
    D2D1_RECT_F const clip =
        D2D1::RectF(m_bounds.left, header.bottom, m_bounds.right, m_bounds.bottom);
    if (context) {
        context->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    }
    for (std::size_t i = 1; i < m_children.size(); ++i) {
        if (!m_children[i]->visible()) {
            continue;
        }
        if (i > 1) {
            drawDivider(canvas, m_bounds.left + 1.0f, m_bounds.right - 1.0f,
                        m_children[i]->bounds().top, palette.dividerStroke);
        }
        m_children[i]->paint(canvas);
    }
    if (context) {
        context->PopAxisAlignedClip();
    }
}

Widget* ScrollView::setContent(std::unique_ptr<Widget> content) {
    m_content = std::move(content);
    if (m_content) {
        m_content->setParent(this);
        m_content->attach(host());
    }
    invalidateLayout();
    return m_content.get();
}

void ScrollView::attach(WidgetHost* host) {
    Widget::attach(host);
    if (m_content) {
        m_content->attach(host);
    }
}

void ScrollView::collectFocusable(std::vector<Widget*>& out) {
    if (m_content && visible()) {
        m_content->collectFocusable(out);
    }
}

float ScrollView::maximumOffset() const {
    return std::max(0.0f, m_contentHeight - heightOf(m_bounds));
}

float ScrollView::measure(float availableWidth) {
    return m_content ? m_content->measure(std::max(0.0f, availableWidth - kScrollbarGutter))
                     : 0.0f;
}

void ScrollView::arrange(D2D1_RECT_F bounds) {
    Widget::arrange(bounds);
    if (!m_content) {
        return;
    }
    float const width = std::max(0.0f, widthOf(bounds) - kScrollbarGutter);
    m_contentHeight = m_content->measure(width);
    m_offset = std::clamp(m_offset, 0.0f, maximumOffset());
    m_content->arrange(rectOf(bounds.left, bounds.top - m_offset, width, m_contentHeight));
}

void ScrollView::scrollTo(float offset) {
    float const clamped = std::clamp(offset, 0.0f, maximumOffset());
    if (clamped == m_offset) {
        return;
    }
    m_offset = clamped;
    invalidateLayout();
    invalidate();
}

void ScrollView::scrollBy(float delta) { scrollTo(m_offset + delta); }

void ScrollView::reveal(D2D1_RECT_F rect) {
    if (rect.top < m_bounds.top) {
        scrollBy(rect.top - m_bounds.top - 8.0f);
    } else if (rect.bottom > m_bounds.bottom) {
        scrollBy(rect.bottom - m_bounds.bottom + 8.0f);
    }
}

D2D1_RECT_F ScrollView::thumbRect() const {
    float const track = heightOf(m_bounds);
    if (m_contentHeight <= track || track <= 0.0f) {
        return D2D1_RECT_F{};
    }
    float const height =
        std::max(kScrollbarMinimumThumb, track * track / m_contentHeight);
    float const travel = track - height;
    float const top = m_bounds.top + travel * (m_offset / std::max(1.0f, maximumOffset()));
    float const expansion = m_scrollbar.value();
    float const width = kScrollbarRailWidth + (kScrollbarThumbWidth - kScrollbarRailWidth) * expansion;
    float const right = m_bounds.right - (kScrollbarGutter - width) * 0.5f;
    return D2D1::RectF(right - width, top, right, top + height);
}

void ScrollView::paint(Canvas& canvas) {
    auto* context = canvas.deviceContext();
    if (context) {
        context->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_ALIASED);
    }
    if (m_content && m_content->visible()) {
        m_content->paint(canvas);
    }
    if (context) {
        context->PopAxisAlignedClip();
    }

    D2D1_RECT_F const thumb = thumbRect();
    if (widthOf(thumb) <= 0.0f) {
        return;
    }

    auto const& palette = theme().colors();
    float const expansion = m_scrollbar.value();
    if (expansion > 0.01f) {
        D2D1_COLOR_F rail = palette.cardFillSecondary;
        rail.a *= expansion;
        D2D1_RECT_F const track =
            D2D1::RectF(m_bounds.right - kScrollbarGutter, m_bounds.top, m_bounds.right,
                        m_bounds.bottom);
        fillRounded(canvas, track, kScrollbarGutter * 0.5f, rail);
    }
    fillRounded(canvas, thumb, widthOf(thumb) * 0.5f, palette.controlStrongFill);
}

bool ScrollView::tick(std::chrono::steady_clock::time_point now) {
    bool running = Widget::tick(now);
    running |= m_scrollbar.tick(now);
    if (m_content) {
        running |= m_content->tick(now);
    }
    return running;
}

Widget* ScrollView::hitTest(D2D1_POINT_2F point) {
    if (!visible() || !contains(m_bounds, point)) {
        return nullptr;
    }
    D2D1_RECT_F const thumb = thumbRect();
    if (widthOf(thumb) > 0.0f && point.x >= m_bounds.right - kScrollbarGutter) {
        return this;
    }
    if (m_content) {
        if (Widget* hit = m_content->hitTest(point)) {
            return hit;
        }
    }
    return this;
}

void ScrollView::onPointerEnter() {
    m_scrollbar.animateTo(1.0f, kDurationFlyout, Easing::Entrance);
    invalidate();
}

void ScrollView::onPointerLeave() {
    if (!m_draggingThumb) {
        m_scrollbar.animateTo(0.0f, kDurationFlyout, Easing::Exit);
        invalidate();
    }
}

void ScrollView::onPointerDown(D2D1_POINT_2F point) {
    D2D1_RECT_F const thumb = thumbRect();
    // Only the scrollbar gutter reacts; a press on empty page background must do nothing.
    if (widthOf(thumb) <= 0.0f || point.x < m_bounds.right - kScrollbarGutter) {
        return;
    }
    if (point.y >= thumb.top && point.y < thumb.bottom) {
        m_draggingThumb = true;
        m_dragAnchor = point.y - thumb.top;
        return;
    }
    // Clicking the track pages towards the click, which is what a Fluent scrollbar does.
    scrollBy(point.y < thumb.top ? -heightOf(m_bounds) : heightOf(m_bounds));
}

void ScrollView::onPointerMove(D2D1_POINT_2F point) {
    if (!m_draggingThumb) {
        return;
    }
    float const track = heightOf(m_bounds);
    D2D1_RECT_F const thumb = thumbRect();
    float const travel = track - heightOf(thumb);
    if (travel <= 0.0f) {
        return;
    }
    float const position = point.y - m_dragAnchor - m_bounds.top;
    scrollTo(position / travel * maximumOffset());
}

void ScrollView::onPointerUp(D2D1_POINT_2F, bool) { m_draggingThumb = false; }

bool ScrollView::onWheel(float delta) {
    if (maximumOffset() <= 0.0f) {
        return false;
    }
    scrollBy(-delta * kWheelStep);
    return true;
}

bool ScrollView::onKey(WPARAM key) {
    switch (key) {
        case VK_PRIOR: scrollBy(-heightOf(m_bounds)); return true;
        case VK_NEXT: scrollBy(heightOf(m_bounds)); return true;
        case VK_HOME: scrollTo(0.0f); return true;
        case VK_END: scrollTo(maximumOffset()); return true;
        default: return false;
    }
}

NavigationRail::NavigationRail(std::vector<Item> items, Handler onSelected)
    : m_items(std::move(items)), m_onSelected(std::move(onSelected)) {
    m_labels.resize(m_items.size());
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        m_labels[i].setStyle(TypeStyle::Body);
        m_labels[i].setText(m_items[i].label);
    }
}

float NavigationRail::itemTop(int index) const {
    return m_bounds.top + kNavItemInset + static_cast<float>(index) * Metrics::navItemHeight;
}

void NavigationRail::setSelected(int index, bool animate) {
    int const clamped = std::clamp(index, 0, std::max(0, static_cast<int>(m_items.size()) - 1));
    if (clamped == m_selected) {
        return;
    }
    float const from = m_pillPosition.value();
    m_selected = clamped;
    float const to = static_cast<float>(clamped);

    if (animate) {
        // WinUI stretches the pill towards its destination a third of the way through and lets
        // it settle. Starting stretched and easing back to its resting length reproduces that
        // read with one track instead of a keyframe timeline.
        float const travel = std::fabs(to - from) * Metrics::navItemHeight;
        m_pillStretch.snapTo(1.0f + travel / Metrics::navIndicatorLength);
        m_pillStretch.animateTo(1.0f, kPillDuration, Easing::Entrance);
        m_pillPosition.animateTo(to, kPillDuration, Easing::Entrance);
    } else {
        m_pillPosition.snapTo(to);
        m_pillStretch.snapTo(1.0f);
    }
    invalidate();
}

float NavigationRail::measure(float availableWidth) {
    if (m_expanded) {
        for (auto& label : m_labels) {
            label.measure(std::max(0.0f, availableWidth - kNavIconSize - 24.0f));
        }
    }
    return kNavItemInset * 2.0f + static_cast<float>(m_items.size()) * Metrics::navItemHeight;
}

bool NavigationRail::tick(std::chrono::steady_clock::time_point now) {
    bool running = Widget::tick(now);
    running |= m_pillPosition.tick(now);
    running |= m_pillStretch.tick(now);
    return running;
}

int NavigationRail::indexAt(D2D1_POINT_2F point) const {
    if (!contains(m_bounds, point)) {
        return -1;
    }
    int const index =
        static_cast<int>((point.y - m_bounds.top - kNavItemInset) / Metrics::navItemHeight);
    return index >= 0 && index < static_cast<int>(m_items.size()) ? index : -1;
}

void NavigationRail::paint(Canvas& canvas) {
    auto const& palette = theme().colors();

    for (std::size_t i = 0; i < m_items.size(); ++i) {
        int const index = static_cast<int>(i);
        D2D1_RECT_F const row =
            rectOf(m_bounds.left + kNavItemInset, itemTop(index),
                   widthOf(m_bounds) - kNavItemInset * 2.0f, Metrics::navItemHeight);

        bool const selected = index == m_selected;
        D2D1_COLOR_F fill{0.0f, 0.0f, 0.0f, 0.0f};
        if (index == m_pressedItem) {
            fill = palette.subtleFillTertiary;
        } else if (index == m_hovered) {
            fill = selected ? palette.subtleFillTertiary : palette.subtleFillSecondary;
        } else if (selected) {
            fill = palette.subtleFillSecondary;
        }
        if (fill.a > 0.0f) {
            fillRounded(canvas, row, Metrics::controlCornerRadius, fill);
        }

        D2D1_COLOR_F const foreground =
            index == m_pressedItem ? palette.textSecondary : palette.textPrimary;
        float const iconLeft = m_expanded ? row.left + 12.0f
                                          : row.left + (widthOf(row) - kNavIconSize) * 0.5f;
        drawIcon(canvas, m_items[i].glyph, kNavIconSize,
                 rectOf(iconLeft, row.top, kNavIconSize, heightOf(row)), foreground);
        if (m_expanded) {
            m_labels[i].draw(canvas,
                             D2D1::Point2F(iconLeft + kNavIconSize + 12.0f,
                                           (row.top + row.bottom - m_labels[i].size().height) * 0.5f),
                             foreground);
        }
    }

    if (m_items.empty()) {
        return;
    }

    float const centre =
        itemTop(0) + m_pillPosition.value() * Metrics::navItemHeight + Metrics::navItemHeight * 0.5f;
    float const length = Metrics::navIndicatorLength * std::max(1.0f, m_pillStretch.value());
    fillRounded(canvas,
                D2D1::RectF(m_bounds.left + kNavItemInset, centre - length * 0.5f,
                            m_bounds.left + kNavItemInset + Metrics::navIndicatorWidth,
                            centre + length * 0.5f),
                Metrics::navIndicatorWidth * 0.5f, palette.accent);

    if (focused() && host() && host()->focusVisible()) {
        drawFocusRing(canvas,
                      rectOf(m_bounds.left + kNavItemInset, itemTop(m_selected),
                             widthOf(m_bounds) - kNavItemInset * 2.0f, Metrics::navItemHeight),
                      Metrics::controlCornerRadius);
    }
}

void NavigationRail::onPointerMove(D2D1_POINT_2F point) {
    int const index = indexAt(point);
    if (index != m_hovered) {
        m_hovered = index;
        invalidate();
    }
}

void NavigationRail::onPointerLeave() {
    if (m_hovered != -1) {
        m_hovered = -1;
        invalidate();
    }
}

void NavigationRail::onPointerDown(D2D1_POINT_2F point) {
    m_pressedItem = indexAt(point);
    invalidate();
}

void NavigationRail::onPointerUp(D2D1_POINT_2F point, bool insideBounds) {
    int const index = m_pressedItem;
    m_pressedItem = -1;
    invalidate();
    if (!insideBounds || index < 0 || index != indexAt(point)) {
        return;
    }
    setSelected(index);
    if (m_onSelected) {
        m_onSelected(index);
    }
}

bool NavigationRail::onKey(WPARAM key) {
    int next = m_selected;
    if (key == VK_UP || key == VK_LEFT) {
        next = m_selected - 1;
    } else if (key == VK_DOWN || key == VK_RIGHT) {
        next = m_selected + 1;
    } else {
        return false;
    }
    if (next < 0 || next >= static_cast<int>(m_items.size())) {
        return true;
    }
    setSelected(next);
    if (m_onSelected) {
        m_onSelected(next);
    }
    return true;
}

WidgetHost::WidgetHost(D2DWindow& window) : m_window(window) {}

WidgetHost::~WidgetHost() = default;

Widget* WidgetHost::setRoot(std::unique_ptr<Widget> root) {
    m_hovered = nullptr;
    m_hoverChain.clear();
    m_captured = nullptr;
    m_focused = nullptr;
    m_overlay = nullptr;
    m_root = std::move(root);
    if (m_root) {
        m_root->attach(this);
    }
    m_layoutValid = false;
    invalidate();
    return m_root.get();
}

float WidgetHost::scale() const { return m_window.scale(); }

void WidgetHost::invalidate() { m_window.invalidate(); }

void WidgetHost::invalidateLayout() {
    m_layoutValid = false;
    m_window.invalidate();
}

void WidgetHost::layout(D2D1_RECT_F bounds) {
    bool const moved = std::memcmp(&bounds, &m_bounds, sizeof(bounds)) != 0;
    if (!moved && m_layoutValid) {
        return;
    }
    m_bounds = bounds;
    m_layoutValid = true;
    if (m_root) {
        m_root->measure(bounds.right - bounds.left);
        m_root->arrange(bounds);
    }
}

void WidgetHost::paint(Canvas& canvas) {
    if (m_root && m_root->visible()) {
        m_root->paint(canvas);
    }
    if (m_overlay) {
        m_overlay->paint(canvas);
    }
}

bool WidgetHost::tick(std::chrono::steady_clock::time_point now) {
    bool running = m_root ? m_root->tick(now) : false;
    if (m_overlay) {
        running |= m_overlay->tick(now);
    }
    return running;
}

void WidgetHost::openOverlay(Widget* overlay) {
    m_overlay = overlay;
    m_hovered = nullptr;
    m_hoverChain.clear();
    invalidate();
}

void WidgetHost::closeOverlay(Widget* overlay) {
    if (m_overlay == overlay) {
        m_overlay = nullptr;
        m_hovered = nullptr;
        m_hoverChain.clear();
        if (m_captured == overlay) {
            m_captured = nullptr;
        }
        invalidate();
    }
}

void WidgetHost::resetInput() {
    for (Widget* widget : m_hoverChain) {
        widget->onPointerLeave();
    }
    m_hoverChain.clear();
    if (m_hovered) {
        m_hovered->setHovered(false);
        m_hovered = nullptr;
    }
    if (m_captured) {
        m_captured->setPressed(false);
        m_captured = nullptr;
        // Only when this host still holds it: WM_LBUTTONUP releases capture before it invokes
        // the handler that gets us here, and releasing twice steals it from whoever took it.
        if (GetCapture() == m_window.handle()) {
            ReleaseCapture();
        }
    }
    if (m_focused) {
        m_focused->setFocused(false);
        m_focused = nullptr;
    }
    m_overlay = nullptr;
    invalidate();
}

void WidgetHost::setFocus(Widget* widget) {
    if (m_focused == widget) {
        return;
    }
    if (m_focused) {
        m_focused->setFocused(false);
    }
    m_focused = widget;
    if (m_focused) {
        m_focused->setFocused(true);
        // Focus that lands off-screen has to be brought into view, or Tab appears to do
        // nothing at all.
        for (Widget* parent = m_focused->parent(); parent; parent = parent->parent()) {
            if (auto* view = dynamic_cast<ScrollView*>(parent)) {
                view->reveal(m_focused->bounds());
                break;
            }
        }
    }
    invalidate();
}

void WidgetHost::moveFocus(bool forward) {
    std::vector<Widget*> order;
    if (m_overlay) {
        m_overlay->collectFocusable(order);
    } else if (m_root) {
        m_root->collectFocusable(order);
    }
    if (order.empty()) {
        return;
    }

    auto const current = std::find(order.begin(), order.end(), m_focused);
    std::size_t index = 0;
    if (current == order.end()) {
        index = forward ? 0 : order.size() - 1;
    } else {
        auto const position = static_cast<std::size_t>(current - order.begin());
        index = forward ? (position + 1) % order.size()
                        : (position + order.size() - 1) % order.size();
    }
    setFocus(order[index]);
}

D2D1_POINT_2F WidgetHost::toDip(LPARAM lparam) const {
    float const s = scale();
    return D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lparam)) / s,
                         static_cast<float>(GET_Y_LPARAM(lparam)) / s);
}

void WidgetHost::updateHover(D2D1_POINT_2F point) {
    Widget* hit = m_overlay ? m_overlay->hitTest(point)
                            : (m_root ? m_root->hitTest(point) : nullptr);
    if (hit == m_hovered) {
        return;
    }

    std::vector<Widget*> chain;
    for (Widget* widget = hit; widget; widget = widget->parent()) {
        chain.push_back(widget);
    }
    std::reverse(chain.begin(), chain.end());

    // Enter and leave run over the whole ancestor path, so a scroll view learns that the
    // pointer is inside it even while the pointer sits on a button three levels down.
    for (Widget* widget : m_hoverChain) {
        if (std::find(chain.begin(), chain.end(), widget) == chain.end()) {
            widget->onPointerLeave();
        }
    }
    for (Widget* widget : chain) {
        if (std::find(m_hoverChain.begin(), m_hoverChain.end(), widget) == m_hoverChain.end()) {
            widget->onPointerEnter();
        }
    }
    m_hoverChain = std::move(chain);

    if (m_hovered) {
        m_hovered->setHovered(false);
    }
    m_hovered = hit;
    if (m_hovered) {
        m_hovered->setHovered(true);
    }
}

bool WidgetHost::handleMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result) {
    result = 0;
    switch (message) {
        case WM_MOUSEMOVE: {
            if (!m_tracking) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, m_window.handle(), 0};
                m_tracking = TrackMouseEvent(&track) != FALSE;
            }
            D2D1_POINT_2F const point = toDip(lparam);
            updateHover(point);
            if (Widget* target = m_captured ? m_captured : m_hovered) {
                if (target->enabled()) {
                    target->onPointerMove(point);
                }
            }
            return false;  // the window still wants this for its own hover visuals
        }

        case WM_MOUSELEAVE:
            m_tracking = false;
            for (Widget* widget : m_hoverChain) {
                widget->onPointerLeave();
            }
            m_hoverChain.clear();
            if (m_hovered) {
                m_hovered->setHovered(false);
                m_hovered = nullptr;
            }
            return false;

        case WM_LBUTTONDOWN: {
            D2D1_POINT_2F const point = toDip(lparam);
            m_focusVisible = false;

            if (m_overlay && !m_overlay->hitTest(point)) {
                // A click outside an open drop-down dismisses it and goes no further, which is
                // what every menu on the system does.
                if (auto* combo = dynamic_cast<ComboBox*>(m_overlay->parent())) {
                    combo->onKey(VK_ESCAPE);  // the combo owns the popup and must forget it
                } else {
                    closeOverlay(m_overlay);
                }
                return true;
            }

            updateHover(point);
            m_captured = m_hovered;
            if (!m_captured) {
                setFocus(nullptr);
                return false;
            }
            SetCapture(m_window.handle());
            if (m_captured->enabled()) {
                m_captured->setPressed(true);
                setFocus(m_captured->focusable() ? m_captured : nullptr);
                m_captured->onPointerDown(point);
            }
            return true;
        }

        case WM_LBUTTONUP: {
            if (!m_captured) {
                return false;
            }
            D2D1_POINT_2F const point = toDip(lparam);
            Widget* target = m_captured;
            m_captured = nullptr;
            ReleaseCapture();
            target->setPressed(false);
            if (target->enabled()) {
                target->onPointerUp(point, target->hitTest(point) != nullptr);
            }
            updateHover(point);
            return true;
        }

        case WM_CAPTURECHANGED:
            if (m_captured) {
                m_captured->setPressed(false);
                m_captured = nullptr;
            }
            return false;

        case WM_MOUSEWHEEL: {
            POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(m_window.handle(), &screen);
            float const s = scale();
            D2D1_POINT_2F const point =
                D2D1::Point2F(static_cast<float>(screen.x) / s, static_cast<float>(screen.y) / s);
            float const delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;

            Widget* target = m_overlay ? m_overlay->hitTest(point)
                                       : (m_root ? m_root->hitTest(point) : nullptr);
            for (; target; target = target->parent()) {
                if (target->onWheel(delta)) {
                    result = 0;
                    return true;
                }
            }
            return false;
        }

        case WM_KEYDOWN: {
            if (wparam == VK_TAB) {
                m_focusVisible = true;
                moveFocus((GetKeyState(VK_SHIFT) & 0x8000) == 0);
                return true;
            }
            m_focusVisible = true;

            if (m_overlay && m_overlay->onKey(wparam)) {
                return true;
            }
            if (m_focused && m_focused->enabled() && m_focused->onKey(wparam)) {
                return true;
            }
            // Paging and Home/End belong to the scroll view when nothing else claimed them.
            if (m_root && m_root->onKey(wparam)) {
                return true;
            }
            return false;
        }

        default:
            return false;
    }
}

}  // namespace peek::ui
