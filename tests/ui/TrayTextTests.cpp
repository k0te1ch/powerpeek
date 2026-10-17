// What the notification area says: the tooltip and the device lines of the context menu.
//
// The tooltip lands in a fixed WCHAR[128] that the shell truncates silently, so a line that
// overruns does not fail, it just disappears half-written. The rules here are the whole of
// what stops that: whole lines only, and an honest count of whatever was left out.

#include "TestSupport.h"

#include <string>
#include <vector>

#include "core/Strings.h"
#include "ui/TrayText.h"

namespace {

using peek::ChargeState;
using peek::DeviceInfo;
using peek::Fidelity;
using peek::LanguagePreference;
using peek::PowerSource;
using peek::test::makeController;
using peek::ui::buildTrayTooltip;
using peek::ui::describeDevice;
using peek::ui::kTrayTipCapacity;
using peek::ui::trayMenuLines;
using peek::ui::trayOrder;

// Every expectation below is written in English; the table is process-wide.
struct English {
    English() { peek::setLanguage(LanguagePreference::English); }
};

DeviceInfo named(std::wstring name, int percent,
                 ChargeState charge = ChargeState::Discharging) {
    DeviceInfo device = makeController(name, percent, charge);
    device.name = std::move(name);
    device.fidelity = Fidelity::Exact;
    return device;
}

DeviceInfo wired(std::wstring name) {
    DeviceInfo device = named(std::move(name), -1, ChargeState::Unknown);
    device.source = PowerSource::Wired;
    return device;
}

std::size_t lineCount(std::wstring const& tip) {
    std::size_t lines = 1;
    for (std::size_t at = tip.find(L"\r\n"); at != std::wstring::npos;
         at = tip.find(L"\r\n", at + 2)) {
        ++lines;
    }
    return lines;
}

}  // namespace

TEST_CASE("trayText: a device line carries its name, level and charging state") {
    English const english;
    CHECK(describeDevice(named(L"Pad", 45, ChargeState::Charging)) ==
          std::wstring(L"Pad \x2014 45%, Charging"));
}

TEST_CASE("trayText: a coarse reading says it is approximate") {
    English const english;
    DeviceInfo device = named(L"Pad", 75);
    device.fidelity = Fidelity::Coarse;
    CHECK(describeDevice(device) == std::wstring(L"Pad \x2014 75% (approximate)"));
}

TEST_CASE("trayText: a device with no level names its power source instead") {
    English const english;
    CHECK(describeDevice(wired(L"Cable")) == std::wstring(L"Cable \x2014 Wired, no battery"));
}

TEST_CASE("trayText: the lowest battery comes first and devices with no level come last") {
    std::vector<DeviceInfo> const devices = {wired(L"A"), named(L"B", 80), named(L"C", 20),
                                             named(L"D", 80)};
    auto const ordered = trayOrder(devices);
    REQUIRE(ordered.size() == 4);
    CHECK(ordered[0]->name == L"C");
    // Equal levels keep their arrival order so a steady list does not reshuffle.
    CHECK(ordered[1]->name == L"B");
    CHECK(ordered[2]->name == L"D");
    CHECK(ordered[3]->name == L"A");
}

TEST_CASE("trayText: with nothing connected both the tooltip and the menu say so") {
    English const english;
    CHECK(buildTrayTooltip({}) == std::wstring(L"PowerPeek\r\nNo devices with a battery"));
    auto const lines = trayMenuLines({});
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == std::wstring(L"No devices with a battery"));
}

TEST_CASE("trayText: the tooltip lists one device per line in tray order") {
    English const english;
    std::wstring const tip = buildTrayTooltip({named(L"High", 90), named(L"Low", 10)});
    CHECK(tip == std::wstring(L"PowerPeek\r\nLow \x2014 10%\r\nHigh \x2014 90%"));
}

TEST_CASE("trayText: devices that do not fit are counted rather than cut") {
    English const english;
    std::vector<DeviceInfo> devices;
    for (int i = 0; i < 12; ++i) {
        devices.push_back(named(L"Wireless Controller " + std::to_wstring(i), 10 + i));
    }
    std::wstring const tip = buildTrayTooltip(devices);

    CHECK(tip.size() <= kTrayTipCapacity);
    CHECK(tip.find(L"Wireless Controller 0 \x2014 10%") != std::wstring::npos);
    // Every device line is whole, and the ones missing add up to the notice.
    std::size_t const shown = lineCount(tip) - 2;
    std::wstring const notice = L"+" + std::to_wstring(devices.size() - shown) + L" more";
    CHECK(tip.ends_with(L"\r\n" + notice));
}

TEST_CASE("trayText: the notice always has room, even when the lines nearly fill the tip") {
    English const english;
    // Two lines that just fit together, plus a third: the second has to give way so the
    // notice for both can go in.
    std::wstring const longName(50, L'x');
    std::wstring const tip =
        buildTrayTooltip({named(longName, 1), named(longName, 2), named(longName, 3)});
    CHECK(tip.size() <= kTrayTipCapacity);
    CHECK(tip.ends_with(L"\r\n+2 more"));
}

TEST_CASE("trayText: a capacity too small for any device still ends in the count") {
    English const english;
    std::wstring const tip = buildTrayTooltip({named(L"Pad", 50), named(L"Pad 2", 60)}, 24);
    CHECK(tip == std::wstring(L"PowerPeek\r\n+2 more"));
}

TEST_CASE("trayText: menu lines double an ampersand so it is not taken as a mnemonic") {
    English const english;
    auto const lines = trayMenuLines({named(L"Tom & Jerry", 50)});
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == std::wstring(L"Tom && Jerry \x2014 50%"));
}

TEST_CASE("trayText: the menu lists every device, with no capacity to cut it") {
    std::vector<DeviceInfo> devices;
    for (int i = 0; i < 12; ++i) {
        devices.push_back(named(L"Pad " + std::to_wstring(i), 90 - i));
    }
    auto const lines = trayMenuLines(devices);
    REQUIRE(lines.size() == devices.size());
    CHECK(lines.front().starts_with(L"Pad 11 "));
}

TEST_CASE("trayText: the count is translated with the rest of the tooltip") {
    peek::setLanguage(LanguagePreference::Russian);
    std::wstring const tip = buildTrayTooltip({named(L"Pad", 50), named(L"Pad 2", 60)}, 24);
    peek::setLanguage(LanguagePreference::English);
    CHECK(tip.ends_with(L"\r\n+\x0435\x0449\x0451 2"));
}
