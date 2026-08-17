#pragma once

#include <vector>

#include "battery/DeviceInfo.h"

namespace peek {

// Battery levels out of the Windows device tree -- the number Settings itself shows next to a
// paired Bluetooth device.
//
// It comes from an undocumented device property that two Bluetooth components write: the LE
// enumerator, which maps the GATT Battery Service onto it, and the audio gateway service,
// which parses the battery indications a hands-free headset sends. There is no header for it
// anywhere in the SDK, so the key is spelled out in the implementation and every read is
// treated as optional -- a Windows that stopped writing it must leave this source silent, not
// broken.
//
// What it does not cover is as important as what it does: Xbox pads carry no such property on
// any transport, devices on their own 2.4 GHz dongles are invisible to it, and DualSense pads
// keep their level in a HID report instead. Those stay with the providers that can read them.
//
// The sweep needs no elevation, no COM and no apartment, and takes about 200 ms for a few
// hundred devnodes. The value behind it only moves when the device says so -- on connect, and
// then on a meaningful change -- so polling it faster than the tens of seconds buys nothing.
//
// What comes back is a level and nothing else. There is no direction to it: the property says
// 8 per cent, not whether 8 is on the way up or down, and the device tree keeps reporting the
// last number it was told long after the earbuds went back in their case. Readings from here
// are therefore ChargeState::Unknown, which is what keeps them out of the low-battery warnings
// -- a warning fired at a headset that is charging is worse than no warning at all, and
// nothing here can yet tell those two apart.
class PnpBatteryProvider {
public:
    std::vector<DeviceInfo> poll();
};

}  // namespace peek
