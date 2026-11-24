// Application state machine, input handling and screen layout.
//
//   GameSelect --A--> (load + verified backup) --> BoxView
//   BoxView:  A pick up / place (swap)   B cancel     L/R change box
//             SELECT toggle Save/Bank    START exit (-> ConfirmExit if anything changed)
//   ConfirmExit: A save all + exit   X exit without saving   B back
//   Message: modal text, A/B returns to the previous state
//   Credits (X on GameSelect): attributions required by the GPLv3 7b terms, B returns
#pragma once

#include "core/db_bank.hpp"
#include "core/db_save.hpp"
#include "core/db_storage.hpp"
#include "ui/db_gfx.hpp"
#include "ui/db_icons.hpp"
#include "ui/db_summary.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace db
{
    class App
    {
    public:
        bool init();
        void run();
        void fini(); // releases everything init() acquired, in reverse order

    private:
        enum class State
        {
            GameSelect,
            BoxView,
            ConfirmExit,
            Message,
            Credits,
        };

        // ---- actions
        void scan();
        void openSelected();
        bool saveAll(std::string& err);
        void showMessage(const std::string& text, bool error);
        void setStatus(const std::string& text, u32 color);
        void progressFrame(const std::string& text);

        // ---- input
        void inputGameSelect(u32 down, u32 repeat);
        void inputBoxView(u32 down, u32 repeat, const touchPosition& touch);
        void inputConfirmExit(u32 down);
        void inputMessage(u32 down);
        void pressA();
        void changeBox(int delta);
        void togglePane();

        // ---- render
        void render();
        void renderGameList();
        void renderSummary();
        void renderGrid();
        void renderStatusBar();
        void renderConfirmExit();
        void renderMessage();
        void renderCredits();

        [[nodiscard]] int paneIdx() const { return pane == Pane::Save ? 0 : 1; }
        [[nodiscard]] SlotRef cursorRef() const;
        void invalidate();
        void refreshCaches();

        ui::Gfx gfx;
        ui::IconCache icons; // must be cleared before gfx.fini()
        Bank bank;
        std::unique_ptr<SaveSource> save;
        std::unique_ptr<Storage> storage;

        std::vector<SaveLocation> saves;
        int listCursor = 0;

        State state        = State::GameSelect;
        State messageReturn = State::GameSelect;
        std::string message;
        bool messageError = false;

        Pane pane     = Pane::Bank;
        int boxIdx[2] = {0, 0};
        int cursor[2] = {0, 0};
        std::optional<SlotRef> held;

        std::string status;
        u32 statusColor = ui::col::DIM;
        bool quit       = false;

        // Display caches: parsing 30 Pokemon every frame is wasteful on Old 3DS.
        bool cellsValid = false;
        std::array<ui::CellInfo, BOX_SLOTS> cells;
        bool summaryValid = false;
        SlotRef summaryRef;
        ui::Summary summary;

        // Service bookkeeping for fini().
        bool romfsUp = false, amUp = false, i18nUp = false;
    };
}
