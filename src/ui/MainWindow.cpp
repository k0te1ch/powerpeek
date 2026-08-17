#include "ui/MainWindow.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "core/Strings.h"
#include "platform/Platform.h"
#include "ui/Drawing.h"
#include "ui/Widgets.h"
#include "ui/pages/AboutPage.h"
#include "ui/pages/ControllersPage.h"
#include "ui/pages/HistoryPage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/pages/SoundsPage.h"

namespace peek::ui {
namespace {

constexpr float kCaptionButtonWidth = 46.0f;
constexpr float kCaptionGlyphSize = 10.0f;
constexpr float kTitleIconSize = 16.0f;
constexpr float kTitleIconLeft = 14.0f;
constexpr float kTitleTextLeft = 42.0f;

// Above this the navigation rail shows its labels, as an expanded NavigationView does.
constexpr float kExpandedNavThreshold = 780.0f;
constexpr float kExpandedNavWidth = 200.0f;

constexpr int kPageCount = 5;

enum class CaptionCommand { Minimise, MaximiseOrRestore, Close };

// The shell paints this red on both themes, exactly as the system title bar does; the
// palette's critical colour is a light pink in dark theme and would read as a highlight.
constexpr D2D1_COLOR_F kCloseHover{0.769f, 0.169f, 0.110f, 1.0f};

bool sameDevices(std::vector<DeviceInfo> const& a, std::vector<DeviceInfo> const& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](DeviceInfo const& x, DeviceInfo const& y) { return x.id == y.id; });
}

// A reading that moved is also a sample the history may have just recorded.
bool sameReadings(std::vector<DeviceInfo> const& a, std::vector<DeviceInfo> const& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](DeviceInfo const& x, DeviceInfo const& y) {
                          return x.percent == y.percent && x.charge == y.charge;
                      });
}

D2D1_COLOR_F mix(D2D1_COLOR_F const& from, D2D1_COLOR_F const& to, float t) {
    float const k = std::clamp(t, 0.0f, 1.0f);
    return D2D1_COLOR_F{from.r + (to.r - from.r) * k, from.g + (to.g - from.g) * k,
                        from.b + (to.b - from.b) * k, from.a + (to.a - from.a) * k};
}

// A title-bar button: no border, a full-height hover fill, and the close button going red.
class CaptionButton : public Widget {
public:
    CaptionButton(std::wstring glyph, std::function<void()> onClick, bool danger)
        : m_glyph(std::move(glyph)), m_onClick(std::move(onClick)), m_danger(danger) {}

    void setGlyph(std::wstring glyph) {
        m_glyph = std::move(glyph);
        invalidate();
    }

    float measure(float) override { return Metrics::titleBarHeight; }

    void paint(Canvas& canvas) override {
        auto const& palette = theme().colors();
        float const hover = m_hoverFade.value();
        float const press = m_pressFade.value();

        D2D1_COLOR_F fill = m_danger
                                ? kCloseHover
                                : (press > 0.0f ? palette.subtleFillTertiary
                                                : palette.subtleFillSecondary);
        fill.a *= m_danger ? hover * (1.0f - press * 0.1f) : std::max(hover, press);
        if (fill.a > 0.0f) {
            fillRect(canvas, m_bounds, fill);
        }

        D2D1_COLOR_F const foreground =
            m_danger ? mix(palette.textPrimary, D2D1::ColorF(D2D1::ColorF::White), hover)
                     : palette.textPrimary;
        drawIcon(canvas, m_glyph, kCaptionGlyphSize, m_bounds, foreground);
    }

    void onPointerUp(D2D1_POINT_2F, bool insideBounds) override {
        if (insideBounds && m_onClick) {
            m_onClick();
        }
    }

private:
    std::wstring m_glyph;
    std::function<void()> m_onClick;
    bool m_danger = false;
};

// The area the current page lives in: the NavigationView content grid, rounded where it meets
// the pane and square where it meets the window edge.
class PageSlot : public Container {
public:
    void showPage(int index) {
        for (std::size_t i = 0; i < m_children.size(); ++i) {
            m_children[i]->setVisible(static_cast<int>(i) == index);
        }
    }

    // Fills whatever the shell gives it; its height is never a function of its content.
    float measure(float) override { return 0.0f; }

    void arrange(D2D1_RECT_F bounds) override {
        Widget::arrange(bounds);
        for (auto const& child : m_children) {
            if (child->visible()) {
                child->arrange(bounds);
            }
        }
    }

    void paint(Canvas& canvas) override {
        auto const& palette = theme().colors();
        float const radius = Metrics::overlayCornerRadius;
        fillRounded(canvas, m_bounds, radius, palette.layerFill);
        // Only the corner facing the navigation pane is rounded; the other three meet either
        // the window edge, which has its own radius, or nothing at all.
        fillRect(canvas,
                 D2D1::RectF(m_bounds.right - radius, m_bounds.top, m_bounds.right,
                             m_bounds.top + radius),
                 palette.layerFill);
        Container::paint(canvas);
    }
};

// The window chrome as one widget tree: caption buttons, navigation rail, page slot.
class Shell : public Container {
public:
    Shell(std::vector<NavigationRail::Item> items,
          NavigationRail::Handler onNavigate,
          std::function<void(CaptionCommand)> onCaption) {
        m_rail = emplace<NavigationRail>(std::move(items), std::move(onNavigate));
        m_slot = emplace<PageSlot>();
        m_minimise = emplace<CaptionButton>(
            std::wstring(glyph::kMinimise), [onCaption] { onCaption(CaptionCommand::Minimise); },
            false);
        m_maximise = emplace<CaptionButton>(
            std::wstring(glyph::kMaximise),
            [onCaption] { onCaption(CaptionCommand::MaximiseOrRestore); }, false);
        m_close = emplace<CaptionButton>(
            std::wstring(glyph::kClose), [onCaption] { onCaption(CaptionCommand::Close); }, true);
    }

    NavigationRail& rail() const noexcept { return *m_rail; }
    PageSlot& slot() const noexcept { return *m_slot; }

    // What the title bar has to keep clear of, so that dragging the window still works
    // everywhere else along the strip.
    float captionWidth() const noexcept { return kCaptionButtonWidth * 3.0f; }

    void setMaximised(bool maximised) {
        m_maximise->setGlyph(std::wstring(maximised ? glyph::kRestore : glyph::kMaximise));
    }

    // Fills the window body; see PageSlot::measure.
    float measure(float) override { return 0.0f; }

    void arrange(D2D1_RECT_F bounds) override {
        Widget::arrange(bounds);

        float x = bounds.right;
        for (CaptionButton* button : {m_close, m_maximise, m_minimise}) {
            x -= kCaptionButtonWidth;
            button->arrange(
                D2D1::RectF(x, bounds.top, x + kCaptionButtonWidth, bounds.top + Metrics::titleBarHeight));
        }

        float const railWidth = bounds.right - bounds.left >= kExpandedNavThreshold
                                    ? kExpandedNavWidth
                                    : Metrics::navPaneWidth;
        m_rail->setExpanded(railWidth > Metrics::navPaneWidth);
        float const top = bounds.top + Metrics::titleBarHeight;
        m_rail->measure(railWidth);
        m_rail->arrange(D2D1::RectF(bounds.left, top, bounds.left + railWidth, bounds.bottom));
        m_slot->arrange(D2D1::RectF(bounds.left + railWidth, top, bounds.right, bounds.bottom));
    }

private:
    NavigationRail* m_rail = nullptr;
    PageSlot* m_slot = nullptr;
    CaptionButton* m_minimise = nullptr;
    CaptionButton* m_maximise = nullptr;
    CaptionButton* m_close = nullptr;
};

}  // namespace

struct MainWindow::Impl {
    explicit Impl(D2DWindow& window) : host(window) {}

    WidgetHost host;
    WindowShadow shadow;
    Dependencies deps;
    std::vector<DeviceInfo> controllers;
    std::array<Page*, kPageCount> pages{};
    Shell* shell = nullptr;
    int selected = 0;

    // True while a page is writing through SettingsStore, so the resulting `changed` signal
    // is recognised as this window's own doing and does not throw the page away underneath it.
    bool applying = false;

    // A language change replaces every string in the tree, the navigation rail included, so
    // the whole shell is rebuilt -- on the next frame, never inside the control that asked.
    bool rebuildPending = false;
};

MainWindow::MainWindow() : m_impl(std::make_unique<Impl>(*this)) {}

MainWindow::~MainWindow() = default;

bool MainWindow::create(Dependencies dependencies) {
    m_impl->deps = std::move(dependencies);

    CreateParams params;
    params.className = L"PowerPeek.MainWindow";
    params.title = std::wstring(text(Text::AppName));
    params.initialSizeDip = {960, 700};
    params.minimumSizeDip = {560, 460};
    if (!createWindow(params)) {
        return false;
    }

    // Only ever seen in the Alt+Tab thumbnail and the taskbar preview, since the window draws
    // no system frame of its own -- but wrong there is still wrong.
    platform::setTitleBarDarkMode(handle(), theme().isDark());
    setBackdrop(SettingsStore::instance().get().backdrop);
    buildShell();
    return true;
}

void MainWindow::buildShell() {
    PageContext context;
    context.controllers = &m_impl->controllers;
    context.history = m_impl->deps.history;
    context.notifications = m_impl->deps.notifications;
    context.refreshControllers = m_impl->deps.refreshControllers;
    context.owner = handle();
    context.systemToastsAvailable = m_impl->deps.systemToastsAvailable;
    context.applySettings = [this](Settings next) {
        m_impl->applying = true;
        SettingsStore::instance().apply(std::move(next));
        m_impl->applying = false;
    };

    std::vector<NavigationRail::Item> items{
        {std::wstring(glyph::kGamepad), std::wstring(text(Text::NavDevices))},
        {std::wstring(glyph::kChart), std::wstring(text(Text::NavHistory))},
        {std::wstring(glyph::kVolume), std::wstring(text(Text::NavSounds))},
        {std::wstring(glyph::kSettings), std::wstring(text(Text::NavSettings))},
        {std::wstring(glyph::kInfo), std::wstring(text(Text::NavAbout))},
    };

    auto shell = std::make_unique<Shell>(
        std::move(items), [this](int index) { showPage(index); },
        [this](CaptionCommand command) {
            switch (command) {
                case CaptionCommand::Minimise:
                    ShowWindow(handle(), SW_MINIMIZE);
                    return;
                case CaptionCommand::MaximiseOrRestore:
                    ShowWindow(handle(), isMaximised() ? SW_RESTORE : SW_MAXIMIZE);
                    return;
                case CaptionCommand::Close:
                    requestClose();
                    return;
            }
        });

    PageSlot& slot = shell->slot();
    m_impl->pages[0] = slot.emplace<ControllersPage>(context);
    m_impl->pages[1] = slot.emplace<HistoryPage>(context);
    m_impl->pages[2] = slot.emplace<SoundsPage>(context);
    m_impl->pages[3] = slot.emplace<SettingsPage>(context);
    m_impl->pages[4] = slot.emplace<AboutPage>(std::move(context));

    m_impl->shell = shell.get();
    m_impl->host.setRoot(std::move(shell));
    m_impl->shell->setMaximised(isMaximised());
    showPage(m_impl->selected);
}

void MainWindow::showPage(int index) {
    m_impl->selected = std::clamp(index, 0, kPageCount - 1);
    m_impl->shell->rail().setSelected(m_impl->selected);
    m_impl->shell->slot().showPage(m_impl->selected);
    m_impl->host.invalidateLayout();
}

void MainWindow::showAndActivate() {
    if (!handle()) {
        return;
    }
    ShowWindow(handle(), IsIconic(handle()) ? SW_RESTORE : SW_SHOW);
    platform::bringToForeground(handle());
    invalidate();
}

void MainWindow::hide() {
    if (handle()) {
        ShowWindow(handle(), SW_HIDE);
    }
}

void MainWindow::setControllers(std::vector<DeviceInfo> controllers) {
    bool const shapeHeld = sameDevices(m_impl->controllers, controllers);
    bool const levelsHeld = shapeHeld && sameReadings(m_impl->controllers, controllers);
    m_impl->controllers = std::move(controllers);
    if (!m_impl->shell || levelsHeld) {
        return;
    }

    if (shapeHeld) {
        // The cards stay; their gauges sweep to the new levels.
        m_impl->pages[0]->refreshValues();
    } else {
        m_impl->pages[0]->invalidateContent();
    }
    m_impl->pages[1]->invalidateContent();
    invalidate();
}

void MainWindow::settingsChanged(Settings const& current, Settings const& previous) {
    if (!m_impl->shell) {
        return;
    }
    if (current.backdrop != previous.backdrop) {
        // Before the `applying` guard below: the settings page is what writes this, and the
        // frame has to change under it rather than wait for the page to be rebuilt.
        setBackdrop(current.backdrop);
    } else if (current.windowOpacity != previous.windowOpacity) {
        invalidate();
    }
    if (current.language != previous.language) {
        m_impl->rebuildPending = true;
        invalidate();
        return;
    }
    if (m_impl->applying) {
        // The page that wrote this is already showing it, and rebuilding it now would destroy
        // the control whose event handler is still on the stack.
        return;
    }
    for (Page* page : m_impl->pages) {
        page->invalidateContent();
    }
}

void MainWindow::requestClose() {
    if (SettingsStore::instance().get().minimiseToTrayOnClose || !m_impl->deps.exitApplication) {
        hide();
        return;
    }
    m_impl->deps.exitApplication();
}

void MainWindow::onThemeChanged() { platform::setTitleBarDarkMode(handle(), theme().isDark()); }

bool MainWindow::isCaptionArea(D2D1_POINT_2F point) const {
    if (!m_impl->shell) {
        return false;
    }
    D2D1_RECT_F const body = bodyRect();
    if (point.y < body.top || point.y >= body.top + Metrics::titleBarHeight) {
        return false;
    }
    return point.x >= body.left && point.x < body.right - m_impl->shell->captionWidth();
}

bool MainWindow::onMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result) {
    switch (message) {
        case WM_CLOSE:
            requestClose();
            result = 0;
            return true;

        case WM_SIZE:
            if (m_impl->shell) {
                m_impl->shell->setMaximised(wparam == SIZE_MAXIMIZED);
            }
            break;  // the base class needs this one too

        default:
            break;
    }
    return m_impl->host.handleMessage(message, wparam, lparam, result);
}

void MainWindow::onPaint(ID2D1DeviceContext& ctx, D2D1_SIZE_F) {
    // The first WM_WINDOWPOSCHANGED arrives from inside createWindow, before the shell exists;
    // the window is not on screen yet, so an empty frame costs nothing.
    if (!m_impl->shell) {
        return;
    }
    if (m_impl->rebuildPending) {
        m_impl->rebuildPending = false;
        buildShell();
    }

    Canvas canvas(ctx);
    auto const& palette = theme().colors();
    D2D1_RECT_F const body = bodyRect();
    float const radius = cornerRadius();

    // Under a backdrop the window rectangle is the body, so there is no transparent margin
    // left to blur a shadow into -- and none is wanted: the compositor is already drawing
    // its own around the frame it now owns.
    if (!isMaximised() && !hasSystemBackdrop()) {
        m_impl->shadow.draw(canvas, body, radius, palette.shadow);
    }

    // Everything above this fill is a layer on top of it: the page area, the settings cards
    // and the flyouts all draw over the window background rather than replacing it. So the
    // window background is the only thing thinned, and the contrast floor it is clamped to
    // (Settings.h) is what keeps body text legible whatever the desktop behind it looks like.
    D2D1_COLOR_F background = palette.windowBackground;
    background.a *= SettingsStore::instance().get().windowOpacity;
    fillRounded(canvas, body, radius, background);

    // Ticked before the layout: an animation that changes how much room a widget needs has to
    // be reflected in this frame, not in the next one.
    if (m_impl->host.tick(frameTime())) {
        requestFrame();
    }
    m_impl->host.layout(body);
    m_impl->host.paint(canvas);

    drawTitleBar(canvas, body);
    if (!isMaximised()) {
        strokeRounded(canvas, body, radius, palette.surfaceStroke);
    }
}

void MainWindow::drawTitleBar(Canvas& canvas, D2D1_RECT_F body) const {
    auto const& palette = theme().colors();
    float const bottom = body.top + Metrics::titleBarHeight;

    drawIcon(canvas, glyph::kGamepad, kTitleIconSize,
             D2D1::RectF(body.left + kTitleIconLeft, body.top,
                         body.left + kTitleIconLeft + kTitleIconSize, bottom),
             palette.accent);
    drawText(canvas, text(Text::AppName), theme().textFormat(TypeStyle::Caption),
             D2D1::RectF(body.left + kTitleTextLeft, body.top,
                         body.right - m_impl->shell->captionWidth(), bottom),
             palette.textPrimary, Align::Start, Align::Center);
}

}  // namespace peek::ui
