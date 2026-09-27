#pragma once

#include <vector>

#include "battery/DeviceInfo.h"

namespace peek {

// Whether two readings would draw the same card. lastUpdate is left out on purpose: it moves
// on every poll and would make every poll look like a change.
bool sameReading(DeviceInfo const& a, DeviceInfo const& b) noexcept;

// sameReading over two whole lists, in order.
bool sameList(std::vector<DeviceInfo> const& a, std::vector<DeviceInfo> const& b) noexcept;

// Whether a finished poll is handed to the UI thread. A regular poll only speaks when
// something the UI renders has moved, so a quiet pad costs nothing. A poll the user asked
// for always speaks: the answer to "refresh" is a fresh "updated just now" even when every
// level is where it was, and a button that changes nothing on screen reads as a broken one.
bool shouldPublish(std::vector<DeviceInfo> const& previous, std::vector<DeviceInfo> const& next,
                   bool requested) noexcept;

}  // namespace peek
