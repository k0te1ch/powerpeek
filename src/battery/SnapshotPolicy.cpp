#include "battery/SnapshotPolicy.h"

#include <algorithm>

namespace peek {

bool sameReading(DeviceInfo const& a, DeviceInfo const& b) noexcept {
    return a.id == b.id && a.name == b.name && a.percent == b.percent &&
           a.fidelity == b.fidelity && a.source == b.source && a.charge == b.charge;
}

bool sameList(std::vector<DeviceInfo> const& a, std::vector<DeviceInfo> const& b) noexcept {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), sameReading);
}

bool shouldPublish(std::vector<DeviceInfo> const& previous, std::vector<DeviceInfo> const& next,
                   bool requested) noexcept {
    return requested || !sameList(previous, next);
}

}  // namespace peek
