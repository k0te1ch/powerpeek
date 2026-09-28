// The battery query of Compx 2.4 GHz receivers, byte by byte.
//
// The receiver is a USB device with a mouse at the other end of a radio link, and a build agent
// has neither. What it can check is everything that decides what goes out and what is believed
// when something comes back -- and that is where a mistake does harm: a request with a wrong
// checksum is a frame the firmware was never meant to see, and a reply misread as 0 % is a
// critical warning for a mouse that is only switched off.
//
// The frames are written out in hex rather than assembled from the unit's own constants, so a
// test cannot pass merely because the file agrees with itself. The first reply below is the one
// a VGN receiver actually sent.

#include "TestSupport.h"

#include "battery/CompxProtocol.h"

#include <string>

namespace {

using peek::ChargeState;
using peek::compx::batteryRequest;
using peek::compx::chargeStateOf;
using peek::compx::deviceIdFromPath;
using peek::compx::identify;
using peek::compx::isModelReply;
using peek::compx::modelRequest;
using peek::compx::ModelId;
using peek::compx::parseModelReply;
using peek::compx::isBatteryReply;
using peek::compx::isKnownVendor;
using peek::compx::nameFromProduct;
using peek::compx::parseBatteryReply;
using peek::compx::Reading;
using peek::compx::Report;
using peek::compx::vendorIdFromPath;

// Measured on a VGN receiver: 30 %, off the cable, 0x0E95 = 3733 mV.
constexpr Report kMeasuredReply = {0x08, 0x04, 0x00, 0x00, 0x00, 0x02, 0x1e, 0x00, 0x0e,
                                   0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86};

// Puts the checksum on a hand-made frame, the way the firmware does: the byte that brings the
// sum of all seventeen to 0x55.
Report sealed(Report frame) {
    unsigned sum = 0;
    for (std::size_t i = 0; i + 1 < frame.size(); ++i) {
        sum += frame[i];
    }
    frame.back() = static_cast<std::uint8_t>(0x55 - sum);
    return frame;
}

Report reply(std::uint8_t percent, std::uint8_t cable, std::uint16_t millivolts) {
    Report frame{};
    frame[0] = 0x08;
    frame[1] = 0x04;
    frame[6] = percent;
    frame[7] = cable;
    frame[8] = static_cast<std::uint8_t>(millivolts >> 8);
    frame[9] = static_cast<std::uint8_t>(millivolts & 0xff);
    return sealed(frame);
}

}  // namespace

TEST_CASE("compxProtocol: the request is the read command and nothing else") {
    constexpr Report expected = {0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49};
    CHECK(batteryRequest() == expected);
}

TEST_CASE("compxProtocol: the reply a real receiver sent reads as 30 % on battery") {
    auto const reading = parseBatteryReply(kMeasuredReply);
    REQUIRE(reading.has_value());
    CHECK(reading->percent == 30);
    CHECK_FALSE(reading->onCable);
    CHECK(reading->millivolts == 3733);
}

TEST_CASE("compxProtocol: a reply whose checksum does not add up is not believed") {
    Report damaged = kMeasuredReply;
    damaged[6] = 0x1f;
    CHECK_FALSE(parseBatteryReply(damaged).has_value());

    Report wrongSum = kMeasuredReply;
    wrongSum[16] = 0x87;
    CHECK_FALSE(parseBatteryReply(wrongSum).has_value());
}

TEST_CASE("compxProtocol: a report answering another command is not a battery reply") {
    // The receiver shares this collection with the vendor's own utility, and every handle sees
    // every reply; one to another command must be passed over, not parsed as a level.
    Report other = kMeasuredReply;
    other[1] = 0x05;
    other = sealed(other);
    CHECK_FALSE(isBatteryReply(other));
    CHECK_FALSE(parseBatteryReply(other).has_value());

    Report otherId = kMeasuredReply;
    otherId[0] = 0x09;
    otherId = sealed(otherId);
    CHECK_FALSE(isBatteryReply(otherId));
    CHECK(isBatteryReply(kMeasuredReply));
}

TEST_CASE("compxProtocol: a report of the wrong length is not a battery reply") {
    std::array<std::uint8_t, 16> const shorter{0x08, 0x04};
    CHECK_FALSE(isBatteryReply(shorter));
    CHECK_FALSE(parseBatteryReply(shorter).has_value());
}

TEST_CASE("compxProtocol: an empty reply is no reading, not an empty battery") {
    // A receiver with its mouse switched off still answers, with nothing in the answer. Read as
    // a level, that is 0 % and a critical warning about a mouse that is merely asleep.
    CHECK_FALSE(parseBatteryReply(reply(0, 0, 0)).has_value());
    // And a buffer that is zero from end to end is not even a reply.
    CHECK_FALSE(parseBatteryReply(Report{}).has_value());
}

TEST_CASE("compxProtocol: a flat battery that still has a voltage is a real 0 %") {
    auto const reading = parseBatteryReply(reply(0, 0, 3300));
    REQUIRE(reading.has_value());
    CHECK(reading->percent == 0);
}

TEST_CASE("compxProtocol: a level no percentage could be is no reading") {
    CHECK(parseBatteryReply(reply(100, 0, 4200)).has_value());
    CHECK_FALSE(parseBatteryReply(reply(101, 0, 4200)).has_value());
    CHECK_FALSE(parseBatteryReply(reply(0xff, 0, 4200)).has_value());
}

TEST_CASE("compxProtocol: the cable flag decides whether the battery is charging") {
    auto const charging = parseBatteryReply(reply(64, 1, 3900));
    REQUIRE(charging.has_value());
    CHECK(charging->onCable);
    CHECK(chargeStateOf(*charging) == ChargeState::Charging);

    CHECK(chargeStateOf(Reading{64, false, 3900}) == ChargeState::Discharging);
    CHECK(chargeStateOf(Reading{100, true, 4200}) == ChargeState::Full);
    // Unplugged at 100 % is a full battery starting to drain, not a full one on the charger.
    CHECK(chargeStateOf(Reading{100, false, 4200}) == ChargeState::Discharging);
}

TEST_CASE("compxProtocol: only the vendors known to carry the firmware are asked") {
    CHECK(isKnownVendor(0x3554));
    CHECK(isKnownVendor(0x373b));
    CHECK_FALSE(isKnownVendor(0x046d));
    CHECK_FALSE(isKnownVendor(0x0000));
}

TEST_CASE("compxProtocol: the vendor id is read out of a HID path in either case") {
    CHECK(vendorIdFromPath(L"\\\\?\\hid#vid_3554&pid_f503&mi_01#8&2d4f&0&0000#{4d1e55b2-f16f}") ==
          0x3554);
    CHECK(vendorIdFromPath(L"\\\\?\\HID#VID_373B&PID_1085&MI_01#8&1&0&0000") == 0x373b);
    CHECK_FALSE(vendorIdFromPath(L"\\\\?\\hid#vid_35#").has_value());
    CHECK_FALSE(vendorIdFromPath(L"\\\\?\\hid#vid_zz54&pid_f503").has_value());
    CHECK_FALSE(vendorIdFromPath(L"").has_value());
}

TEST_CASE("compxProtocol: the receiver suffix comes off the product name") {
    CHECK(nameFromProduct(L"VGN Mouse 2.4G Receiver") == L"VGN Mouse");
    CHECK(nameFromProduct(L"ATK 2.4G Wireless Receiver") == L"ATK");
    CHECK(nameFromProduct(L"Pulsar 2.4GHz receiver") == L"Pulsar");
    CHECK(nameFromProduct(L"  VXE Dongle ") == L"VXE");
}

TEST_CASE("compxProtocol: a name that is nothing but the suffix, or has none, is kept") {
    CHECK(nameFromProduct(L"2.4G Receiver") == L"2.4G Receiver");
    CHECK(nameFromProduct(L"VXE R1 Pro Max") == L"VXE R1 Pro Max");
    CHECK(nameFromProduct(L"") == L"");
}

TEST_CASE("compxProtocol: the id keys the receiver without spelling out its path") {
    std::wstring const path = L"\\\\?\\hid#vid_3554&pid_f503&mi_01#8&2d4f1a&0&0000#{4d1e55b2}";
    std::wstring const id = deviceIdFromPath(path);

    CHECK(id.starts_with(L"compx:"));
    CHECK(id.find(L"f503") == std::wstring::npos);
    CHECK(id.find(L"2d4f1a") == std::wstring::npos);
    CHECK(id == deviceIdFromPath(path));
    CHECK(id == deviceIdFromPath(L"\\\\?\\HID#VID_3554&PID_F503&MI_01#8&2D4F1A&0&0000#{4D1E55B2}"));
    CHECK(id != deviceIdFromPath(L"\\\\?\\hid#vid_3554&pid_f503&mi_01#8&2d4f1b&0&0000#{4d1e55b2}"));
}

TEST_CASE("compxProtocol: a reply with a non-zero status byte is not believed") {
    Report failed = reply(64, 0, 3900);
    failed[2] = 0x01;
    CHECK_FALSE(parseBatteryReply(sealed(failed)).has_value());
}

TEST_CASE("compxProtocol: the model request is the read command 0x10 and nothing else") {
    constexpr Report expected = {0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3d};
    CHECK(modelRequest() == expected);
}

TEST_CASE("compxProtocol: the model reply a real receiver sent names customer 2, model 6") {
    // Measured on the receiver of a VGN Dragonfly F1 MOBA.
    constexpr Report measured = {0x08, 0x10, 0x00, 0x00, 0x00, 0x03, 0x02, 0x06, 0x4d,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe5};
    CHECK(isModelReply(measured));
    CHECK_FALSE(isModelReply(kMeasuredReply));
    auto const model = parseModelReply(measured);
    REQUIRE(model.has_value());
    CHECK(*model == ModelId{2, 6});
    CHECK_FALSE(parseModelReply(kMeasuredReply).has_value());
}

TEST_CASE("compxProtocol: a model reply that cannot be believed names nothing") {
    Report reply{};
    reply[0] = 0x08;
    reply[1] = 0x10;
    reply[5] = 0x02;
    reply[6] = 0x02;
    reply[7] = 0x06;
    CHECK(parseModelReply(sealed(reply)).has_value());

    Report shortLength = reply;
    shortLength[5] = 0x01;
    CHECK_FALSE(parseModelReply(sealed(shortLength)).has_value());

    Report failed = reply;
    failed[2] = 0x01;
    CHECK_FALSE(parseModelReply(sealed(failed)).has_value());

    Report zeros = reply;
    zeros[6] = 0x00;
    zeros[7] = 0x00;
    CHECK_FALSE(parseModelReply(sealed(zeros)).has_value());

    CHECK_FALSE(parseModelReply(reply).has_value());  // no checksum
}

TEST_CASE("compxProtocol: the model the mouse names is the name on the card") {
    // The receiver id 3554:f503 ships with every F1; only the mouse can say which one it is.
    auto const moba = identify(0x3554, 0xf503, L"VGN Mouse 2.4G Receiver", ModelId{2, 6});
    CHECK(moba.known);
    CHECK(moba.name == L"VGN Dragonfly F1 MOBA");
    CHECK(moba.viaReceiver);

    CHECK(identify(0x3554, 0xf503, L"VGN Mouse 2.4G Receiver", ModelId{2, 3}).name ==
          L"VGN Dragonfly F1 Pro Max");
    CHECK(identify(0x3554, 0xf58a, L"VXE NordicMouse 1K Dongle", ModelId{2, 27}).name ==
          L"VXE R1 Pro Max");
}

TEST_CASE("compxProtocol: the mouse names the model, the USB ids still say which end it is") {
    auto const cabled = identify(0x3554, 0xf58c, L"VXE R1 Pro Max", ModelId{2, 27});
    CHECK(cabled.name == L"VXE R1 Pro Max");
    CHECK_FALSE(cabled.viaReceiver);
}

TEST_CASE("compxProtocol: ids on record name the model before the mouse has") {
    auto const receiver = identify(0x3554, 0xf58a, L"VXE NordicMouse 1K Dongle", std::nullopt);
    CHECK(receiver.known);
    CHECK(receiver.name == L"VXE R1 Pro Max");
    CHECK(receiver.viaReceiver);

    auto const cabled = identify(0x3554, 0xf58c, L"VXE R1 Pro Max", std::nullopt);
    CHECK(cabled.known);
    CHECK_FALSE(cabled.viaReceiver);

    CHECK(identify(0x373b, 0x1085, L"Wireless mouse -1k dongle", std::nullopt).name ==
          L"VXE R1 SE+");
}

TEST_CASE("compxProtocol: a receiver shared by a range gets the brand until the mouse speaks") {
    auto const vgn = identify(0x3554, 0xf503, L"VGN Mouse 2.4G Receiver", std::nullopt);
    CHECK_FALSE(vgn.known);
    CHECK(vgn.name == L"VGN wireless mouse");
    CHECK(vgn.viaReceiver);

    // A model id nobody has put on record is no better than none.
    CHECK(identify(0x3554, 0xf503, L"VGN Mouse 2.4G Receiver", ModelId{2, 200}).name ==
          L"VGN wireless mouse");
    CHECK(identify(0x3554, 0x0001, L"pulsar 8K Dongle", std::nullopt).name ==
          L"Pulsar wireless mouse");
}

TEST_CASE("compxProtocol: with no brand to read, the product string is the name") {
    // "VGNX" starts with a brand but is not one; only a whole word counts.
    CHECK(identify(0x3554, 0x0001, L"VGNX 2.4G Receiver", std::nullopt).name == L"VGNX");
    CHECK(identify(0x3554, 0x0001, L"Compx Dongle", std::nullopt).name == L"Compx");
    CHECK(identify(0x3554, 0x0001, L"", std::nullopt).name == L"Wireless mouse");
}
