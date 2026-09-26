#include "battery/DeviceNames.h"

namespace peek {

void applyDeviceName(DeviceInfo& device, DeviceNames const& names) {
    if (device.reportedName.empty()) {
        device.reportedName = device.name;
    }
    auto const custom = names.find(device.id);
    device.name = custom != names.end() ? custom->second : device.reportedName;
}

void applyDeviceNames(std::vector<DeviceInfo>& devices, DeviceNames const& names) {
    for (DeviceInfo& device : devices) {
        applyDeviceName(device, names);
    }
}

}  // namespace peek
