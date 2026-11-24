// Devious Bank - Pokemon storage for Gen 1-7 on Nintendo 3DS.
// Licensed under GPLv3 (see LICENSE). Uses PKSM-Core by FlagBrew (GPLv3 + 7b/7c terms).

#include "ui/db_app.hpp"

int main()
{
    db::App app;
    if (app.init())
    {
        app.run();
    }
    // Always runs, including after a failed init: frees GPU resources and services,
    // then returns to the Homebrew Launcher.
    app.fini();
    return 0;
}
