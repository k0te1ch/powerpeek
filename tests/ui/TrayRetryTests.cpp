// The schedule for putting the icon back into the notification area.
//
// What it has to guarantee is that the icon is never given up on: a run at logon that loses the
// race with Explorer for longer than a minute used to be left with no icon and no window, and so
// with no way in at all short of the task manager.

#include "TestSupport.h"

#include <chrono>

#include "ui/TrayRetry.h"

namespace {

using peek::ui::kTrayFastAttempts;
using peek::ui::kTrayFastRetry;
using peek::ui::kTraySlowRetry;
using peek::ui::shouldLogTrayFailure;
using peek::ui::trayRetryDelay;

TEST_CASE("trayRetry: the first minute is tried every two seconds") {
    CHECK(trayRetryDelay(1) == kTrayFastRetry);
    CHECK(trayRetryDelay(kTrayFastAttempts - 1) == kTrayFastRetry);
    CHECK(kTrayFastRetry * kTrayFastAttempts == std::chrono::minutes{1});
}

TEST_CASE("trayRetry: after the first minute it slows down and never stops") {
    CHECK(trayRetryDelay(kTrayFastAttempts) == kTraySlowRetry);
    CHECK(trayRetryDelay(kTrayFastAttempts + 1) == kTraySlowRetry);
    // A day of failures at thirty seconds each, and the answer is still "try again".
    CHECK(trayRetryDelay(kTrayFastAttempts + 2880) == kTraySlowRetry);
    CHECK(trayRetryDelay(1'000'000) == kTraySlowRetry);
}

TEST_CASE("trayRetry: the slow phase writes one log line in ten") {
    CHECK(shouldLogTrayFailure(1));
    CHECK(shouldLogTrayFailure(kTrayFastAttempts));
    CHECK_FALSE(shouldLogTrayFailure(kTrayFastAttempts + 1));
    CHECK_FALSE(shouldLogTrayFailure(kTrayFastAttempts + 9));
    CHECK(shouldLogTrayFailure(kTrayFastAttempts + 10));
    CHECK_FALSE(shouldLogTrayFailure(kTrayFastAttempts + 11));
    CHECK(shouldLogTrayFailure(kTrayFastAttempts + 20));
}

}  // namespace
