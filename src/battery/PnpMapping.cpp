#include "battery/PnpMapping.h"

#include <format>

namespace peek::pnp {
namespace {

// The Class of Device word, as the Bluetooth assigned numbers lay it out: two format bits, six
// minor bits, five major bits, and the service classes above those.
constexpr std::uint32_t kFormatMask = 0x03;
constexpr std::uint32_t kMinorShift = 2;
constexpr std::uint32_t kMinorMask = 0x3f;
constexpr std::uint32_t kMajorShift = 8;
constexpr std::uint32_t kMajorMask = 0x1f;

constexpr std::uint32_t kMajorAudioVideo = 0x04;
constexpr std::uint32_t kMajorPeripheral = 0x05;

// Audio/Video minors that mean something worn on a head.
constexpr std::uint32_t kAudioHeadset = 0x01;
constexpr std::uint32_t kAudioHandsFree = 0x02;
constexpr std::uint32_t kAudioHeadphones = 0x06;

// Under the Peripheral major class the six minor bits are two fields, not one number: the top
// two say whether the thing has a keyboard, a pointing device or both, and the bottom four name
// a device type. A mouse is 0b10'0000 and a gamepad is 0b00'0010, so reading the six bits as a
// single value files one of them under the other.
constexpr std::uint32_t kPeripheralTypeMask = 0x0f;
constexpr std::uint32_t kPeripheralFormShift = 4;
constexpr std::uint32_t kPeripheralFormKeyboard = 0x1;
constexpr std::uint32_t kPeripheralFormPointing = 0x2;

constexpr std::uint32_t kPeripheralJoystick = 0x01;
constexpr std::uint32_t kPeripheralGamepad = 0x02;
constexpr std::uint32_t kPeripheralDigitiser = 0x05;
constexpr std::uint32_t kPeripheralDigitalPen = 0x07;

DeviceKind peripheralKind(std::uint32_t minor) noexcept {
    switch (minor & kPeripheralTypeMask) {
        case kPeripheralJoystick:
        case kPeripheralGamepad:
            return DeviceKind::Gamepad;
        case kPeripheralDigitiser:
        case kPeripheralDigitalPen:
            return DeviceKind::Pen;
        default:
            break;
    }

    // No named device type, so the shape of the thing is all there is. A combo device reports
    // both bits; it is called a keyboard because that is the half a user looks at.
    std::uint32_t const form = minor >> kPeripheralFormShift;
    if ((form & kPeripheralFormKeyboard) != 0) {
        return DeviceKind::Keyboard;
    }
    if ((form & kPeripheralFormPointing) != 0) {
        return DeviceKind::Mouse;
    }
    return DeviceKind::Other;
}

}  // namespace

int levelFromProperty(std::uint32_t raw) noexcept {
    return raw <= 100 ? static_cast<int>(raw) : -1;
}

DeviceKind kindFromClassOfDevice(std::uint32_t classOfDevice) noexcept {
    // The lowest two bits say which format the rest of the word is in, and exactly one format
    // has ever been defined. A word that claims another one is not a word this knows how to
    // read, and decoding it anyway would file a device by fields that mean something else.
    if ((classOfDevice & kFormatMask) != 0) {
        return DeviceKind::Other;
    }

    std::uint32_t const major = (classOfDevice >> kMajorShift) & kMajorMask;
    std::uint32_t const minor = (classOfDevice >> kMinorShift) & kMinorMask;

    switch (major) {
        case kMajorAudioVideo:
            switch (minor) {
                case kAudioHeadset:
                case kAudioHandsFree:
                case kAudioHeadphones:
                    return DeviceKind::Headset;
                default:
                    // A speaker, a car stereo or a camcorder: audio, but not a thing whose
                    // charge belongs on a card that says "headset".
                    return DeviceKind::Other;
            }
        case kMajorPeripheral:
            return peripheralKind(minor);
        default:
            return DeviceKind::Other;
    }
}

std::wstring deviceIdFromContainer(GUID const& container) {
    return std::format(L"pnp:container:{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}"
                       L"{:02x}{:02x}",
                       container.Data1, container.Data2, container.Data3, container.Data4[0],
                       container.Data4[1], container.Data4[2], container.Data4[3],
                       container.Data4[4], container.Data4[5], container.Data4[6],
                       container.Data4[7]);
}

std::wstring deviceIdFromInstance(std::wstring_view instanceId) {
    // FNV-1a over the code units. A hash, not a cipher: the point is only that the battery log
    // ends up holding something that keys the device without spelling out its address.
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;

    std::uint64_t digest = kOffsetBasis;
    for (wchar_t const unit : instanceId) {
        auto const value = static_cast<std::uint16_t>(unit);
        digest = (digest ^ (value & 0xff)) * kPrime;
        digest = (digest ^ (value >> 8)) * kPrime;
    }
    return std::format(L"pnp:device:{:016x}", digest);
}

}  // namespace peek::pnp
