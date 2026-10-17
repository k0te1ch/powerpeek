#include "ui/TrayRetry.h"

namespace peek::ui {
namespace {

constexpr int kSlowLogEvery = 10;

}  // namespace

std::chrono::milliseconds trayRetryDelay(int failedAttempts) noexcept {
    return failedAttempts < kTrayFastAttempts ? kTrayFastRetry : kTraySlowRetry;
}

bool shouldLogTrayFailure(int failedAttempts) noexcept {
    if (failedAttempts <= kTrayFastAttempts) {
        return true;
    }
    return (failedAttempts - kTrayFastAttempts) % kSlowLogEvery == 0;
}

}  // namespace peek::ui
