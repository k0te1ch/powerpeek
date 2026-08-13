#pragma once

#include "ui/pages/Page.h"

namespace peek::ui {

// The recorded battery levels of one controller over a chosen window, as a line chart with
// its drain rate. Falls back to an explicit empty state, because "no data yet" is the normal
// state for the first hours after installation and must not look like a broken chart.
class HistoryPage : public Page {
public:
    explicit HistoryPage(PageContext context);

protected:
    void build(StackPanel& column) override;

private:
    int m_controller = 0;
    int m_range = 0;
};

}  // namespace peek::ui
