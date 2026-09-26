#pragma once

#include <vector>

#include "battery/DeviceInfo.h"
#include "core/Settings.h"

namespace peek {

// Puts the names the user chose on a list of readings.
//
// Applied once, where a poll's readings enter the application, so that the devices page, the
// history, the notification cards and the tray tooltip all read `name` and cannot disagree
// about what a device is called.
//
// The provider's own name is kept in `reportedName`, and the function is idempotent: running
// it again with different names -- after a rename -- starts from the reported name rather than
// from the previous custom one, so clearing a name brings the reported one back.
void applyDeviceNames(std::vector<DeviceInfo>& devices, DeviceNames const& names);

// The same for one reading, for a device held on to after it left the list -- a reminder about
// a pad that was put away still names it the way the user calls it now.
void applyDeviceName(DeviceInfo& device, DeviceNames const& names);

}  // namespace peek
