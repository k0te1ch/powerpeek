#include "ui/pages/Page.h"

#include <memory>
#include <utility>

namespace peek::ui {
namespace {

// PowerToys pads a settings page 20 left and right of a 1000 DIP content column; this window
// is narrower than that everywhere, so the page margin is all that is left of the rule.
constexpr float kPageBottomPadding = 48.0f;

}  // namespace

Page::Page(PageContext context) : m_context(std::move(context)) {}

void Page::invalidateContent() {
    m_dirty = true;
    invalidate();
}

bool Page::tick(std::chrono::steady_clock::time_point now) {
    // Before the base class walks into the content: this is what replaces it.
    if (m_dirty && visible()) {
        rebuild();
    }
    return ScrollView::tick(now);
}

void Page::rebuild() {
    m_dirty = false;

    // The host keeps raw pointers into the tree that setContent is about to free.
    if (host()) {
        host()->resetInput();
    }

    auto column = std::make_unique<StackPanel>();
    column->setSpacing(Metrics::cardGap);
    column->setPadding({Metrics::pageMarginX, Metrics::pageMarginTop, Metrics::pageMarginX,
                        kPageBottomPadding});
    auto* raw = column.get();
    setContent(std::move(column));
    build(*raw);
}

}  // namespace peek::ui
