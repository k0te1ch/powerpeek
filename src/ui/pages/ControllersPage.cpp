#include "ui/pages/ControllersPage.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "core/Strings.h"
#include "ui/Drawing.h"
#include "ui/pages/PageWidgets.h"

namespace peek::ui {
namespace {

constexpr float kCardPadding = 16.0f;
constexpr float kGaugeSize = 80.0f;
constexpr float kGaugeTextGap = 20.0f;
constexpr float kArtHeight = 68.0f;
constexpr float kArtGap = 16.0f;
// Narrower than this and the portrait is costing the name and the status more room than it
// earns, so the card drops it and goes back to a gauge and two lines of text.
constexpr float kMinimumTextWidth = 150.0f;
constexpr float kLineGap = 2.0f;
constexpr float kApproximateIndent = 18.0f;
constexpr float kApproximateGlyphSize = 12.0f;
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
ControllerLink connectionLink(ControllerInfo const& info) {
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

std::wstring_view chargeText(ControllerInfo const& info) {
    if (info.charge == ChargeState::Unknown && info.source == PowerSource::Wired) {
        return text(Text::StatusWired);
    }
    return toString(info.charge);
}

std::wstring statusLine(ControllerInfo const& info) {
    std::wstring line;
    if (auto const connection = connectionText(connectionLink(info))) {
        line.append(*connection);
        line.append(L" \x00B7 ");
    }
    line.append(chargeText(info));
    return line;
}

std::wstring updatedLine(ControllerInfo const& info) {
    auto const age = std::chrono::system_clock::now() - info.lastUpdate;
    auto const minutes = std::chrono::duration_cast<std::chrono::minutes>(age).count();
    if (minutes <= 0) {
        return std::wstring(text(Text::UpdatedJustNow));
    }
    return formatText(Text::UpdatedMinutesAgo, minutes);
}

}  // namespace

// One controller, drawn as a Fluent card with a ring gauge that sweeps to the level rather
// than jumping to it.
class ControllerCard : public Widget {
public:
    ControllerCard(ControllerInfo const& info, std::optional<std::chrono::minutes> remaining) {
        m_name.setStyle(TypeStyle::BodyStrong);
        m_status.setStyle(TypeStyle::Body);
        m_approximate.setStyle(TypeStyle::Caption);
        m_remaining.setStyle(TypeStyle::Caption);
        m_updated.setStyle(TypeStyle::Caption);
        apply(info, remaining);
        m_fill.snapTo(fraction(info));
    }

    std::wstring const& id() const noexcept { return m_id; }

    void update(ControllerInfo const& info, std::optional<std::chrono::minutes> remaining) {
        apply(info, remaining);
        m_fill.animateTo(fraction(info), kDurationSlow, Easing::Entrance);
        invalidateLayout();
        invalidate();
    }

    float measure(float availableWidth) override {
        m_showArt = textColumnWidth(availableWidth, true) >= kMinimumTextWidth;
        float const textWidth = std::max(80.0f, textColumnWidth(availableWidth, m_showArt));
        float lines = m_name.measure(textWidth) + kLineGap + m_status.measure(textWidth);
        if (!m_approximate.empty()) {
            lines += kLineGap + m_approximate.measure(textWidth - kApproximateIndent);
        }
        if (!m_remaining.empty()) {
            lines += kLineGap + m_remaining.measure(textWidth);
        }
        lines += kLineGap + m_updated.measure(textWidth);
        static_assert(kArtHeight <= kGaugeSize, "the gauge sets the card's minimum height");
        return std::max(kGaugeSize + kCardPadding * 2.0f, lines + kCardPadding * 2.0f);
    }

    bool tick(std::chrono::steady_clock::time_point now) override {
        bool running = Widget::tick(now);
        running |= m_fill.tick(now);
        return running;
    }

    void paint(Canvas& canvas) override {
        auto const& palette = theme().colors();
        fillRounded(canvas, m_bounds, Metrics::controlCornerRadius, palette.cardFill);
        strokeRounded(canvas, m_bounds, Metrics::controlCornerRadius, palette.cardStroke);

        float const centreY = (m_bounds.top + m_bounds.bottom) * 0.5f;
        float column = m_bounds.left + kCardPadding;
        if (m_showArt) {
            float const artWidth = controllerArtWidth(kArtHeight);
            drawControllerArt(canvas,
                              D2D1::RectF(column, centreY - kArtHeight * 0.5f, column + artWidth,
                                          centreY + kArtHeight * 0.5f),
                              art());
            column += artWidth + kArtGap;
        }
        drawRingGauge(canvas,
                      D2D1::RectF(column, centreY - kGaugeSize * 0.5f, column + kGaugeSize,
                                  centreY + kGaugeSize * 0.5f),
                      gauge(), theme().textFormat(TypeStyle::Subtitle));

        float const left = column + kGaugeSize + kGaugeTextGap;
        float height = m_name.size().height + kLineGap + m_status.size().height;
        if (!m_approximate.empty()) {
            height += kLineGap + m_approximate.size().height;
        }
        if (!m_remaining.empty()) {
            height += kLineGap + m_remaining.size().height;
        }
        height += kLineGap + m_updated.size().height;

        float y = centreY - height * 0.5f;
        auto line = [&](TextBlock& block, D2D1_COLOR_F color) {
            block.draw(canvas, D2D1::Point2F(left, y), color);
            y += block.size().height + kLineGap;
        };
        line(m_name, palette.textPrimary);
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
            line(m_remaining, palette.textSecondary);
        }
        line(m_updated, palette.textTertiary);
    }

private:
    static float fraction(ControllerInfo const& info) {
        return info.percent < 0 ? 0.0f : static_cast<float>(info.percent) / 100.0f;
    }

    static float textColumnWidth(float availableWidth, bool withArt) {
        float used = kCardPadding * 2.0f + kGaugeSize + kGaugeTextGap;
        if (withArt) {
            used += controllerArtWidth(kArtHeight) + kArtGap;
        }
        return availableWidth - used;
    }

    void apply(ControllerInfo const& info, std::optional<std::chrono::minutes> remaining) {
        m_id = info.id;
        m_percent = info.percent;
        m_charging = info.charge == ChargeState::Charging;
        m_coarse = info.fidelity == Fidelity::Coarse && info.percent >= 0;
        m_link = connectionLink(info);

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
    Animated m_fill{0.0f};
    ControllerLink m_link = ControllerLink::None;
    int m_percent = -1;
    bool m_charging = false;
    bool m_coarse = false;
    bool m_showArt = true;
};

ControllersPage::ControllersPage(PageContext context) : Page(std::move(context)) {}

void ControllersPage::build(StackPanel& column) {
    m_cards.clear();

    auto header = std::make_unique<PageHeader>(std::wstring(text(Text::DevicesTitle)));
    auto refresh = std::make_unique<Button>(std::wstring(text(Text::Refresh)),
                                            [this] {
                                                if (m_context.refreshControllers) {
                                                    m_context.refreshControllers();
                                                }
                                            });
    refresh->setGlyph(glyph::kRefresh);
    header->setAction(std::move(refresh));
    column.add(std::move(header));

    auto const& controllers = *m_context.controllers;
    if (controllers.empty()) {
        column.emplace<EmptyState>(glyph::kGamepad, std::wstring(text(Text::NoControllers)),
                                   std::wstring(text(Text::NoControllersHint)));
        return;
    }

    for (auto const& controller : controllers) {
        m_cards.push_back(column.emplace<ControllerCard>(controller, remainingFor(controller)));
    }
}

void ControllersPage::refreshValues() {
    auto const& controllers = *m_context.controllers;
    for (auto* card : m_cards) {
        auto const found = std::find_if(
            controllers.begin(), controllers.end(),
            [card](ControllerInfo const& info) { return info.id == card->id(); });
        if (found != controllers.end()) {
            card->update(*found, remainingFor(*found));
        }
    }
}

std::optional<std::chrono::minutes> ControllersPage::remainingFor(
    ControllerInfo const& controller) const {
    if (!m_context.history || !controller.hasBattery()) {
        return std::nullopt;
    }
    return m_context.history->estimatedRemaining(controller);
}

}  // namespace peek::ui
