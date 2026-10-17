#pragma once

#include <chrono>

namespace peek::ui {

// When to try the notification area again after NIM_ADD has failed `failedAttempts` times in
// a row. Kept apart from TrayIcon so that it links into PowerPeekCore and the schedule can be
// tested without a shell.
//
// Started from Run at logon, the process regularly beats Explorer's notification area to the
// punch. Most of the time the shell turns up within seconds, so the first minute is tried
// often; after that the attempts slow down but never stop. A tray application that is running
// with no icon and no window cannot be reached at all, so giving up is never the better outcome.
inline constexpr int kTrayFastAttempts = 30;
inline constexpr std::chrono::milliseconds kTrayFastRetry{2000};
inline constexpr std::chrono::milliseconds kTraySlowRetry{30000};

std::chrono::milliseconds trayRetryDelay(int failedAttempts) noexcept;

// Whether a failure is worth a line in the log. Every failure of the first minute is, as is
// the moment the schedule slows down; after that one line in ten, so that a shell that never
// recovers costs the log one line every five minutes rather than one every thirty seconds.
bool shouldLogTrayFailure(int failedAttempts) noexcept;

}  // namespace peek::ui
