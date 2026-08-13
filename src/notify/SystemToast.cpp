#include "notify/SystemToast.h"

#include "core/Win.h"

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>

#include <chrono>

#include "core/Logger.h"
#include "platform/Platform.h"

namespace peek::notify::systemToast {
namespace {

using winrt::Windows::Data::Xml::Dom::XmlDocument;
using winrt::Windows::UI::Notifications::NotificationSetting;
using winrt::Windows::UI::Notifications::ToastFailedEventArgs;
using winrt::Windows::UI::Notifications::ToastNotification;
using winrt::Windows::UI::Notifications::ToastNotificationManager;
using winrt::Windows::UI::Notifications::ToastNotifier;

// A stale "20 % left" card a day later is worse than no card, and the Action Center keeps
// them until the user sweeps it out otherwise.
constexpr std::chrono::hours kExpiry{1};

// Tag and Group are limited to 64 characters on 1703 and later, 16 before it; anything
// longer makes Show() throw rather than truncate.
constexpr std::size_t kTagLimit = 16;

constexpr wchar_t kGroup[] = L"battery";

std::wstring escape(std::wstring_view text) {
    std::wstring escaped;
    escaped.reserve(text.size() + 16);
    for (wchar_t character : text) {
        switch (character) {
            case L'&': escaped += L"&amp;"; break;
            case L'<': escaped += L"&lt;"; break;
            case L'>': escaped += L"&gt;"; break;
            case L'"': escaped += L"&quot;"; break;
            case L'\'': escaped += L"&apos;"; break;
            default: escaped += character; break;
        }
    }
    return escaped;
}

// A controller name is a fine tag as far as we are concerned but not as far as the
// notification platform is concerned, so it is reduced to something certainly acceptable.
std::wstring sanitiseTag(std::wstring_view tag) {
    std::wstring clean;
    for (wchar_t character : tag) {
        if (clean.size() >= kTagLimit) {
            break;
        }
        clean += (character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'z')
                     ? character
                     : (character >= L'A' && character <= L'Z' ? character : L'-');
    }
    return clean;
}

std::wstring buildXml(std::wstring_view title, std::wstring_view body) {
    // Child order inside <toast> is fixed by the schema: visual, audio, commands, actions,
    // header. <audio silent="true"/> before </visual> is silently ignored and the system
    // ding plays over ours.
    std::wstring xml = L"<toast duration=\"short\"><visual><binding template=\"ToastGeneric\">";
    xml += L"<text>";
    xml += escape(title);
    xml += L"</text><text>";
    xml += escape(body);
    xml += L"</text></binding></visual>";
    // The user's own clip is played by our audio engine instead; a custom src on <audio>
    // only accepts ms-winsoundevent values and would fall back to the default sound.
    xml += L"<audio silent=\"true\"/></toast>";
    return xml;
}

}  // namespace

bool available() {
    static bool const registered = platform::shell::ensureStartMenuShortcut();
    return registered;
}

bool show(std::wstring_view title, std::wstring_view body, std::wstring_view tag) {
    if (!available()) {
        return false;
    }

    try {
        XmlDocument document;
        document.LoadXml(buildXml(title, body));

        ToastNotification toast{document};
        std::wstring const clean = sanitiseTag(tag);
        if (!clean.empty()) {
            toast.Tag(clean);
            toast.Group(kGroup);
        }
        toast.ExpirationTime(winrt::Windows::Foundation::DateTime{winrt::clock::now() + kExpiry});
        toast.Failed([](ToastNotification const&, ToastFailedEventArgs const& args) {
            // Raised asynchronously, well after Show() returned success, and it is the only
            // place a rejected payload or a throttled sender is ever reported.
            log::warning(L"Windows rejected a notification: {}", describeHresult(args.ErrorCode()));
        });

        // The parameterless overload is for packaged applications and throws here; a
        // desktop application has to name its AppUserModelID.
        ToastNotifier const notifier =
            ToastNotificationManager::CreateToastNotifier(platform::shell::appUserModelId());

        if (notifier.Setting() != NotificationSetting::Enabled) {
            // Switched off for this application, for the user, or by policy. Honour it
            // quietly; the caller shows its own flyout, which is not a notification.
            return false;
        }

        notifier.Show(toast);
        return true;
    } catch (winrt::hresult_error const& error) {
        log::warning(L"Could not raise a Windows notification: {}", describeHresult(error.code()));
        return false;
    }
}

}  // namespace peek::notify::systemToast
