// When a finished poll reaches the UI thread.
//
// The monitor used to post only when a rendered field moved, and a manual refresh went
// through the same filter -- so pressing Refresh with every level unchanged did nothing
// visible at all, not even the "updated" line. These cases pin both halves: a regular poll
// stays quiet over an unchanged list, and a requested one speaks regardless.

#include "TestSupport.h"

#include "battery/SnapshotPolicy.h"

#include <chrono>
#include <vector>

namespace {

using peek::ChargeState;
using peek::DeviceInfo;
using peek::sameList;
using peek::shouldPublish;
using peek::test::makeController;

}  // namespace

TEST_CASE("snapshotPolicy: an unchanged list is not published by a regular poll") {
    std::vector<DeviceInfo> const before{makeController(L"pad", 60)};
    std::vector<DeviceInfo> after = before;
    after[0].lastUpdate += std::chrono::minutes{5};

    CHECK(sameList(before, after));
    CHECK_FALSE(shouldPublish(before, after, false));
}

TEST_CASE("snapshotPolicy: a requested poll is published even when nothing moved") {
    std::vector<DeviceInfo> const before{makeController(L"pad", 60)};
    std::vector<DeviceInfo> after = before;
    after[0].lastUpdate += std::chrono::minutes{5};

    CHECK(shouldPublish(before, after, true));
}

TEST_CASE("snapshotPolicy: a requested poll over no devices is still published") {
    CHECK(shouldPublish({}, {}, true));
    CHECK_FALSE(shouldPublish({}, {}, false));
}

TEST_CASE("snapshotPolicy: a moved level, charge state or name is published by any poll") {
    std::vector<DeviceInfo> const before{makeController(L"pad", 60)};

    auto level = before;
    level[0].percent = 59;
    CHECK(shouldPublish(before, level, false));

    auto charge = before;
    charge[0].charge = ChargeState::Charging;
    CHECK(shouldPublish(before, charge, false));

    auto name = before;
    name[0].name = L"Couch pad";
    CHECK(shouldPublish(before, name, false));
}

TEST_CASE("snapshotPolicy: a device arriving or leaving is published by any poll") {
    std::vector<DeviceInfo> const one{makeController(L"pad", 60)};
    std::vector<DeviceInfo> const two{makeController(L"pad", 60), makeController(L"headset", 40)};

    CHECK(shouldPublish(one, two, false));
    CHECK(shouldPublish(two, one, false));
    CHECK(shouldPublish(one, {}, false));
}
