#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "battery/ControllerInfo.h"

namespace peek {

struct HistorySample {
    std::chrono::system_clock::time_point when{};
    std::wstring controllerId;
    std::uint8_t percent = 0;
    ChargeState charge = ChargeState::Unknown;
};

// An append-only log of battery readings, one line of JSON per sample.
//
// Samples are only appended when the level or the charge state actually changed, so a
// controller sitting at 78% for an hour costs one line, not a hundred. That also makes
// the discharge-rate estimate meaningful: consecutive samples are real transitions.
class BatteryHistory {
public:
    explicit BatteryHistory(std::filesystem::path file);
    ~BatteryHistory();

    void setEnabled(bool enabled);
    void setRetention(std::chrono::days retention);

    // Records the reading if it differs from the last one for that controller.
    void record(ControllerInfo const& controller);

    // Samples for one controller, oldest first, limited to the retention window.
    std::vector<HistorySample> samplesFor(std::wstring const& controllerId) const;

    // Mean drain in percent per hour over the most recent discharge run, or nothing when
    // there is not enough data (fewer than two samples, or the pad has been charging).
    std::optional<double> drainPercentPerHour(std::wstring const& controllerId) const;

    // Projected time until the battery reaches zero at the current drain rate.
    std::optional<std::chrono::minutes> estimatedRemaining(ControllerInfo const& controller) const;

    // Drops samples older than the retention window and rewrites the file. Called at
    // startup rather than on every append.
    void prune();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace peek
