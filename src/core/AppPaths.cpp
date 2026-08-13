#include "core/AppPaths.h"

#include "core/Win.h"

#include <shlobj.h>

#include <string>
#include <system_error>

#include "core/Logger.h"

namespace peek::paths {
namespace {

// Longest path Windows accepts even with long paths enabled, in characters.
constexpr std::size_t kPathLimit = 32768;

std::filesystem::path knownFolder(KNOWNFOLDERID const& folder) {
    PWSTR raw = nullptr;
    HRESULT const hr = SHGetKnownFolderPath(folder, KF_FLAG_CREATE, nullptr, &raw);
    if (FAILED(hr)) {
        log::error(L"SHGetKnownFolderPath failed: {}", describeHresult(hr));
        return {};
    }

    std::filesystem::path result(raw);
    CoTaskMemFree(raw);
    return result;
}

std::filesystem::path createDataDir() {
    std::filesystem::path base = knownFolder(FOLDERID_LocalAppData);
    if (base.empty()) {
        // Without a profile directory the application still has to store something
        // somewhere; next to the executable is visible and recoverable for the user.
        base = executableDir();
        log::warning(L"Falling back to {} for application data", base.wstring());
    }

    std::filesystem::path result = base / L"PowerPeek";
    std::error_code ec;
    std::filesystem::create_directories(result, ec);
    if (ec) {
        log::error(L"Cannot create {}: {}", result.wstring(),
                   describeHresult(HRESULT_FROM_WIN32(static_cast<DWORD>(ec.value()))));
    }
    return result;
}

}  // namespace

std::filesystem::path dataDir() {
    static std::filesystem::path const dir = createDataDir();
    return dir;
}

std::filesystem::path settingsFile() {
    return dataDir() / L"settings.json";
}

std::filesystem::path historyFile() {
    return dataDir() / L"history.jsonl";
}

std::filesystem::path logFile() {
    return dataDir() / L"PowerPeek.log";
}

std::filesystem::path executable() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD const length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            log::error(L"GetModuleFileNameW failed: {}",
                       describeHresult(HRESULT_FROM_WIN32(GetLastError())));
            return {};
        }

        // On truncation the call returns the size of the buffer it filled, never the size
        // it needed, so the only way forward is to grow and ask again.
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }

        if (buffer.size() >= kPathLimit) {
            log::error(L"The executable path is longer than Windows allows");
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path executableDir() {
    return executable().parent_path();
}

std::filesystem::path startMenuShortcut() {
    std::filesystem::path const programs = knownFolder(FOLDERID_Programs);
    if (programs.empty()) {
        return {};
    }
    return programs / L"PowerPeek.lnk";
}

}  // namespace peek::paths
