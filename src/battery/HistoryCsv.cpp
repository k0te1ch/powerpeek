#include "battery/HistoryCsv.h"

#include <cstdlib>
#include <format>

#include "core/Win.h"

namespace peek {
namespace {

constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";
constexpr std::string_view kLineEnd = "\r\n";
constexpr std::string_view kFormulaLeads = "=+-@\t\r";

// Fixed English words rather than the localised status text: a column that changes language
// with the UI setting cannot be filtered on.
std::string_view chargeWord(ChargeState state) {
    switch (state) {
        case ChargeState::Discharging:
            return "discharging";
        case ChargeState::Charging:
            return "charging";
        case ChargeState::Full:
            return "full";
        case ChargeState::Unknown:
            break;
    }
    return "unknown";
}

}  // namespace

std::string csvField(std::wstring_view value) {
    std::string utf8 = narrow(value);
    // A spreadsheet runs a cell that opens with one of these as a formula, and a device name is
    // text the user or the device chose. A leading single quote makes Excel and LibreOffice
    // show it as text, which is what the OWASP guidance on CSV injection prescribes.
    if (!utf8.empty() && kFormulaLeads.find(utf8.front()) != std::string_view::npos) {
        utf8.insert(utf8.begin(), '\'');
    }
    if (utf8.find_first_of(",\"\r\n") == std::string::npos) {
        return utf8;
    }

    std::string quoted = "\"";
    for (char const c : utf8) {
        if (c == '"') {
            quoted += '"';
        }
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

std::string isoLocalTimestamp(std::chrono::system_clock::time_point when,
                              std::chrono::minutes utcOffset) {
    auto const local = std::chrono::floor<std::chrono::seconds>(when) + utcOffset;
    auto const magnitude = std::abs(utcOffset.count());
    return std::format("{:%Y-%m-%dT%H:%M:%S}{}{:02}:{:02}", local,
                       utcOffset.count() < 0 ? '-' : '+', magnitude / 60, magnitude % 60);
}

std::string historyToCsv(std::vector<HistorySample> const& samples, CsvExport const& options) {
    std::string out{kByteOrderMark};
    out += "device,timestamp,level_percent,charge_state";
    out += kLineEnd;

    for (HistorySample const& sample : samples) {
        out += csvField(options.deviceName(sample.controllerId));
        out += ',';
        out += isoLocalTimestamp(sample.when, options.utcOffset(sample.when));
        out += ',';
        out += std::to_string(sample.percent);
        out += ',';
        out += chargeWord(sample.charge);
        out += kLineEnd;
    }
    return out;
}

}  // namespace peek
