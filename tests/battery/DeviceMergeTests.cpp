// How many cards one poll turns into, and which reading is behind each of them.
//
// Everything downstream of the monitor treats a device id as an identity without ever checking
// that it is one: the event detector keeps a single state per id and would let a second entry
// overwrite the first's threshold baseline inside one update, the history log appends both
// readings to one series, and the devices page binds two cards to whichever entry it finds
// first. None of that fails loudly -- it shows up as a warning that never fires, or a drain
// rate computed across two devices -- and none of it is reachable from a test on a provider,
// which wants a controller, an MTA and an XInput DLL. It is reachable here.
//
// The ranking is the other half. It decides what the user sees when one device is described
// twice, and its rungs are in an order that looks wrong until you read the providers: a
// reading that carries a level beats one that does not, *before* an exact reading beats a
// coarse one, because the WinRT provider stamps Fidelity::Exact on a reading before it has
// read a battery at all. The cases below pin that order in both input orders, so a comparator
// that merely keeps whichever reading arrived first cannot pass them.

#include "TestSupport.h"

#include "battery/DeviceMerge.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

using peek::DeviceInfo;
using peek::DeviceKind;
using peek::Fidelity;
using peek::mergeReadings;
using peek::PowerSource;
using peek::test::makeController;

// A reading as a provider hands it over: an id, a level, and how well that level is known.
// percent < 0 is the model's "no level", which is what a pad with no battery report leaves
// behind.
DeviceInfo reading(std::wstring id, int percent, Fidelity fidelity) {
    DeviceInfo device = makeController(std::move(id), percent);
    device.fidelity = fidelity;
    return device;
}

std::vector<std::wstring> idsOf(std::vector<DeviceInfo> const& devices) {
    std::vector<std::wstring> ids;
    for (DeviceInfo const& device : devices) {
        ids.push_back(device.id);
    }
    return ids;
}

}  // namespace

TEST_CASE("mergeReadings: a poll where every device appears once comes back untouched") {
    // Every poll that happens today. The case that fails loudest if merging ever turns eager.
    std::vector<DeviceInfo> const poll{reading(L"pad-a", 80, Fidelity::Exact),
                                       reading(L"pad-b", 40, Fidelity::Coarse),
                                       reading(L"pad-c", 10, Fidelity::Exact)};

    std::vector<DeviceInfo> const merged = mergeReadings(poll);

    REQUIRE(merged.size() == 3);
    CHECK(idsOf(merged) == idsOf(poll));
    for (std::size_t i = 0; i < merged.size(); ++i) {
        CHECK(merged[i].percent == poll[i].percent);
        CHECK(merged[i].fidelity == poll[i].fidelity);
    }
}

TEST_CASE("mergeReadings: an empty poll stays empty") {
    // What the idle path hands it every thirty seconds.
    CHECK(mergeReadings({}).empty());
}

TEST_CASE("mergeReadings: two readings of one id become one entry") {
    std::vector<DeviceInfo> const merged = mergeReadings(
        {reading(L"pad-a", 80, Fidelity::Exact), reading(L"pad-a", 55, Fidelity::Exact)});

    REQUIRE(merged.size() == 1);
    CHECK(merged[0].id == L"pad-a");
}

TEST_CASE("mergeReadings: two devices alike in everything but their ids stay two entries") {
    // Two identical pads, which is every field a resemblance heuristic would reach for: same
    // vendor, same product, same name, same level, same charge state.
    DeviceInfo first = reading(L"pad-a", 80, Fidelity::Exact);
    first.name = L"Xbox Wireless Controller";
    first.vendorId = 0x045E;
    first.productId = 0x0B12;
    DeviceInfo second = first;
    second.id = L"pad-b";

    std::vector<DeviceInfo> const merged = mergeReadings({first, second});

    CHECK(merged.size() == 2);
}

TEST_CASE("mergeReadings: two readings that could not key themselves stay two entries") {
    // An unknown identity is not a shared one, and the event detector skips empty ids anyway:
    // collapsing them would lose a device to gain nothing.
    DeviceInfo first = reading(L"", 80, Fidelity::Exact);
    DeviceInfo second = reading(L"", 40, Fidelity::Coarse);

    CHECK(mergeReadings({first, second}).size() == 2);
}

TEST_CASE("mergeReadings: a coarse reading that carries a level beats an exact one that does not") {
    // The rung that is reachable from the shipped providers: WinRT stamps Exact before it
    // reads, and a pad with no battery report leaves the percent at -1 behind it. Asserted in
    // both input orders, because the whole point is that arrival order must not decide it.
    DeviceInfo const blank = reading(L"pad-a", -1, Fidelity::Exact);
    DeviceInfo const bucket = reading(L"pad-a", 40, Fidelity::Coarse);

    SUBCASE("the empty reading arrives first") {
        std::vector<DeviceInfo> const merged = mergeReadings({blank, bucket});
        REQUIRE(merged.size() == 1);
        CHECK(merged[0].percent == 40);
        CHECK(merged[0].fidelity == Fidelity::Coarse);
    }
    SUBCASE("the empty reading arrives second") {
        std::vector<DeviceInfo> const merged = mergeReadings({bucket, blank});
        REQUIRE(merged.size() == 1);
        CHECK(merged[0].percent == 40);
    }
}

TEST_CASE("mergeReadings: an exact reading beats a coarse one when both carry a level") {
    // The headline of the stage: a measured percentage displaces a four-bucket guess.
    DeviceInfo const exact = reading(L"pad-a", 83, Fidelity::Exact);
    DeviceInfo const coarse = reading(L"pad-a", 40, Fidelity::Coarse);

    SUBCASE("the coarse reading arrives first") {
        std::vector<DeviceInfo> const merged = mergeReadings({coarse, exact});
        REQUIRE(merged.size() == 1);
        CHECK(merged[0].percent == 83);
        CHECK(merged[0].fidelity == Fidelity::Exact);
    }
    SUBCASE("the exact reading arrives first") {
        std::vector<DeviceInfo> const merged = mergeReadings({exact, coarse});
        REQUIRE(merged.size() == 1);
        CHECK(merged[0].percent == 83);
        CHECK(merged[0].fidelity == Fidelity::Exact);
    }
}

TEST_CASE("mergeReadings: the surviving record takes nothing from the reading it displaced") {
    // A card describes one reading a provider actually made, right down to the fields where
    // the loser's value looks like the better one. Assembling a record out of the best of both
    // would show a name, a slot and a vendor that no single source ever reported together.
    DeviceInfo loser = reading(L"pad-a", -1, Fidelity::Exact);
    loser.name = L"Xbox Wireless Controller";
    loser.vendorId = 0x045E;
    loser.productId = 0x0B12;
    loser.kind = DeviceKind::Gamepad;

    DeviceInfo winner = reading(L"pad-a", 40, Fidelity::Coarse);
    winner.name = L"Xbox Controller 1";
    winner.vendorId = 0;
    winner.productId = 0;
    winner.xinputSlot = 0;

    std::vector<DeviceInfo> const merged = mergeReadings({loser, winner});

    REQUIRE(merged.size() == 1);
    CHECK(merged[0].name == L"Xbox Controller 1");
    CHECK(merged[0].vendorId == 0);
    CHECK(merged[0].productId == 0);
    CHECK(merged[0].xinputSlot == 0);
}

TEST_CASE("mergeReadings: choosing between readings never invents a battery") {
    // Both readings describe a device running off a cable. The better of the two is still a
    // device with nothing to report, and a card that showed a level here would be showing one
    // no provider ever measured.
    DeviceInfo first = reading(L"pad-a", -1, Fidelity::Exact);
    first.source = PowerSource::Wired;
    DeviceInfo second = reading(L"pad-a", 100, Fidelity::Coarse);
    second.source = PowerSource::Wired;

    std::vector<DeviceInfo> const merged = mergeReadings({first, second});

    REQUIRE(merged.size() == 1);
    CHECK_FALSE(merged[0].hasBattery());
}

TEST_CASE("mergeReadings: between readings that rank the same, the first one collected wins") {
    // Source priority is something the caller states by the order it collects in, not
    // something this function decides -- which is how a provider added later declares its own
    // rank without a table naming every provider.
    DeviceInfo preferred = reading(L"pad-a", 80, Fidelity::Exact);
    preferred.name = L"from the source that spoke first";
    DeviceInfo other = reading(L"pad-a", 55, Fidelity::Exact);
    other.name = L"from the one that spoke after";

    std::vector<DeviceInfo> const merged = mergeReadings({preferred, other});

    REQUIRE(merged.size() == 1);
    CHECK(merged[0].name == L"from the source that spoke first");
    CHECK(merged[0].percent == 80);
}

TEST_CASE("mergeReadings: the survivor stays where the device first appeared") {
    // The snapshot comparison in the monitor and the one in the main window both pair records
    // element by element. A merge that put the winner in the winner's slot would reorder the
    // list whenever a rank flipped, and every card would repaint for a poll that changed
    // nothing.
    std::vector<DeviceInfo> const merged = mergeReadings({reading(L"pad-a", -1, Fidelity::Exact),
                                                          reading(L"pad-b", 40, Fidelity::Exact),
                                                          reading(L"pad-a", 70, Fidelity::Exact)});

    REQUIRE(merged.size() == 2);
    CHECK(merged[0].id == L"pad-a");
    CHECK(merged[0].percent == 70);
    CHECK(merged[1].id == L"pad-b");
}

TEST_CASE("mergeReadings: three readings of one device collapse to the best of the three") {
    // The rule is a fold over the whole poll, not a comparison of the first pair it meets, so
    // shuffling the two that lose changes nothing about the one that wins.
    DeviceInfo const blank = reading(L"pad-a", -1, Fidelity::Exact);
    DeviceInfo const bucket = reading(L"pad-a", 40, Fidelity::Coarse);
    DeviceInfo const measured = reading(L"pad-a", 83, Fidelity::Exact);

    SUBCASE("the best reading arrives last") {
        std::vector<DeviceInfo> const merged = mergeReadings({blank, bucket, measured});
        REQUIRE(merged.size() == 1);
        CHECK(merged[0].percent == 83);
    }
    SUBCASE("the best reading arrives first") {
        std::vector<DeviceInfo> const merged = mergeReadings({measured, bucket, blank});
        REQUIRE(merged.size() == 1);
        CHECK(merged[0].percent == 83);
    }
    SUBCASE("the two that lose arrive the other way round") {
        std::vector<DeviceInfo> const merged = mergeReadings({bucket, blank, measured});
        REQUIRE(merged.size() == 1);
        CHECK(merged[0].percent == 83);
    }
}
