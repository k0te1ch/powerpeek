// The devices page as arithmetic: which section a device lands in, in what order the sections
// come, how many tiles share a row at a given width, and where each column's edges fall at a
// given display scale.
//
// The two widths used most below are roughly what the window produces. At its minimum size of
// 560 DIPs the navigation rail is collapsed and the page column is about 424 DIPs wide; at the
// default 960 it is expanded and the column is about 688. Those are the cases a user sees most,
// and they fall either side of the line between two tiles a row and three.

#include "TestSupport.h"

#include "ui/DeviceGrid.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace {

using peek::ChargeState;
using peek::DeviceInfo;
using peek::DeviceKind;
using peek::PowerSource;
using peek::test::makeController;
using peek::ui::groupByKind;
using peek::ui::kindGlyph;
using peek::ui::kTileGap;
using peek::ui::kTileMinWidth;
using peek::ui::TileAlert;
using peek::ui::tileAlert;
using peek::ui::TileColumn;
using peek::ui::tileColumns;

DeviceInfo device(std::wstring id, DeviceKind kind) {
    DeviceInfo info = makeController(std::move(id), 50);
    info.kind = kind;
    return info;
}

bool onPixelGrid(float dip, float scale) {
    float const pixels = dip * scale;
    return std::fabs(pixels - std::round(pixels)) < 1e-3f;
}

}  // namespace

TEST_CASE("deviceGrid: devices are grouped by kind in the fixed section order") {
    std::vector<DeviceInfo> const devices{
        device(L"mouse", DeviceKind::Mouse),     device(L"pad-1", DeviceKind::Gamepad),
        device(L"other", DeviceKind::Other),     device(L"headset", DeviceKind::Headset),
        device(L"pad-2", DeviceKind::Gamepad),   device(L"pen", DeviceKind::Pen),
        device(L"keyboard", DeviceKind::Keyboard),
    };

    auto const groups = groupByKind(devices);

    REQUIRE(groups.size() == 6);
    CHECK(groups[0].kind == DeviceKind::Gamepad);
    CHECK(groups[1].kind == DeviceKind::Headset);
    CHECK(groups[2].kind == DeviceKind::Mouse);
    CHECK(groups[3].kind == DeviceKind::Keyboard);
    CHECK(groups[4].kind == DeviceKind::Pen);
    CHECK(groups[5].kind == DeviceKind::Other);

    // Within a section the devices keep the order the monitor listed them in, which is what
    // stops two pads from trading places between polls.
    REQUIRE(groups[0].members.size() == 2);
    CHECK(groups[0].members[0] == 1);
    CHECK(groups[0].members[1] == 4);
}

TEST_CASE("deviceGrid: a kind with no device has no section") {
    std::vector<DeviceInfo> const devices{
        device(L"headset", DeviceKind::Headset),
        device(L"pad", DeviceKind::Gamepad),
    };

    auto const groups = groupByKind(devices);

    REQUIRE(groups.size() == 2);
    CHECK(groups[0].kind == DeviceKind::Gamepad);
    CHECK(groups[1].kind == DeviceKind::Headset);
    CHECK(groupByKind({}).empty());
}

TEST_CASE("deviceGrid: the section order does not depend on arrival order") {
    std::vector<DeviceInfo> const one{device(L"a", DeviceKind::Keyboard),
                                      device(L"b", DeviceKind::Gamepad)};
    std::vector<DeviceInfo> const two{device(L"b", DeviceKind::Gamepad),
                                      device(L"a", DeviceKind::Keyboard)};

    auto const first = groupByKind(one);
    auto const second = groupByKind(two);

    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].kind == second[i].kind);
    }
}

TEST_CASE("deviceGrid: every device lands in exactly one section") {
    std::vector<DeviceInfo> devices;
    for (DeviceKind const kind : peek::ui::kKindOrder) {
        devices.push_back(device(L"x", kind));
        devices.push_back(device(L"y", kind));
    }

    std::multiset<std::size_t> seen;
    for (auto const& group : groupByKind(devices)) {
        for (std::size_t const index : group.members) {
            CHECK(devices[index].kind == group.kind);
            seen.insert(index);
        }
    }
    CHECK(seen.size() == devices.size());
    CHECK(std::set<std::size_t>(seen.begin(), seen.end()).size() == devices.size());
}

TEST_CASE("deviceGrid: the minimum window gives two tiles a row, the default one three") {
    CHECK(tileColumns(424.0f, 1.0f).size() == 2);
    CHECK(tileColumns(688.0f, 1.0f).size() == 3);
}

TEST_CASE("deviceGrid: a column is dropped exactly when a tile would get too narrow") {
    float const twoFit = kTileMinWidth * 2.0f + kTileGap;
    float const threeFit = kTileMinWidth * 3.0f + kTileGap * 2.0f;

    CHECK(tileColumns(twoFit, 1.0f).size() == 2);
    CHECK(tileColumns(twoFit - 1.0f, 1.0f).size() == 1);
    CHECK(tileColumns(threeFit, 1.0f).size() == 3);
    CHECK(tileColumns(threeFit - 1.0f, 1.0f).size() == 2);
}

TEST_CASE("deviceGrid: a very wide page still stops at three columns") {
    CHECK(tileColumns(2400.0f, 1.0f).size() == 3);
}

TEST_CASE("deviceGrid: a page narrower than one tile still gets one column") {
    auto const columns = tileColumns(120.0f, 1.0f);
    REQUIRE(columns.size() == 1);
    CHECK(columns[0].left == 0.0f);
    CHECK(columns[0].right == doctest::Approx(120.0f));

    auto const none = tileColumns(0.0f, 1.0f);
    REQUIRE(none.size() == 1);
    CHECK(none[0].right == 0.0f);
}

TEST_CASE("deviceGrid: at 100 % the columns fill the width with equal gaps") {
    auto const columns = tileColumns(688.0f, 1.0f);
    REQUIRE(columns.size() == 3);

    CHECK(columns.front().left == 0.0f);
    CHECK(columns.back().right == 688.0f);
    CHECK(columns[1].left - columns[0].right == kTileGap);
    CHECK(columns[2].left - columns[1].right == kTileGap);
    // 688 - 24 = 664 does not split three ways; the spare pixel goes to the first tile.
    CHECK(columns[0].right - columns[0].left == 222.0f);
    CHECK(columns[1].right - columns[1].left == 221.0f);
    CHECK(columns[2].right - columns[2].left == 221.0f);
}

TEST_CASE("deviceGrid: at fractional scales every edge sits on a physical pixel") {
    for (float const scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f}) {
        for (float const width : {424.0f, 555.5f, 688.0f, 1001.3f}) {
            CAPTURE(scale);
            CAPTURE(width);
            auto const columns = tileColumns(width, scale);
            float previousRight = -1.0f;
            float gap = -1.0f;
            float narrowest = 1e9f;
            float widest = 0.0f;
            for (TileColumn const& column : columns) {
                CHECK(onPixelGrid(column.left, scale));
                CHECK(onPixelGrid(column.right, scale));
                CHECK(column.right > column.left);
                narrowest = std::min(narrowest, column.right - column.left);
                widest = std::max(widest, column.right - column.left);
                if (previousRight >= 0.0f) {
                    float const thisGap = column.left - previousRight;
                    if (gap >= 0.0f) {
                        CHECK(thisGap == doctest::Approx(gap));
                    }
                    gap = thisGap;
                }
                previousRight = column.right;
            }
            // Never past the edge it was given, and never more than a pixel short of it.
            CHECK(columns.back().right <= width + 1e-3f);
            CHECK(width - columns.back().right < 1.0f / scale + 1e-3f);
            // Widths differ by one physical pixel at most.
            CHECK((widest - narrowest) * scale <= 1.0f + 1e-3f);
        }
    }
}

TEST_CASE("deviceGrid: the column count is decided in DIPs, not pixels") {
    // The same page at a higher scale has more pixels but not more room: the tile count has to
    // match what 96 DPI gives, or a 150 % display would get tiles two thirds of the size.
    for (float const scale : {1.0f, 1.5f, 2.0f}) {
        CHECK(tileColumns(424.0f, scale).size() == 2);
        CHECK(tileColumns(688.0f, scale).size() == 3);
    }
}

TEST_CASE("deviceGrid: a scale of zero is treated as 100 %") {
    auto const broken = tileColumns(688.0f, 0.0f);
    auto const normal = tileColumns(688.0f, 1.0f);
    REQUIRE(broken.size() == normal.size());
    for (std::size_t i = 0; i < broken.size(); ++i) {
        CHECK(broken[i].left == normal[i].left);
        CHECK(broken[i].right == normal[i].right);
    }
}

TEST_CASE("deviceGrid: only a level on its way down raises the tile accent") {
    DeviceInfo pad = makeController(L"pad", 15, ChargeState::Discharging);
    CHECK(tileAlert(pad, 20, 10) == TileAlert::Low);

    pad.percent = 10;
    CHECK(tileAlert(pad, 20, 10) == TileAlert::Critical);

    pad.percent = 21;
    CHECK(tileAlert(pad, 20, 10) == TileAlert::None);

    pad.percent = 5;
    pad.charge = ChargeState::Charging;
    CHECK(tileAlert(pad, 20, 10) == TileAlert::None);

    pad.charge = ChargeState::Full;
    CHECK(tileAlert(pad, 20, 10) == TileAlert::None);
}

TEST_CASE("deviceGrid: a level of unknown direction still shows as low") {
    // The device tree reports a bare percentage. The tile says what the ring already says in
    // colour; it is the notifications that stay quiet about such a device.
    DeviceInfo headset = makeController(L"headset", 8, ChargeState::Unknown);
    headset.kind = DeviceKind::Headset;
    CHECK(tileAlert(headset, 20, 10) == TileAlert::Critical);
}

TEST_CASE("deviceGrid: a device with no level never raises the tile accent") {
    DeviceInfo wired = makeController(L"wired", -1, ChargeState::Unknown);
    wired.source = PowerSource::Wired;
    CHECK(tileAlert(wired, 20, 10) == TileAlert::None);

    DeviceInfo unread = makeController(L"unread", -1, ChargeState::Discharging);
    CHECK(tileAlert(unread, 20, 10) == TileAlert::None);
}

TEST_CASE("deviceGrid: every kind has a glyph of its own") {
    std::set<std::wstring> glyphs;
    for (DeviceKind const kind : peek::ui::kKindOrder) {
        auto const glyph = kindGlyph(kind);
        CHECK(glyph.size() == 1);
        glyphs.emplace(glyph);
    }
    CHECK(glyphs.size() == peek::ui::kKindOrder.size());
}
