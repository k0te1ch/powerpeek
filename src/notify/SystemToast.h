#pragma once

#include <string>
#include <string_view>

namespace peek::notify::systemToast {

// Real Windows notifications, the kind that land in the Action Center and obey Focus
// Assist, as opposed to the application's own flyout.
//
// An unpackaged desktop application can only raise these if Explorer can resolve its
// AppUserModelID, which in practice means a Start Menu shortcut carrying it. Where that
// shortcut cannot be written -- a locked-down profile, a read-only Start Menu -- toasts
// are simply unavailable and the caller shows a flyout instead.

// Whether the identity a toast needs is in place. Probed once; the settings page uses it
// to explain why the per-event switch is off.
bool available();

// Raises one notification. `tag` groups replacements: a second toast with the same tag
// replaces the first rather than stacking, which is what stops a controller sitting on
// the low threshold from filling the Action Center.
//
// False means Windows would not take it -- notifications disabled for this application,
// by policy, or the identity is missing -- and the caller should fall back to the flyout.
bool show(std::wstring_view title, std::wstring_view body, std::wstring_view tag);

}  // namespace peek::notify::systemToast
