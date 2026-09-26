#include "battery/DeviceNames.h"

namespace peek {

void applyDeviceNames(std::vector<DeviceInfo>& devices, DeviceNames const& names) {
    for (DeviceInfo& device : devices) {
        if (device.reportedName.empty()) {
            device.reportedName = device.name;
        }
        auto const custom = names.find(device.id);
        device.name = custom != names.end() ? custom->second : device.reportedName;
    }
}

}  // namespace peek
