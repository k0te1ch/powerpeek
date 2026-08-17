#include "battery/XInputBatteryProvider.h"

#include <format>

#include "core/Logger.h"

namespace peek {
namespace {

// XInputGetState on an empty slot walks the device stack and costs milliseconds, so a
// slot known to be empty is left alone for this long. Microsoft's own guidance is "space
// out checks for new controllers every few seconds".
constexpr ULONGLONG kEmptySlotRetryMs = 5000;

// The four levels are buckets, not measurements. 0 and 100 are avoided on purpose: "0%"
// reads as dead when it means "nearly dead", and "100%" reads as exact when it is not.
constexpr int percentForLevel(BYTE level) noexcept {
    switch (level) {
        case BATTERY_LEVEL_EMPTY:
            return 5;
        case BATTERY_LEVEL_LOW:
            return 25;
        case BATTERY_LEVEL_MEDIUM:
            return 60;
        case BATTERY_LEVEL_FULL:
            return 95;
        default:
            return -1;
    }
}

}  // namespace

XInputBatteryProvider::XInputBatteryProvider() noexcept {
    // XInput1_4 ships with every Windows 8 or later install; xinput1_3 is a DirectX
    // redistributable that happens to be present on Windows 10. XInput9_1_0 is skipped
    // deliberately: it has no XInputGetBatteryInformation export at all.
    static wchar_t const* const kCandidates[] = {L"xinput1_4.dll", L"xinput1_3.dll"};

    for (auto const* name : kCandidates) {
        // Search System32 only, so a DLL of the same name beside the executable cannot be
        // loaded instead.
        m_module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (m_module == nullptr) {
            continue;
        }

        m_getState = reinterpret_cast<GetStateFn>(
            reinterpret_cast<void*>(GetProcAddress(m_module, "XInputGetState")));
        m_getBattery = reinterpret_cast<GetBatteryFn>(
            reinterpret_cast<void*>(GetProcAddress(m_module, "XInputGetBatteryInformation")));
        if (m_getState != nullptr && m_getBattery != nullptr) {
            return;
        }

        FreeLibrary(m_module);
        m_module = nullptr;
        m_getState = nullptr;
        m_getBattery = nullptr;
    }

    log::warning(L"No XInput DLL with battery support was found; the fallback is disabled");
}

XInputBatteryProvider::~XInputBatteryProvider() {
    if (m_module != nullptr) {
        FreeLibrary(m_module);
    }
}

void XInputBatteryProvider::invalidatePresence() noexcept {
    m_nextProbeTick.fill(0);
}

std::vector<DeviceInfo> XInputBatteryProvider::poll() noexcept {
    std::vector<DeviceInfo> result;
    if (m_getState == nullptr) {
        return result;
    }

    ULONGLONG const now = GetTickCount64();
    auto const timestamp = std::chrono::system_clock::now();

    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
        if (!m_connected[slot] && now < m_nextProbeTick[slot]) {
            continue;
        }

        XINPUT_STATE state{};
        m_connected[slot] = m_getState(slot, &state) == ERROR_SUCCESS;
        if (!m_connected[slot]) {
            m_nextProbeTick[slot] = now + kEmptySlotRetryMs;
            continue;
        }

        DeviceInfo info;
        // There is no supported way to correlate an XInput slot with a WinRT device, so
        // the key is the slot itself and history recorded under it never merges with a
        // WinRT record.
        // One slot answers separately for BATTERY_DEVTYPE_GAMEPAD and _HEADSET, which are two
        // physical things. Only the pad is asked for below, so this key is unambiguous today;
        // whoever adds the headset has to give it a key of its own, or the merge downstream
        // will fold the headset into the pad and show one card for both.
        info.id = std::format(L"xinput:slot:{}", slot);
        info.name = std::format(L"Xbox Controller {}", slot + 1);
        info.kind = DeviceKind::Gamepad;
        info.fidelity = Fidelity::Coarse;
        info.xinputSlot = static_cast<int>(slot);
        // XInput only ever enumerates XInput-compatible pads, which is as close to "Xbox
        // controller" as this API can get.
        info.isXboxController = true;
        info.lastUpdate = timestamp;

        XINPUT_BATTERY_INFORMATION battery{};
        if (m_getBattery(slot, BATTERY_DEVTYPE_GAMEPAD, &battery) == ERROR_SUCCESS) {
            switch (battery.BatteryType) {
                case BATTERY_TYPE_WIRED:
                    info.source = PowerSource::Wired;
                    break;
                case BATTERY_TYPE_ALKALINE:
                case BATTERY_TYPE_NIMH:
                    info.source = PowerSource::Battery;
                    // XInput cannot tell charging from discharging; a pad on a charging
                    // cable reports BATTERY_TYPE_WIRED, so a battery type here means the
                    // pad is running the battery down.
                    info.charge = ChargeState::Discharging;
                    info.percent = percentForLevel(battery.BatteryLevel);
                    break;
                default:
                    break;
            }
        }

        result.push_back(std::move(info));
    }

    return result;
}

}  // namespace peek
