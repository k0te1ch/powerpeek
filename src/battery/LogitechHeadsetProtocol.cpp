#include "battery/LogitechHeadsetProtocol.h"

#include <algorithm>
#include <cwctype>
#include <format>

#include "core/Digest.h"

namespace peek::logitech {
namespace {

// HID++ framing: a long report, addressed to the headset behind the dongle.
constexpr std::uint8_t kLongReport = 0x11;
constexpr std::uint8_t kDeviceIndex = 0xff;
// The error frame names feature index 0xFF, then echoes the index and function it refuses.
constexpr std::uint8_t kErrorIndex = 0xff;
constexpr std::uint8_t kRootIndex = 0x00;

// The low nibble of the function byte is the caller's own tag, echoed in the reply. Events the
// device sends unprompted carry 0, so a non-zero tag tells this program's replies from those.
// HeadsetControl uses 0xD and the kernel 0x1; this one is neither, so the three cannot take
// each other's answers for their own when they run side by side.
constexpr std::uint8_t kSoftwareId = 0x0a;
constexpr std::uint8_t kGetFunction = 0x00;
constexpr std::uint8_t kFunctionByte = kGetFunction | kSoftwareId;

// The flags byte of an ADC measurement, as hid-logitech-hidpp reads it.
constexpr std::uint8_t kFlagValid = 0x01;
constexpr std::uint8_t kFlagsDischarging = 0x01;
constexpr std::uint8_t kFlagsCharging = 0x03;
constexpr std::uint8_t kFlagsFull = 0x07;

constexpr std::uint16_t kProXWireless = 0x0aba;

// HeadsetControl, lib/devices/logitech_gpro.hpp and protocols/logitech_calibrations.hpp: the
// G PRO range shares one curve, interpolated linearly between these points.
struct CurvePoint {
    int millivolts;
    int percent;
};
constexpr std::array<CurvePoint, 6> kCurve{{
    {4150, 100},
    {3830, 50},
    {3780, 30},
    {3740, 20},
    {3670, 5},
    {3320, 0},
}};

Report request(std::uint8_t featureIndex) noexcept {
    Report frame{};
    frame[0] = kLongReport;
    frame[1] = kDeviceIndex;
    frame[2] = featureIndex;
    frame[3] = kFunctionByte;
    return frame;
}

bool isLongReport(std::span<std::uint8_t const> report) noexcept {
    return report.size() == kReportLength && report[0] == kLongReport &&
           report[1] == kDeviceIndex;
}

bool isError(std::span<std::uint8_t const> report) noexcept {
    return report[2] == kErrorIndex;
}

}  // namespace

bool isSupportedHeadset(std::uint16_t vendorId, std::uint16_t productId) noexcept {
    return vendorId == kVendorLogitech && productId == kProXWireless;
}

std::wstring headsetName(std::uint16_t productId) {
    return productId == kProXWireless ? L"Logitech PRO X Wireless" : L"Logitech headset";
}

Report featureIndexRequest(std::uint16_t featureId) noexcept {
    Report frame = request(kRootIndex);
    frame[4] = static_cast<std::uint8_t>(featureId >> 8);
    frame[5] = static_cast<std::uint8_t>(featureId & 0xff);
    return frame;
}

Report adcRequest(std::uint8_t featureIndex) noexcept {
    return request(featureIndex);
}

bool isReplyTo(std::uint8_t featureIndex, std::span<std::uint8_t const> report) noexcept {
    if (!isLongReport(report)) {
        return false;
    }
    if (isError(report)) {
        return report[3] == featureIndex && report[4] == kFunctionByte;
    }
    return report[2] == featureIndex && report[3] == kFunctionByte;
}

bool isOwnReply(std::span<std::uint8_t const> report) noexcept {
    if (!isLongReport(report)) {
        return false;
    }
    return isError(report) ? report[4] == kFunctionByte : report[3] == kFunctionByte;
}

std::optional<std::uint8_t> parseFeatureIndexReply(std::span<std::uint8_t const> report) noexcept {
    if (!isReplyTo(kRootIndex, report) || isError(report) || report[4] == 0) {
        return std::nullopt;
    }
    return report[4];
}

std::optional<Reading> parseAdcReply(std::uint8_t featureIndex,
                                     std::span<std::uint8_t const> report) noexcept {
    if (!isReplyTo(featureIndex, report) || isError(report)) {
        return std::nullopt;
    }
    std::uint8_t const flags = report[6];
    if ((flags & kFlagValid) == 0) {
        return std::nullopt;
    }
    Reading reading;
    reading.millivolts = (report[4] << 8) | report[5];
    switch (flags) {
        case kFlagsDischarging:
            reading.charge = ChargeState::Discharging;
            break;
        case kFlagsCharging:
            reading.charge = ChargeState::Charging;
            break;
        case kFlagsFull:
            reading.charge = ChargeState::Full;
            break;
        default:
            reading.charge = ChargeState::Unknown;
            break;
    }
    return reading;
}

int percentFromMillivolts(int millivolts) noexcept {
    if (millivolts >= kCurve.front().millivolts) {
        return kCurve.front().percent;
    }
    for (std::size_t i = 1; i < kCurve.size(); ++i) {
        CurvePoint const upper = kCurve[i - 1];
        CurvePoint const lower = kCurve[i];
        if (millivolts >= lower.millivolts) {
            int const span = upper.millivolts - lower.millivolts;
            int const above = millivolts - lower.millivolts;
            // Rounded to the nearest percent rather than truncated, so a reading on the way up
            // and the same reading on the way down show the same number.
            return lower.percent + ((upper.percent - lower.percent) * above + span / 2) / span;
        }
    }
    return kCurve.back().percent;
}

std::wstring deviceIdFromPath(std::wstring_view interfacePath) {
    // The same interface can be handed out in either case, and one device must not become two
    // over that.
    std::wstring folded{interfacePath};
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return std::format(L"logitech:{:016x}", digestOf(folded));
}

}  // namespace peek::logitech
