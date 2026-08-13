// shlobj.h first: it drags in the PROPERTYKEY definition that propkeydef.h assumes is
// already there, and propkey.h on its own fails to compile without it.
#include <shlobj.h>

#include <propkey.h>
#include <propvarutil.h>

#include <cwchar>
#include <filesystem>
#include <string>
#include <system_error>

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/Strings.h"
#include "platform/Platform.h"
#include "platform/PlatformInternal.h"

namespace peek::platform::shell {
namespace {

// One AppUserModelID, used by the process, by the shortcut and by the toast notifier. If
// the three ever disagree, Show() succeeds and nothing appears.
constexpr wchar_t kAumid[] = L"Savelka.PowerPeek";

bool shortcutMatches(std::filesystem::path const& link, std::filesystem::path const& executable) {
    com_ptr<IShellLinkW> shortcut;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(shortcut.put())))) {
        return false;
    }

    auto const file = shortcut.try_as<IPersistFile>();
    if (!file || FAILED(file->Load(link.c_str(), STGM_READ))) {
        return false;
    }

    // IShellLink::GetPath has no long-path form: a target past MAX_PATH cannot be read
    // back and the shortcut is simply rewritten instead.
    wchar_t target[MAX_PATH]{};
    if (FAILED(shortcut->GetPath(target, ARRAYSIZE(target), nullptr, SLGP_RAWPATH)) ||
        !equalPathsIgnoringCase(target, executable.native())) {
        return false;
    }

    auto const store = shortcut.try_as<IPropertyStore>();
    if (!store) {
        return false;
    }
    PROPVARIANT id{};
    if (FAILED(store->GetValue(PKEY_AppUserModel_ID, &id))) {
        return false;
    }
    bool const matches = id.vt == VT_LPWSTR && id.pwszVal && std::wcscmp(id.pwszVal, kAumid) == 0;
    PropVariantClear(&id);
    return matches;
}

bool writeShortcut(std::filesystem::path const& link, std::filesystem::path const& executable) {
    com_ptr<IShellLinkW> shortcut;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(shortcut.put()));
    if (FAILED(hr)) {
        log::error(L"CoCreateInstance(ShellLink) failed: {}", describeHresult(hr));
        return false;
    }

    try {
        check_hresult(shortcut->SetPath(executable.c_str()));
        check_hresult(shortcut->SetWorkingDirectory(paths::executableDir().c_str()));
        std::wstring const description{text(Text::AppTagline)};
        check_hresult(shortcut->SetDescription(description.c_str()));

        auto const store = shortcut.as<IPropertyStore>();
        PROPVARIANT id{};
        check_hresult(InitPropVariantFromString(kAumid, &id));
        hr = store->SetValue(PKEY_AppUserModel_ID, id);
        PropVariantClear(&id);
        check_hresult(hr);
        check_hresult(store->Commit());

        check_hresult(shortcut.as<IPersistFile>()->Save(link.c_str(), TRUE));
    } catch (winrt::hresult_error const& error) {
        log::error(L"Could not write {}: {}", link.wstring(), describeHresult(error.code()));
        return false;
    }
    return true;
}

}  // namespace

std::wstring appUserModelId() {
    return kAumid;
}

void applyAppUserModelId() {
    // Must precede every window and every shell call: the taskbar reads the id when it
    // first sees a window, and toasts resolve it at notifier creation.
    HRESULT const hr = SetCurrentProcessExplicitAppUserModelID(kAumid);
    if (FAILED(hr)) {
        log::warning(L"SetCurrentProcessExplicitAppUserModelID failed: {}; toasts and taskbar "
                     L"grouping will be wrong",
                     describeHresult(hr));
    }
}

bool ensureStartMenuShortcut() {
    std::filesystem::path const executable = paths::executable();
    std::filesystem::path const link = paths::startMenuShortcut();
    if (executable.empty() || link.empty()) {
        log::error(L"Cannot place the Start Menu shortcut: the paths could not be resolved");
        return false;
    }

    if (shortcutMatches(link, executable)) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(link.parent_path(), ec);
    if (ec) {
        log::error(L"Could not create {}: {}", link.parent_path().wstring(),
                   widen(ec.message()));
        return false;
    }

    if (!writeShortcut(link, executable)) {
        return false;
    }

    // The shell indexes the new shortcut asynchronously, so a toast raised in the first
    // seconds after this is written can still be dropped. There is no API to force it.
    log::info(L"Start Menu shortcut written: {}", link.wstring());
    return true;
}

}  // namespace peek::platform::shell
