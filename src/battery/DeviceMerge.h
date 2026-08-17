#pragma once

#include <vector>

#include "battery/DeviceInfo.h"

namespace peek {

// Collapses one poll's readings to one record per device, keeping the better description of
// each device and leaving everything else exactly as it arrived.
//
// Two readings describe the same device when their ids are equal, and never otherwise.
// Deciding that two different ids name one physical device is a question about the device
// tree -- a container id, a Bluetooth address, a HID path -- and it stays on the Win32 side
// of the wall, in the provider that can prove it. What is left here is the part worth
// pinning down: how many cards the user sees, and which reading is behind each one.
//
// Free of Win32 on purpose, and for the same reason as placeToast: this rule is unreachable
// from a test on a provider, which wants a controller, an MTA and an XInput DLL, and the
// build agent has none of the three. It is reachable here because the whole input is an
// argument.
//
// The survivor is one provider's reading verbatim -- no field is ever taken from a loser, so
// every card describes a reading something actually made, and can be checked against a line
// in the log rather than against a rule for assembling one.
//
// `readings` arrives in whatever order the caller collected it, and readings that rank equal
// are resolved by position: the caller states which source outranks which by the order it
// concatenates them, and this function never names a provider.
//
// A list with no repeated ids comes back unchanged, in the order it arrived.
std::vector<DeviceInfo> mergeReadings(std::vector<DeviceInfo> readings);

}  // namespace peek
