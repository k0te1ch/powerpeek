#pragma once

#include <array>

#include "ui/pages/Page.h"

namespace peek::ui {

// One expander per notification event: whether it fires at all, whether it makes a sound,
// raises the application's own flyout or a real Windows toast, which file it plays and how
// loudly. Master volume sits above them.
class SoundsPage : public Page {
public:
    explicit SoundsPage(PageContext context);

protected:
    void build(StackPanel& column) override;

private:
    // The rows an event's master switch enables and disables, kept so that flipping it does
    // not have to rebuild the page and collapse the expander the user is working in.
    struct EventRows {
        Expander* expander = nullptr;
        SettingsCard* sound = nullptr;
        SettingsCard* file = nullptr;
        SettingsCard* volume = nullptr;
        SettingsCard* flyout = nullptr;
        SettingsCard* toast = nullptr;
    };

    void addEvent(StackPanel& column, NotificationEvent event);
    void setEventEnabled(EventRows const& rows, bool enabled);
    void chooseFile(NotificationEvent event);
    void writeEvent(NotificationEvent event, EventSettings const& value);

    std::array<EventRows, kNotificationEventCount> m_rows{};
};

}  // namespace peek::ui
