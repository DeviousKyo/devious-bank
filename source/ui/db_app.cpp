#include "ui/db_app.hpp"
#include "core/db_fs.hpp"

#include "utils/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace db
{
    using namespace ui;

    namespace
    {
        // ---- Bottom screen box grid (320x240): 6x5 equal tiles in a centered tray.
        constexpr int HEADER_H = 28;
        constexpr int TILE_W   = 40;
        constexpr int TILE_H   = 34;
        constexpr int GAP      = 6;
        constexpr int PITCH_X  = TILE_W + GAP;
        constexpr int PITCH_Y  = TILE_H + GAP;
        constexpr int GRID_W   = BOX_COLS * TILE_W + (BOX_COLS - 1) * GAP; // 270
        constexpr int GRID_H   = BOX_ROWS * TILE_H + (BOX_ROWS - 1) * GAP; // 194
        constexpr int GRID_X   = (BOTTOM_W - GRID_W) / 2;                  // 25: equal side margins
        constexpr int GRID_Y   = 36;
        constexpr float ICON_SCALE  = 0.60f; // 64x56 -> 38x34
        constexpr float RAISE_SCALE = 0.66f; // hovered / carried sprite, slightly larger

        int tileX(int i) { return GRID_X + (i % BOX_COLS) * PITCH_X; }
        int tileY(int i) { return GRID_Y + (i / BOX_COLS) * PITCH_Y; }

        // Touch -> slot index. Gaps belong to the nearest tile so the stylus never misses.
        int cellAt(const touchPosition& t)
        {
            const int rx = t.px - GRID_X + GAP / 2;
            const int ry = t.py - GRID_Y + GAP / 2;
            if (rx < 0 || ry < 0)
            {
                return -1;
            }
            const int c = rx / PITCH_X, r = ry / PITCH_Y;
            return (c < BOX_COLS && r < BOX_ROWS) ? r * BOX_COLS + c : -1;
        }

        // ---- Top screen
        constexpr int TOP_HEADER_H = 20;
        constexpr int STATUS_Y     = 222;
        constexpr u32 STATUS_FRAMES = 180; // a status message shows for ~3 s, then the hint

        // ---- Game select tiles: 4 columns x 2 rows of box-art covers on the top screen.
        // Each tile is an 84x84 cover with a 12px source label (DS Cart, Digital, ...) below.
        constexpr int CARD_COLS  = 4;
        constexpr int CARD_ROWS  = 2;
        constexpr int COVER      = 84;
        constexpr int LABEL_H    = 12;
        constexpr int CARD_W     = COVER;
        constexpr int CARD_H     = COVER + LABEL_H;
        constexpr int CARD_GX    = 12;
        constexpr int CARD_GY    = 10;
        constexpr int CARD_X0    = (TOP_W - CARD_COLS * CARD_W - (CARD_COLS - 1) * CARD_GX) / 2;
        constexpr int CARD_Y0    = 32;

        std::vector<std::string> wrap(const std::string& text, size_t width)
        {
            std::vector<std::string> out;
            std::string line, word;
            auto flushWord = [&]()
            {
                if (word.empty()) return;
                if (!line.empty() && line.size() + 1 + word.size() > width)
                {
                    out.push_back(line);
                    line.clear();
                }
                line += (line.empty() ? "" : " ") + word;
                word.clear();
            };
            for (char c : text)
            {
                if (c == ' ') flushWord();
                else if (c == '\n')
                {
                    flushWord();
                    out.push_back(line);
                    line.clear();
                }
                else word += c;
            }
            flushWord();
            if (!line.empty()) out.push_back(line);
            return out;
        }

        const char* paneName(Pane p) { return p == Pane::Save ? "Save" : "Bank"; }
    }

    int App::Selection::count() const
    {
        return (int)std::count(marked.begin(), marked.end(), true);
    }

    // =====================================================================================
    // Lifecycle
    // =====================================================================================

    bool App::init()
    {
        if (R_FAILED(romfsInit()))
        {
            return false;
        }
        romfsUp = true;
        if (R_SUCCEEDED(amInit()))
        {
            amUp = true; // without AM, only SD .sav files are listed
        }
        dscart::start(); // SPI for DS carts; if it fails, DS carts just aren't listed
        if (!gfx.init())
        {
            return false;
        }
        hidSetRepeatParameters(18, 5);

        progressFrame("Loading...");
        try
        {
            i18n::init(pksm::Language::ENG);
            i18nUp = true;
        }
        catch (...)
        {
            return false;
        }

        fs::mkdirs(fs::BACKUP_DIR);
        fs::mkdirs(fs::SAVES_DIR);

        std::string err;
        bank.load(fs::BANK_PATH, err);
        storage = std::make_unique<Storage>(bank, nullptr);

        scan();
        if (!err.empty())
        {
            showMessage(err, bank.readOnly());
        }
        return true;
    }

    void App::fini()
    {
        // Drop save/bank objects first (no file handles are held open between operations;
        // every archive/file handle is scoped to a single read or write).
        storage.reset();
        save.reset();
        icons.clear();  // icon textures, before citro3d goes away
        covers.clear(); // box art textures, same
        gfx.fini();     // text buffer, render targets, citro2d, citro3d, gfx
        if (i18nUp)
        {
            i18n::exit();
            i18nUp = false;
        }
        dscart::stop();
        if (amUp)
        {
            amExit();
            amUp = false;
        }
        if (romfsUp)
        {
            romfsExit();
            romfsUp = false;
        }
    }

    void App::run()
    {
        // aptMainLoop() returns false when the user closes the app from the HOME menu;
        // in that case nothing is written (unsaved moves are discarded, files untouched).
        while (!quit && aptMainLoop())
        {
            hidScanInput();
            const u32 down   = hidKeysDown();
            const u32 repeat = hidKeysDownRepeat();
            const u32 keys   = hidKeysHeld();
            touchPosition touch;
            hidTouchRead(&touch);

            switch (state)
            {
                case State::GameSelect: inputGameSelect(down, repeat); break;
                case State::BoxView:    inputBoxView(down, repeat, keys, touch); break;
                case State::Menu:       inputMenu(down, repeat); break;
                case State::Message:    inputMessage(down); break;
                case State::Help:
                    if (down & (KEY_A | KEY_B | KEY_START)) state = helpReturn;
                    break;
                case State::Credits:
                    if (down & (KEY_B | KEY_A | KEY_X)) state = State::GameSelect;
                    break;
            }
            if (!quit)
            {
                render();
            }
        }
    }

    // =====================================================================================
    // Actions
    // =====================================================================================

    void App::scan()
    {
        progressFrame("Looking for Pokemon saves...");
        // If AM failed to start, the title scan finds nothing and only SD saves are listed.
        saves      = scanSaves();
        listCursor = 0;
    }

    void App::openSelected()
    {
        storage.reset();
        save.reset();
        held.reset();
        group.reset();
        sel.reset();
        ySelecting = false;
        touchStart = -1;
        covers.clear(); // box art isn't shown in the boxes; free it

        if (listCursor > 0)
        {
            const SaveLocation& loc = saves[listCursor - 1];
            progressFrame("Loading and backing up\n" + loc.label);
            std::string err;
            save = SaveSource::open(loc, err);
            if (!save)
            {
                storage = std::make_unique<Storage>(bank, nullptr);
                showMessage(err, true);
                return;
            }
        }

        storage   = std::make_unique<Storage>(bank, save.get());
        pane      = save ? Pane::Save : Pane::Bank;
        boxIdx[0] = boxIdx[1] = 0;
        cursor[0] = cursor[1] = 0;
        state     = State::BoxView;
        invalidate();

        if (save && !save->readOnly())
        {
            setStatus("Backup: " + save->backupPath(), col::GOOD);
        }
        // First box of the session: show the button guide so nobody starts out lost.
        if (!helpShown)
        {
            helpShown  = true;
            helpReturn = State::BoxView;
            state      = State::Help;
        }
        if (save && save->readOnly())
        {
            showMessage(save->readOnlyReason(), true); // returns to the guide, then the box
        }
    }

    bool App::saveAll(std::string& err)
    {
        // HOME is blocked while writing so the app cannot be suspended mid-write.
        aptSetHomeAllowed(false);
        progressFrame("Saving... do not power off");

        // Bank first: its writes are atomic, and a deposited Pokemon then exists in the
        // bank before it is removed from the game save.
        bool ok = true;
        if (bank.dirty() && !bank.save(err))
        {
            ok = false;
        }
        if (ok && save && save->dirty() && !save->commit(err))
        {
            ok = false;
        }
        aptSetHomeAllowed(true);
        invalidate();
        return ok;
    }

    void App::showMessage(const std::string& text, bool error)
    {
        message       = text;
        messageError  = error;
        messageReturn = state == State::Message ? messageReturn : state;
        state         = State::Message;
    }

    void App::setStatus(const std::string& text, u32 color)
    {
        status      = text;
        statusColor = color;
        statusFrame = frame;
    }

    void App::progressFrame(const std::string& text)
    {
        gfx.beginFrame();
        gfx.drawTop();
        snow.drawTop(gfx);
        int y = 100;
        for (const auto& line : wrap(text, 48))
        {
            gfx.text(TOP_W / 2, y, 0.6f, col::TEXT, line, Align::Center, TOP_W - 20);
            y += 20;
        }
        gfx.drawBottom();
        snow.drawBottom(gfx);
        gfx.endFrame();
    }

    void App::openMenu()
    {
        menu.clear();
        if (storage && storage->dirty())
        {
            menu.push_back({"Save, then go to game list", col::GOOD, MenuAction::SaveReturn});
            menu.push_back({"Save everything and exit", col::GOOD, MenuAction::SaveExit});
            menu.push_back({"Game list WITHOUT saving", col::WARN, MenuAction::ReturnNoSave});
            menu.push_back({"Exit WITHOUT saving", col::WARN, MenuAction::ExitNoSave});
        }
        else
        {
            menu.push_back({"Back to game list", col::TEXT, MenuAction::Return});
            menu.push_back({"Exit", col::TEXT, MenuAction::Exit});
        }
        menu.push_back({"Button guide", col::TEXT, MenuAction::Help});
        menu.push_back({"Back", col::DIM, MenuAction::Back});
        menuCursor = 0;
        state      = State::Menu;
    }

    void App::runMenuAction(MenuAction a)
    {
        switch (a)
        {
            case MenuAction::SaveReturn:
            {
                std::string err;
                if (saveAll(err))
                {
                    returnToGameList(false);
                }
                else
                {
                    state = State::BoxView;
                    showMessage("Save failed: " + err +
                                    "\nNothing was lost: your moves are still here and the "
                                    "backups are in sdmc:/3ds/DB/backups/",
                        true);
                }
                break;
            }
            case MenuAction::ReturnNoSave:
                returnToGameList(true);
                break;
            case MenuAction::Return:
                returnToGameList(false);
                break;
            case MenuAction::SaveExit:
            {
                std::string err;
                if (saveAll(err))
                {
                    quit = true;
                }
                else
                {
                    state = State::BoxView;
                    showMessage("Save failed: " + err +
                                    "\nNothing was lost: your moves are still here and the "
                                    "backups are in sdmc:/3ds/DB/backups/",
                        true);
                }
                break;
            }
            case MenuAction::ExitNoSave:
            case MenuAction::Exit:
                quit = true; // nothing written
                break;
            case MenuAction::Help:
                helpReturn = State::BoxView;
                state      = State::Help;
                break;
            case MenuAction::Back:
                state = State::BoxView;
                break;
        }
    }

    void App::returnToGameList(bool discard)
    {
        held.reset();
        group.reset();
        clearSelection();
        touchStart = -1;
        storage.reset();
        save.reset(); // the game save's unsaved moves go with it; the file is untouched
        icons.clear();

        if (discard && bank.dirty())
        {
            // Throw away unsaved bank moves by reloading the file as it is on the SD card.
            std::string err;
            bank.load(fs::BANK_PATH, err);
            if (!err.empty())
            {
                showMessage(err, bank.readOnly());
            }
        }
        storage = std::make_unique<Storage>(bank, nullptr);
        status.clear();
        state = State::GameSelect;
        scan(); // the cart in the slot may have changed while the boxes were open
    }

    // =====================================================================================
    // Input
    // =====================================================================================

    void App::inputGameSelect(u32 down, u32 repeat)
    {
        const int entries = (int)saves.size() + 1; // entry 0 = bank only
        int row = listCursor / CARD_COLS, colIdx = listCursor % CARD_COLS;
        const int rows = (entries + CARD_COLS - 1) / CARD_COLS;

        if (repeat & KEY_DOWN) row = std::min(rows - 1, row + 1);
        if (repeat & KEY_UP) row = std::max(0, row - 1);
        if (repeat & KEY_RIGHT) colIdx = std::min(CARD_COLS - 1, colIdx + 1);
        if (repeat & KEY_LEFT) colIdx = std::max(0, colIdx - 1);
        listCursor = std::min(entries - 1, row * CARD_COLS + colIdx);

        if (down & KEY_A) openSelected();
        if (down & KEY_Y) scan();
        if (down & KEY_X) state = State::Credits;
        if (down & KEY_START) quit = true;
    }

    void App::inputBoxView(u32 down, u32 repeat, u32 keys, const touchPosition& touch)
    {
        int& cur = cursor[paneIdx()];
        int row = cur / BOX_COLS, colIdx = cur % BOX_COLS;
        const int before = cur;

        if (repeat & KEY_UP) row = (row + BOX_ROWS - 1) % BOX_ROWS;
        if (repeat & KEY_DOWN) row = (row + 1) % BOX_ROWS;
        if (repeat & KEY_LEFT) colIdx = (colIdx + BOX_COLS - 1) % BOX_COLS;
        if (repeat & KEY_RIGHT) colIdx = (colIdx + 1) % BOX_COLS;
        cur = row * BOX_COLS + colIdx;
        const bool moved = cur != before;
        if (moved) summaryValid = false;

        // ---- Y + move: drag-select a rectangle
        if (down & KEY_Y)
        {
            if (carrying())
            {
                setStatus("Put down what you're holding first (A to place, B to cancel)",
                    col::WARN);
            }
            else
            {
                ySelecting = true;
                selAnchor  = cur;
                setSelectionRect(cur, cur);
            }
        }
        if (ySelecting)
        {
            if (keys & KEY_Y)
            {
                if (moved) setSelectionRect(selAnchor, cur);
            }
            else
            {
                ySelecting = false; // released: the selection stays until used or cleared
            }
        }

        if (repeat & (KEY_L | KEY_ZL)) changeBox(-1);
        if (repeat & (KEY_R | KEY_ZR)) changeBox(+1);
        if (down & KEY_SELECT) togglePane();
        if (down & KEY_A) pressA();
        if (down & KEY_B) cancel();
        if (down & KEY_X) sendToOtherSide();
        if (down & KEY_START)
        {
            openMenu();
            return;
        }

        // ---- Stylus: tap = A, drag = select (or carry, then drop on release)
        if (down & KEY_TOUCH)
        {
            if (touch.py < HEADER_H)
            {
                if (touch.px < 44) changeBox(-1);
                else if (touch.px > BOTTOM_W - 44) changeBox(+1);
                else togglePane();
            }
            else if (int c = cellAt(touch); c >= 0)
            {
                touchStart   = c;
                touchDragged = false;
                cur          = c;
                summaryValid = false;
            }
        }
        else if ((keys & KEY_TOUCH) && touchStart >= 0)
        {
            const int c = cellAt(touch);
            if (c >= 0 && c != cur)
            {
                cur          = c;
                summaryValid = false;
                touchDragged = true;
                if (!carrying())
                {
                    setSelectionRect(touchStart, c);
                }
            }
        }
        else if (!(keys & KEY_TOUCH) && touchStart >= 0)
        {
            // Released. Carrying: drop here. A plain tap: same as A. A drag that made a
            // selection: keep it, so A / X / another tap can act on it.
            if (carrying() || !touchDragged)
            {
                pressA();
            }
            touchStart   = -1;
            touchDragged = false;
        }
    }

    void App::inputMenu(u32 down, u32 repeat)
    {
        const int n = (int)menu.size();
        if (repeat & KEY_DOWN) menuCursor = (menuCursor + 1) % n;
        if (repeat & KEY_UP) menuCursor = (menuCursor + n - 1) % n;
        if (down & KEY_A) runMenuAction(menu[menuCursor].action);
        else if (down & (KEY_B | KEY_START)) state = State::BoxView;
    }

    void App::inputMessage(u32 down)
    {
        if (down & (KEY_A | KEY_B))
        {
            state = messageReturn;
        }
    }

    void App::pressA()
    {
        if (group)
        {
            placeGroup();
            return;
        }

        const SlotRef here = cursorRef();
        if (held)
        {
            std::string why;
            if (storage->swap(*held, here, why))
            {
                held.reset();
                invalidate();
                setStatus("Moved (not saved yet)", col::GOOD);
            }
            else
            {
                setStatus(why, col::WARN);
            }
            return;
        }

        if (selectionHere() && sel->marked[here.slot])
        {
            pickUpSelection();
            return;
        }
        clearSelection(); // pressing outside a selection drops it

        const Slot& s = storage->at(here);
        if (s.empty())
        {
            return;
        }
        if (!s.movable())
        {
            setStatus("This slot is damaged/locked and can't be moved", col::WARN);
            return;
        }
        held     = here;
        heldIcon = cells[here.slot].iconKey;
        invalidate();
        setStatus("Holding - A to place, X to send, B to put back", col::MODE_HOLD);
    }

    void App::pickUpSelection()
    {
        Group g;
        g.pane   = pane;
        g.box    = boxIdx[paneIdx()];
        g.minRow = BOX_ROWS;
        g.minCol = BOX_COLS;
        for (int i = 0; i < BOX_SLOTS; i++)
        {
            if (!sel->marked[i])
            {
                continue;
            }
            const Slot& s = storage->box(g.pane, g.box).slots[i];
            if (s.empty())
            {
                continue;
            }
            if (!s.movable())
            {
                setStatus("The selection has a damaged/locked Pokemon", col::WARN);
                return;
            }
            g.slots.push_back(i);
            g.iconKeys.push_back(cells[i].iconKey);
            g.minRow = std::min(g.minRow, i / BOX_COLS);
            g.minCol = std::min(g.minCol, i % BOX_COLS);
        }
        clearSelection();
        if (g.slots.empty())
        {
            setStatus("Nothing to pick up in that selection", col::DIM);
            return;
        }
        // Single Pokemon in the selection: behave exactly like a normal pick-up.
        if (g.slots.size() == 1)
        {
            held     = SlotRef{g.pane, g.box, g.slots[0]};
            heldIcon = g.iconKeys[0];
            invalidate();
            setStatus("Holding - A to place, X to send, B to put back", col::MODE_HOLD);
            return;
        }
        const size_t n = g.slots.size();
        group          = std::move(g);
        invalidate();
        setStatus("Holding " + std::to_string(n) +
                      " - cursor is the group's top-left. A place, X send, B put back",
            col::MODE_GROUP);
    }

    void App::placeGroup()
    {
        const int cur = cursor[paneIdx()];
        const int cr = cur / BOX_COLS, cc = cur % BOX_COLS;
        std::vector<SlotRef> from, to;
        bool same = true;
        for (int s : group->slots)
        {
            const int r = s / BOX_COLS - group->minRow + cr;
            const int c = s % BOX_COLS - group->minCol + cc;
            if (r >= BOX_ROWS || c >= BOX_COLS)
            {
                setStatus("The group doesn't fit here - move the cursor up or left", col::WARN);
                return;
            }
            from.push_back(SlotRef{group->pane, group->box, s});
            to.push_back(SlotRef{pane, boxIdx[paneIdx()], r * BOX_COLS + c});
            same = same && from.back() == to.back();
        }
        if (same)
        {
            group.reset(); // put back exactly where it was: nothing changes
            invalidate();
            setStatus("Put back", col::DIM);
            return;
        }
        std::string why;
        if (storage->moveGroup(from, to, why))
        {
            setStatus("Moved " + std::to_string(from.size()) + " (not saved yet)", col::GOOD);
            group.reset();
            invalidate();
        }
        else
        {
            setStatus(why, col::WARN);
        }
    }

    void App::sendToOtherSide()
    {
        std::vector<SlotRef> from;
        if (group)
        {
            for (int s : group->slots) from.push_back(SlotRef{group->pane, group->box, s});
        }
        else if (held)
        {
            from.push_back(*held);
        }
        else if (selectionHere())
        {
            for (int i = 0; i < BOX_SLOTS; i++)
            {
                if (sel->marked[i] && !storage->box(sel->pane, sel->box).slots[i].empty())
                {
                    from.push_back(SlotRef{sel->pane, sel->box, i});
                }
            }
        }
        else if (!storage->at(cursorRef()).empty())
        {
            from.push_back(cursorRef());
        }
        if (from.empty())
        {
            setStatus("Nothing to send", col::DIM);
            return;
        }

        const Pane src  = from[0].pane;
        const Pane dest = src == Pane::Save ? Pane::Bank : Pane::Save;
        if (!storage->hasPane(dest))
        {
            setStatus("Bank only - no game save loaded to send to", col::WARN);
            return;
        }
        const int destIdx = dest == Pane::Save ? 0 : 1;
        auto targets      = storage->findEmpty(dest, (int)from.size(), boxIdx[destIdx]);
        if (targets.size() < from.size())
        {
            setStatus(std::string("Not enough room in the ") + paneName(dest) + " (needs " +
                          std::to_string(from.size()) + " free slots)",
                col::WARN);
            return;
        }
        std::string why;
        if (storage->moveGroup(from, targets, why))
        {
            setStatus("Sent " + std::to_string(from.size()) + " to the " + paneName(dest) +
                          ", from box " + std::to_string(targets[0].box + 1) +
                          " (not saved yet)",
                col::GOOD);
            held.reset();
            group.reset();
            clearSelection();
            invalidate();
        }
        else
        {
            setStatus(why, col::WARN);
        }
    }

    void App::cancel()
    {
        if (group || held)
        {
            group.reset();
            held.reset();
            invalidate();
            setStatus("Put back", col::DIM);
        }
        else if (sel)
        {
            clearSelection();
            setStatus("Selection cleared", col::DIM);
        }
    }

    void App::changeBox(int delta)
    {
        const int n = storage->boxCount(pane);
        if (n <= 0) return;
        boxIdx[paneIdx()] = (boxIdx[paneIdx()] + delta + n) % n;
        clearSelection(); // a selection belongs to one box; a carried group comes along
        invalidate();
    }

    void App::togglePane()
    {
        if (!save)
        {
            setStatus("Bank only - no game save loaded", col::DIM);
            return;
        }
        pane = pane == Pane::Save ? Pane::Bank : Pane::Save;
        clearSelection();
        invalidate();
    }

    void App::setSelectionRect(int a, int b)
    {
        Selection s;
        s.pane = pane;
        s.box  = boxIdx[paneIdx()];
        const int r0 = std::min(a / BOX_COLS, b / BOX_COLS), r1 = std::max(a / BOX_COLS, b / BOX_COLS);
        const int c0 = std::min(a % BOX_COLS, b % BOX_COLS), c1 = std::max(a % BOX_COLS, b % BOX_COLS);
        for (int r = r0; r <= r1; r++)
        {
            for (int c = c0; c <= c1; c++)
            {
                s.marked[r * BOX_COLS + c] = true;
            }
        }
        sel = s;
    }

    void App::clearSelection()
    {
        sel.reset();
        ySelecting = false;
    }

    // =====================================================================================
    // Render helpers
    // =====================================================================================

    SlotRef App::cursorRef() const
    {
        return SlotRef{pane, boxIdx[paneIdx()], cursor[paneIdx()]};
    }

    bool App::selectionHere() const
    {
        return sel && sel->pane == pane && sel->box == boxIdx[paneIdx()];
    }

    u32 App::modeColor() const
    {
        if (group) return col::MODE_GROUP;
        if (held) return col::MODE_HOLD;
        if (ySelecting || selectionHere()) return col::MODE_SELECT;
        return col::MODE_POINT;
    }

    void App::invalidate()
    {
        cellsValid   = false;
        summaryValid = false;
    }

    void App::refreshCaches()
    {
        if (!storage || storage->boxCount(pane) == 0)
        {
            return;
        }
        bool changed = false;
        if (!cellsValid)
        {
            Box& b = storage->box(pane, boxIdx[paneIdx()]);
            for (int i = 0; i < BOX_SLOTS; i++)
            {
                cells[i] = buildCell(b.slots[i]);
            }
            cellsValid = true;
            changed    = true;
        }
        const SlotRef ref = cursorRef();
        if (!summaryValid || !(summaryRef == ref))
        {
            summary      = buildSummary(storage->at(ref));
            summaryRef   = ref;
            summaryValid = true;
            changed      = true;
        }
        if (changed)
        {
            // Keep only what's on screen in memory: this box, the summary, and anything
            // being carried in from another box.
            std::unordered_set<int> keep;
            for (const CellInfo& c : cells) keep.insert(c.iconKey);
            keep.insert(summary.iconKey);
            if (held) keep.insert(heldIcon);
            if (group) keep.insert(group->iconKeys.begin(), group->iconKeys.end());
            icons.retainOnly(keep);
        }
    }

    float App::drawButton(float x, float y, const char* name)
    {
        // Face buttons are circles in the classic four colors; everything else is a pill.
        const char n = name[0];
        if (name[1] == '\0' && (n == 'A' || n == 'B' || n == 'X' || n == 'Y'))
        {
            const u32 c = n == 'A' ? rgba(0xE0, 0x5A, 0x5A)
                        : n == 'B' ? rgba(0xD9, 0xA9, 0x3A)
                        : n == 'X' ? rgba(0x4F, 0x8F, 0xD9)
                                   : rgba(0x4F, 0xB3, 0x6B);
            gfx.circle(x + 8, y + 8, 8, c);
            gfx.text(x + 8, y + 1.5f, 0.42f, col::TEXT, name, Align::Center);
            return 16;
        }
        const float w = 8 + 6.5f * std::strlen(name);
        gfx.rect(x, y, w, 16, rgba(0x5A, 0x60, 0x78));
        gfx.text(x + w / 2, y + 2, 0.36f, col::TEXT, name, Align::Center, w - 4);
        return w;
    }

    // =====================================================================================
    // Render
    // =====================================================================================

    void App::render()
    {
        frame++;
        const bool listScreens =
            state == State::GameSelect || state == State::Credits ||
            (state == State::Message &&
                (messageReturn == State::GameSelect || messageReturn == State::Credits));

        if (listScreens)
        {
            snow.update();
        }
        else
        {
            refreshCaches();
        }

        gfx.beginFrame();

        gfx.drawTop();
        if (listScreens)
        {
            snow.drawTop(gfx);
            if (state == State::Credits) renderCredits();
            else renderGameListTop();
        }
        else
        {
            renderSummary();
            renderStatusBar();
        }
        if (state == State::Menu) renderMenu();
        if (state == State::Help) renderHelp();
        if (state == State::Message) renderMessage();

        gfx.drawBottom();
        if (listScreens)
        {
            snow.drawBottom(gfx);
            renderGameListBottom();
        }
        else
        {
            renderGrid();
            if (state == State::Help) renderHelpLegend();
        }

        gfx.endFrame();
    }

    // ---- Game select -------------------------------------------------------------------

    void App::renderGameListTop()
    {
        gfx.text(14, 8, 0.7f, rgba(0, 0, 0, 0x80), "Devious Bank"); // drop shadow
        gfx.text(12, 6, 0.7f, col::TEXT, "Devious Bank");
        gfx.text(TOP_W - 12, 12, 0.42f, col::DIM,
            std::to_string(saves.size()) + (saves.size() == 1 ? " save found" : " saves found"),
            Align::Right);

        const int entries  = (int)saves.size() + 1;
        const int rows     = (entries + CARD_COLS - 1) / CARD_COLS;
        const int curRow   = listCursor / CARD_COLS;
        const int firstRow = std::clamp(curRow - 1, 0, std::max(0, rows - CARD_ROWS));
        std::unordered_set<std::string> keep;

        for (int r = 0; r < CARD_ROWS; r++)
        {
            for (int c = 0; c < CARD_COLS; c++)
            {
                const int idx = (firstRow + r) * CARD_COLS + c;
                if (idx >= entries) break;

                const bool isSel = idx == listCursor;
                float x = CARD_X0 + c * (CARD_W + CARD_GX);
                float y = CARD_Y0 + r * (CARD_H + CARD_GY);

                // Bank tile: a plain square. Game tiles: the box art.
                const C2D_Image* cover = nullptr;
                std::string label      = "Bank only";
                if (idx > 0)
                {
                    const SaveLocation& s = saves[idx - 1];
                    const std::string key = coverKeyFor(s.version);
                    keep.insert(key);
                    cover = covers.get(key);
                    label = s.tag;
                }

                if (isSel)
                {
                    // Raised tile: lift it, drop a shadow, outline it.
                    gfx.rect(x + 3, y + 4, CARD_W, CARD_H, rgba(0x08, 0x0C, 0x18));
                    y -= 3;
                    gfx.rect(x - 2, y - 2, CARD_W + 4, CARD_H + 4, col::CURSOR);
                }
                gfx.rect(x, y, CARD_W, CARD_H, isSel ? col::CARD_SEL : col::CARD);
                if (cover)
                {
                    gfx.image(*cover, x, y, 1.0f);
                }
                else if (idx > 0)
                {
                    // No box art for this game: fall back to its name.
                    gfx.text(x + COVER / 2, y + COVER / 2 - 8, 0.42f, col::TEXT,
                        saves[idx - 1].game, Align::Center, COVER - 6);
                }
                gfx.text(x + CARD_W / 2, y + COVER, 0.34f, isSel ? col::TEXT : col::DIM, label,
                    Align::Center, CARD_W - 4);
            }
        }
        covers.retainOnly(keep); // only the covers on screen stay in memory

        if (rows > CARD_ROWS)
        {
            gfx.text(TOP_W - 12, 22, 0.36f, col::DIM,
                "rows " + std::to_string(firstRow + 1) + "-" +
                    std::to_string(firstRow + CARD_ROWS) + " of " + std::to_string(rows),
                Align::Right);
        }
    }

    void App::renderGameListBottom()
    {
        const float px = 20, py = 20, pw = BOTTOM_W - 40;
        if (state == State::Credits ||
            (state == State::Message && messageReturn == State::Credits))
        {
            gfx.rect(px, 90, pw, 50, col::CARD);
            const float w = drawButton(BOTTOM_W / 2 - 40, 107, "B");
            gfx.text(BOTTOM_W / 2 - 40 + w + 8, 107, 0.48f, col::TEXT, "Back");
            return;
        }

        gfx.rect(px, py, pw, 118, col::CARD);
        // The tiles only show box art, so name the highlighted one here.
        const std::string selected =
            listCursor == 0 ? "Bank only (no game save)" : saves[listCursor - 1].label;
        gfx.text(BOTTOM_W / 2, py + 8, 0.5f, col::TEXT, selected, Align::Center, pw - 16);

        // 2x2 button guide
        struct Row
        {
            const char* btn;
            const char* what;
        };
        const Row rows[] = {{"A", "Open"}, {"Y", "Rescan"}, {"X", "Credits"}, {"START", "Exit"}};
        for (int i = 0; i < 4; i++)
        {
            const float x = px + 22 + (i % 2) * 136;
            const float y = py + 38 + (i / 2) * 28;
            const float w = drawButton(x, y, rows[i].btn);
            gfx.text(x + w + 8, y, 0.46f, col::TEXT, rows[i].what);
        }
        gfx.text(BOTTOM_W / 2, py + 98, 0.36f, col::DIM, "+Pad: choose a tile", Align::Center);

        gfx.rect(px, 150, pw, 62, col::CARD);
        gfx.text(px + 14, 158, 0.4f, col::TEXT, "Gen 4/5 DS cart: insert it, then press Y");
        gfx.text(px + 14, 176, 0.4f, col::TEXT, "GBA / emulator saves: copy .sav files to");
        gfx.text(px + 14, 192, 0.4f, col::DIM, "sdmc:/3ds/DB/saves/");
    }

    // ---- Box view: top screen ------------------------------------------------------------

    void App::renderSummary()
    {
        gfx.gradient(0, 0, TOP_W, SCREEN_H, col::BG, rgba(0x18, 0x19, 0x22));
        gfx.rect(0, 0, TOP_W, TOP_HEADER_H, col::PANEL);
        gfx.text(8, 2, 0.5f, col::TEXT, save ? save->gameName() : "Bank only", Align::Left, 260);
        if (storage && storage->dirty())
        {
            gfx.text(TOP_W - 8, 3, 0.45f, col::CURSOR, "Unsaved changes", Align::Right);
        }

        if (!summary.present)
        {
            gfx.text(TOP_W / 2, 110, 0.5f, col::DIM, "Empty slot", Align::Center);
            return;
        }

        gfx.text(8, 24, 0.6f, col::TEXT, summary.title, Align::Left, 290);

        // Sprite: 64x56 icon at 1.25x (80x70) on a soft shadow in the top-right corner.
        if (const C2D_Image* img = icons.get(summary.iconKey))
        {
            gfx.ellipse(TOP_W - 78, TOP_HEADER_H + 60, 60, 12, col::SHADOW);
            gfx.image(*img, TOP_W - 88, TOP_HEADER_H + 2, 1.25f);
        }

        // Left column: trainer + origin data
        int y = 48;
        for (const std::string& line : summary.left)
        {
            gfx.text(8, y, 0.42f, col::TEXT, line, Align::Left, 222);
            y += 15;
            if (y > STATUS_Y - 14) break;
        }

        if (!summary.hasStats) return;

        // Right column: stat table in fixed columns, then moves
        static const char* NAMES[6] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};
        const float labelX = 236, colX = 278, colW = 20;
        y = 96; // below the sprite
        for (int i = 0; i < 6; i++)
        {
            gfx.text(colX + i * colW + colW / 2, y, 0.38f, col::DIM, NAMES[i], Align::Center);
        }
        y += 14;
        gfx.text(labelX, y, 0.4f, col::DIM, summary.ivLabel, Align::Left, 40);
        for (int i = 0; i < 6; i++)
        {
            gfx.text(colX + i * colW + colW / 2, y, 0.4f, col::TEXT, std::to_string(summary.iv[i]),
                Align::Center, colW - 1);
        }
        y += 14;
        gfx.text(labelX, y, 0.4f, col::DIM, summary.evLabel, Align::Left, 40);
        for (int i = 0; i < 6; i++)
        {
            gfx.text(colX + i * colW + colW / 2, y, 0.4f, col::TEXT, std::to_string(summary.ev[i]),
                Align::Center, colW - 1);
        }
        y += 22;
        gfx.text(labelX, y, 0.42f, col::DIM, "Moves");
        for (int m = 0; m < 4; m++)
        {
            y += 15;
            gfx.text(labelX + 6, y, 0.42f, col::TEXT, summary.moves[m], Align::Left, 150);
        }
    }

    void App::renderStatusBar()
    {
        gfx.rect(0, STATUS_Y, TOP_W, SCREEN_H - STATUS_Y, col::PANEL);
        gfx.rect(0, STATUS_Y, 4, SCREEN_H - STATUS_Y, modeColor()); // mode color stripe

        if (!status.empty() && frame - statusFrame < STATUS_FRAMES)
        {
            gfx.text(10, STATUS_Y + 3, 0.42f, statusColor, status, Align::Left, TOP_W - 16);
            return;
        }

        // Context hint for the current mode.
        std::string hint;
        if (group)
            hint = "Holding " + std::to_string(group->slots.size()) +
                   ": A place (cursor = top-left)  X send  B put back";
        else if (held)
            hint = "Holding: A place/swap  X send  B put back";
        else if (ySelecting)
            hint = "Keep holding Y and move to grow the selection";
        else if (selectionHere())
            hint = "Selected " + std::to_string(sel->count()) +
                   ": A pick up  X send all  B clear";
        else
            hint = "A pick up  Y+move select  X send  START menu & button guide";
        gfx.text(10, STATUS_Y + 3, 0.4f, col::DIM, hint, Align::Left, TOP_W - 16);
    }

    // ---- Box view: bottom screen ---------------------------------------------------------

    void App::renderGrid()
    {
        gfx.gradient(0, 0, BOTTOM_W, SCREEN_H, col::BG, rgba(0x18, 0x19, 0x22));
        if (!storage || storage->boxCount(pane) == 0)
        {
            return;
        }
        const int bi   = boxIdx[paneIdx()];
        const Box& box = storage->box(pane, bi);
        const int cur  = cursor[paneIdx()];

        // ---- Header: [L] SAVE · box name  n/N [R]
        const u32 hdr = pane == Pane::Save ? col::SAVE_HDR : col::BANK_HDR;
        gfx.rect(0, 0, BOTTOM_W, HEADER_H, hdr);
        drawButton(6, 6, "L");
        drawButton(BOTTOM_W - 6 - 14.5f, 6, "R");
        gfx.rect(30, 5, 44, 18, rgba(0x00, 0x00, 0x00));
        gfx.text(52, 7, 0.42f, col::TEXT, pane == Pane::Save ? "SAVE" : "BANK", Align::Center);
        gfx.text(82, 6, 0.48f, col::TEXT, box.name, Align::Left, 160);
        gfx.text(BOTTOM_W - 28, 7, 0.42f, col::TEXT,
            std::to_string(bi + 1) + "/" + std::to_string(storage->boxCount(pane)), Align::Right);

        // ---- Where a carried group would land (pink if it fits, red if it doesn't)
        std::array<bool, BOX_SLOTS> preview{};
        bool previewFits = true;
        if (group)
        {
            const int cr = cur / BOX_COLS, cc = cur % BOX_COLS;
            for (int s : group->slots)
            {
                const int r = s / BOX_COLS - group->minRow + cr;
                const int c = s % BOX_COLS - group->minCol + cc;
                if (r >= BOX_ROWS || c >= BOX_COLS)
                {
                    previewFits = false;
                    continue;
                }
                const int t = r * BOX_COLS + c;
                preview[t]  = true;
                const bool ownSpot = group->pane == pane && group->box == bi &&
                                     std::find(group->slots.begin(), group->slots.end(), t) !=
                                         group->slots.end();
                if (cells[t].present && !ownSpot)
                {
                    previewFits = false;
                }
            }
        }

        auto isOrigin = [&](int i)
        {
            if (held && held->pane == pane && held->box == bi && held->slot == i) return true;
            return group && group->pane == pane && group->box == bi &&
                   std::find(group->slots.begin(), group->slots.end(), i) != group->slots.end();
        };

        // ---- Tray and tiles
        gfx.rect(GRID_X - 7, GRID_Y - 6, GRID_W + 14, GRID_H + 12, col::TRAY);
        const float bob = std::sin(frame * 0.08f) * 1.2f;

        for (int i = 0; i < BOX_SLOTS; i++)
        {
            const float x     = tileX(i);
            const float y     = tileY(i);
            const CellInfo& c = cells[i];
            const bool origin = isOrigin(i);
            const bool marked = selectionHere() && sel->marked[i];
            const bool hover  = i == cur;

            // Outline drawn behind the tile so it reads as a rounded border.
            if (hover)
                gfx.rect(x - 2, y - 2, TILE_W + 4, TILE_H + 4, modeColor());
            else if (preview[i])
                gfx.rect(x - 2, y - 2, TILE_W + 4, TILE_H + 4,
                    previewFits ? col::MODE_GROUP : col::WARN);
            else if (marked)
                gfx.rect(x - 2, y - 2, TILE_W + 4, TILE_H + 4, col::MODE_SELECT);

            u32 bg = col::CELL;
            if (c.bad) bg = col::CELL_BAD;
            else if (hover) bg = col::CELL_HOVER;
            else if (c.present && !origin) bg = col::CELL_FULL;
            gfx.rect(x, y, TILE_W, TILE_H, bg);
            if (marked)
            {
                gfx.rect(x + 2, y + 2, TILE_W - 4, TILE_H - 4, col::SELECT_TINT);
            }

            if (!c.present)
            {
                continue;
            }

            const C2D_Image* img = icons.get(c.iconKey);
            // The hovered Pokemon lifts off its tile with a shadow, unless something is being
            // carried (then the carried sprite floats there instead).
            const bool lift = hover && !origin && !carrying();
            if (img && lift)
            {
                gfx.ellipse(x + 8, y + TILE_H - 8, TILE_W - 16, 7, col::SHADOW);
                gfx.image(*img, x + (TILE_W - 64 * RAISE_SCALE) / 2,
                    y - 6 + bob, RAISE_SCALE);
            }
            else if (img)
            {
                gfx.image(*img, x + (TILE_W - 64 * ICON_SCALE) / 2, y, ICON_SCALE,
                    origin ? 0.3f : 1.0f);
            }
            else
            {
                gfx.text(x + TILE_W / 2, y + 10, 0.34f, origin ? col::DIM : col::TEXT, c.name,
                    Align::Center, TILE_W - 4);
            }

            if (!origin)
            {
                if (!c.egg)
                {
                    gfx.text(x + TILE_W - 2, y + TILE_H - 10, 0.28f, col::TEXT,
                        std::to_string(c.level), Align::Right);
                }
                if (pane == Pane::Bank)
                {
                    gfx.text(x + 2, y + TILE_H - 10, 0.28f, col::DIM,
                        "G" + std::to_string((int)c.format));
                }
                if (c.shiny)
                {
                    gfx.text(x + 2, y, 0.32f, col::SHINY, "★");
                }
            }
        }

        // ---- Carried Pokemon float above the grid, following the cursor.
        auto floatIcon = [&](int iconKey, int slot)
        {
            const float x = tileX(slot), y = tileY(slot);
            gfx.ellipse(x + 6, y + TILE_H - 7, TILE_W - 12, 7, col::SHADOW);
            if (const C2D_Image* img = icons.get(iconKey))
            {
                gfx.image(*img, x + (TILE_W - 64 * RAISE_SCALE) / 2, y - 12 + bob, RAISE_SCALE,
                    0.95f);
            }
        };
        if (held)
        {
            floatIcon(heldIcon, cur);
        }
        else if (group)
        {
            const int cr = cur / BOX_COLS, cc = cur % BOX_COLS;
            for (size_t k = 0; k < group->slots.size(); k++)
            {
                const int s = group->slots[k];
                const int r = s / BOX_COLS - group->minRow + cr;
                const int c = s % BOX_COLS - group->minCol + cc;
                if (r < BOX_ROWS && c < BOX_COLS)
                {
                    floatIcon(group->iconKeys[k], r * BOX_COLS + c);
                }
            }
        }
    }

    // ---- Overlays ------------------------------------------------------------------------

    void App::renderMenu()
    {
        gfx.rect(0, 0, TOP_W, SCREEN_H, col::OVERLAY);
        const float w = 240, h = 40 + menu.size() * 26.0f;
        const float x = (TOP_W - w) / 2, y = (SCREEN_H - h) / 2;
        gfx.rect(x, y, w, h, col::CARD);
        gfx.text(TOP_W / 2, y + 8, 0.5f, col::TEXT, "Menu", Align::Center);
        for (size_t i = 0; i < menu.size(); i++)
        {
            const float iy = y + 32 + i * 26.0f;
            if ((int)i == menuCursor)
            {
                gfx.rect(x + 10, iy - 2, w - 20, 22, col::CARD_SEL);
                gfx.rect(x + 10, iy - 2, 4, 22, col::CURSOR);
            }
            gfx.text(x + 24, iy + 1, 0.46f, menu[i].color, menu[i].label);
        }
    }

    void App::renderHelp()
    {
        struct Row
        {
            const char* btn;
            const char* btn2; // optional second badge
            const char* what;
        };
        static const Row ROWS[] = {
            {"+Pad", nullptr, "Move the cursor (Circle Pad works too)"},
            {"A", nullptr, "Pick up / place - swaps if the spot is taken"},
            {"Y", nullptr, "Hold + move: select an area (or drag the stylus)"},
            {"A", nullptr, "On a selection: pick up the whole group"},
            {"X", nullptr, "Send to the Bank (from the Bank: to the save)"},
            {"B", nullptr, "Put back / clear the selection"},
            {"L", "R", "Previous / next box"},
            {"SELECT", nullptr, "Switch between Save and Bank"},
            {"START", nullptr, "Menu: game list, save, exit, this guide"},
        };

        gfx.rect(0, 0, TOP_W, SCREEN_H, col::OVERLAY);
        gfx.rect(10, 6, TOP_W - 20, SCREEN_H - 12, col::CARD);
        gfx.text(TOP_W / 2, 12, 0.55f, col::TEXT, "Button guide", Align::Center);

        float y = 38;
        for (const Row& r : ROWS)
        {
            float x = 24;
            x += drawButton(x, y, r.btn) + 4;
            if (r.btn2)
            {
                x += drawButton(x, y, r.btn2) + 4;
            }
            gfx.text(92, y + 1, 0.42f, col::TEXT, r.what, Align::Left, TOP_W - 120);
            y += 19;
        }
        gfx.text(TOP_W / 2, SCREEN_H - 26, 0.42f, col::CURSOR, "Press A to start",
            Align::Center);
    }

    void App::renderHelpLegend()
    {
        // Cursor colors, drawn as sample tiles on the bottom screen.
        gfx.rect(0, 0, BOTTOM_W, SCREEN_H, col::OVERLAY);
        gfx.rect(14, 14, BOTTOM_W - 28, SCREEN_H - 28, col::CARD);
        gfx.text(BOTTOM_W / 2, 22, 0.5f, col::TEXT, "Cursor colors", Align::Center);

        struct Sample
        {
            u32 color;
            const char* what;
        };
        const Sample samples[] = {
            {col::MODE_POINT, "Pointing"},
            {col::MODE_SELECT, "Selecting an area"},
            {col::MODE_HOLD, "Holding one Pokemon"},
            {col::MODE_GROUP, "Holding a group"},
        };
        float y = 50;
        for (const Sample& s : samples)
        {
            gfx.rect(36, y - 2, 30, 26, s.color);
            gfx.rect(38, y, 26, 22, col::CELL_HOVER);
            gfx.text(80, y + 3, 0.45f, col::TEXT, s.what);
            y += 34;
        }
        gfx.text(BOTTOM_W / 2, y + 2, 0.38f, col::DIM, "Stylus: tap = A, drag = select or carry",
            Align::Center, BOTTOM_W - 40);
    }

    void App::renderCredits()
    {
        // Required by the GPLv3 additional terms 7b/7c of PKSM-Core and PKHeX: keep the
        // authors' attributions and don't present this as their software.
        static const char* LINES[] = {
            "Devious Bank - GPLv3 - not affiliated with FlagBrew or Nintendo",
            "",
            "PKSM-Core (modified build config, unmodified sources)",
            "  (C) 2016-2022 Bernardo Giordano, Admiral Fish, piepie62",
            "  github.com/FlagBrew/PKSM-Core",
            "  Thanks: BernardoGiordano, Admiral-Fish, piepie62, kwsch,",
            "  SciresM, Epicpkmn11",
            "memecrypto - SciresM (FlagBrew fork)",
            "PKHeX - kwsch: format research and box sprites",
            "Box art: libretro-thumbnails project",
            "DS cart SPI (modified): PKSM, from TWLSaveTool (TuxSH)",
            "  and savegame-manager (Pokedoc)",
            "",
            "Pokemon and all related names are trademarks of",
            "Nintendo, Game Freak and Creatures Inc.",
        };
        gfx.rect(8, 6, TOP_W - 16, SCREEN_H - 12, col::CARD);
        gfx.text(TOP_W / 2, 10, 0.5f, col::TEXT, "Credits", Align::Center);
        int y = 32;
        for (const char* line : LINES)
        {
            gfx.text(18, y, 0.4f, line[0] == ' ' ? col::DIM : col::TEXT, line, Align::Left,
                TOP_W - 36);
            y += 13; // 14 lines * 13px ends inside the card
        }
    }

    void App::renderMessage()
    {
        const auto lines = wrap(message, 52);
        const int h      = 40 + (int)lines.size() * 16;
        const int y0     = std::max(10, (SCREEN_H - h) / 2);
        gfx.rect(0, 0, TOP_W, SCREEN_H, col::OVERLAY);
        gfx.rect(18, y0 - 2, TOP_W - 36, h + 4, messageError ? col::WARN : col::GOOD);
        gfx.rect(20, y0, TOP_W - 40, h, col::CARD);
        int y = y0 + 10;
        for (const auto& line : lines)
        {
            gfx.text(30, y, 0.45f, col::TEXT, line, Align::Left, TOP_W - 60);
            y += 16;
        }
        gfx.text(TOP_W / 2, y + 4, 0.42f, col::DIM, "A: OK", Align::Center);
    }
}
