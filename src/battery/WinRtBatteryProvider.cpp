#include "battery/WinRtBatteryProvider.h"

#include "core/Win.h"

#include <winrt/Windows.Devices.Power.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.System.Power.h>

#include <algorithm>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

#include "core/Logger.h"

namespace peek {
namespace {

using winrt::Windows::Devices::Power::BatteryReport;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::IReference;
using winrt::Windows::Gaming::Input::IGameController;
using winrt::Windows::Gaming::Input::IGameControllerBatteryInfo;
using winrt::Windows::Gaming::Input::IRawGameController2;
using winrt::Windows::Gaming::Input::RawGameController;
using winrt::Windows::System::Power::BatteryStatus;

constexpr std::uint16_t kVendorMicrosoft = 0x045E;

// Every Microsoft pad and adapter product id, so that a wireless adapter that surfaces
// itself instead of its child device is still classified as Xbox hardware. The list is a
// hint and not proof: virtual pads (ViGEm, x360ce, DS4Windows) present these ids too.
constexpr bool isXboxProductId(std::uint16_t pid) noexcept {
    switch (pid) {
        case 0x028E:  // Xbox 360 wired
        case 0x028F:  // Xbox 360 wireless, through a receiver
        case 0x02A1:  // xusb22.sys software id
        case 0x0719:  // Xbox 360 Wireless Receiver for Windows
        case 0x02D1:  // Xbox One (2013)
        case 0x02DD:  // Xbox One (2015 firmware)
        case 0x02E3:  // Elite Series 1
        case 0x02E6:  // Xbox Wireless Adapter, gen 1
        case 0x02EA:  // Xbox One S, USB
        case 0x02E0:  // Xbox One S, Bluetooth rev 1
        case 0x02FD:  // Xbox One S, Bluetooth rev 2
        case 0x02FE:  // Xbox Wireless Adapter, gen 2
        case 0x02FF:  // xboxgip.sys software id
        case 0x0B00:  // Elite Series 2, USB
        case 0x0B05:  // Elite Series 2, Bluetooth
        case 0x0B22:  // Elite Series 2, BLE
        case 0x0B0A:  // Adaptive Controller, USB
        case 0x0B0C:  // Adaptive Controller, Bluetooth
        case 0x0B21:  // Adaptive Controller, BLE
        case 0x0B12:  // Xbox Series X|S, USB
        case 0x0B13:  // Xbox Series X|S, Bluetooth
        case 0x0B20:  // Xbox One S, BLE
            return true;
        default:
            return false;
    }
}

// The getters return S_OK with a null IReference, so the value is optional twice over.
std::optional<std::int32_t> unwrap(IReference<std::int32_t> const& value) {
    if (!value) {
        return std::nullopt;
    }
    return value.Value();
}

std::optional<int> percentOf(BatteryReport const& report) {
    auto const remaining = unwrap(report.RemainingCapacityInMilliwattHours());
    auto const full = unwrap(report.FullChargeCapacityInMilliwattHours());
    if (!remaining || !full || *full <= 0 || *remaining < 0) {
        return std::nullopt;
    }

    // The milliwatt-hour figures are a firmware-quantised four-step level dressed up as
    // energy; only their ratio means anything and it must never reach the UI as mWh.
    auto const ratio = (static_cast<std::int64_t>(*remaining) * 100 + *full / 2) / *full;
    return static_cast<int>(std::clamp<std::int64_t>(ratio, 0, 100));
}

void applyReport(BatteryReport const& report, bool wireless, DeviceInfo& info) {
    switch (report.Status()) {
        case BatteryStatus::NotPresent:
            // A cabled pad with no battery pack. A *wireless* link reporting NotPresent is
            // something else -- an undetected pack -- and must not be called wired, and
            // above all must not be shown as 0%.
            info.source = wireless ? PowerSource::Unknown : PowerSource::Wired;
            return;
        case BatteryStatus::Discharging:
            info.charge = ChargeState::Discharging;
            break;
        case BatteryStatus::Charging:
            info.charge = ChargeState::Charging;
            break;
        case BatteryStatus::Idle:
            // A pack that is neither taking nor giving current: charged, on the cable.
            info.charge = ChargeState::Full;
            break;
        default:
            return;
    }

    info.source = PowerSource::Battery;
    if (auto const percent = percentOf(report)) {
        info.percent = *percent;
    }
}

void keepMtaAlive() noexcept {
    // Windows can unload a Windows.Gaming.Input dependency before WGI itself, and
    // ~GameController then calls into an unmapped DLL at process exit (libsdl-org/SDL#5552).
    // Pinning the MTA keeps those DLLs loaded; the cookie is deliberately never released.
    static CO_MTA_USAGE_COOKIE cookie = nullptr;
    if (cookie == nullptr && FAILED(CoIncrementMTAUsage(&cookie))) {
        log::warning(L"CoIncrementMTAUsage failed; the WGI shutdown crash mitigation is off");
    }
}

}  // namespace

struct WinRtBatteryProvider::Impl {
    std::function<void()> deviceChanged;

    std::mutex mutex;
    std::vector<RawGameController> controllers;

    RawGameController::RawGameControllerAdded_revoker addedRevoker;
    RawGameController::RawGameControllerRemoved_revoker removedRevoker;
    bool started = false;

    void add(RawGameController const& controller) {
        {
            std::scoped_lock lock{mutex};
            if (std::find(controllers.begin(), controllers.end(), controller) != controllers.end()) {
                return;
            }
            controllers.push_back(controller);
        }
        notifyChanged();
    }

    void remove(RawGameController const& controller) {
        {
            std::scoped_lock lock{mutex};
            if (std::erase(controllers, controller) == 0) {
                return;
            }
        }
        notifyChanged();
    }

    void notifyChanged() const {
        if (deviceChanged) {
            deviceChanged();
        }
    }
};

WinRtBatteryProvider::WinRtBatteryProvider() : m_impl(std::make_unique<Impl>()) {}

WinRtBatteryProvider::~WinRtBatteryProvider() {
    stop();
}

void WinRtBatteryProvider::setDeviceChangedHandler(std::function<void()> handler) {
    m_impl->deviceChanged = std::move(handler);
}

bool WinRtBatteryProvider::start() {
    if (m_impl->started) {
        return true;
    }

    keepMtaAlive();

    try {
        Impl* impl = m_impl.get();
        m_impl->addedRevoker = RawGameController::RawGameControllerAdded(
            winrt::auto_revoke,
            [impl](IInspectable const&, RawGameController const& c) { impl->add(c); });
        m_impl->removedRevoker = RawGameController::RawGameControllerRemoved(
            winrt::auto_revoke,
            [impl](IInspectable const&, RawGameController const& c) { impl->remove(c); });

        for (auto const& controller : RawGameController::RawGameControllers()) {
            m_impl->add(controller);
        }
    } catch (winrt::hresult_error const& error) {
        log::error(L"Windows.Gaming.Input is unavailable: {}", describeHresult(error.code()));
        stop();
        return false;
    }

    m_impl->started = true;
    return true;
}

void WinRtBatteryProvider::stop() noexcept {
    m_impl->addedRevoker.revoke();
    m_impl->removedRevoker.revoke();
    std::scoped_lock lock{m_impl->mutex};
    m_impl->controllers.clear();
    m_impl->started = false;
}

std::vector<DeviceInfo> WinRtBatteryProvider::poll() {
    std::vector<RawGameController> controllers;
    {
        std::scoped_lock lock{m_impl->mutex};
        controllers = m_impl->controllers;
    }

    auto const now = std::chrono::system_clock::now();
    std::vector<DeviceInfo> result;
    result.reserve(controllers.size());

    // How many devices of each vendor/product pair have already had to be named below.
    std::map<std::pair<std::uint16_t, std::uint16_t>, std::size_t> unnamedSoFar;

    for (RawGameController const& controller : controllers) {
        DeviceInfo info;
        // Windows.Gaming.Input enumerates pads, wheels, flight sticks and arcade sticks --
        // things you play with, and nothing else.
        info.kind = DeviceKind::Gamepad;
        info.fidelity = Fidelity::Exact;
        info.lastUpdate = now;

        try {
            info.vendorId = controller.HardwareVendorId();
            info.productId = controller.HardwareProductId();
            info.isXboxController =
                info.vendorId == kVendorMicrosoft && isXboxProductId(info.productId);

            bool wireless = false;
            if (auto const gameController = controller.try_as<IGameController>()) {
                wireless = gameController.IsWireless();
            }

            // IRawGameController2 has shipped since 1709, but a device provided through
            // Windows.Gaming.Input.Custom need not implement it.
            if (auto const identity = controller.try_as<IRawGameController2>()) {
                info.id = identity.NonRoamableId();
                info.name = identity.DisplayName();
            }
            if (info.id.empty()) {
                // The discriminator counts only the devices that also could not name
                // themselves, never this device's position in the vector: that position
                // moves whenever any unrelated pad connects or disconnects, and every
                // consumer keys on the id, so the pad would read as having left and a
                // different one as having arrived -- a chime, a reset "first seen" and a
                // forked history series, for a pad that did nothing.
                //
                // Two of the same model that both land here still trade numbers when the
                // earlier one leaves. Nothing reachable from this API tells them apart, and
                // a shared id would be worse: consumers that collapse by id would show one
                // card for the two of them.
                std::size_t const ordinal = unnamedSoFar[{info.vendorId, info.productId}]++;
                info.id = std::format(L"wgi:{:04x}:{:04x}:{}", info.vendorId, info.productId,
                                      ordinal);
            }

            // Reaching TryGetBatteryReport through the projection throws E_NOINTERFACE on
            // a pad without battery support, which most third-party pads are.
            if (auto const battery = controller.try_as<IGameControllerBatteryInfo>()) {
                if (BatteryReport const report = battery.TryGetBatteryReport()) {
                    applyReport(report, wireless, info);
                }
            }
        } catch (winrt::hresult_error const& error) {
            // The pad was unplugged between the copy above and this call. Report what was
            // read; the Removed event drops it from the next poll.
            log::debug(L"Controller read failed, device is going away: {}",
                       describeHresult(error.code()));
        }

        result.push_back(std::move(info));
    }

    return result;
}

}  // namespace peek
