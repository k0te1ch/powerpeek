#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "battery/DeviceInfo.h"

namespace peek::ui {

// The text the notification area shows: the tooltip and the device lines at the top of the
// context menu. Kept apart from TrayIcon so that it links into PowerPeekCore and can be tested
// without a shell to hand it to.

// szTip is WCHAR[128] under NOTIFYICON_VERSION_4, minus its terminator.
inline constexpr std::size_t kTrayTipCapacity = 127;

// The order devices are listed in: lowest battery first, because that is the one about to
// interrupt play, then everything with no level to report. Equal levels keep the order they
// arrived in, so a list that did not change does not reshuffle between polls.
std::vector<DeviceInfo const*> trayOrder(std::vector<DeviceInfo> const& devices);

// "Name — 45%, charging": one device on one line.
std::wstring describeDevice(DeviceInfo const& device);

// The application name, then one line per device, cut to `capacity` characters. Only whole
// lines go in; whatever does not fit is summed up in a closing "+N more" line.
std::wstring buildTrayTooltip(std::vector<DeviceInfo> const& devices,
                              std::size_t capacity = kTrayTipCapacity);

// One menu label per device in tray order, or a single "nothing connected" line. Ampersands
// are doubled, since a menu otherwise reads one as a mnemonic and swallows it.
std::vector<std::wstring> trayMenuLines(std::vector<DeviceInfo> const& devices);

}  // namespace peek::ui
