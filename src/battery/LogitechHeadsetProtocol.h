#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "battery/DeviceInfo.h"

namespace peek::logitech {

// The battery of Logitech's wireless gaming headsets on their Lightspeed dongles, read through
// HID++ 2.0 the way HeadsetControl and the Linux hid-logitech-hidpp driver read it: look up the
// index of the ADC measurement feature (0x1F20) with the root feature, then ask that feature for
// its measurement -- a voltage and a flags byte saying whether the headset is charging.
//
// Two requests are ever built here, and both are getters: IRoot.GetFeature and
// ADCMeasurement.GetADCMeasurement, function 0 of each. Every other function of the ADC
// feature's index -- HeadsetControl sends the auto-off timer to the same feature on some
// models -- is out of reach, because no frame for it can be built from here.
//
// Measured on a PRO X Wireless (046D:0ABA): the root answers 0x1F20 at index 6, version 4, and
// the dongle reports none of the battery features 0x1000, 0x1001 or 0x1004.
//
// Everything in this unit is bytes in and values out, so it can be pinned down by tests on a
// machine with no headset. The provider that finds the dongle and talks to it is the Win32 half.

// The collection the requests go through: the HID++ long-report collection on the dongle's
// fourth interface, with 20-byte reports in both directions.
inline constexpr std::uint16_t kVendorLogitech = 0x046d;
inline constexpr std::uint16_t kUsagePage = 0xff43;
inline constexpr std::uint16_t kUsage = 0x0202;
inline constexpr std::size_t kReportLength = 20;

using Report = std::array<std::uint8_t, kReportLength>;

inline constexpr std::uint16_t kAdcMeasurementFeature = 0x1f20;

// The headsets known to answer this way, by the dongle's product id. Not a guess from the
// collection's shape: the wired PRO X (046D:0AAA) carries the same collection and has no battery
// at all, and a request to a device that speaks something else is a request nobody can predict
// the effect of.
bool isSupportedHeadset(std::uint16_t vendorId, std::uint16_t productId) noexcept;

// What the card calls the headset.
std::wstring headsetName(std::uint16_t productId);

// IRoot.GetFeature(featureId): which index the feature answers at.
Report featureIndexRequest(std::uint16_t featureId) noexcept;

// ADCMeasurement.GetADCMeasurement on the index the root named.
Report adcRequest(std::uint8_t featureIndex) noexcept;

// Whether an input report answers a request this unit sent to `featureIndex` -- its own answer
// or the HID++ error for it. The dongle also sends events of its own, and every open handle sees
// replies to other programs' requests, so the replies worth parsing have to be picked out.
bool isReplyTo(std::uint8_t featureIndex, std::span<std::uint8_t const> report) noexcept;

// Whether an input report answers any request this unit sent, to whichever index. The filter the
// transport waits on: a plain function, so it cannot know the index, which the parse then checks.
bool isOwnReply(std::span<std::uint8_t const> report) noexcept;

// The index the root gave for the feature, or nothing when the feature is absent (index 0) or
// the report is an error.
std::optional<std::uint8_t> parseFeatureIndexReply(std::span<std::uint8_t const> report) noexcept;

struct Reading {
    int millivolts = 0;
    ChargeState charge = ChargeState::Unknown;
};

// The measurement in an ADC reply, or nothing when there is none to believe: an error -- which
// is what the dongle answers while the headset is off or out of range -- or flags that do not
// mark the measurement as valid.
std::optional<Reading> parseAdcReply(std::uint8_t featureIndex,
                                     std::span<std::uint8_t const> report) noexcept;

// The charge for a battery voltage, by the curve HeadsetControl uses for the G PRO range. The
// curve is not documented by Logitech and was not measured here; it is the one already in use.
int percentFromMillivolts(int millivolts) noexcept;

// The device id, from the HID interface path of the dongle's collection. A digest rather than
// the path, which spells out this machine's USB topology and is written to the battery log.
std::wstring deviceIdFromPath(std::wstring_view interfacePath);

}  // namespace peek::logitech
