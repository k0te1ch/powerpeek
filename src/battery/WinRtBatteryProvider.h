#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "battery/DeviceInfo.h"

namespace peek {

// Reads controller batteries through Windows.Gaming.Input, the only SDK-only API that
// covers USB, the Xbox Wireless Adapter and Bluetooth and that gives a device identity.
//
// Every member must be called on one and the same MTA thread, and stop() must run before
// that thread leaves its apartment: a RawGameController released after
// winrt::uninit_apartment() faults inside a torn-down proxy. ControllerMonitor owns that
// thread and that ordering.
class WinRtBatteryProvider {
public:
    WinRtBatteryProvider();
    ~WinRtBatteryProvider();

    WinRtBatteryProvider(WinRtBatteryProvider const&) = delete;
    WinRtBatteryProvider& operator=(WinRtBatteryProvider const&) = delete;

    // Raised on the Windows.Gaming.Input worker thread, not on the polling thread, so the
    // handler may only signal. Set it before start().
    void setDeviceChangedHandler(std::function<void()> handler);

    // Subscribes to the add/remove events and then primes the collection, in that order:
    // the collection is asynchronously populated and is normally still empty at this
    // point, so subscribing second would lose every already-connected pad.
    bool start();

    void stop() noexcept;

    std::vector<DeviceInfo> poll();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace peek
