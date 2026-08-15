#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/Win.h"
#include "ui/Animation.h"
#include "ui/Drawing.h"
#include "ui/Theme.h"
#include "ui/Window.h"

namespace peek::ui {

// A retained widget layer for windows that draw themselves.
//
// ==== What this is for ====================================================================
//
// The application's windows have no redirection surface, so there are no child HWNDs and no
// common controls: every button, switch and scrollbar is drawn by us and hit-tested by us.
// This is that layer. A page is built by describing it once, and the host below routes input,
// runs the state animations and paints.
//
// ==== Building a page =====================================================================
//
//     class SettingsWindow : public D2DWindow {
//         WidgetHost m_host{*this};
//         WindowShadow m_shadow;
//     };
//
//     auto page = std::make_unique<ScrollView>();
//     auto* body = page->setContent(std::make_unique<StackPanel>());
//     body->setPadding({36, 16, 36, 48});
//
//     auto* group = body->emplace<SettingsGroup>(text(Text::SoundsTitle));
//     auto* row = group->addCard(glyph::kVolume, text(Text::MasterVolume));
//     row->setDescription(text(Text::MasterVolumeDesc));
//     row->setControl(std::make_unique<Slider>(0.0f, 1.0f, settings.masterVolume,
//                                              [](float v) { ... }));
//
//     auto* toggle = group->addCard(glyph::kPlay, text(Text::PlaySound));
//     toggle->setControl(std::make_unique<ToggleSwitch>(true, [](bool on) { ... }));
//
//     m_host.setRoot(std::move(page));
//
// and in the window:
//
//     void onPaint(ID2D1DeviceContext& ctx, D2D1_SIZE_F size) override {
//         Canvas canvas(ctx);
//         D2D1_RECT_F const body = bodyRect();
//         m_shadow.draw(canvas, body, Metrics::windowCornerRadius, theme().colors().shadow);
//         fillRounded(canvas, body, ..., theme().colors().windowBackground);
//         // tick first: an animation that changes how much room a widget needs must be
//         // reflected in this frame's layout, not in the next one.
//         if (m_host.tick(frameTime())) requestFrame();
//         m_host.layout(contentRect);
//         m_host.paint(canvas);
//     }
//
//     bool onMessage(UINT m, WPARAM w, LPARAM l, LRESULT& r) override {
//         return m_host.handleMessage(m, w, l, r);
//     }
//
// ==== The rules that matter ===============================================================
//
//  * All coordinates are DIPs relative to the window's top-left corner, which is what
//    D2DWindow::onPaint draws in and what isCaptionArea receives.
//  * Layout is a single vertical pass: measure(availableWidth) returns the height a widget
//    wants, arrange(rect) places it. Containers own their children.
//  * Nothing here owns a timer. tick() advances the state animations and reports whether
//    another frame is needed; the window turns that into requestFrame().
//  * A widget never paints outside its bounds except through the host's overlay, which the
//    ComboBox drop-down uses. The overlay is drawn inside the window, so a drop-down near the
//    bottom edge opens upwards rather than escaping the window.
//  * Keyboard focus is the host's business: Tab and Shift+Tab walk the tree, and the focus
//    ring is only drawn after a key has been used, exactly as Windows does it.

class WidgetHost;

struct Thickness {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// Which of the theme's text colours a piece of text takes.
enum class TextRole { Primary, Secondary, Tertiary, Accent, OnAccent };

class Widget {
public:
    virtual ~Widget() = default;

    Widget(Widget const&) = delete;
    Widget& operator=(Widget const&) = delete;

    // Height this widget wants when it is given `availableWidth` DIPs.
    virtual float measure(float availableWidth) = 0;
    virtual void arrange(D2D1_RECT_F bounds);
    D2D1_RECT_F bounds() const noexcept { return m_bounds; }

    // Width this widget would like when it sits in the trailing column of a settings card.
    // Zero means "whatever the column is", which is what a stretchy control such as a slider
    // wants.
    virtual float desiredWidth() const { return 0.0f; }

    virtual void paint(Canvas& canvas) = 0;

    // Advances animations. True while another frame is needed.
    virtual bool tick(std::chrono::steady_clock::time_point now);

    // Deepest visible widget containing `point`, or nullptr.
    virtual Widget* hitTest(D2D1_POINT_2F point);

    virtual void onPointerEnter() {}
    virtual void onPointerLeave() {}
    virtual void onPointerMove(D2D1_POINT_2F) {}
    virtual void onPointerDown(D2D1_POINT_2F) {}
    virtual void onPointerUp(D2D1_POINT_2F, bool insideBounds) { (void)insideBounds; }
    virtual bool onWheel(float delta) { (void)delta; return false; }
    virtual bool onKey(WPARAM key) { (void)key; return false; }

    virtual bool focusable() const { return false; }
    virtual void collectFocusable(std::vector<Widget*>& out);

    bool enabled() const noexcept { return m_enabled; }
    void setEnabled(bool enabled);
    bool visible() const noexcept { return m_visible; }
    void setVisible(bool visible);

    // Driven by the host; a widget reads them when it paints.
    bool hovered() const noexcept { return m_hovered; }
    bool pressed() const noexcept { return m_pressed; }
    bool focused() const noexcept { return m_focused; }
    void setHovered(bool hovered);
    void setPressed(bool pressed);
    void setFocused(bool focused);

    Widget* parent() const noexcept { return m_parent; }
    void setParent(Widget* parent) noexcept { m_parent = parent; }
    WidgetHost* host() const noexcept { return m_host; }
    virtual void attach(WidgetHost* host);

protected:
    Widget() = default;

    void invalidate();

    // For a change that alters how much room the widget needs, such as an expander opening.
    void invalidateLayout();

    // 0 at rest, 1 fully in the state. Animated so a pointer crossing a settings page leaves
    // a trail of fades rather than hard switches.
    Animated m_hoverFade{0.0f};
    Animated m_pressFade{0.0f};
    Animated m_focusFade{0.0f};

    D2D1_RECT_F m_bounds{};

private:
    Widget* m_parent = nullptr;
    WidgetHost* m_host = nullptr;
    bool m_enabled = true;
    bool m_visible = true;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_focused = false;
};

// Owns children and forwards the tree operations to them. Layout is left to subclasses.
class Container : public Widget {
public:
    Widget* add(std::unique_ptr<Widget> child);

    template <class T, class... Args>
    T* emplace(Args&&... args) {
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.get();
        add(std::move(owned));
        return raw;
    }

    void clear();
    std::vector<std::unique_ptr<Widget>> const& children() const noexcept { return m_children; }

    void paint(Canvas& canvas) override;
    bool tick(std::chrono::steady_clock::time_point now) override;
    Widget* hitTest(D2D1_POINT_2F point) override;
    void collectFocusable(std::vector<Widget*>& out) override;
    void attach(WidgetHost* host) override;

protected:
    std::vector<std::unique_ptr<Widget>> m_children;
};

// A vertical stack. The only layout container this application needs: every page in it is a
// column of rows, and anything wider is a widget's own internal business.
class StackPanel : public Container {
public:
    void setSpacing(float spacing) { m_spacing = spacing; }
    void setPadding(Thickness padding) { m_padding = padding; }

    float measure(float availableWidth) override;
    void arrange(D2D1_RECT_F bounds) override;

private:
    float m_spacing = 0.0f;
    Thickness m_padding{};
};

class Label : public Widget {
public:
    explicit Label(std::wstring text = {},
                   TypeStyle style = TypeStyle::Body,
                   TextRole role = TextRole::Primary);

    void setText(std::wstring text);
    void setStyle(TypeStyle style);
    void setRole(TextRole role) { m_role = role; }
    void setWrapping(bool wrap);
    void setMargin(Thickness margin) { m_margin = margin; }

    float measure(float availableWidth) override;
    void paint(Canvas& canvas) override;

private:
    TextBlock m_block;
    TextRole m_role = TextRole::Primary;
    Thickness m_margin{};
};

// Standard and accent buttons. `glyph` is a Segoe MDL2 codepoint from ui::glyph and is drawn
// to the left of the text; either may be empty.
class Button : public Widget {
public:
    using Handler = std::function<void()>;

    Button(std::wstring text, Handler onClick, bool accent = false);

    void setText(std::wstring text);
    void setGlyph(std::wstring glyph) { m_glyph = std::move(glyph); }
    void setAccent(bool accent) { m_accent = accent; }
    void setMinimumWidth(float width) { m_minimumWidth = width; }

    float measure(float availableWidth) override;
    void paint(Canvas& canvas) override;
    void onPointerUp(D2D1_POINT_2F point, bool insideBounds) override;
    bool onKey(WPARAM key) override;
    bool focusable() const override { return true; }

    float desiredWidth() const override;

private:
    void fire();

    TextBlock m_label;
    std::wstring m_glyph;
    Handler m_onClick;
    bool m_accent = false;
    float m_minimumWidth = 0.0f;
};

class ToggleSwitch : public Widget {
public:
    using Handler = std::function<void(bool)>;

    ToggleSwitch(bool value, Handler onChanged);

    void setValue(bool value, bool animate = true);
    bool value() const noexcept { return m_value; }

    float measure(float availableWidth) override;
    void paint(Canvas& canvas) override;
    void onPointerUp(D2D1_POINT_2F point, bool insideBounds) override;
    bool onKey(WPARAM key) override;
    bool focusable() const override { return true; }
    bool tick(std::chrono::steady_clock::time_point now) override;
    float desiredWidth() const override;

private:
    void toggle();

    bool m_value = false;
    Animated m_knob{0.0f};
    Handler m_onChanged;
};

class Slider : public Widget {
public:
    using Handler = std::function<void(float)>;

    // The range is a pair of decimals the user is shown, so it is held as they are written
    // rather than as the floats nearest to them; see ui::snapToStep.
    Slider(double minimum, double maximum, float value, Handler onChanged);

    void setValue(float value, bool animate = true);
    float value() const noexcept { return m_value; }
    // A double, and deliberately: the step is a decimal the user is shown ("5 %", "0.05"),
    // and the float nearest to it is not that decimal. See ui::snapToStep.
    void setStep(double step) { m_step = step; }
    // Drawn to the right of the track; "80 %" and the like.
    void setFormatter(std::function<std::wstring(float)> formatter);

    float measure(float availableWidth) override;
    void paint(Canvas& canvas) override;
    void onPointerDown(D2D1_POINT_2F point) override;
    void onPointerMove(D2D1_POINT_2F point) override;
    void onPointerUp(D2D1_POINT_2F point, bool insideBounds) override;
    bool onKey(WPARAM key) override;
    bool focusable() const override { return true; }
    bool tick(std::chrono::steady_clock::time_point now) override;
    float desiredWidth() const override;

private:
    D2D1_RECT_F trackRect() const;
    void setFromPoint(D2D1_POINT_2F point);
    void commit(float value);

    double m_minimum = 0.0;
    double m_maximum = 1.0;
    float m_value = 0.0f;
    double m_step = 0.0;  // 0 = continuous
    Animated m_position{0.0f};
    Handler m_onChanged;
    std::function<std::wstring(float)> m_formatter;
    TextBlock m_readout;
};

// The drop-down list a ComboBox raises. Created and owned by the ComboBox; it exists in the
// header only because the host has to know the type it is painting as an overlay.
class ComboPopup : public Widget {
public:
    using Handler = std::function<void(int)>;

    ComboPopup(std::vector<std::wstring> const& items, int selected, Handler onPick);

    float measure(float availableWidth) override;
    void paint(Canvas& canvas) override;
    void onPointerMove(D2D1_POINT_2F point) override;
    void onPointerUp(D2D1_POINT_2F point, bool insideBounds) override;
    bool onKey(WPARAM key) override;

    int highlighted() const noexcept { return m_highlighted; }

private:
    int indexAt(D2D1_POINT_2F point) const;

    std::vector<TextBlock> m_items;
    int m_selected = 0;
    int m_highlighted = 0;
    Handler m_onPick;
};

class ComboBox : public Widget {
public:
    using Handler = std::function<void(int)>;

    ComboBox(std::vector<std::wstring> items, int selected, Handler onChanged);

    void setSelected(int index);
    int selected() const noexcept { return m_selected; }

    float measure(float availableWidth) override;
    void paint(Canvas& canvas) override;
    void onPointerUp(D2D1_POINT_2F point, bool insideBounds) override;
    bool onKey(WPARAM key) override;
    bool focusable() const override { return true; }
    bool tick(std::chrono::steady_clock::time_point now) override;
    void attach(WidgetHost* host) override;
    float desiredWidth() const override;

private:
    void open();
    void close();
    void pick(int index);

    std::vector<std::wstring> m_items;
    TextBlock m_label;
    int m_selected = 0;
    bool m_open = false;
    Animated m_chevron{0.0f};
    Handler m_onChanged;
    std::unique_ptr<ComboPopup> m_popup;
};

// One row of a settings page: an optional icon, a title, an optional description, and one
// control on the right. Below `wrapThreshold` DIPs of width the control drops onto its own
// line, which is what makes the same page usable in a narrow flyout.
class SettingsCard : public Widget {
public:
    SettingsCard(std::wstring glyph, std::wstring title);

    void setTitle(std::wstring title);
    void setDescription(std::wstring description);
    Widget* setControl(std::unique_ptr<Widget> control);
    Widget* control() const noexcept { return m_control.get(); }

    // A clickable card runs the hover and pressed states and reports clicks; a plain one is
    // static, which is the usual case.
    void setOnClick(std::function<void()> handler);

    void setCornerRadii(float top, float bottom);

    // An expander paints one rounded block behind its child rows, so those rows draw their
    // content only. `inset` shifts the text column right, which is how a child row lines up
    // with the header's text rather than with its icon.
    void setChrome(bool drawBackground) { m_chrome = drawBackground; }
    void setContentInset(float inset) { m_contentInset = inset; }

    // Room kept free on the right, for something the card's owner draws there -- the
    // expander's chevron is the only such thing so far.
    void setTrailingInset(float inset) { m_trailingInset = inset; }

    float measure(float availableWidth) override;
    void arrange(D2D1_RECT_F bounds) override;
    void paint(Canvas& canvas) override;
    bool tick(std::chrono::steady_clock::time_point now) override;
    Widget* hitTest(D2D1_POINT_2F point) override;
    void onPointerUp(D2D1_POINT_2F point, bool insideBounds) override;
    bool onKey(WPARAM key) override;
    bool focusable() const override { return static_cast<bool>(m_onClick); }
    void collectFocusable(std::vector<Widget*>& out) override;
    void attach(WidgetHost* host) override;

private:
    float textColumnLeft() const;

    std::wstring m_glyph;
    TextBlock m_title;
    TextBlock m_description;
    std::unique_ptr<Widget> m_control;
    std::function<void()> m_onClick;
    float m_topRadius = Metrics::controlCornerRadius;
    float m_bottomRadius = Metrics::controlCornerRadius;
    float m_contentInset = 0.0f;
    float m_trailingInset = 0.0f;
    bool m_chrome = true;
    bool m_wrapped = false;
};

// A group header, its description, and the cards under it, spaced as PowerToys spaces them.
class SettingsGroup : public Container {
public:
    explicit SettingsGroup(std::wstring header, std::wstring description = {});

    SettingsCard* addCard(std::wstring glyph, std::wstring title);

    float measure(float availableWidth) override;
    void arrange(D2D1_RECT_F bounds) override;
    void paint(Canvas& canvas) override;

private:
    TextBlock m_header;
    TextBlock m_description;
    float m_headerHeight = 0.0f;
};

// A card row that opens to reveal more rows, joined into one rounded block.
class Expander : public Container {
public:
    Expander(std::wstring glyph, std::wstring title);

    void setDescription(std::wstring description);
    Widget* setHeaderControl(std::unique_ptr<Widget> control);
    SettingsCard* addRow(std::wstring title);
    void setExpanded(bool expanded, bool animate = true);
    bool expanded() const noexcept { return m_expanded; }

    float measure(float availableWidth) override;
    void arrange(D2D1_RECT_F bounds) override;
    void paint(Canvas& canvas) override;
    bool tick(std::chrono::steady_clock::time_point now) override;
    Widget* hitTest(D2D1_POINT_2F point) override;

private:
    SettingsCard* m_header = nullptr;
    bool m_expanded = false;
    Animated m_reveal{0.0f};
    float m_contentHeight = 0.0f;
};

// A scrolling viewport with a Fluent scrollbar: a two-DIP rail that grows into a six-DIP
// thumb over a track when the pointer is anywhere in the view.
class ScrollView : public Widget {
public:
    Widget* setContent(std::unique_ptr<Widget> content);
    Widget* content() const noexcept { return m_content.get(); }

    void scrollBy(float delta);
    void scrollTo(float offset);
    float offset() const noexcept { return m_offset; }

    float measure(float availableWidth) override;
    void arrange(D2D1_RECT_F bounds) override;
    void paint(Canvas& canvas) override;
    bool tick(std::chrono::steady_clock::time_point now) override;
    Widget* hitTest(D2D1_POINT_2F point) override;
    void onPointerEnter() override;
    void onPointerLeave() override;
    void onPointerDown(D2D1_POINT_2F point) override;
    void onPointerMove(D2D1_POINT_2F point) override;
    void onPointerUp(D2D1_POINT_2F point, bool insideBounds) override;
    bool onWheel(float delta) override;
    bool onKey(WPARAM key) override;
    void collectFocusable(std::vector<Widget*>& out) override;
    void attach(WidgetHost* host) override;

    // Brings a widget into view; the host calls this when focus lands off-screen.
    void reveal(D2D1_RECT_F rect);

private:
    D2D1_RECT_F thumbRect() const;
    float maximumOffset() const;

    std::unique_ptr<Widget> m_content;
    float m_offset = 0.0f;
    float m_contentHeight = 0.0f;
    Animated m_scrollbar{0.0f};
    bool m_draggingThumb = false;
    float m_dragAnchor = 0.0f;
};

// The vertical rail of a NavigationView, with the selection pill that stretches towards its
// destination and settles, the way WinUI animates it.
class NavigationRail : public Widget {
public:
    using Handler = std::function<void(int)>;

    struct Item {
        std::wstring glyph;
        std::wstring label;
    };

    NavigationRail(std::vector<Item> items, Handler onSelected);

    void setSelected(int index, bool animate = true);
    int selected() const noexcept { return m_selected; }
    // Above this width the rail also shows the labels, as an expanded NavigationView does.
    void setExpanded(bool expanded) { m_expanded = expanded; }

    float measure(float availableWidth) override;
    void paint(Canvas& canvas) override;
    void onPointerMove(D2D1_POINT_2F point) override;
    void onPointerLeave() override;
    void onPointerDown(D2D1_POINT_2F point) override;
    void onPointerUp(D2D1_POINT_2F point, bool insideBounds) override;
    bool onKey(WPARAM key) override;
    bool focusable() const override { return true; }
    bool tick(std::chrono::steady_clock::time_point now) override;

private:
    int indexAt(D2D1_POINT_2F point) const;
    float itemTop(int index) const;

    std::vector<Item> m_items;
    std::vector<TextBlock> m_labels;
    int m_selected = 0;
    int m_hovered = -1;
    int m_pressedItem = -1;
    bool m_expanded = false;
    Animated m_pillPosition{0.0f};
    Animated m_pillStretch{1.0f};
    Handler m_onSelected;
};

// Routes Win32 input into a widget tree, owns focus and hover, and paints it.
//
// One host per window. It needs the window only to ask for another frame and to read the
// current scale, so a window can own several hosts if it ever wants independent trees.
class WidgetHost {
public:
    explicit WidgetHost(D2DWindow& window);
    ~WidgetHost();

    WidgetHost(WidgetHost const&) = delete;
    WidgetHost& operator=(WidgetHost const&) = delete;

    Widget* setRoot(std::unique_ptr<Widget> root);
    Widget* root() const noexcept { return m_root.get(); }

    // Measures and arranges the tree into `bounds`. Cheap to call every frame: the layout is
    // recomputed only when the rectangle or the content changed.
    void layout(D2D1_RECT_F bounds);
    void paint(Canvas& canvas);

    bool tick(std::chrono::steady_clock::time_point now);

    // True when the message was consumed and `result` should be returned from the window
    // procedure.
    bool handleMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result);

    void invalidate();
    void invalidateLayout();

    // Forgets hover, capture, focus and any open overlay. A page that is about to replace its
    // widgets must call this first: the host holds raw pointers into the tree it is destroying,
    // and the next mouse move would dereference them.
    void resetInput();

    void setFocus(Widget* widget);
    Widget* focused() const noexcept { return m_focused; }
    void moveFocus(bool forward);
    // The focus ring is drawn only once the keyboard has been used, matching Windows.
    bool focusVisible() const noexcept { return m_focusVisible; }

    // The combo drop-down: one overlay at a time, drawn last and given input first.
    void openOverlay(Widget* overlay);
    void closeOverlay(Widget* overlay);
    Widget* overlay() const noexcept { return m_overlay; }

    D2D1_RECT_F bounds() const noexcept { return m_bounds; }
    float scale() const;

private:
    D2D1_POINT_2F toDip(LPARAM lparam) const;
    void updateHover(D2D1_POINT_2F point);

    D2DWindow& m_window;
    std::unique_ptr<Widget> m_root;
    D2D1_RECT_F m_bounds{};
    bool m_layoutValid = false;
    bool m_tracking = false;
    bool m_focusVisible = false;
    Widget* m_hovered = nullptr;
    // Root-to-leaf path of the hovered widget. A scroll view has to know the pointer is
    // somewhere inside it, not only when it is over the scrollbar itself.
    std::vector<Widget*> m_hoverChain;
    Widget* m_captured = nullptr;
    Widget* m_focused = nullptr;
    Widget* m_overlay = nullptr;
};

}  // namespace peek::ui
