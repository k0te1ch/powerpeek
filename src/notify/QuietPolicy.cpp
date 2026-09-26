#include "notify/QuietPolicy.h"

namespace peek::notify {

Delivery decideDelivery(NotificationEvent event, EventSettings const& config, bool quietMode,
                        UserBusyState state, bool ignoreEnabled) {
    if (!config.enabled && !ignoreEnabled) {
        return {};
    }

    Delivery delivery{config.playSound, config.showFlyout, config.showSystemToast};
    if (!quietMode || state == UserBusyState::Available) {
        return delivery;
    }

    if (event != NotificationEvent::BatteryCritical) {
        return {};
    }

    // Only a critical event gets this far, and only when it would have been seen at all.
    bool const visible = delivery.showFlyout || delivery.showSystemToast;
    return {false, visible, false};
}

}  // namespace peek::notify
