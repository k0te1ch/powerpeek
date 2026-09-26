// The CSV export: what a spreadsheet makes of the file, byte for byte.
//
// The file leaves the application for Excel, LibreOffice or a script, none of which will tell
// the user that a comma in a device name shifted every column after it. Those are the cases
// worth pinning: quoting, the byte order mark, the timestamp with its offset.

#include "TestSupport.h"

#include "battery/HistoryCsv.h"

#include <chrono>
#include <string>
#include <vector>

namespace {

using peek::ChargeState;
using peek::CsvExport;
using peek::HistorySample;
using peek::csvField;
using peek::historyToCsv;
using peek::isoLocalTimestamp;

using Clock = std::chrono::system_clock;
using std::chrono::hours;
using std::chrono::minutes;

// 2026-09-26 11:03:07 UTC.
Clock::time_point fixedInstant() {
    using namespace std::chrono;
    return sys_days{year{2026} / September / 26} + hours{11} + minutes{3} + seconds{7};
}

HistorySample sample(std::wstring id, int percent, ChargeState charge, Clock::time_point when) {
    HistorySample result;
    result.when = when;
    result.controllerId = std::move(id);
    result.percent = static_cast<std::uint8_t>(percent);
    result.charge = charge;
    return result;
}

CsvExport inZone(minutes offset) {
    CsvExport options;
    options.deviceName = [](std::wstring const& id) { return id; };
    options.utcOffset = [offset](Clock::time_point) { return offset; };
    return options;
}

}  // namespace

TEST_CASE("csvField leaves a plain value alone") {
    CHECK(csvField(L"Xbox Wireless Controller") == "Xbox Wireless Controller");
    CHECK(csvField(L"") == "");
}

TEST_CASE("csvField quotes a value a reader would otherwise split") {
    CHECK(csvField(L"Pad, left") == "\"Pad, left\"");
    CHECK(csvField(L"The \"good\" pad") == "\"The \"\"good\"\" pad\"");
    CHECK(csvField(L"two\nlines") == "\"two\nlines\"");
    CHECK(csvField(L"carriage\rreturn") == "\"carriage\rreturn\"");
}

TEST_CASE("csvField writes UTF-8") {
    CHECK(csvField(L"Геймпад") ==
          "\xD0\x93\xD0\xB5\xD0\xB9\xD0\xBC\xD0\xBF\xD0\xB0\xD0\xB4");
}

TEST_CASE("isoLocalTimestamp shifts to local time and names the offset") {
    CHECK(isoLocalTimestamp(fixedInstant(), minutes{0}) == "2026-09-26T11:03:07+00:00");
    CHECK(isoLocalTimestamp(fixedInstant(), hours{3}) == "2026-09-26T14:03:07+03:00");
    CHECK(isoLocalTimestamp(fixedInstant(), minutes{330}) == "2026-09-26T16:33:07+05:30");
    CHECK(isoLocalTimestamp(fixedInstant(), -minutes{570}) == "2026-09-26T01:33:07-09:30");
    CHECK(isoLocalTimestamp(fixedInstant(), -hours{12}) == "2026-09-25T23:03:07-12:00");
}

TEST_CASE("isoLocalTimestamp drops the fraction of a second") {
    auto const when = fixedInstant() + std::chrono::milliseconds{999};
    CHECK(isoLocalTimestamp(when, minutes{0}) == "2026-09-26T11:03:07+00:00");
}

TEST_CASE("historyToCsv writes a byte order mark and a header even with no samples") {
    CHECK(historyToCsv({}, inZone(minutes{0})) ==
          "\xEF\xBB\xBF"
          "device,timestamp,level_percent,charge_state\r\n");
}

TEST_CASE("historyToCsv writes one row per sample") {
    std::vector<HistorySample> const samples{
        sample(L"pad-1", 78, ChargeState::Discharging, fixedInstant()),
        sample(L"pad-2", 100, ChargeState::Full, fixedInstant() + hours{1}),
        sample(L"pad-1", 5, ChargeState::Charging, fixedInstant() + hours{2}),
        sample(L"pad-3", 0, ChargeState::Unknown, fixedInstant() + hours{3}),
    };

    CHECK(historyToCsv(samples, inZone(hours{3})) ==
          "\xEF\xBB\xBF"
          "device,timestamp,level_percent,charge_state\r\n"
          "pad-1,2026-09-26T14:03:07+03:00,78,discharging\r\n"
          "pad-2,2026-09-26T15:03:07+03:00,100,full\r\n"
          "pad-1,2026-09-26T16:03:07+03:00,5,charging\r\n"
          "pad-3,2026-09-26T17:03:07+03:00,0,unknown\r\n");
}

TEST_CASE("historyToCsv names devices through the lookup and escapes the result") {
    CsvExport options = inZone(minutes{0});
    options.deviceName = [](std::wstring const& id) {
        return id == L"pad-1" ? std::wstring{L"Pad \"A\", blue"} : id;
    };

    std::vector<HistorySample> const samples{
        sample(L"pad-1", 50, ChargeState::Discharging, fixedInstant()),
        sample(L"pad-9", 40, ChargeState::Discharging, fixedInstant()),
    };

    CHECK(historyToCsv(samples, options) ==
          "\xEF\xBB\xBF"
          "device,timestamp,level_percent,charge_state\r\n"
          "\"Pad \"\"A\"\", blue\",2026-09-26T11:03:07+00:00,50,discharging\r\n"
          "pad-9,2026-09-26T11:03:07+00:00,40,discharging\r\n");
}

TEST_CASE("historyToCsv asks for the offset of each sample's own instant") {
    // A history crossing a daylight-saving change carries two offsets.
    CsvExport options = inZone(minutes{0});
    Clock::time_point const change = fixedInstant() + hours{1};
    options.utcOffset = [change](Clock::time_point when) {
        return when < change ? hours{2} : hours{1};
    };

    std::vector<HistorySample> const samples{
        sample(L"pad-1", 50, ChargeState::Discharging, fixedInstant()),
        sample(L"pad-1", 40, ChargeState::Discharging, fixedInstant() + hours{2}),
    };

    CHECK(historyToCsv(samples, options) ==
          "\xEF\xBB\xBF"
          "device,timestamp,level_percent,charge_state\r\n"
          "pad-1,2026-09-26T13:03:07+02:00,50,discharging\r\n"
          "pad-1,2026-09-26T14:03:07+01:00,40,discharging\r\n");
}
