#include "core/Logger.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <system_error>

#include "core/Win.h"

namespace peek::log {
namespace {

constexpr std::uint64_t kRotateBytes = 1024 * 1024;

constexpr std::array<wchar_t const*, 4> kLevelTags = {L"DEBUG", L"INFO ", L"WARN ", L"ERROR"};

static_assert(kLevelTags.size() == static_cast<std::size_t>(Level::Error) + 1,
              "every Level needs a tag, and the tags are indexed by the enumerator");

struct State {
    std::mutex mutex;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::filesystem::path path;
    std::uint64_t written = 0;
#ifdef NDEBUG
    Level minimum = Level::Info;
#else
    // Debug lines are the ones that describe controller polling, which is exactly what a
    // developer attached to the process wants and a shipped build should not pay for.
    Level minimum = Level::Debug;
#endif
};

State& state() {
    static State instance;
    return instance;
}

// Failures of the log file itself cannot be logged: write() holds the lock these run
// under, and the file is the thing that just failed.
void reportToDebugger(std::wstring const& message) {
    OutputDebugStringW((message + L'\n').c_str());
}

std::wstring lastErrorText() {
    return describeHresult(HRESULT_FROM_WIN32(GetLastError()));
}

void closeLocked(State& s) {
    if (s.file != INVALID_HANDLE_VALUE) {
        CloseHandle(s.file);
        s.file = INVALID_HANDLE_VALUE;
    }
}

void writeBytesLocked(State& s, std::string_view bytes) {
    if (s.file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    if (!WriteFile(s.file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)) {
        reportToDebugger(std::format(L"xbs: writing to the log file failed ({}); logging to "
                                     L"the debugger only from now on",
                                     lastErrorText()));
        closeLocked(s);
        return;
    }
    s.written += written;
}

void openLocked(State& s) {
    // FILE_APPEND_DATA sends every write to the end of the file no matter where the file
    // pointer is, so an external tail or a second handle cannot make lines overwrite.
    s.file = CreateFileW(s.path.c_str(), FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (s.file == INVALID_HANDLE_VALUE) {
        reportToDebugger(
            std::format(L"xbs: cannot open log file {} ({})", s.path.wstring(), lastErrorText()));
        s.written = 0;
        return;
    }

    LARGE_INTEGER size{};
    s.written = GetFileSizeEx(s.file, &size) ? static_cast<std::uint64_t>(size.QuadPart) : 0;
    if (s.written == 0) {
        // Notepad on Windows 10 falls back to the ANSI code page for a BOM-less file, which
        // turns every Cyrillic log line into mojibake.
        writeBytesLocked(s, "\xEF\xBB\xBF");
    }
}

void rotateLocked(State& s) {
    closeLocked(s);

    std::filesystem::path aside = s.path;
    aside += L".1";
    if (!MoveFileExW(s.path.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        reportToDebugger(std::format(L"xbs: cannot rotate the log file ({}); it will keep growing",
                                     lastErrorText()));
    }

    s.written = 0;
    openLocked(s);
}

}  // namespace

void open(std::filesystem::path const& file) {
    State& s = state();
    std::scoped_lock const lock(s.mutex);

    closeLocked(s);
    s.path = file;
    s.written = 0;

    if (!file.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        if (ec) {
            reportToDebugger(std::format(L"xbs: cannot create the log directory {} ({})",
                                         file.parent_path().wstring(),
                                         describeHresult(HRESULT_FROM_WIN32(
                                             static_cast<DWORD>(ec.value())))));
        }
    }

    openLocked(s);
    if (s.written > kRotateBytes) {
        rotateLocked(s);
    }
}

void close() {
    State& s = state();
    std::scoped_lock const lock(s.mutex);
    closeLocked(s);
}

void setMinimumLevel(Level level) {
    State& s = state();
    std::scoped_lock const lock(s.mutex);
    s.minimum = level;
}

void write(Level level, std::wstring_view message) {
    State& s = state();
    std::scoped_lock const lock(s.mutex);
    if (level < s.minimum) {
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::wstring const line =
        std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}  {}  {}\r\n", now.wYear, now.wMonth,
                    now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
                    kLevelTags[static_cast<std::size_t>(level)], message);

    OutputDebugStringW(line.c_str());

    if (s.file == INVALID_HANDLE_VALUE) {
        return;
    }
    if (s.written >= kRotateBytes) {
        rotateLocked(s);
    }
    writeBytesLocked(s, narrow(line));
}

}  // namespace peek::log
