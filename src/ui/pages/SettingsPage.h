#pragma once

#include "ui/pages/Page.h"

namespace peek::ui {

// Every setting that is not about sounds, as PowerToys lays them out: grouped settings cards,
// each writing straight through SettingsStore so the change takes effect at once.
class SettingsPage : public Page {
public:
    explicit SettingsPage(PageContext context);

protected:
    void build(StackPanel& column) override;

private:
    void addGeneral(StackPanel& column);
    void addMonitoring(StackPanel& column);
    void addThresholds(SettingsGroup& group);
    void addAppearance(StackPanel& column);
    void addHistory(StackPanel& column);

    // The two threshold sliders police each other: critical has to stay below low, and either
    // one being dragged past the other pushes it rather than being silently clamped later.
    Slider* m_low = nullptr;
    Slider* m_critical = nullptr;
};

}  // namespace peek::ui
