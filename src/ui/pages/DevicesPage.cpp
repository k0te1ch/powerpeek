#include "ui/pages/DevicesPage.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Settings.h"
#include "core/Strings.h"
#include "ui/DeviceGrid.h"
#include "ui/Drawing.h"
#include "ui/pages/PageWidgets.h"

namespace peek::ui {
namespace {

constexpr float kTilePadding = 16.0f;
constexpr float kTopRowHeight = 60.0f;
constexpr float kRingSize = 60.0f;
constexpr float kArtHeight = 52.0f;
constexpr float kBadgeSize = 52.0f;
constexpr float kBadgeRadius = 8.0f;
constexpr float kBadgeGlyphSize = 26.0f;
constexpr float kTopRowGap = 12.0f;
constexpr float kLineGap = 2.0f;
constexpr float kAlertBarWidth = 3.0f;
constexpr float kAlertStrokeWidth = 1.5f;
constexpr float kApproximateIndent = 18.0f;
constexpr float kApproximateGlyphSize = 12.0f;
constexpr float kSectionTopMargin = 12.0f;
constexpr float kSectionBottomMargin = 4.0f;
constexpr float kRenameButtonSize = 28.0f;
constexpr float kRenameGlyphSize = 14.0f;
constexpr float kRenameGap = 4.0f;
constexpr std::uint16_t kVendorMicrosoft = 0x045E;

// Microsoft ships a separate product id per transport, and that is the only signal either
// battery provider leaves behind: neither Windows.Devices.Power nor XInput reports the link.
constexpr bool isBluetoothProductId(std::uint16_t pid) noexcept {
    switch (pid) {
        case 0x02E0:  // Xbox One S, Bluetooth rev 1
        case 0x02FD:  // Xbox One S, Bluetooth rev 2
        case 0x0B20:  // Xbox One S, BLE
        case 0x0B05:  // Elite Series 2, Bluetooth
        case 0x0B22:  // Elite Series 2, BLE
        case 0x0B0C:  // Adaptive Controller, Bluetooth
        case 0x0B21:  // Adaptive Controller, BLE
        case 0x0B13:  // Xbox Series X|S, Bluetooth
            return true;
        default:
            return false;
    }
}

// Nothing at all rather than a guess: claiming the wrong transport is worse than staying quiet.
// The badge on the portrait and the status line both come from here, so they cannot disagree.
ControllerLink connectionLink(DeviceInfo const& info) {
    if (info.source == PowerSource::Wired) {
        return ControllerLink::Usb;
    }
    if (info.vendorId == kVendorMicrosoft && isBluetoothProductId(info.productId)) {
        return ControllerLink::Bluetooth;
    }
    if (info.isXboxController && info.source == PowerSource::Battery) {
        return ControllerLink::Wireless;
    }
    return ControllerLink::None;
}

std::optional<std::wstring_view> connectionText(ControllerLink link) {
    switch (link) {
        case ControllerLink::Usb:
            return text(Text::ConnectionUsb);
        case ControllerLink::Wireless:
            return text(Text::ConnectionWireless);
        case ControllerLink::Bluetooth:
            return text(Text::ConnectionBluetooth);
        case ControllerLink::None:
            break;
    }
    return std::nullopt;
}

std::wstring_view chargeText(DeviceInfo const& info) {
    if (info.charge == ChargeState::Unknown && info.source == PowerSource::Wired) {
        return text(Text::StatusWired);
    }
    // The device tree reports a level and nothing about where it is heading. "Unknown" would
    // read as though the level were in doubt; what is actually known is that it is there.
    if (info.charge == ChargeState::Unknown && info.hasBattery()) {
        return text(Text::StatusConnected);
    }
    return toString(info.charge);
}

std::wstring statusLine(DeviceInfo const& info) {
    std::wstring line;
    if (auto const connection = connectionText(connectionLink(info))) {
        line.append(*connection);
        line.append(L" \x00B7 ");
    }
    line.append(chargeText(info));
    return line;
}

std::wstring updatedLine(DeviceInfo const& info) {
    auto const age = std::chrono::system_clock::now() - info.lastUpdate;
    auto const minutes = std::chrono::duration_cast<std::chrono::minutes>(age).count();
    if (minutes <= 0) {
        return std::wstring(text(Text::UpdatedJustNow));
    }
    return formatText(Text::UpdatedMinutesAgo, minutes);
}

Text sectionTitle(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::Gamepad:
            return Text::GroupGamepads;
        case DeviceKind::Headset:
            return Text::GroupHeadsets;
        case DeviceKind::Mouse:
            return Text::GroupMice;
        case DeviceKind::Keyboard:
            return Text::GroupKeyboards;
        case DeviceKind::Pen:
            return Text::GroupPens;
        case DeviceKind::Other:
            break;
    }
    return Text::GroupOther;
}

}  // namespace

namespace {

// The pencil on a tile. It takes no room of its own in the text column's flow -- the name
// always leaves space for it -- and it is drawn only while the pointer is over the tile or the
// button has keyboard focus, so a grid at rest is not a grid of pencils. It stays in the Tab
// order throughout, which is what keeps renaming reachable without a mouse.
class RenameButton : public Widget {
public:
    explicit RenameButton(std::function<void()> onClick) : m_onClick(std::move(onClick)) {}

    // 0 hidden, 1 fully drawn; the tile animates it with the pointer.
    void setReveal(float reveal) noexcept { m_reveal = reveal; }

    float measure(float) override { return kRenameButtonSize; }
    bool focusable() const override { return true; }

    void paint(Canvas& canvas) override {
        float const shown = focused() ? 1.0f : std::max(m_reveal, m_hoverFade.value());
        if (shown <= 0.0f) {
            return;
        }
        auto const& palette = theme().colors();
        D2D1_COLOR_F fill = palette.controlFillSecondary;
        fill.a *= std::max(m_hoverFade.value(), m_pressFade.value()) * shown;
        fillRounded(canvas, m_bounds, Metrics::controlCornerRadius, fill);

        D2D1_COLOR_F glyphColor = palette.textSecondary;
        glyphColor.a *= shown;
        drawIcon(canvas, glyph::kEdit, kRenameGlyphSize, m_bounds, glyphColor);

        if (focused() && host() && host()->focusVisible()) {
            drawFocusRing(canvas, m_bounds, Metrics::controlCornerRadius);
        }
    }

    void onPointerUp(D2D1_POINT_2F, bool insideBounds) override {
        if (insideBounds && m_onClick) {
            m_onClick();
        }
    }

    bool onKey(WPARAM key) override {
        if (key != VK_RETURN && key != VK_SPACE) {
            return false;
        }
        if (m_onClick) {
            m_onClick();
        }
        return true;
    }

private:
    std::function<void()> m_onClick;
    float m_reveal = 0.0f;
};

// The field a tile's name turns into. TextBox already commits on Enter and on losing focus and
// reverts on Escape; this adds the one thing the tile needs to know, which is that editing is
// over, and whether the keyboard ended it (and so wants focus back on the pencil).
class NameEditor : public TextBox {
public:
    using Done = std::function<void(bool byKeyboard)>;

    NameEditor(std::wstring text, std::wstring placeholder, Handler onCommit, Done onDone)
        : TextBox(std::move(text), std::move(placeholder), kMaxDeviceNameLength,
                  std::move(onCommit)),
          m_onDone(std::move(onDone)) {}

    bool onKey(WPARAM key) override {
        bool const handled = TextBox::onKey(key);
        if ((key == VK_RETURN || key == VK_ESCAPE) && m_onDone) {
            m_onDone(true);
        }
        return handled;
    }

protected:
    void onFocusChanged(bool focused) override {
        TextBox::onFocusChanged(focused);
        if (!focused && m_onDone) {
            m_onDone(false);
        }
    }

private:
    Done m_onDone;
};

}  // namespace

// One device: what it is, the level on a ring that sweeps to it rather than jumping, and the
// few lines of text that say how it is doing. A level at or below a warning threshold on its
// way down tints the edge of the tile, so the one that needs a charger stands out of the grid.
//
// The name line doubles as the rename field. Its slot is always as tall as the field, so
// switching between the two moves nothing else on the tile or in the grid.
class DeviceTile : public Container {
public:
    using Rename = std::function<void(std::wstring const&)>;

    DeviceTile(DeviceInfo const& info, std::optional<std::chrono::minutes> remaining,
               std::wstring customName, Rename onRename) {
        m_name.setStyle(TypeStyle::BodyStrong);
        m_name.setWrapping(true);
        m_status.setStyle(TypeStyle::Caption);
        m_status.setWrapping(true);
        m_approximate.setStyle(TypeStyle::Caption);
        m_remaining.setStyle(TypeStyle::Caption);
        m_remaining.setWrapping(true);
        m_updated.setStyle(TypeStyle::Caption);

        std::wstring reported = info.reportedName.empty() ? info.name : info.reportedName;
        m_editor = emplace<NameEditor>(std::move(customName), std::move(reported),
                                       std::move(onRename),
                                       [this](bool byKeyboard) { stopEditing(byKeyboard); });
        m_editor->setVisible(false);
        m_pencil = emplace<RenameButton>([this] { startEditing(); });

        apply(info, remaining);
        m_fill.snapTo(fraction(info));
    }

    std::wstring const& id() const noexcept { return m_id; }

    void update(DeviceInfo const& info, std::optional<std::chrono::minutes> remaining) {
        apply(info, remaining);
        m_fill.animateTo(fraction(info), kDurationSlow, Easing::Entrance);
        invalidateLayout();
        invalidate();
    }

    float measure(float availableWidth) override {
        m_textWidth = std::max(40.0f, availableWidth - kTilePadding * 2.0f);
        float const nameWidth = std::max(20.0f, m_textWidth - kRenameButtonSize - kRenameGap);
        m_nameSlot = nameSlotHeight(m_name.measure(nameWidth), Metrics::controlHeight);

        float height = kTilePadding + kTopRowHeight + kTopRowGap + m_nameSlot;
        height += kLineGap + m_status.measure(m_textWidth);
        if (!m_approximate.empty()) {
            height += kLineGap + m_approximate.measure(m_textWidth - kApproximateIndent);
        }
        if (!m_remaining.empty()) {
            height += kLineGap + m_remaining.measure(m_textWidth);
        }
        height += kLineGap + m_updated.measure(m_textWidth);
        return height + kTilePadding;
    }

    void arrange(D2D1_RECT_F bounds) override {
        Widget::arrange(bounds);
        float const left = bounds.left + kTilePadding;
        float const right = left + m_textWidth;
        float const slotTop = nameSlotTop();

        float const editorTop = slotTop + (m_nameSlot - Metrics::controlHeight) * 0.5f;
        m_editor->arrange(
            D2D1::RectF(left, editorTop, right, editorTop + Metrics::controlHeight));

        float const pencilTop = slotTop + (m_nameSlot - kRenameButtonSize) * 0.5f;
        m_pencil->arrange(D2D1::RectF(right - kRenameButtonSize, pencilTop, right,
                                      pencilTop + kRenameButtonSize));
    }

    // The tile itself is a target, not only its pencil: the pointer anywhere on it is what
    // reveals the pencil, and a click on it is a click away from an open name field.
    Widget* hitTest(D2D1_POINT_2F point) override {
        if (Widget* child = Container::hitTest(point)) {
            return child;
        }
        bool const inside = point.x >= m_bounds.left && point.x < m_bounds.right &&
                            point.y >= m_bounds.top && point.y < m_bounds.bottom;
        return visible() && inside ? this : nullptr;
    }

    void onPointerEnter() override {
        m_reveal.animateTo(1.0f, kDurationFast, Easing::Entrance);
        invalidate();
    }

    void onPointerLeave() override {
        m_reveal.animateTo(0.0f, kDurationFast, Easing::Entrance);
        invalidate();
    }

    bool tick(std::chrono::steady_clock::time_point now) override {
        bool running = Container::tick(now);
        running |= m_fill.tick(now);
        running |= m_reveal.tick(now);
        return running;
    }

    void paint(Canvas& canvas) override {
        auto const& palette = theme().colors();
        fillRounded(canvas, m_bounds, Metrics::controlCornerRadius, palette.cardFill);
        if (m_alert == TileAlert::None) {
            strokeRounded(canvas, m_bounds, Metrics::controlCornerRadius, palette.cardStroke);
        } else {
            D2D1_COLOR_F const accent = alertColor();
            D2D1_COLOR_F edge = accent;
            edge.a *= 0.6f;
            strokeRounded(canvas, m_bounds, Metrics::controlCornerRadius, edge,
                          kAlertStrokeWidth);
            float const inset = kTilePadding * 0.5f;
            fillRounded(canvas,
                        D2D1::RectF(m_bounds.left + 1.0f, m_bounds.top + inset,
                                    m_bounds.left + 1.0f + kAlertBarWidth,
                                    m_bounds.bottom - inset),
                        kAlertBarWidth * 0.5f, accent);
        }

        float const left = m_bounds.left + kTilePadding;
        float const right = m_bounds.right - kTilePadding;
        float const top = m_bounds.top + kTilePadding;
        float const rowCentre = top + kTopRowHeight * 0.5f;

        paintKind(canvas, left, rowCentre);
        drawRingGauge(canvas,
                      D2D1::RectF(right - kRingSize, rowCentre - kRingSize * 0.5f, right,
                                  rowCentre + kRingSize * 0.5f),
                      gauge(), theme().textFormat(TypeStyle::BodyStrong));

        float const slotTop = nameSlotTop();
        if (!m_editing) {
            m_name.draw(canvas,
                        D2D1::Point2F(left, slotTop + (m_nameSlot - m_name.size().height) * 0.5f),
                        palette.textPrimary);
        }

        float y = slotTop + m_nameSlot + kLineGap;
        auto line = [&](TextBlock& block, D2D1_COLOR_F color) {
            block.draw(canvas, D2D1::Point2F(left, y), color);
            y += block.size().height + kLineGap;
        };
        line(m_status, palette.textSecondary);
        if (!m_approximate.empty()) {
            // XInput only reports four buckets; saying so with a warning glyph is the whole
            // difference between an honest gauge and one that invents a precision it lacks.
            drawIcon(canvas, glyph::kWarning, kApproximateGlyphSize,
                     D2D1::RectF(left, y, left + kApproximateIndent,
                                 y + m_approximate.size().height),
                     palette.caution);
            m_approximate.draw(canvas, D2D1::Point2F(left + kApproximateIndent, y),
                               palette.caution);
            y += m_approximate.size().height + kLineGap;
        }
        if (!m_remaining.empty()) {
            line(m_remaining, m_alert == TileAlert::None ? palette.textSecondary : alertColor());
        }
        line(m_updated, palette.textTertiary);

        m_pencil->setReveal(m_reveal.value());
        Container::paint(canvas);
    }

private:
    float nameSlotTop() const noexcept {
        return m_bounds.top + kTilePadding + kTopRowHeight + kTopRowGap;
    }

    void startEditing() {
        if (m_editing || host() == nullptr) {
            return;
        }
        m_editing = true;
        m_pencil->setVisible(false);
        m_editor->setVisible(true);
        host()->setFocus(m_editor);
        invalidate();
    }

    // Reached twice when the keyboard ends the edit -- once for the key, once for the focus
    // the key moves back to the pencil -- and only the first one does anything.
    void stopEditing(bool byKeyboard) {
        if (!m_editing) {
            return;
        }
        m_editing = false;
        m_editor->setVisible(false);
        m_pencil->setVisible(true);
        if (byKeyboard && host() != nullptr) {
            host()->setFocus(m_pencil);
        }
        invalidate();
    }

    static float fraction(DeviceInfo const& info) {
        return info.percent < 0 ? 0.0f : static_cast<float>(info.percent) / 100.0f;
    }

    D2D1_COLOR_F alertColor() const {
        auto const& palette = theme().colors();
        return m_alert == TileAlert::Critical ? palette.critical : palette.caution;
    }

    // The pad keeps its portrait; every other kind gets its Fluent glyph on a badge of the
    // same height and a similar tone, so a row of mixed tiles reads as one set.
    void paintKind(Canvas& canvas, float left, float centreY) const {
        if (m_kind == DeviceKind::Gamepad) {
            drawControllerArt(canvas,
                              D2D1::RectF(left, centreY - kArtHeight * 0.5f,
                                          left + controllerArtWidth(kArtHeight),
                                          centreY + kArtHeight * 0.5f),
                              art());
            return;
        }
        auto const& palette = theme().colors();
        D2D1_COLOR_F fill = palette.controlStrongFill;
        fill.a *= 0.16f;
        D2D1_RECT_F const badge = D2D1::RectF(left, centreY - kBadgeSize * 0.5f,
                                              left + kBadgeSize, centreY + kBadgeSize * 0.5f);
        fillRounded(canvas, badge, kBadgeRadius, fill);
        drawIcon(canvas, kindGlyph(m_kind), kBadgeGlyphSize, badge, palette.textSecondary);
    }

    void apply(DeviceInfo const& info, std::optional<std::chrono::minutes> remaining) {
        auto const& settings = SettingsStore::instance().get();
        m_id = info.id;
        m_kind = info.kind;
        m_percent = info.percent;
        m_charging = info.charge == ChargeState::Charging;
        m_coarse = info.fidelity == Fidelity::Coarse && info.percent >= 0;
        m_link = connectionLink(info);
        m_alert = tileAlert(info, settings.lowThresholdPercent, settings.criticalThresholdPercent);

        m_name.setText(info.name);
        m_status.setText(statusLine(info));
        m_approximate.setText(m_coarse ? std::wstring(text(Text::ApproximateSuffix))
                                       : std::wstring{});
        m_remaining.setText(remaining ? formatText(Text::EstimatedRemaining,
                                                   formatDuration(*remaining))
                                      : std::wstring{});
        m_updated.setText(updatedLine(info));
    }

    GaugeVisual gauge() const {
        auto const& palette = theme().colors();
        GaugeVisual visual;
        visual.percent = m_percent;
        visual.fill = m_fill.value();
        visual.charging = m_charging;
        visual.approximate = m_coarse;
        visual.level = theme().levelColor(m_percent);
        // The neutral strong fill at full strength competes with the level arc; at a quarter
        // it reads as the empty part of the ring, which is what it is.
        visual.track = palette.controlStrongFill;
        visual.track.a *= 0.28f;
        visual.outline = palette.controlStrongStroke;
        visual.text = palette.textPrimary;
        visual.surface = palette.windowBackground;
        return visual;
    }

    ControllerArt art() const {
        auto const& palette = theme().colors();
        // One neutral tone at several strengths: it is the token that stays legible against
        // both a near-white card and a near-black one, which a fixed grey would not.
        auto shade = [&palette](float strength) {
            D2D1_COLOR_F color = palette.controlStrongFill;
            color.a *= strength;
            return color;
        };

        ControllerArt art;
        art.body = shade(0.34f);
        art.bodyEdge = shade(0.55f);
        art.recess = shade(0.62f);
        art.detail = palette.controlStrongFill;
        art.guide = theme().levelColor(m_percent);
        art.badge = palette.textSecondary;
        art.badgeFill = shade(0.18f);
        art.link = m_link;
        return art;
    }

    std::wstring m_id;
    TextBlock m_name;
    TextBlock m_status;
    TextBlock m_approximate;
    TextBlock m_remaining;
    TextBlock m_updated;
    NameEditor* m_editor = nullptr;
    RenameButton* m_pencil = nullptr;
    Animated m_fill{0.0f};
    Animated m_reveal{0.0f};
    DeviceKind m_kind = DeviceKind::Other;
    ControllerLink m_link = ControllerLink::None;
    TileAlert m_alert = TileAlert::None;
    float m_textWidth = 0.0f;
    float m_nameSlot = 0.0f;
    int m_percent = -1;
    bool m_charging = false;
    bool m_coarse = false;
    bool m_editing = false;
};

namespace {

// The tiles of one section, placed on the columns tileColumns works out. Every tile in a row
// takes the height of the tallest, so a name that wraps does not leave its neighbours short.
class TileGrid : public Container {
public:
    float measure(float availableWidth) override {
        m_columns = tileColumns(availableWidth, host() != nullptr ? host()->scale() : 1.0f);
        m_rowHeights.clear();
        std::size_t const perRow = m_columns.size();
        for (std::size_t i = 0; i < m_children.size(); ++i) {
            TileColumn const& column = m_columns[i % perRow];
            float const height = m_children[i]->measure(column.right - column.left);
            if (i % perRow == 0) {
                m_rowHeights.push_back(height);
            } else {
                m_rowHeights.back() = std::max(m_rowHeights.back(), height);
            }
        }
        float total = 0.0f;
        for (float const height : m_rowHeights) {
            total += height;
        }
        if (!m_rowHeights.empty()) {
            total += kTileGap * static_cast<float>(m_rowHeights.size() - 1);
        }
        return total;
    }

    void arrange(D2D1_RECT_F bounds) override {
        Widget::arrange(bounds);
        if (m_columns.empty()) {
            return;
        }
        std::size_t const perRow = m_columns.size();
        float y = bounds.top;
        for (std::size_t i = 0; i < m_children.size(); ++i) {
            std::size_t const row = i / perRow;
            TileColumn const& column = m_columns[i % perRow];
            float const height = m_rowHeights[row];
            m_children[i]->arrange(D2D1::RectF(bounds.left + column.left, y,
                                               bounds.left + column.right, y + height));
            if (i % perRow == perRow - 1) {
                y += height + kTileGap;
            }
        }
    }

private:
    std::vector<TileColumn> m_columns;
    std::vector<float> m_rowHeights;
};

}  // namespace

DevicesPage::DevicesPage(PageContext context) : Page(std::move(context)) {}

void DevicesPage::build(StackPanel& column) {
    m_tiles.clear();
    m_refresh = nullptr;

    auto header = std::make_unique<PageHeader>(std::wstring(text(Text::DevicesTitle)));
    auto refresh = std::make_unique<Button>(std::wstring(text(Text::Refresh)), [this] {
        if (!m_context.refreshControllers) {
            return;
        }
        // The monitor always answers a refresh, and refreshValues or a rebuild of this page
        // is what that answer arrives as; either one brings the button back.
        m_refresh->setEnabled(false);
        m_context.refreshControllers();
    });
    refresh->setGlyph(glyph::kRefresh);
    m_refresh = refresh.get();
    header->setAction(std::move(refresh));
    column.add(std::move(header));

    auto const& devices = *m_context.controllers;
    if (devices.empty()) {
        column.emplace<EmptyState>(glyph::kDevices, std::wstring(text(Text::NoDevices)),
                                   std::wstring(text(Text::NoDevicesHint)));
        return;
    }

    DeviceNames const& names = SettingsStore::instance().get().deviceNames;
    bool first = true;
    for (DeviceGroup const& group : groupByKind(devices)) {
        auto* title = column.emplace<Label>(std::wstring(text(sectionTitle(group.kind))),
                                            TypeStyle::BodyStrong);
        title->setMargin({0.0f, first ? 0.0f : kSectionTopMargin, 0.0f, kSectionBottomMargin});
        first = false;

        auto* grid = column.emplace<TileGrid>();
        for (std::size_t const index : group.members) {
            DeviceInfo const& device = devices[index];
            auto const custom = names.find(device.id);
            m_tiles.push_back(grid->emplace<DeviceTile>(
                device, remainingFor(device),
                custom != names.end() ? custom->second : std::wstring{},
                [this, id = device.id](std::wstring const& name) { rename(id, name); }));
        }
    }
}

// The field holds only the custom name, so an empty one means "go back to what the device
// calls itself". The page is not rebuilt for it -- MainWindow holds rebuilds back while the
// settings it applies are its own -- so the tile that is committing survives its own commit.
void DevicesPage::rename(std::wstring const& id, std::wstring const& name) {
    if (!m_context.applySettings) {
        return;
    }
    Settings next = SettingsStore::instance().get();
    next.setDeviceName(id, name);
    if (next.deviceNames != SettingsStore::instance().get().deviceNames) {
        m_context.applySettings(std::move(next));
    }
}

void DevicesPage::refreshValues() {
    if (m_refresh != nullptr) {
        m_refresh->setEnabled(true);
    }
    auto const& devices = *m_context.controllers;
    for (auto* tile : m_tiles) {
        auto const found = std::find_if(
            devices.begin(), devices.end(),
            [tile](DeviceInfo const& info) { return info.id == tile->id(); });
        if (found != devices.end()) {
            tile->update(*found, remainingFor(*found));
        }
    }
}

std::optional<std::chrono::minutes> DevicesPage::remainingFor(DeviceInfo const& device) const {
    if (!m_context.history || !device.hasBattery()) {
        return std::nullopt;
    }
    return m_context.history->estimatedRemaining(device);
}

}  // namespace peek::ui
