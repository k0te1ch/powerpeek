#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "battery/DeviceInfo.h"
#include "core/Win.h"

namespace peek::pnp {

// What the device tree hands over, in the shape it hands it over in: a percentage byte whose
// contract nobody wrote down, a Bluetooth Class of Device word, and the container GUID that
// says which physical device the devnode belongs to. Turning those three into a DeviceInfo is
// arithmetic and table lookup, so it lives here rather than in the sweep -- the sweep needs a
// device tree and a Bluetooth radio, and this needs neither.

// The battery percentage, or -1 when the byte does not describe one.
//
// The property is undocumented and validated by nobody: it is written by two different
// Bluetooth components and read by the Settings UI, and nothing in between promises a range.
// 0xFF is the value seen in the wild for "no reading", and anything above 100 is not a
// percentage whatever it was meant to be.
int levelFromProperty(std::uint32_t raw) noexcept;

// The device class, from the Bluetooth Class of Device field.
//
// Chosen over the Windows device categories on purpose: those classify an Xbox wireless
// adapter as "Network", while Class of Device is a documented Bluetooth field that says what
// the thing is. A value that names nothing recognised comes back as Other, which is honest --
// the card then says what the device reports about itself and nothing more.
DeviceKind kindFromClassOfDevice(std::uint32_t classOfDevice) noexcept;

// The device id, from the container GUID.
//
// One physical device answers on several devnodes -- a headset carries its battery on the
// hands-free child and its name on the parent -- and the container id is the one value they
// all share. Keying on it is what makes those several readings collapse into one card.
std::wstring deviceIdFromContainer(GUID const& container);

// The device id for the rare devnode that carries no container id.
//
// Not the instance id itself: for a Bluetooth device that string contains the hardware
// address, and this id is written to the battery log that sits in the user's profile. A digest
// keys just as well and names nothing.
std::wstring deviceIdFromInstance(std::wstring_view instanceId);

// Whether a devnode is the device itself rather than one of its services.
//
// A Bluetooth device is one devnode named after it -- BTHENUM\DEV_<address> for classic,
// BTHLE\DEV_<address> for LE -- with a child per service under it. The battery level lives on a
// service child (the hands-free one for a headset) under that service's name, "REDMI Buds 8 Pro
// Hands-Free AG"; the device node carries the name the user paired and the connection state.
// Measured on Windows 10 19045 with REDMI Buds 8 Pro.
bool isBluetoothDeviceNode(std::wstring_view instanceId) noexcept;

// The name a card shows for a battery found on `serviceName`'s devnode: the device node's name
// when the sweep found one in the same container, otherwise the service name without the
// service it names.
std::wstring displayName(std::wstring_view deviceName, std::wstring_view serviceName);

// Whether a battery reading belongs to a device that is connected right now.
//
// The connection state is the device node's to report. Measured on REDMI Buds 8 Pro: with the
// buds in the ears and playing, the device node says connected and the hands-free child that
// carries the level says not connected -- and the child keeps its last level after the buds go
// back in the case. Reading the child alone would hide a connected headset for good. So the
// device node's answer wins when the sweep found one; without it the service's own answer is
// taken, and a devnode that says nothing at all counts as connected.
bool isConnected(std::optional<bool> deviceNode, std::optional<bool> serviceNode) noexcept;

}  // namespace peek::pnp
