// Which name a device is shown under. Every surface -- the card, the history, the notification
// and the tray -- reads the one field this sets, so a mistake here is a mistake everywhere.

#include "TestSupport.h"

#include "battery/DeviceNames.h"

#include <string>
#include <vector>

namespace {

using peek::applyDeviceName;
using peek::applyDeviceNames;
using peek::DeviceInfo;
using peek::DeviceNames;

DeviceInfo makeDevice(std::wstring id, std::wstring name) {
    DeviceInfo device;
    device.id = std::move(id);
    device.name = std::move(name);
    return device;
}

}  // namespace

TEST_CASE("device names: a device without a custom name keeps the reported one") {
    std::vector<DeviceInfo> devices{makeDevice(L"pad-1", L"Xbox Wireless Controller")};
    applyDeviceNames(devices, {});

    CHECK(devices[0].name == L"Xbox Wireless Controller");
    CHECK(devices[0].reportedName == L"Xbox Wireless Controller");
}

TEST_CASE("device names: a custom name replaces the reported one by id") {
    std::vector<DeviceInfo> devices{makeDevice(L"pad-1", L"Xbox Wireless Controller"),
                                    makeDevice(L"pad-2", L"Xbox Wireless Controller")};
    applyDeviceNames(devices, DeviceNames{{L"pad-2", L"Couch"}});

    CHECK(devices[0].name == L"Xbox Wireless Controller");
    CHECK(devices[1].name == L"Couch");
    CHECK(devices[1].reportedName == L"Xbox Wireless Controller");
}

TEST_CASE("device names: a name for an absent device changes nothing") {
    std::vector<DeviceInfo> devices{makeDevice(L"pad-1", L"Pad")};
    applyDeviceNames(devices, DeviceNames{{L"pad-9", L"Gone"}});
    CHECK(devices[0].name == L"Pad");
}

TEST_CASE("device names: applying again starts from the reported name") {
    std::vector<DeviceInfo> devices{makeDevice(L"pad-1", L"Pad")};
    applyDeviceNames(devices, DeviceNames{{L"pad-1", L"Desk"}});
    REQUIRE(devices[0].name == L"Desk");

    SUBCASE("a rename replaces the previous custom name") {
        applyDeviceNames(devices, DeviceNames{{L"pad-1", L"Couch"}});
        CHECK(devices[0].name == L"Couch");
        CHECK(devices[0].reportedName == L"Pad");
    }
    SUBCASE("clearing the name brings the reported one back") {
        applyDeviceNames(devices, {});
        CHECK(devices[0].name == L"Pad");
    }
}

TEST_CASE("device names: a reading kept from before a rename takes the new name") {
    // A charge reminder carries the reading of a pad that has since left the list.
    std::vector<DeviceInfo> devices{makeDevice(L"pad-1", L"Pad")};
    applyDeviceNames(devices, DeviceNames{{L"pad-1", L"Desk"}});
    DeviceInfo kept = devices[0];

    applyDeviceName(kept, DeviceNames{{L"pad-1", L"Couch"}});
    CHECK(kept.name == L"Couch");
    CHECK(kept.reportedName == L"Pad");

    applyDeviceName(kept, {});
    CHECK(kept.name == L"Pad");
}
