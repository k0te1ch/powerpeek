#pragma once

#include <cstdint>
#include <string_view>

namespace peek {

// FNV-1a over the UTF-16 code units, low byte first.
//
// A hash, not a cipher. It exists for the device ids that end up in the battery log on disk:
// a device path can spell out a Bluetooth address or a USB serial, and a digest keys the device
// just as well while naming nothing. The value is persisted, so the algorithm must never change.
std::uint64_t digestOf(std::wstring_view text) noexcept;

}  // namespace peek
