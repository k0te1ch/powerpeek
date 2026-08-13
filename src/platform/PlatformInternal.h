#pragma once

#include <string_view>

#include "core/Win.h"

// Shared between the platform translation units only; nothing outside src/platform/
// includes this.
namespace peek::platform {

// Ordinal, case-insensitive: Windows file names compare that way, and the locale-aware
// _wcsicmp answers wrongly on a Turkish system, where "I" does not lowercase to "i".
// Empty operands never compare equal -- an empty path means "could not be determined".
bool equalPathsIgnoringCase(std::wstring_view a, std::wstring_view b);

// Build number from RtlGetVersion. Zero when ntdll refuses to answer, which reads as
// "oldest supported build" everywhere it is compared.
DWORD osBuildNumber();

}  // namespace peek::platform
