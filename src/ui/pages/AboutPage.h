#pragma once

#include "ui/pages/Page.h"

namespace peek::ui {

// Version, what the application actually does, and the two places worth opening: the folder
// its data lives in and the repository it came from.
class AboutPage : public Page {
public:
    explicit AboutPage(PageContext context);

protected:
    void build(StackPanel& column) override;
};

}  // namespace peek::ui
