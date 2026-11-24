// Application state machine, input handling and screen layout.
//
//   GameSelect --A--> (load + verified backup) --> BoxView
//   BoxView:
//     D-pad / Circle Pad  move              A      pick up / place (swaps if occupied)
//     Y (hold) + move     select an area    A      on a selection: pick up the group
//     stylus drag         select an area,   or carry what you are holding, drop on release
//     X                   send the selection / held group / Pokemon under the cursor to
//                         the other side (Save -> Bank, Bank -> Save) into free slots
//     B                   cancel / clear selection     L / R   previous / next box
//     SELECT              switch Save <-> Bank         START   menu
//   Menu: back to the game list (save first or discard), save & exit, exit without
//         saving, button guide, back
//   Help: the button guide (shown automatically the first time a box opens)
//   Message: modal text, A/B returns to the previous state
//   Credits (X on GameSelect): attributions required by the GPLv3 7b terms
//
// Cursor color shows the mode: yellow pointing, cyan selecting, green holding one
// Pokemon, pink holding a group.
#pragma once

#include "core/db_bank.hpp"
#include "core/db_save.hpp"
#include "core/db_storage.hpp"
#include "ui/db_gameart.hpp"
#include "ui/db_gfx.hpp"
#include "ui/db_icons.hpp"
#include "ui/db_snow.hpp"
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
            Menu,
            Help,
            Message,
            Credits,
        };

        // A rectangle of slots in one box, made with Y+move or a stylus drag.
        struct Selection
        {
            Pane pane = Pane::Bank;
            int box   = 0;
            std::array<bool, BOX_SLOTS> marked{};
            [[nodiscard]] int count() const;
        };

        // Several Pokemon picked up together. Keeps their layout so the group lands in
        // the same shape, with the cursor at the group's top-left corner.
        struct Group
        {
            Pane pane = Pane::Bank;
            int box   = 0;
            std::vector<int> slots;    // slot indices in (pane, box)
            std::vector<int> iconKeys; // for drawing the carried icons
            int minRow = 0, minCol = 0;
        };

        enum class MenuAction
        {
            SaveReturn,   // save, then back to the game list
            ReturnNoSave, // discard changes, back to the game list
            Return,       // nothing to save: back to the game list
            SaveExit,
            ExitNoSave,
            Exit,
            Help,
            Back,
        };
        struct MenuItem
        {
            std::string label;
            u32 color;
            MenuAction action;
        };

        // ---- actions
        void scan();
        void openSelected();
        bool saveAll(std::string& err);
        void showMessage(const std::string& text, bool error);
        void setStatus(const std::string& text, u32 color);
        void progressFrame(const std::string& text);
        void openMenu();
        void runMenuAction(MenuAction a);
        // Closes the open save and shows the game list. `discard` also throws away unsaved
        // bank changes by reloading the bank file.
        void returnToGameList(bool discard);

        // ---- input
        void inputGameSelect(u32 down, u32 repeat);
        void inputBoxView(u32 down, u32 repeat, u32 keys, const touchPosition& touch);
        void inputMenu(u32 down, u32 repeat);
        void inputMessage(u32 down);
        void pressA();
        void pickUpSelection();
        void placeGroup();
        void sendToOtherSide();
        void cancel();
        void changeBox(int delta);
        void togglePane();
        void setSelectionRect(int a, int b);
        void clearSelection();

        // ---- render
        void render();
        void renderGameListTop();
        void renderGameListBottom();
        void renderSummary();
        void renderGrid();
        void renderStatusBar();
        void renderMenu();
        void renderHelp();
        void renderHelpLegend();
        void renderMessage();
        void renderCredits();
        // Draws a button badge (A/B/X/Y circle, or a pill for L/R/START/SELECT/D-pad).
        // Returns its width.
        float drawButton(float x, float y, const char* name);

        [[nodiscard]] int paneIdx() const { return pane == Pane::Save ? 0 : 1; }
        [[nodiscard]] SlotRef cursorRef() const;
        [[nodiscard]] bool carrying() const { return held.has_value() || group.has_value(); }
        [[nodiscard]] u32 modeColor() const;
        [[nodiscard]] bool selectionHere() const;
        void invalidate();
        void refreshCaches();

        ui::Gfx gfx;
        ui::IconCache icons;   // must be cleared before gfx.fini()
        ui::GameArtCache gameArt; // game-list tile icons; same rule
        ui::Snow snow;
        Bank bank;
        std::unique_ptr<SaveSource> save;
        std::unique_ptr<Storage> storage;

        std::vector<SaveLocation> saves;
        int listCursor = 0; // 0 = bank only, i = saves[i - 1]

        State state         = State::GameSelect;
        State messageReturn = State::GameSelect;
        State helpReturn    = State::BoxView;
        std::string message;
        bool messageError = false;
        bool helpShown    = false; // the guide pops up once, the first time a box opens

        std::vector<MenuItem> menu;
        std::string menuTitle = "Menu";
        int menuCursor        = 0;

        Pane pane     = Pane::Bank;
        int boxIdx[2] = {0, 0};
        int cursor[2] = {0, 0};

        std::optional<SlotRef> held; // one Pokemon being carried
        int heldIcon = ui::ICON_NONE;
        std::optional<Group> group;  // several Pokemon being carried
        std::optional<Selection> sel;

        // Y + move selection
        bool ySelecting = false;
        int selAnchor   = 0;
        // Stylus: where the touch started (-1 = none) and whether it has dragged.
        int touchStart    = -1;
        bool touchDragged = false;

        std::string status;
        u32 statusColor = ui::col::DIM;
        u32 statusFrame = 0; // when the status was set; it fades back to the hint
        bool quit       = false;
        u32 frame       = 0; // animation clock

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
