#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "battery/BatteryHistory.h"
#include "battery/DeviceInfo.h"
#include "core/Settings.h"
#include "core/Win.h"
#include "notify/NotificationCenter.h"
#include "ui/Window.h"

namespace peek::ui {

class Canvas;

// The application's main window: a NavigationView-style shell with a title bar this window
// draws itself, and five pages -- controllers, history, sounds, settings, about.
//
// It is a view and nothing else. It is told what the controllers are and what the settings
// became; everything it changes goes back out through SettingsStore::apply, so the poller,
// the tray icon and the notification centre react live without this window knowing they
// exist and without anything being restarted.
class MainWindow : public D2DWindow {
public:
    struct Dependencies {
        // Owned by the application, and outlive the window.
        BatteryHistory* history = nullptr;
        notify::NotificationCenter* notifications = nullptr;

        // Asks the controller monitor to poll out of band, behind the refresh button.
        std::function<void()> refreshControllers;

        // Called when the close button really means "quit" -- that is, when close-to-tray is
        // off. Left empty, the window only hides, which is the safe default for a tray
        // application.
        std::function<void()> exitApplication;

        // False when the Start Menu shortcut could not be written: without it an unpackaged
        // process cannot raise Windows toasts, and the switches asking for them say so.
        bool systemToastsAvailable = false;
    };

    MainWindow();
    ~MainWindow() override;

    bool create(Dependencies dependencies);

    void showAndActivate();
    void hide();

    // The controller monitor's latest snapshot. Cheap to call at every poll: the pages only
    // rebuild when the set of devices changed, otherwise the gauges animate to the new levels.
    void setControllers(std::vector<DeviceInfo> controllers);

    // Raised as (current, previous), matching SettingsStore::changed. Safe to call from a
    // handler this window itself triggered: the rebuild it may need is deferred to the next
    // frame rather than run inside the control that caused it.
    void settingsChanged(Settings const& current, Settings const& previous);

protected:
    void onPaint(ID2D1DeviceContext& ctx, D2D1_SIZE_F) override;
    bool onMessage(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result) override;
    bool isCaptionArea(D2D1_POINT_2F point) const override;
    void onThemeChanged() override;

private:
    void buildShell();
    void showPage(int index);
    void requestClose();
    void drawTitleBar(Canvas& canvas, D2D1_RECT_F body) const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace peek::ui
