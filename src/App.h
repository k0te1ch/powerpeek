#pragma once

#include <memory>

#include "core/Win.h"

namespace peek {

// Owns everything and lives for the whole process.
//
// A hidden message-only-ish window is the hub: the controller monitor posts to it from
// its background thread, the tray icon sends its callbacks to it, and WM_SETTINGCHANGE
// arrives there. That keeps every observer on one thread without a single lock, and it
// means the application still works with the main window closed -- which is the normal
// state for a tray utility.
class App {
public:
    App();
    ~App();

    App(App const&) = delete;
    App& operator=(App const&) = delete;

    // Returns the process exit code. Handles the single-instance handshake, so a second
    // launch returns immediately after asking the first to show itself.
    int run(HINSTANCE instance, int showCommand);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace peek
