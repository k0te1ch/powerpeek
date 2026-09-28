#include "battery/CompxProtocol.h"

#include <algorithm>
#include <cwctype>
#include <format>

#include "core/Digest.h"

namespace peek::compx {
namespace {

// Compx itself, and the vendor id the same firmware ships under on some ATK and VXE models.
// A brand that reuses one of these needs nothing; one with a vendor id of its own is a line
// here, once somebody has seen its receiver answer.
constexpr std::array<std::uint16_t, 2> kKnownVendors = {0x3554, 0x373b};

// Models by the ids the mouse itself reports. The VGN rows are the table VGN's own configurator
// (hub.vgnlab.com) names its mice from; the VXE and ATK rows are ATK's, as the OpenMouse project
// (mouse-protocol, drivers/atk/products.ts) recorded them. Several model ids per mouse are the
// colourways and editions of one model. Customer 2, model 6 is the VGN Dragonfly F1 MOBA this
// was measured on.
struct KnownModelId {
    ModelId id;
    std::wstring_view name;
};

constexpr KnownModelId kKnownModelIds[] = {
    {{1, 8}, L"ATK F1 Ultimate 2.0"},
    {{1, 31}, L"ATK A9 Mini+"},
    {{1, 52}, L"ATK A9 Mini+"},
    {{2, 1}, L"VGN Dragonfly F1 Pro"},
    {{2, 2}, L"VGN Dragonfly F1 Pro"},
    {{2, 23}, L"VGN Dragonfly F1 Pro"},
    {{2, 24}, L"VGN Dragonfly F1 Pro"},
    {{2, 3}, L"VGN Dragonfly F1 Pro Max"},
    {{2, 4}, L"VGN Dragonfly F1 Pro Max"},
    {{2, 25}, L"VGN Dragonfly F1 Pro Max"},
    {{2, 26}, L"VGN Dragonfly F1 Pro Max"},
    {{2, 48}, L"VGN Dragonfly F1 Pro Max"},
    {{2, 52}, L"VGN Dragonfly F1 Pro Max"},
    {{2, 5}, L"VGN Dragonfly F1 MOBA"},
    {{2, 6}, L"VGN Dragonfly F1 MOBA"},
    {{2, 33}, L"VGN Dragonfly F1 MOBA"},
    {{2, 19}, L"VGN Dragonfly F1 S"},
    {{2, 20}, L"VGN Dragonfly F1 S"},
    {{2, 21}, L"VGN Dragonfly F1 S"},
    {{2, 22}, L"VGN Dragonfly F1 S"},
    {{2, 11}, L"VXE R1"},
    {{2, 12}, L"VXE R1"},
    {{2, 27}, L"VXE R1 Pro Max"},
    {{2, 32}, L"VXE R1 SE+"},
    {{2, 39}, L"ATK X1 Pro Max"},
};

// Models by USB ids, for a mouse that has not named itself yet -- asleep since the receiver
// arrived, or on firmware that does not answer the model request. Only ids that stand for one
// mouse are here, seen on hardware by HaloBattery (providers/pulsar.py) and OpenMouse (drivers/atk
// and drivers/vgn). A mouse on its cable answers under the second id of its pair.
struct KnownProductId {
    std::uint16_t vendorId;
    std::uint16_t productId;
    std::wstring_view name;
    bool cable;
};

constexpr KnownProductId kKnownProductIds[] = {
    {0x3554, 0xf508, L"Pulsar X2 V2 Mini", false},
    {0x3554, 0xf507, L"Pulsar X2 V2 Mini", true},
    {0x3554, 0xf58a, L"VXE R1 Pro Max", false},
    {0x3554, 0xf58c, L"VXE R1 Pro Max", true},
    {0x3554, 0xf58e, L"VXE R1 SE+", false},
    {0x3554, 0xf58f, L"VXE R1 SE+", true},
    {0x373b, 0x1085, L"VXE R1 SE+", false},
    {0x3554, 0xfb56, L"VGN Dragonfly F2 Master+", false},
    {0x3554, 0xfb57, L"VGN Dragonfly F2 Master+", true},
};

// Brands that ship this firmware, spelled the way they spell themselves, for a receiver whose
// ids are not on record but whose product string starts with the brand.
constexpr std::wstring_view kBrands[] = {L"VGN", L"VXE", L"ATK", L"Pulsar"};

constexpr std::uint8_t kReportId = 0x08;
constexpr std::uint8_t kCommandBattery = 0x04;
constexpr std::uint8_t kCommandModel = 0x10;
// The checksum byte brings the sum of the whole frame to this.
constexpr std::uint8_t kChecksumTarget = 0x55;

constexpr std::size_t kCommandOffset = 1;
// Zero on every reply that carries data; the vendor configurators ignore a reply with anything
// else here.
constexpr std::size_t kStatusOffset = 2;
constexpr std::size_t kLengthOffset = 5;
constexpr std::size_t kDataOffset = 6;
constexpr std::size_t kPercentOffset = kDataOffset;
constexpr std::size_t kCableOffset = kDataOffset + 1;
constexpr std::size_t kMillivoltsOffset = kDataOffset + 2;
constexpr std::size_t kChecksumOffset = kReportLength - 1;

std::uint8_t checksumOf(std::span<std::uint8_t const> frame) noexcept {
    unsigned sum = 0;
    for (std::size_t i = 0; i < kChecksumOffset; ++i) {
        sum += frame[i];
    }
    return static_cast<std::uint8_t>(kChecksumTarget - sum);
}

Report requestFor(std::uint8_t command) noexcept {
    Report frame{};
    frame[0] = kReportId;
    frame[kCommandOffset] = command;
    frame[kChecksumOffset] = checksumOf(frame);
    return frame;
}

bool isReplyTo(std::span<std::uint8_t const> report, std::uint8_t command) noexcept {
    return report.size() == kReportLength && report[0] == kReportId &&
           report[kCommandOffset] == command;
}

// A reply to `command` that can be believed at all: the checksum adds up and the status says
// the receiver did what was asked.
bool isSoundReplyTo(std::span<std::uint8_t const> report, std::uint8_t command) noexcept {
    return isReplyTo(report, command) && report[kChecksumOffset] == checksumOf(report) &&
           report[kStatusOffset] == 0;
}

wchar_t lower(wchar_t c) noexcept {
    return static_cast<wchar_t>(std::towlower(c));
}

bool equalIgnoringCase(std::wstring_view a, std::wstring_view b) noexcept {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](wchar_t x, wchar_t y) { return lower(x) == lower(y); });
}

bool endsWithIgnoringCase(std::wstring_view text, std::wstring_view suffix) noexcept {
    return text.size() >= suffix.size() &&
           equalIgnoringCase(text.substr(text.size() - suffix.size()), suffix);
}

std::wstring_view trimmed(std::wstring_view text) noexcept {
    auto const isSpace = [](wchar_t c) { return std::iswspace(c) != 0; };
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

int hexDigit(wchar_t c) noexcept {
    wchar_t const folded = lower(c);
    if (folded >= L'0' && folded <= L'9') {
        return folded - L'0';
    }
    if (folded >= L'a' && folded <= L'f') {
        return folded - L'a' + 10;
    }
    return -1;
}

}  // namespace

bool isKnownVendor(std::uint16_t vendorId) noexcept {
    return std::find(kKnownVendors.begin(), kKnownVendors.end(), vendorId) != kKnownVendors.end();
}

std::optional<std::uint16_t> vendorIdFromPath(std::wstring_view path) noexcept {
    constexpr std::wstring_view kMarker = L"vid_";
    constexpr std::size_t kDigits = 4;

    for (std::size_t at = 0; at + kMarker.size() + kDigits <= path.size(); ++at) {
        if (!equalIgnoringCase(path.substr(at, kMarker.size()), kMarker)) {
            continue;
        }
        unsigned value = 0;
        std::size_t digit = 0;
        for (; digit < kDigits; ++digit) {
            int const nibble = hexDigit(path[at + kMarker.size() + digit]);
            if (nibble < 0) {
                break;
            }
            value = (value << 4) | static_cast<unsigned>(nibble);
        }
        if (digit == kDigits) {
            return static_cast<std::uint16_t>(value);
        }
    }
    return std::nullopt;
}

Report batteryRequest() noexcept {
    return requestFor(kCommandBattery);
}

Report modelRequest() noexcept {
    return requestFor(kCommandModel);
}

bool isBatteryReply(std::span<std::uint8_t const> report) noexcept {
    return isReplyTo(report, kCommandBattery);
}

bool isModelReply(std::span<std::uint8_t const> report) noexcept {
    return isReplyTo(report, kCommandModel);
}

std::optional<Reading> parseBatteryReply(std::span<std::uint8_t const> report) noexcept {
    if (!isSoundReplyTo(report, kCommandBattery)) {
        return std::nullopt;
    }

    Reading reading;
    reading.percent = report[kPercentOffset];
    reading.onCable = report[kCableOffset] != 0;
    reading.millivolts = (report[kMillivoltsOffset] << 8) | report[kMillivoltsOffset + 1];

    if (reading.percent > 100) {
        return std::nullopt;
    }
    // A battery at 0 % still has a voltage; a reply with neither is the receiver saying it has
    // nothing to report.
    if (reading.percent == 0 && reading.millivolts == 0) {
        return std::nullopt;
    }
    return reading;
}

ChargeState chargeStateOf(Reading const& reading) noexcept {
    if (!reading.onCable) {
        return ChargeState::Discharging;
    }
    return reading.percent >= 100 ? ChargeState::Full : ChargeState::Charging;
}

std::optional<ModelId> parseModelReply(std::span<std::uint8_t const> report) noexcept {
    if (!isSoundReplyTo(report, kCommandModel) || report[kLengthOffset] < 2) {
        return std::nullopt;
    }
    ModelId const id{report[kDataOffset], report[kDataOffset + 1]};
    // Zeros are what a receiver with no mouse behind it answers, as it does to the battery query.
    if (id.customer == 0 && id.model == 0) {
        return std::nullopt;
    }
    return id;
}

Identity identify(std::uint16_t vendorId,
                  std::uint16_t productId,
                  std::wstring_view product,
                  std::optional<ModelId> model) {
    Identity identity;
    for (KnownProductId const& known : kKnownProductIds) {
        if (known.vendorId == vendorId && known.productId == productId) {
            identity.name = known.name;
            identity.viaReceiver = !known.cable;
            identity.known = true;
            break;
        }
    }
    if (model) {
        for (KnownModelId const& known : kKnownModelIds) {
            if (known.id == *model) {
                identity.name = known.name;
                identity.known = true;
                return identity;
            }
        }
    }
    if (identity.known) {
        return identity;
    }

    std::wstring_view const name = trimmed(product);
    for (std::wstring_view const brand : kBrands) {
        bool const wholeWord = name.size() == brand.size() ||
                               (name.size() > brand.size() && std::iswspace(name[brand.size()]));
        if (wholeWord && equalIgnoringCase(name.substr(0, brand.size()), brand)) {
            identity.name = std::wstring{brand} + L" wireless mouse";
            return identity;
        }
    }
    identity.name = nameFromProduct(product);
    if (identity.name.empty()) {
        identity.name = L"Wireless mouse";
    }
    return identity;
}

std::wstring nameFromProduct(std::wstring_view product) {
    // Longest first, so that "2.4G Wireless Receiver" is not cut down to "... 2.4G Wireless".
    constexpr std::array<std::wstring_view, 6> kReceiverSuffixes = {
        L" 2.4G Wireless Receiver", L" 2.4GHz Receiver", L" 2.4G Receiver",
        L" Wireless Receiver",      L" Receiver",        L" Dongle",
    };

    std::wstring_view const name = trimmed(product);
    // Matched against the name with a space in front, so that a product string which is nothing
    // but a suffix is recognised as one whole suffix rather than cut down to part of it.
    std::wstring const padded = L" " + std::wstring{name};
    for (std::wstring_view const suffix : kReceiverSuffixes) {
        if (!endsWithIgnoringCase(padded, suffix)) {
            continue;
        }
        std::wstring_view const rest =
            trimmed(std::wstring_view{padded}.substr(0, padded.size() - suffix.size()));
        if (!rest.empty()) {
            return std::wstring{rest};
        }
        break;
    }
    return std::wstring{name};
}

std::wstring deviceIdFromPath(std::wstring_view interfacePath) {
    // The same interface can be handed out in either case, and one device must not become two
    // over that.
    std::wstring folded{interfacePath};
    std::transform(folded.begin(), folded.end(), folded.begin(), lower);
    return std::format(L"compx:{:016x}", digestOf(folded));
}

}  // namespace peek::compx
