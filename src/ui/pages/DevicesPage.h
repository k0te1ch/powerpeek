#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include "ui/pages/Page.h"

namespace peek::ui {

class Button;
class DeviceTile;

// Every connected device with a battery, as a grid of tiles in one section per kind: gamepads,
// headsets, mice, keyboards, pens, the rest. A tile shows what the device is, the level on a
// ring, its name, how it is connected and what it is doing, and how long it should last.
class DevicesPage : public Page {
public:
    explicit DevicesPage(PageContext context);

    void refreshValues() override;

protected:
    void build(StackPanel& column) override;

private:
    void buildNames(StackPanel& column);
    std::optional<std::chrono::minutes> remainingFor(DeviceInfo const& device) const;

    std::vector<DeviceTile*> m_tiles;
    // Disabled from the click until the monitor answers, so a refresh visibly happens.
    Button* m_refresh = nullptr;
};

}  // namespace peek::ui
