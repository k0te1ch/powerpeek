#include "TestSupport.h"

#include <atomic>
#include <format>

#include "core/Win.h"

namespace doctest {

String StringMaker<std::wstring>::convert(std::wstring const& value) {
    return String(peek::narrow(value).c_str());
}

String StringMaker<std::wstring_view>::convert(std::wstring_view value) {
    return String(peek::narrow(value).c_str());
}

}  // namespace doctest

namespace peek::test {
namespace {

// The process id keeps two suites running at once out of each other's way -- CTest runs
// cases in parallel by default -- and the counter does the same for two TempDirs inside one
// process. Neither is a guess: the loop below only settles on a name nothing else holds.
std::filesystem::path makeUniqueDirectory() {
    static std::atomic<unsigned> counter{0};

    std::filesystem::path const root = std::filesystem::temp_directory_path();
    unsigned const pid = ::GetCurrentProcessId();

    for (;;) {
        std::filesystem::path candidate =
            root / std::format(L"powerpeek-tests-{}-{}", pid, counter.fetch_add(1));
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec)) {
            return candidate;
        }
        if (ec) {
            throw std::filesystem::filesystem_error("could not create a temporary directory",
                                                    candidate, ec);
        }
    }
}

}  // namespace

TempDir::TempDir() : m_path(makeUniqueDirectory()) {}

TempDir::~TempDir() {
    // A destructor that throws during stack unwinding from a failed assertion would replace
    // the useful failure with a terminate, so the error code overload is the only option.
    std::error_code ec;
    std::filesystem::remove_all(m_path, ec);
}

DeviceInfo makeController(std::wstring id, int percent, ChargeState charge) {
    DeviceInfo controller;
    controller.name = id;
    controller.id = std::move(id);
    controller.percent = percent;
    controller.charge = charge;
    controller.source = PowerSource::Battery;
    controller.fidelity = Fidelity::Exact;
    controller.kind = DeviceKind::Gamepad;
    controller.isXboxController = true;
    controller.firstSeen = testEpoch();
    controller.lastUpdate = testEpoch();
    return controller;
}

std::chrono::system_clock::time_point testEpoch() {
    using namespace std::chrono;
    return time_point_cast<system_clock::duration>(sys_days{2026y / January / 1}) + 12h;
}

}  // namespace peek::test
