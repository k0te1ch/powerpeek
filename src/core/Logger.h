#pragma once

#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace peek::log {

enum class Level {
    Debug,
    Info,
    Warning,
    Error,
};

// Starts writing to `file`, rotating it aside first if it has grown past a size cap.
// Before this is called (and if it fails) messages still reach the debugger via
// OutputDebugString, so logging is always safe to call.
void open(std::filesystem::path const& file);
void close();

void setMinimumLevel(Level level);

void write(Level level, std::wstring_view message);

template <class... Args>
void debug(std::wformat_string<Args...> fmt, Args&&... args) {
    write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void info(std::wformat_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void warning(std::wformat_string<Args...> fmt, Args&&... args) {
    write(Level::Warning, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void error(std::wformat_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace peek::log
