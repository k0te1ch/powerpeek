#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include "ui/pages/Page.h"

namespace peek::ui {

class ControllerCard;

// One card per connected controller: name, connection kind, an animated gauge, the charge
// state, how coarse the reading is, and how long the pad is expected to last.
class ControllersPage : public Page {
public:
    explicit ControllersPage(PageContext context);

    void refreshValues() override;

protected:
    void build(StackPanel& column) override;

private:
    std::optional<std::chrono::minutes> remainingFor(ControllerInfo const& controller) const;

    std::vector<ControllerCard*> m_cards;
};

}  // namespace peek::ui
