#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "battery/DeviceInfo.h"

namespace peek::compx {

// The battery query of the 2.4 GHz receivers built on Compx firmware -- the dongles that ship
// with VGN, VXE, ATK and Pulsar mice, among others. Nothing about it is documented by the
// vendor in prose: the frame layout below is the one the community worked out, the command
// numbers are the ones the vendors' own web configurators name, and all of it was measured on a
// VGN receiver.
//
// Two commands are ever sent, and both only read: the battery query, and the query that names
// the mouse model. Everything else the protocol can do -- settings, pairing, firmware -- is left
// alone, and no frame for it can be built from here.
//
// Everything in this unit is bytes in and values out, so it can be pinned down by tests on a
// machine with no receiver. The provider that finds a receiver and talks to it is the Win32 half.

// The vendor collection the queries go through: one top-level collection on the receiver's
// second interface, with 17-byte reports in both directions. The mouse collection itself is
// held exclusively by Windows and could not be written to anyway.
inline constexpr std::uint16_t kUsagePage = 0xff02;
inline constexpr std::uint16_t kUsage = 0x0002;
inline constexpr std::size_t kReportLength = 17;

using Report = std::array<std::uint8_t, kReportLength>;

// Whether receivers from this USB vendor are worth asking. The list is short on purpose: the
// collection shape alone is no proof of this firmware, and a write to a device that speaks
// something else is a write nobody can predict the effect of.
bool isKnownVendor(std::uint16_t vendorId) noexcept;

// The vendor id spelled into a HID device path ("...#vid_3554&pid_f503&mi_01#..."), or nothing
// when the path carries none. Lets the sweep pass over every other device without opening it.
std::optional<std::uint16_t> vendorIdFromPath(std::wstring_view path) noexcept;

// The read-battery request, checksum included.
Report batteryRequest() noexcept;

// The request for the model of the paired mouse -- ReadCIDMID in VGN's configurator,
// GetMouseCIDMID in ATK's. A read of two bytes the firmware carries for exactly this purpose.
Report modelRequest() noexcept;

// Whether an input report answers the battery request, or the model request, at all. The
// receiver also reports on its own and answers other programs' commands on the same collection,
// and every open handle sees every report, so the replies worth parsing have to be picked out of
// that stream.
bool isBatteryReply(std::span<std::uint8_t const> report) noexcept;
bool isModelReply(std::span<std::uint8_t const> report) noexcept;

struct Reading {
    int percent = 0;
    // The mouse is on its cable, which is all the firmware says about charging.
    bool onCable = false;
    int millivolts = 0;
};

// The reading in a battery reply, or nothing when the report is not one that can be believed:
// the wrong length, the wrong command, a checksum that does not add up, an error status, a
// level no percentage could be, or an all-zero payload. The last one matters most -- read as a
// level it would be an empty battery and a critical warning for a mouse that is only asleep.
std::optional<Reading> parseBatteryReply(std::span<std::uint8_t const> report) noexcept;

// On the cable means charging, and charging at 100 % means full. Off the cable the battery is
// draining, and saying so is what lets the low warnings and the time-left estimate apply.
ChargeState chargeStateOf(Reading const& reading) noexcept;

// What the model request answers: a customer id, which is the brand's line, and a model id
// within it. The receiver's USB ids cannot stand in for these -- VGN ships one receiver id with
// every mouse of its F1 range.
struct ModelId {
    int customer = 0;
    int model = 0;

    bool operator==(ModelId const&) const = default;
};

// The model id in a model reply, or nothing when the report cannot be believed -- the same
// checks as a battery reply, plus a length byte that has to cover both ids.
std::optional<ModelId> parseModelReply(std::span<std::uint8_t const> report) noexcept;

// What a receiver or a cabled mouse is, as far as can be told.
struct Identity {
    // Brand and model when either the model id or the USB ids are on record, "<brand> wireless
    // mouse" when only the brand can be read off the product string, and the product string
    // itself when not even that.
    std::wstring name;
    // False for a mouse on its own cable, which answers under a product id of its own.
    bool viaReceiver = true;
    // The model is on record, rather than the name being worked out from the product string.
    bool known = false;
};

// The model id wins when the mouse gave one that is on record; the USB ids come next, and the
// product string last.
Identity identify(std::uint16_t vendorId,
                  std::uint16_t productId,
                  std::wstring_view product,
                  std::optional<ModelId> model);

// What to call the mouse, from the receiver's product string. "VGN Mouse 2.4G Receiver" names
// the receiver, not the mouse, so a trailing receiver suffix is dropped -- but only a suffix
// recognised as one, and never down to nothing.
std::wstring nameFromProduct(std::wstring_view product);

// The device id, from the HID interface path of the receiver's vendor collection. A digest
// rather than the path: the path spells out this machine's USB topology, and the id is written
// to the battery log in the user's profile.
std::wstring deviceIdFromPath(std::wstring_view interfacePath);

}  // namespace peek::compx
