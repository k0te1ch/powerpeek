#include "battery/DeviceInfo.h"

#include "core/Strings.h"

namespace peek {

std::wstring_view toString(PowerSource source) {
    switch (source) {
        case PowerSource::Wired:
            return text(Text::StatusWired);
        case PowerSource::Battery:
            return text(Text::StatusOnBattery);
        case PowerSource::Unknown:
            break;
    }
    return text(Text::StatusUnknown);
}

std::wstring_view toString(ChargeState state) {
    switch (state) {
        case ChargeState::Discharging:
            return text(Text::StatusOnBattery);
        case ChargeState::Charging:
            return text(Text::StatusCharging);
        case ChargeState::Full:
            return text(Text::StatusFull);
        case ChargeState::Unknown:
            break;
    }
    return text(Text::StatusUnknown);
}

}  // namespace peek
