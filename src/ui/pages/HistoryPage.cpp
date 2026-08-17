#include "ui/pages/HistoryPage.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Strings.h"
#include "ui/Drawing.h"
#include "ui/pages/PageWidgets.h"

namespace peek::ui {
namespace {

constexpr float kCardPadding = 16.0f;
constexpr float kPlotHeight = 220.0f;
constexpr float kFooterHeight = 24.0f;

struct Range {
    Text label;
    std::chrono::hours window;
};

constexpr Range kRanges[]{
    {Text::HistoryRange24h, std::chrono::hours{24}},
    {Text::HistoryRange7d, std::chrono::hours{24 * 7}},
    {Text::HistoryRange30d, std::chrono::hours{24 * 30}},
};

// Local wall-clock time, because a chart of "when was my controller flat" is only meaningful
// in the time zone the user was in.
std::wstring formatMoment(std::chrono::system_clock::time_point when, bool withinOneDay) {
    auto const stamp = std::chrono::system_clock::to_time_t(when);
    std::tm local{};
    if (localtime_s(&local, &stamp) != 0) {
        return {};
    }
    if (withinOneDay) {
        return std::format(L"{:02}:{:02}", local.tm_hour, local.tm_min);
    }
    return std::format(L"{:02}.{:02}", local.tm_mday, local.tm_mon + 1);
}

}  // namespace

// The chart itself, on a card, with the drain rate under it.
class ChartCard : public Widget {
public:
    ChartCard(std::vector<ChartPoint> points,
              std::wstring startLabel,
              std::wstring middleLabel,
              std::wstring endLabel,
              std::optional<double> drainPerHour)
        : m_points(std::move(points)),
          m_start(std::move(startLabel)),
          m_middle(std::move(middleLabel)),
          m_end(std::move(endLabel)) {
        m_drain.setStyle(TypeStyle::Caption);
        if (drainPerHour) {
            m_drain.setText(formatText(Text::DrainRate, std::format(L"{:.1f}", *drainPerHour)));
        }
    }

    float measure(float availableWidth) override {
        if (!m_drain.empty()) {
            m_drain.measure(std::max(40.0f, availableWidth - kCardPadding * 2.0f));
        }
        return kPlotHeight + kCardPadding * 2.0f + (m_drain.empty() ? 0.0f : kFooterHeight);
    }

    void paint(Canvas& canvas) override {
        auto const& palette = theme().colors();
        fillRounded(canvas, m_bounds, Metrics::controlCornerRadius, palette.cardFill);
        strokeRounded(canvas, m_bounds, Metrics::controlCornerRadius, palette.cardStroke);

        ChartStyle style;
        style.line = palette.accent;
        style.fill = palette.accent;
        style.fill.a = 0.28f;
        style.grid = palette.dividerStroke;
        style.label = palette.textTertiary;
        style.labelFormat = theme().textFormat(TypeStyle::Caption);
        style.startLabel = m_start;
        style.middleLabel = m_middle;
        style.endLabel = m_end;

        drawHistoryChart(canvas,
                         D2D1::RectF(m_bounds.left + kCardPadding, m_bounds.top + kCardPadding,
                                     m_bounds.right - kCardPadding,
                                     m_bounds.top + kCardPadding + kPlotHeight),
                         m_points, style);

        if (!m_drain.empty()) {
            m_drain.draw(canvas,
                         D2D1::Point2F(m_bounds.left + kCardPadding,
                                       m_bounds.bottom - kCardPadding - m_drain.size().height),
                         palette.textSecondary);
        }
    }

private:
    std::vector<ChartPoint> m_points;
    std::wstring m_start;
    std::wstring m_middle;
    std::wstring m_end;
    TextBlock m_drain;
};

HistoryPage::HistoryPage(PageContext context) : Page(std::move(context)) {}

void HistoryPage::build(StackPanel& column) {
    column.emplace<PageHeader>(std::wstring(text(Text::HistoryTitle)));

    auto const& controllers = *m_context.controllers;
    if (controllers.empty() || !m_context.history) {
        column.emplace<EmptyState>(glyph::kChart, std::wstring(text(Text::HistoryEmpty)),
                                   std::wstring(text(Text::HistoryEmptyHint)));
        return;
    }

    m_controller = std::clamp(m_controller, 0, static_cast<int>(controllers.size()) - 1);

    std::vector<std::wstring> names;
    names.reserve(controllers.size());
    for (auto const& controller : controllers) {
        names.push_back(controller.name);
    }
    auto* pick = column.emplace<SettingsCard>(glyph::kGamepad,
                                              std::wstring(text(Text::HistoryController)));
    pick->setControl(std::make_unique<ComboBox>(std::move(names), m_controller, [this](int index) {
        m_controller = index;
        invalidateContent();
    }));

    std::vector<std::wstring> ranges;
    for (auto const& range : kRanges) {
        ranges.emplace_back(text(range.label));
    }
    auto* window = column.emplace<SettingsCard>(glyph::kChart,
                                                std::wstring(text(Text::HistoryRange)));
    window->setControl(std::make_unique<ComboBox>(std::move(ranges), m_range, [this](int index) {
        m_range = index;
        invalidateContent();
    }));

    DeviceInfo const& selected = controllers[static_cast<std::size_t>(m_controller)];
    auto const span = kRanges[static_cast<std::size_t>(m_range)].window;
    auto const now = std::chrono::system_clock::now();
    auto const cutoff = now - span;

    std::vector<ChartPoint> points;
    auto oldest = now;
    for (auto const& sample : m_context.history->samplesFor(selected.id)) {
        if (sample.when < cutoff) {
            continue;
        }
        oldest = std::min(oldest, sample.when);
        auto const seconds = std::chrono::duration<double>(sample.when.time_since_epoch()).count();
        points.push_back(ChartPoint{seconds, static_cast<double>(sample.percent)});
    }

    if (points.empty()) {
        column.emplace<EmptyState>(glyph::kChart, std::wstring(text(Text::HistoryEmpty)),
                                   std::wstring(text(Text::HistoryEmptyHint)));
        return;
    }

    // The axis is labelled with the data's own extent, not the requested window: a chart of
    // three hours of samples inside a 30-day range would otherwise claim to span a month.
    bool const oneDay = span <= std::chrono::hours{24};
    auto const middle = oldest + (now - oldest) / 2;
    column.emplace<ChartCard>(std::move(points), formatMoment(oldest, oneDay),
                              formatMoment(middle, oneDay), formatMoment(now, oneDay),
                              m_context.history->drainPercentPerHour(selected.id));
}

}  // namespace peek::ui
