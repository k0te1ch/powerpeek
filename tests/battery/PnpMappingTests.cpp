// Turning what the Windows device tree says into what a card shows.
//
// The sweep that reads those values needs a device tree with a Bluetooth radio behind it, and
// the machine this was written on has no radio at all -- so the sweep is unreachable from here
// and everything it decides is not. What is reachable is the arithmetic it hands its findings
// to, and that is where the mistakes with no symptom live: a battery byte whose contract nobody
// wrote down, a Class of Device word whose six minor bits are two fields rather than one
// number, and an id that has to key a log on disk without spelling out a hardware address.
//
// The Class of Device cases carry their hex out in the open. A table of named constants
// checked against constants of the same name would prove only that the file agrees with
// itself; these are the words a device puts on the air, so the test writes them as such.

#include "TestSupport.h"

#include "battery/PnpMapping.h"

#include <string>

namespace {

using peek::DeviceKind;
using peek::pnp::deviceIdFromContainer;
using peek::pnp::deviceIdFromInstance;
using peek::pnp::kindFromClassOfDevice;
using peek::pnp::levelFromProperty;

// Assembles a Class of Device word the way a device does: service classes above the major
// class, the major class from bit 8, the minor from bit 2, and the two format bits left clear.
constexpr std::uint32_t classOfDevice(std::uint32_t major, std::uint32_t minor,
                                      std::uint32_t services = 0) {
    return (services << 13) | (major << 8) | (minor << 2);
}

}  // namespace

TEST_CASE("pnpMapping: a level inside the percentage range is taken at face value") {
    CHECK(levelFromProperty(0) == 0);
    CHECK(levelFromProperty(37) == 37);
    CHECK(levelFromProperty(100) == 100);
}

TEST_CASE("pnpMapping: a level no percentage could be is no reading at all") {
    // The property is undocumented and nothing validates it: two Bluetooth components write it,
    // the Settings page reads it, and no contract sits in between. 0xFF is what the field
    // carries in the wild when there is nothing to report, and a card showing 255 % -- or a
    // critical-battery warning at 101 -- is a worse answer than showing nothing.
    CHECK(levelFromProperty(101) == -1);
    CHECK(levelFromProperty(0xff) == -1);
}

TEST_CASE("pnpMapping: the head-worn audio classes become a headset") {
    // Major class 0x04 is Audio/Video; the three minors below are the ones a user wears.
    CHECK(kindFromClassOfDevice(classOfDevice(0x04, 0x01)) == DeviceKind::Headset);  // headset
    CHECK(kindFromClassOfDevice(classOfDevice(0x04, 0x02)) == DeviceKind::Headset);  // hands-free
    CHECK(kindFromClassOfDevice(classOfDevice(0x04, 0x06)) == DeviceKind::Headset);  // headphones
}

TEST_CASE("pnpMapping: audio that is not worn is not a headset") {
    // A loudspeaker and a car stereo are audio devices too, and calling either a headset would
    // put a picture of headphones on a card describing a speaker.
    CHECK(kindFromClassOfDevice(classOfDevice(0x04, 0x05)) == DeviceKind::Other);  // loudspeaker
    CHECK(kindFromClassOfDevice(classOfDevice(0x04, 0x08)) == DeviceKind::Other);  // car audio
}

TEST_CASE("pnpMapping: the peripheral minor is two fields, not one number") {
    // This is the trap the whole function exists for. Under major class 0x05 the top two minor
    // bits say whether the device has a keyboard, a pointing device or both, and the bottom
    // four name a device type. A mouse is 0b10'0000 and a gamepad is 0b00'0010: read as one
    // six-bit number they are 32 and 2, and any switch over that number files one as the other.
    CHECK(kindFromClassOfDevice(classOfDevice(0x05, 0b10'0000)) == DeviceKind::Mouse);
    CHECK(kindFromClassOfDevice(classOfDevice(0x05, 0b01'0000)) == DeviceKind::Keyboard);
    CHECK(kindFromClassOfDevice(classOfDevice(0x05, 0b00'0010)) == DeviceKind::Gamepad);
}

TEST_CASE("pnpMapping: a keyboard with a trackpad is a keyboard") {
    // Both form bits set. Something has to win, and it is the half the user looks at.
    CHECK(kindFromClassOfDevice(classOfDevice(0x05, 0b11'0000)) == DeviceKind::Keyboard);
}

TEST_CASE("pnpMapping: a named device type beats the shape of the device") {
    // A gamepad that also reports a pointing device -- a pad with a trackpad on it -- is still
    // a gamepad, because the device type field is the more specific of the two statements.
    CHECK(kindFromClassOfDevice(classOfDevice(0x05, 0b10'0010)) == DeviceKind::Gamepad);
}

TEST_CASE("pnpMapping: a class that names nothing recognised is not guessed at") {
    // Better an unclassified card than a wrong picture: an imaging device, a phone or a device
    // whose word is all zeroes says nothing about being a headset or a mouse.
    CHECK(kindFromClassOfDevice(classOfDevice(0x06, 0x20)) == DeviceKind::Other);  // imaging
    CHECK(kindFromClassOfDevice(classOfDevice(0x02, 0x04)) == DeviceKind::Other);  // phone
    CHECK(kindFromClassOfDevice(0) == DeviceKind::Other);
}

TEST_CASE("pnpMapping: the service classes above the device class are not read as one") {
    // Every real device sets service bits -- audio, rendering, telephony -- and they sit in the
    // same word directly above the major class. A mask that ran too wide would turn a headset
    // into an unclassified device the moment it advertised the services that make it useful.
    //
    // The lowest two service bits are the ones that matter for that mistake, because they are
    // the pair a five-bit major mask stops just short of: limited discoverable mode, which a
    // device sets while it is pairing, and LE audio. A headset must not change category on the
    // way in.
    std::uint32_t const advertised = 0b100'0010'0000;  // audio | rendering
    std::uint32_t const pairing = advertised | 0b1;    // and limited discoverable, while pairing

    CHECK(kindFromClassOfDevice(classOfDevice(0x04, 0x06, advertised)) == DeviceKind::Headset);
    CHECK(kindFromClassOfDevice(classOfDevice(0x04, 0x06, pairing)) == DeviceKind::Headset);
    CHECK(kindFromClassOfDevice(classOfDevice(0x05, 0b10'0000, pairing)) == DeviceKind::Mouse);
}

TEST_CASE("pnpMapping: the words real devices put on the air decode to what they are") {
    // Whole values, as measured from shipping hardware rather than assembled from the same
    // constants the implementation uses: a word built by the test out of the fields under test
    // proves only that the file agrees with itself.
    CHECK(kindFromClassOfDevice(0x000508) == DeviceKind::Gamepad);  // Xbox Wireless Controller
    CHECK(kindFromClassOfDevice(0x002508) == DeviceKind::Gamepad);  // DualShock 4
    CHECK(kindFromClassOfDevice(0x240404) == DeviceKind::Headset);  // a hands-free headset
}

TEST_CASE("pnpMapping: a word in a format nobody has defined is not decoded") {
    // The lowest two bits name the format the other twenty-two are in, and exactly one format
    // exists. Reading the fields out of a word that claims another one would file a device by
    // bits that mean something else entirely, with the confidence of a real answer.
    CHECK(kindFromClassOfDevice(classOfDevice(0x04, 0x06) | 0b01) == DeviceKind::Other);
    CHECK(kindFromClassOfDevice(classOfDevice(0x05, 0b10'0000) | 0b10) == DeviceKind::Other);
}

TEST_CASE("pnpMapping: a container id becomes an id that is stable and spelled out in full") {
    GUID const container{
        0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}};

    std::wstring const id = deviceIdFromContainer(container);

    // Every one of the sixteen bytes has to appear: a formatter that dropped a leading zero, or
    // one that stopped at the first four bytes, would key two devices to one card.
    CHECK(id == L"pnp:container:8c7ed206-3f8a-4827-b3ab-ae9e1faefc6c");
}

TEST_CASE("pnpMapping: leading zeroes in a container id survive the formatting") {
    GUID const container{
        0x0000000a, 0x000b, 0x000c, {0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e}};

    CHECK(deviceIdFromContainer(container) ==
          L"pnp:container:0000000a-000b-000c-000d-00000000000e");
}

TEST_CASE("pnpMapping: an id derived from an instance path does not contain the path") {
    // Bluetooth instance ids embed the device's hardware address, and this id is written to the
    // battery log in the user's profile. The digest keys the device just as well as the string
    // does, and a log full of them names nobody's hardware.
    std::wstring const instance =
        L"BTHENUM\\{0000111e-0000-1000-8000-00805f9b34fb}_LOCALMFG&0000\\8&1a2b3c4d&0&AABBCCDDEEFF";

    std::wstring const id = deviceIdFromInstance(instance);

    CHECK(id.find(L"AABBCCDDEEFF") == std::wstring::npos);
    CHECK(id.find(L"BTHENUM") == std::wstring::npos);
    CHECK(id.starts_with(L"pnp:device:"));
}

TEST_CASE("pnpMapping: the same device gets the same id every sweep, and two devices do not") {
    // The id keys a log that outlives the process: a digest that moved between polls would fork
    // one device's history, and one that collided would merge two devices into a single series.
    std::wstring const first = L"BTHLE\\DEV_AABBCCDDEEFF\\8&1a2b3c4d&0&0001";
    std::wstring const second = L"BTHLE\\DEV_AABBCCDDEEFE\\8&1a2b3c4d&0&0001";

    CHECK(deviceIdFromInstance(first) == deviceIdFromInstance(first));
    CHECK(deviceIdFromInstance(first) != deviceIdFromInstance(second));
    CHECK(deviceIdFromInstance(L"") == deviceIdFromInstance(L""));
}
