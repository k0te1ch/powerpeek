#include "battery/DeviceMerge.h"

#include <algorithm>
#include <utility>

namespace peek {
namespace {

// What a reading knows, most significant term first: two bits, so four ranks over a total
// order, transitive because it is arithmetic rather than because a comparator promises to be.
//
// Carrying a level outranks describing one precisely, and that order comes from the shipped
// providers rather than from taste. The WinRT provider stamps Fidelity::Exact on a reading
// before it has read anything at all, and a pad with no IGameControllerBatteryInfo -- most
// third-party pads -- leaves the percent at -1 behind it. Ranked the other way round, an
// exact nothing would win against a usable bucket and the card would go blank.
int rankOf(DeviceInfo const& reading) noexcept {
    return (reading.hasBattery() ? 2 : 0) + (reading.fidelity == Fidelity::Exact ? 1 : 0);
}

}  // namespace

std::vector<DeviceInfo> mergeReadings(std::vector<DeviceInfo> readings) {
    std::vector<DeviceInfo> merged;
    merged.reserve(readings.size());

    for (DeviceInfo& reading : readings) {
        // An unknown identity is not a shared one. Two readings that both failed to key
        // themselves are two devices, not one seen twice.
        auto const held =
            reading.id.empty()
                ? merged.end()
                : std::find_if(merged.begin(), merged.end(), [&reading](DeviceInfo const& kept) {
                      return kept.id == reading.id;
                  });

        if (held == merged.end()) {
            merged.push_back(std::move(reading));
            continue;
        }
        // Strictly better, so an equal reading leaves the incumbent alone. That is what makes
        // the caller's collection order the source priority, and what keeps the surviving
        // record in the position the device first appeared in -- the snapshot comparison and
        // the window both pair records element by element, so a merge that reordered would
        // read as a change on every poll where a rank flipped.
        if (rankOf(reading) > rankOf(*held)) {
            *held = std::move(reading);
        }
    }

    return merged;
}

}  // namespace peek
