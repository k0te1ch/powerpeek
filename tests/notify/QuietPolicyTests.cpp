// The rules for holding notifications back while the user is in a game, a presentation or
// Focus Assist. The busy state itself comes from the shell and cannot be produced on a build
// agent, which is why decideDelivery takes it as an argument.

#include "TestSupport.h"

#include "core/Settings.h"
#include "notify/QuietPolicy.h"

namespace {

using peek::EventSettings;
using peek::NotificationEvent;
using peek::notify::decideDelivery;
using peek::notify::Delivery;
using peek::notify::UserBusyState;

EventSettings everything() {
    EventSettings config;
    config.enabled = true;
    config.playSound = true;
    config.showFlyout = true;
    config.showSystemToast = true;
    return config;
}

constexpr NotificationEvent kOrdinary[] = {
    NotificationEvent::Connected,
    NotificationEvent::Disconnected,
    NotificationEvent::BatteryLow,
    NotificationEvent::FullyCharged,
};

constexpr UserBusyState kBusy[] = {
    UserBusyState::FullScreen,
    UserBusyState::Presentation,
    UserBusyState::QuietTime,
};

}  // namespace

TEST_CASE("quietPolicy: an available user gets exactly what the event is configured for") {
    EventSettings config = everything();
    config.showSystemToast = false;
    Delivery const d =
        decideDelivery(NotificationEvent::BatteryLow, config, true, UserBusyState::Available, false);
    CHECK(d.playSound);
    CHECK(d.showFlyout);
    CHECK_FALSE(d.showSystemToast);
}

TEST_CASE("quietPolicy: a disabled event delivers nothing, busy or not") {
    EventSettings config = everything();
    config.enabled = false;
    CHECK_FALSE(decideDelivery(NotificationEvent::BatteryCritical, config, true,
                               UserBusyState::Available, false)
                    .any());
    CHECK_FALSE(decideDelivery(NotificationEvent::BatteryCritical, config, true,
                               UserBusyState::FullScreen, false)
                    .any());
}

TEST_CASE("quietPolicy: the test button ignores a disabled event") {
    EventSettings config = everything();
    config.enabled = false;
    CHECK(decideDelivery(NotificationEvent::Connected, config, true, UserBusyState::Available, true)
              .any());
}

TEST_CASE("quietPolicy: ordinary events are held back entirely while busy") {
    for (NotificationEvent event : kOrdinary) {
        for (UserBusyState state : kBusy) {
            CHECK_FALSE(decideDelivery(event, everything(), true, state, false).any());
        }
    }
}

TEST_CASE("quietPolicy: with quiet mode off, being busy changes nothing") {
    for (UserBusyState state : kBusy) {
        Delivery const d =
            decideDelivery(NotificationEvent::Connected, everything(), false, state, false);
        CHECK(d.playSound);
        CHECK(d.showFlyout);
        CHECK(d.showSystemToast);
    }
}

TEST_CASE("quietPolicy: a critical battery keeps only the application's own card while busy") {
    for (UserBusyState state : kBusy) {
        Delivery const d =
            decideDelivery(NotificationEvent::BatteryCritical, everything(), true, state, false);
        CHECK_FALSE(d.playSound);
        CHECK(d.showFlyout);
        CHECK_FALSE(d.showSystemToast);
    }
}

TEST_CASE("quietPolicy: a critical battery set to Windows notifications only turns into a card") {
    EventSettings config = everything();
    config.showFlyout = false;
    Delivery const d = decideDelivery(NotificationEvent::BatteryCritical, config, true,
                                      UserBusyState::FullScreen, false);
    CHECK(d.showFlyout);
    CHECK_FALSE(d.showSystemToast);
}

TEST_CASE("quietPolicy: a critical battery set to sound only stays silent while busy") {
    EventSettings config = everything();
    config.showFlyout = false;
    config.showSystemToast = false;
    CHECK_FALSE(decideDelivery(NotificationEvent::BatteryCritical, config, true,
                               UserBusyState::QuietTime, false)
                    .any());
}
