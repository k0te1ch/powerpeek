#pragma once

// Every translation unit reaches the Windows and C++/WinRT headers through this file.
//
// C++/WinRT requires <unknwn.h> to be included before <winrt/base.h>, otherwise
// winrt::com_ptr loses its IUnknown interop and the failure surfaces as a wall of
// errors inside the SDK headers rather than at the offending include. The lean/NOMINMAX
// defines that <windows.h> needs are applied project-wide from CMakeLists.txt.

#include <windows.h>

#include <unknwn.h>

#include <winrt/base.h>

#include <string>
#include <string_view>

namespace peek {

using winrt::check_hresult;
using winrt::com_ptr;

// Wide string conversions live here because both the Win32 and the JSON side of the
// application need them and neither owns the other.
std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view utf16);

// Formats a HRESULT as "0x80070005 (Access is denied)" for logs and error surfaces.
std::wstring describeHresult(HRESULT hr);

}  // namespace peek
