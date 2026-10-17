// The HID++ battery query of Logitech's wireless headsets, byte by byte.
//
// The headset sits behind a Lightspeed dongle and a radio link, and a build agent has neither.
// What can be checked here is everything that decides what goes out and what is believed when
// something comes back: a request to the wrong feature index is a call to some other function
// of the headset, and an error misread as a measurement is a level for a headset that is off.
//
// The frames are written out in hex rather than assembled from the unit's own constants, so a
// test cannot pass merely because the file agrees with itself. The replies are the ones a
// PRO X Wireless (046D:0ABA) sent, with the software tag in byte 3 set to this program's 0xA.

#include "TestSupport.h"

#include "battery/LogitechHeadsetProtocol.h"

#include <cwctype>
#include <optional>
#include <span>
#include <string>

namespace {

using peek::ChargeState;
using peek::logitech::adcRequest;
using peek::logitech::deviceIdFromPath;
using peek::logitech::featureIndexRequest;
using peek::logitech::headsetName;
using peek::logitech::isOwnReply;
using peek::logitech::isReplyTo;
using peek::logitech::isSupportedHeadset;
using peek::logitech::parseAdcReply;
using peek::logitech::parseFeatureIndexReply;
using peek::logitech::percentFromMillivolts;
using peek::logitech::Report;

// IRoot.GetFeature(0x1F20) answered: index 6, type 0, version 4.
constexpr Report kRootReply = {0x11, 0xff, 0x00, 0x0a, 0x06, 0x00, 0x04, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// GetADCMeasurement on index 6: 0x1006 = 4102 mV, flags 0x01 (on battery).
constexpr Report kAdcReply = {0x11, 0xff, 0x06, 0x0a, 0x10, 0x06, 0x01, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// The HID++ 2.0 error for that request: index 0xFF, then the refused index and function, then
// the error code -- what the dongle answers while the headset is off.
constexpr Report kAdcError = {0x11, 0xff, 0xff, 0x06, 0x0a, 0x05, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

Report adcReply(std::uint16_t millivolts, std::uint8_t flags) {
    Report frame = kAdcReply;
    frame[4] = static_cast<std::uint8_t>(millivolts >> 8);
    frame[5] = static_cast<std::uint8_t>(millivolts & 0xff);
    frame[6] = flags;
    return frame;
}

}  // namespace

TEST_CASE("logitechHeadset: only the wireless PRO X is asked, never the wired one") {
    CHECK(isSupportedHeadset(0x046d, 0x0aba));
    // Same HID++ collection, no battery.
    CHECK_FALSE(isSupportedHeadset(0x046d, 0x0aaa));
    CHECK_FALSE(isSupportedHeadset(0x046d, 0xc232));
    CHECK_FALSE(isSupportedHeadset(0x258a, 0x0aba));
    CHECK(headsetName(0x0aba) == L"Logitech PRO X Wireless");
}

TEST_CASE("logitechHeadset: the root is asked where the ADC measurement feature lives") {
    constexpr Report expected = {0x11, 0xff, 0x00, 0x0a, 0x1f, 0x20, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    CHECK(featureIndexRequest(0x1f20) == expected);
}

TEST_CASE("logitechHeadset: the measurement is function 0 of the index the root named") {
    constexpr Report expected = {0x11, 0xff, 0x06, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    CHECK(adcRequest(6) == expected);
}

TEST_CASE("logitechHeadset: the root's answer gives the index, and an absent feature none") {
    CHECK(parseFeatureIndexReply(kRootReply) == std::optional<std::uint8_t>{std::uint8_t{6}});

    Report absent = kRootReply;
    absent[4] = 0x00;
    CHECK_FALSE(parseFeatureIndexReply(absent).has_value());

    Report error = kAdcError;
    error[3] = 0x00;
    CHECK(isReplyTo(0, error));
    CHECK_FALSE(parseFeatureIndexReply(error).has_value());
}

TEST_CASE("logitechHeadset: a measured reply reads as a voltage and a direction") {
    auto const reading = parseAdcReply(6, kAdcReply);
    REQUIRE(reading.has_value());
    CHECK(reading->millivolts == 4102);
    CHECK(reading->charge == ChargeState::Discharging);
}

TEST_CASE("logitechHeadset: the flags say charging, full or unknown") {
    CHECK(parseAdcReply(6, adcReply(4000, 0x03))->charge == ChargeState::Charging);
    CHECK(parseAdcReply(6, adcReply(4180, 0x07))->charge == ChargeState::Full);
    CHECK(parseAdcReply(6, adcReply(3900, 0x0f))->charge == ChargeState::Unknown);
}

TEST_CASE("logitechHeadset: a measurement not marked valid is no reading") {
    CHECK_FALSE(parseAdcReply(6, adcReply(4000, 0x00)).has_value());
    CHECK_FALSE(parseAdcReply(6, adcReply(4000, 0x02)).has_value());
}

TEST_CASE("logitechHeadset: the error a dongle sends for an off headset is no reading") {
    CHECK(isOwnReply(kAdcError));
    CHECK(isReplyTo(6, kAdcError));
    CHECK_FALSE(parseAdcReply(6, kAdcError).has_value());
}

TEST_CASE("logitechHeadset: other programs' replies and the headset's own events are skipped") {
    // HeadsetControl tags its requests 0xD.
    Report other = kAdcReply;
    other[3] = 0x0d;
    CHECK_FALSE(isOwnReply(other));
    CHECK_FALSE(parseAdcReply(6, other).has_value());

    // An event the headset broadcasts unprompted carries no tag at all.
    Report event = kAdcReply;
    event[3] = 0x00;
    CHECK_FALSE(isOwnReply(event));

    // A reply for another index is not the measurement, whatever it holds.
    CHECK_FALSE(parseAdcReply(5, kAdcReply).has_value());

    // A short report, or one for another device index, is not HID++ for this headset.
    Report shortReport = kAdcReply;
    shortReport[0] = 0x10;
    CHECK_FALSE(isOwnReply(shortReport));
    Report otherDevice = kAdcReply;
    otherDevice[1] = 0x01;
    CHECK_FALSE(isOwnReply(otherDevice));
    CHECK_FALSE(isOwnReply(std::span<std::uint8_t const>{kAdcReply.data(), 7}));
}

TEST_CASE("logitechHeadset: the voltage curve hits its points and stays between them") {
    CHECK(percentFromMillivolts(4300) == 100);
    CHECK(percentFromMillivolts(4150) == 100);
    CHECK(percentFromMillivolts(3830) == 50);
    CHECK(percentFromMillivolts(3780) == 30);
    CHECK(percentFromMillivolts(3740) == 20);
    CHECK(percentFromMillivolts(3670) == 5);
    CHECK(percentFromMillivolts(3320) == 0);
    CHECK(percentFromMillivolts(3000) == 0);
    CHECK(percentFromMillivolts(0) == 0);
    // The measured 4102 mV, between 3830 (50 %) and 4150 (100 %).
    CHECK(percentFromMillivolts(4102) == 93);
    CHECK(percentFromMillivolts(3990) == 75);
}

TEST_CASE("logitechHeadset: the curve never goes down as the voltage goes up") {
    int previous = percentFromMillivolts(3000);
    for (int millivolts = 3001; millivolts <= 4300; ++millivolts) {
        int const percent = percentFromMillivolts(millivolts);
        CHECK(percent >= previous);
        CHECK(percent <= 100);
        previous = percent;
    }
}

TEST_CASE("logitechHeadset: the id names no path and ignores the path's case") {
    std::wstring const path =
        L"\\\\?\\hid#vid_046d&pid_0aba&mi_03&col02#8&1beab605&0&0001#{4d1e55b2-f16f-11cf-88cb-"
        L"001111000030}";
    std::wstring upper = path;
    for (wchar_t& c : upper) {
        c = static_cast<wchar_t>(std::towupper(c));
    }
    std::wstring const id = deviceIdFromPath(path);
    CHECK(id.starts_with(L"logitech:"));
    CHECK(id.find(L"1beab605") == std::wstring::npos);
    CHECK(deviceIdFromPath(upper) == id);
}
