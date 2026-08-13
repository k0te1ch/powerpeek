#pragma once

#include <array>
#include <vector>

#include "battery/ControllerInfo.h"
#include "core/Win.h"

#include <Xinput.h>

namespace peek {

// The fallback source, used only when Windows.Gaming.Input reports nothing at all.
//
// XInput sees four slots, has no device identity, no charging state and a two-bit level,
// so everything it produces is Fidelity::Coarse. XInput1_4 and xinput1_3 export
// XInputGetBatteryInformation; XInput9_1_0 does not, which is why the DLL is resolved by
// name at runtime instead of being linked.
class XInputBatteryProvider {
public:
    XInputBatteryProvider() noexcept;
    ~XInputBatteryProvider();

    XInputBatteryProvider(XInputBatteryProvider const&) = delete;
    XInputBatteryProvider& operator=(XInputBatteryProvider const&) = delete;

    bool available() const noexcept { return m_getState != nullptr; }

    // Re-probes the slots currently believed empty at the next poll. Worth calling after a
    // device arrival, since an empty slot is otherwise only retried every few seconds.
    void invalidatePresence() noexcept;

    std::vector<ControllerInfo> poll() noexcept;

private:
    using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using GetBatteryFn = DWORD(WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);

    HMODULE m_module = nullptr;
    GetStateFn m_getState = nullptr;
    GetBatteryFn m_getBattery = nullptr;

    std::array<bool, XUSER_MAX_COUNT> m_connected{};
    std::array<ULONGLONG, XUSER_MAX_COUNT> m_nextProbeTick{};
};

}  // namespace peek
