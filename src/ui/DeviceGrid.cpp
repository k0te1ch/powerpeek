#include "ui/DeviceGrid.h"

#include <algorithm>
#include <cmath>

#include "ui/Theme.h"

namespace peek::ui {

std::vector<DeviceGroup> groupByKind(std::vector<DeviceInfo> const& devices) {
    std::vector<DeviceGroup> groups;
    for (DeviceKind const kind : kKindOrder) {
        DeviceGroup group;
        group.kind = kind;
        for (std::size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].kind == kind) {
                group.members.push_back(i);
            }
        }
        if (!group.members.empty()) {
            groups.push_back(std::move(group));
        }
    }
    return groups;
}

std::vector<TileColumn> tileColumns(float availableWidth, float scale) {
    float const pixelsPerDip = scale > 0.0f ? scale : 1.0f;
    float const width = std::max(0.0f, availableWidth);

    int const fit = static_cast<int>(std::floor((width + kTileGap) / (kTileMinWidth + kTileGap)));
    int const count = std::clamp(fit, 1, kMaxTileColumns);

    // Whole pixels from here on. The available width is rounded down so the last tile never
    // pokes past the edge it was given; the gap is rounded to the nearest so it stays the gap
    // the design asks for at every scale.
    auto const available = static_cast<long>(std::floor(width * pixelsPerDip));
    auto const gap = static_cast<long>(std::lround(kTileGap * pixelsPerDip));
    long const tiles = std::max(0L, available - gap * (count - 1));
    long const base = tiles / count;
    long const extra = tiles % count;

    std::vector<TileColumn> columns;
    columns.reserve(static_cast<std::size_t>(count));
    long x = 0;
    for (int i = 0; i < count; ++i) {
        long const tile = base + (i < extra ? 1 : 0);
        columns.push_back(TileColumn{static_cast<float>(x) / pixelsPerDip,
                                     static_cast<float>(x + tile) / pixelsPerDip});
        x += tile + gap;
    }
    return columns;
}

TileAlert tileAlert(DeviceInfo const& device, int lowThreshold, int criticalThreshold) {
    if (!device.hasBattery() || device.charge == ChargeState::Charging ||
        device.charge == ChargeState::Full) {
        return TileAlert::None;
    }
    if (device.percent <= criticalThreshold) {
        return TileAlert::Critical;
    }
    if (device.percent <= lowThreshold) {
        return TileAlert::Low;
    }
    return TileAlert::None;
}

std::wstring_view kindGlyph(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::Gamepad:
            return glyph::kGamepad;
        case DeviceKind::Headset:
            return glyph::kHeadset;
        case DeviceKind::Mouse:
            return glyph::kMouse;
        case DeviceKind::Keyboard:
            return glyph::kKeyboard;
        case DeviceKind::Pen:
            return glyph::kPen;
        case DeviceKind::Other:
            break;
    }
    return glyph::kDevices;
}

}  // namespace peek::ui
