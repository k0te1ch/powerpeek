#include "ui/TrayText.h"

#include <algorithm>
#include <string_view>

#include "core/Strings.h"

namespace peek::ui {
namespace {

// The notification-area tooltip breaks lines on CRLF, not on a bare LF.
constexpr std::wstring_view kLineBreak = L"\r\n";

std::wstring describeLevel(DeviceInfo const& device) {
    if (device.percent < 0) {
        return std::wstring(toString(device.source));
    }
    std::wstring level = std::to_wstring(device.percent);
    level += text(Text::UnitPercent);
    if (device.fidelity == Fidelity::Coarse) {
        level += L" (";
        level += text(Text::ApproximateSuffix);
        level += L')';
    }
    if (device.charge == ChargeState::Charging || device.charge == ChargeState::Full) {
        level += L", ";
        level += toString(device.charge);
    }
    return level;
}

// Whole lines only: a name cut in half tells the user less than an honest count of what was
// left out. Returns false when the line did not fit, which ends the list.
bool appendLine(std::wstring& tip, std::wstring const& line, std::size_t capacity) {
    if (tip.size() + kLineBreak.size() + line.size() > capacity) {
        return false;
    }
    tip += kLineBreak;
    tip += line;
    return true;
}

std::wstring omittedNotice(std::size_t omitted) {
    return formatText(Text::TrayMoreDevices, omitted);
}

std::wstring escapeMnemonics(std::wstring const& label) {
    std::wstring escaped;
    escaped.reserve(label.size());
    for (wchar_t const c : label) {
        escaped += c;
        if (c == L'&') {
            escaped += L'&';
        }
    }
    return escaped;
}

}  // namespace

std::vector<DeviceInfo const*> trayOrder(std::vector<DeviceInfo> const& devices) {
    std::vector<DeviceInfo const*> ordered;
    ordered.reserve(devices.size());
    for (DeviceInfo const& device : devices) {
        ordered.push_back(&device);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](DeviceInfo const* a, DeviceInfo const* b) {
                         if (a->hasBattery() != b->hasBattery()) {
                             return a->hasBattery();
                         }
                         return a->hasBattery() && a->percent < b->percent;
                     });
    return ordered;
}

std::wstring describeDevice(DeviceInfo const& device) {
    std::wstring line = device.name;
    line += L" \x2014 ";
    line += describeLevel(device);
    return line;
}

std::wstring buildTrayTooltip(std::vector<DeviceInfo> const& devices, std::size_t capacity) {
    std::wstring tip(text(Text::AppName));
    if (devices.empty()) {
        appendLine(tip, std::wstring(text(Text::NoDevices)), capacity);
        return tip;
    }

    std::vector<DeviceInfo const*> const ordered = trayOrder(devices);
    std::size_t shown = 0;
    for (DeviceInfo const* device : ordered) {
        // Room for the notice is reserved before the line goes in. Discovering afterwards
        // that it no longer fits would leave a list that is short without saying so.
        std::size_t const rest = ordered.size() - shown - 1;
        std::size_t budget = capacity;
        if (rest > 0) {
            budget -= std::min(budget, kLineBreak.size() + omittedNotice(rest).size());
        }
        if (!appendLine(tip, describeDevice(*device), budget)) {
            break;
        }
        ++shown;
    }

    if (shown < ordered.size()) {
        appendLine(tip, omittedNotice(ordered.size() - shown), capacity);
    }
    return tip;
}

std::vector<std::wstring> trayMenuLines(std::vector<DeviceInfo> const& devices) {
    std::vector<std::wstring> lines;
    if (devices.empty()) {
        lines.emplace_back(text(Text::NoDevices));
        return lines;
    }
    for (DeviceInfo const* device : trayOrder(devices)) {
        lines.push_back(escapeMnemonics(describeDevice(*device)));
    }
    return lines;
}

}  // namespace peek::ui
