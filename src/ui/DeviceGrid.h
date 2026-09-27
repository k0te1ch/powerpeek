#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "battery/DeviceInfo.h"

namespace peek::ui {

// The arithmetic behind the devices page: which section each device goes in, how many tiles
// share a row, and where exactly each column starts. Kept apart from the page so that it links
// into PowerPeekCore and is tested without a window -- the page itself only creates widgets
// and hands them the rectangles computed here.

// The narrowest a tile may get before the row drops a column. Its content is a portrait, a
// ring and a name, and below this the name is all ellipsis.
inline constexpr float kTileMinWidth = 200.0f;
inline constexpr float kTileGap = 12.0f;
inline constexpr int kMaxTileColumns = 3;

// The order the sections appear in. Fixed rather than by count or by level: a section that
// jumped above another whenever a headset was switched on would move every tile under it.
inline constexpr std::array<DeviceKind, 6> kKindOrder{
    DeviceKind::Gamepad, DeviceKind::Headset, DeviceKind::Mouse,
    DeviceKind::Keyboard, DeviceKind::Pen, DeviceKind::Other,
};

struct DeviceGroup {
    DeviceKind kind = DeviceKind::Other;
    // Indices into the list the groups were made from, in the order the devices arrived.
    std::vector<std::size_t> members;
};

// One group per kind that has at least one device, in kKindOrder.
std::vector<DeviceGroup> groupByKind(std::vector<DeviceInfo> const& devices);

struct TileColumn {
    float left = 0.0f;
    float right = 0.0f;
};

// The columns of one row of tiles, relative to the left edge of `availableWidth` DIPs.
//
// As many columns as fit at kTileMinWidth, never more than kMaxTileColumns and never fewer
// than one. The edges are computed in physical pixels at `scale` (1.0 at 96 DPI, 1.5 at 144)
// and converted back, so every edge lands on the pixel grid, every gap is the same number of
// pixels, and tile widths differ by one pixel at most. A scale of zero or less counts as 1.
std::vector<TileColumn> tileColumns(float availableWidth, float scale);

enum class TileAlert { None, Low, Critical };

// Whether a tile carries the low or the critical accent. Only a level on its way down counts:
// a device charging or full is being looked after, and one with no level has nothing to warn
// about. The thresholds are the same settings the warnings use.
TileAlert tileAlert(DeviceInfo const& device, int lowThreshold, int criticalThreshold);

// The Segoe Fluent Icons glyph that stands for a kind, for the tile and the notification card.
std::wstring_view kindGlyph(DeviceKind kind);

}  // namespace peek::ui
