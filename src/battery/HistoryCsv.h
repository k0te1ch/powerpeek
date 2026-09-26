#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "battery/BatteryHistory.h"

namespace peek {

// What the export needs from outside the history itself. Both are handed in rather than looked
// up, so the output is a function of its arguments alone and a test can pin every byte of it.
struct CsvExport {
    // The display name for a stored controller id.
    std::function<std::wstring(std::wstring const&)> deviceName;

    // Local time minus UTC at that instant. Asked per sample because a history spanning a
    // daylight-saving change has two offsets in it.
    std::function<std::chrono::minutes(std::chrono::system_clock::time_point)> utcOffset;
};

// One field as UTF-8, quoted when it holds a comma, a quote or a line break, quotes doubled.
// A field that opens with = + - @, a tab or a carriage return gets a leading single quote, so a
// spreadsheet shows it as text instead of evaluating it as a formula.
std::string csvField(std::wstring_view value);

// "2026-09-26T14:03:00+03:00".
std::string isoLocalTimestamp(std::chrono::system_clock::time_point when,
                              std::chrono::minutes utcOffset);

// The battery history as a spreadsheet: device, local ISO-8601 moment, level, charge state.
//
// UTF-8 behind a byte order mark -- without one Excel decodes the bytes in the ANSI code page
// and every Cyrillic device name turns to mojibake -- with CRLF line endings as RFC 4180 has.
std::string historyToCsv(std::vector<HistorySample> const& samples, CsvExport const& options);

}  // namespace peek
