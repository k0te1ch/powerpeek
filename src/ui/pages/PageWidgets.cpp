#include "ui/pages/PageWidgets.h"

#include <algorithm>
#include <format>
#include <utility>

#include "core/Strings.h"

namespace peek::ui {
namespace {

constexpr float kHeaderGap = 4.0f;
constexpr float kHeaderBottomMargin = 20.0f;
constexpr float kButtonGap = 8.0f;
constexpr float kEmptyGlyphSize = 40.0f;
constexpr float kEmptyStateHeight = 220.0f;
constexpr float kEmptyTextWidth = 340.0f;

}  // namespace

PageHeader::PageHeader(std::wstring title, std::wstring description) {
    m_title.setStyle(TypeStyle::Title);
    m_title.setText(std::move(title));
    m_description.setStyle(TypeStyle::Body);
    m_description.setWrapping(true);
    m_description.setText(std::move(description));
}

Button* PageHeader::setAction(std::unique_ptr<Button> action) {
    m_action = static_cast<Button*>(add(std::move(action)));
    return m_action;
}

float PageHeader::measure(float availableWidth) {
    float const actionWidth = m_action ? m_action->desiredWidth() + kButtonGap * 2.0f : 0.0f;
    float const textWidth = std::max(80.0f, availableWidth - actionWidth);

    float height = m_title.measure(textWidth);
    if (!m_description.empty()) {
        height += kHeaderGap + m_description.measure(textWidth);
    }
    if (m_action) {
        height = std::max(height, m_action->measure(actionWidth));
    }
    return height + kHeaderBottomMargin;
}

void PageHeader::arrange(D2D1_RECT_F bounds) {
    Widget::arrange(bounds);
    if (!m_action) {
        return;
    }
    float const width = m_action->desiredWidth();
    float const height = m_action->measure(width);
    // Centred on the title line rather than on the whole header, so a long description does
    // not drag the button away from the heading it belongs to.
    float const top = bounds.top + (m_title.size().height - height) * 0.5f;
    m_action->arrange(D2D1::RectF(bounds.right - width, top, bounds.right, top + height));
}

void PageHeader::paint(Canvas& canvas) {
    auto const& palette = theme().colors();
    m_title.draw(canvas, D2D1::Point2F(m_bounds.left, m_bounds.top), palette.textPrimary);
    if (!m_description.empty()) {
        m_description.draw(
            canvas, D2D1::Point2F(m_bounds.left, m_bounds.top + m_title.size().height + kHeaderGap),
            palette.textSecondary);
    }
    Container::paint(canvas);
}

float ButtonRow::measure(float availableWidth) {
    float height = 0.0f;
    for (auto const& child : m_children) {
        height = std::max(height, child->measure(availableWidth));
    }
    return height;
}

void ButtonRow::arrange(D2D1_RECT_F bounds) {
    Widget::arrange(bounds);
    float x = bounds.left;
    for (auto const& child : m_children) {
        float const width = child->desiredWidth();
        child->arrange(D2D1::RectF(x, bounds.top, x + width, bounds.bottom));
        x += width + kButtonGap;
    }
}

float ButtonRow::desiredWidth() const {
    float total = 0.0f;
    for (auto const& child : m_children) {
        total += child->desiredWidth() + kButtonGap;
    }
    return std::max(0.0f, total - kButtonGap);
}

EmptyState::EmptyState(std::wstring glyph, std::wstring headline, std::wstring hint)
    : m_glyph(std::move(glyph)) {
    m_headline.setStyle(TypeStyle::BodyLarge);
    m_headline.setText(std::move(headline));
    m_hint.setStyle(TypeStyle::Body);
    m_hint.setWrapping(true);
    m_hint.setText(std::move(hint));
}

float EmptyState::measure(float availableWidth) {
    m_headline.measure(availableWidth);
    m_hint.measure(std::min(kEmptyTextWidth, availableWidth));
    return kEmptyStateHeight;
}

void EmptyState::paint(Canvas& canvas) {
    auto const& palette = theme().colors();
    float const centreX = (m_bounds.left + m_bounds.right) * 0.5f;
    float const block =
        kEmptyGlyphSize + 16.0f + m_headline.size().height + 4.0f + m_hint.size().height;
    float y = m_bounds.top + (m_bounds.bottom - m_bounds.top - block) * 0.5f;

    drawIcon(canvas, m_glyph, kEmptyGlyphSize,
             D2D1::RectF(m_bounds.left, y, m_bounds.right, y + kEmptyGlyphSize),
             palette.textTertiary);
    y += kEmptyGlyphSize + 16.0f;

    m_headline.draw(canvas, D2D1::Point2F(centreX - m_headline.size().width * 0.5f, y),
                    palette.textPrimary);
    y += m_headline.size().height + 4.0f;
    m_hint.draw(canvas, D2D1::Point2F(centreX - m_hint.size().width * 0.5f, y),
                palette.textSecondary);
}

std::wstring formatDuration(std::chrono::minutes duration) {
    auto const total = std::max<long long>(0, duration.count());
    auto const hours = total / 60;
    auto const minutes = total % 60;
    if (hours == 0) {
        return std::format(L"{} {}", minutes, text(Text::UnitMinutes));
    }
    if (minutes == 0) {
        return std::format(L"{} {}", hours, text(Text::UnitHours));
    }
    return std::format(L"{} {} {} {}", hours, text(Text::UnitHours), minutes,
                       text(Text::UnitMinutes));
}

}  // namespace peek::ui
