#pragma once

#include <filesystem>

namespace peek::paths {

// %LOCALAPPDATA%\PowerPeek, created on first access.
std::filesystem::path dataDir();

std::filesystem::path settingsFile();
std::filesystem::path historyFile();
std::filesystem::path logFile();

// Full path of the running executable, and the directory containing it.
std::filesystem::path executable();
std::filesystem::path executableDir();

// %APPDATA%\Microsoft\Windows\Start Menu\Programs\PowerPeek.lnk -- the shortcut
// that carries the AppUserModelID, without which an unpackaged app cannot raise toasts.
std::filesystem::path startMenuShortcut();

}  // namespace peek::paths
