#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "ui/Widgets.h"

namespace peek::ui {

// The small pieces the five pages share, kept out of Widgets.h because none of them is a
// Fluent control -- they are this application's page furniture.

// A page's title, its one-line description, and one optional action button on the right.
class PageHeader : public Container {
public:
    explicit PageHeader(std::wstring title, std::wstring description = {});

    Button* setAction(std::unique_ptr<Button> action);

    float measure(float availableWidth) override;
    void arrange(D2D1_RECT_F bounds) override;
    void paint(Canvas& canvas) override;

private:
    TextBlock m_title;
    TextBlock m_description;
    Button* m_action = nullptr;
};

// Widgets laid out left to right at their desired widths. A settings card's trailing column
// takes exactly one widget, and this is how several buttons fit into it.
class ButtonRow : public Container {
public:
    float measure(float availableWidth) override;
    void arrange(D2D1_RECT_F bounds) override;
    float desiredWidth() const override;
};

// The centred glyph, headline and hint a page shows when it has nothing to display.
class EmptyState : public Widget {
public:
    EmptyState(std::wstring glyph, std::wstring headline, std::wstring hint);

    float measure(float availableWidth) override;
    void paint(Canvas& canvas) override;

private:
    std::wstring m_glyph;
    TextBlock m_headline;
    TextBlock m_hint;
};

// "2 h 40 min" in the current language, dropping the leading unit when it is zero.
std::wstring formatDuration(std::chrono::minutes duration);

}  // namespace peek::ui
