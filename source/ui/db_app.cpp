#include "ui/db_app.hpp"
#include "core/db_fs.hpp"

#include "utils/i18n.hpp"

#include <algorithm>
#include <unordered_set>

namespace db
{
    using namespace ui;

    namespace
    {
        // Bottom-screen grid geometry (320x240): 6x5 cells of 48x40 under a 26px header.
        constexpr int HEADER_H = 26;
        constexpr int GRID_X   = 16;
        constexpr int GRID_Y   = 30;
        constexpr int CELL_W   = 48;
        constexpr int CELL_H   = 40;
        constexpr float ICON_SCALE = 0.66f;

        // Top-screen layout (400x240).
        constexpr int TOP_HEADER_H = 20;
        constexpr int STATUS_Y     = 222;
        constexpr int LIST_ROWS    = 11;

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
        icons.clear(); // icon textures, before citro3d goes away
        gfx.fini();    // text buffer, render targets, citro2d, citro3d, gfx
        if (i18nUp)
        {
            i18n::exit();
            i18nUp = false;
        }
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
            touchPosition touch;
            hidTouchRead(&touch);

            switch (state)
            {
                case State::GameSelect:  inputGameSelect(down, repeat); break;
                case State::BoxView:     inputBoxView(down, repeat, touch); break;
                case State::ConfirmExit: inputConfirmExit(down); break;
                case State::Message:     inputMessage(down); break;
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

        if (save && save->readOnly())
        {
            showMessage(save->readOnlyReason(), true);
            messageReturn = State::BoxView;
        }
        else if (save)
        {
            setStatus("Backup: " + save->backupPath(), col::GOOD);
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
    }

    void App::progressFrame(const std::string& text)
    {
        gfx.beginFrame();
        gfx.drawTop();
        int y = 100;
        for (const auto& line : wrap(text, 48))
        {
            gfx.text(TOP_W / 2, y, 0.6f, col::TEXT, line, Align::Center, TOP_W - 20);
            y += 20;
        }
        gfx.drawBottom();
        gfx.endFrame();
    }

    // =====================================================================================
    // Input
    // =====================================================================================

    void App::inputGameSelect(u32 down, u32 repeat)
    {
        const int entries = (int)saves.size() + 1; // entry 0 = bank only
        if (repeat & KEY_DOWN) listCursor = (listCursor + 1) % entries;
        if (repeat & KEY_UP) listCursor = (listCursor + entries - 1) % entries;
        if (repeat & KEY_RIGHT) listCursor = std::min(entries - 1, listCursor + LIST_ROWS);
        if (repeat & KEY_LEFT) listCursor = std::max(0, listCursor - LIST_ROWS);
        if (down & KEY_A) openSelected();
        if (down & KEY_Y) scan();
        if (down & KEY_X) state = State::Credits;
        if (down & KEY_START) quit = true;
    }

    void App::inputBoxView(u32 down, u32 repeat, const touchPosition& touch)
    {
        int& cur = cursor[paneIdx()];
        int row = cur / BOX_COLS, colIdx = cur % BOX_COLS;
        const int before = cur;

        if (repeat & KEY_UP) row = (row + BOX_ROWS - 1) % BOX_ROWS;
        if (repeat & KEY_DOWN) row = (row + 1) % BOX_ROWS;
        if (repeat & KEY_LEFT) colIdx = (colIdx + BOX_COLS - 1) % BOX_COLS;
        if (repeat & KEY_RIGHT) colIdx = (colIdx + 1) % BOX_COLS;
        cur = row * BOX_COLS + colIdx;
        if (cur != before) summaryValid = false;

        if (repeat & (KEY_L | KEY_ZL)) changeBox(-1);
        if (repeat & (KEY_R | KEY_ZR)) changeBox(+1);
        if (down & KEY_SELECT) togglePane();
        if (down & KEY_A) pressA();
        if ((down & KEY_B) && held)
        {
            held.reset();
            invalidate();
            setStatus("Cancelled", col::DIM);
        }
        if (down & KEY_START)
        {
            if (storage->dirty()) state = State::ConfirmExit;
            else quit = true;
        }

        if (down & KEY_TOUCH)
        {
            const int x = touch.px, y = touch.py;
            if (y < HEADER_H)
            {
                if (x < 48) changeBox(-1);
                else if (x > BOTTOM_W - 48) changeBox(+1);
                else togglePane();
            }
            else if (x >= GRID_X && x < GRID_X + BOX_COLS * CELL_W && y >= GRID_Y &&
                     y < GRID_Y + BOX_ROWS * CELL_H)
            {
                cur          = ((y - GRID_Y) / CELL_H) * BOX_COLS + (x - GRID_X) / CELL_W;
                summaryValid = false;
                pressA();
            }
        }
    }

    void App::pressA()
    {
        const SlotRef here = cursorRef();
        if (!held)
        {
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
            held = here;
            invalidate();
            setStatus("Holding - A to place, B to cancel", col::CURSOR);
            return;
        }

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
    }

    void App::changeBox(int delta)
    {
        const int n = storage->boxCount(pane);
        if (n <= 0) return;
        boxIdx[paneIdx()] = (boxIdx[paneIdx()] + delta + n) % n;
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
        invalidate();
    }

    void App::inputConfirmExit(u32 down)
    {
        if (down & KEY_A)
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
                                "\nNothing was lost: your moves are still here and the backups "
                                "are in sdmc:/3ds/DB/backups/",
                    true);
            }
        }
        else if (down & KEY_X)
        {
            quit = true; // nothing written
        }
        else if (down & KEY_B)
        {
            state = State::BoxView;
        }
    }

    void App::inputMessage(u32 down)
    {
        if (down & (KEY_A | KEY_B))
        {
            state = messageReturn;
        }
    }

    // =====================================================================================
    // Render
    // =====================================================================================

    SlotRef App::cursorRef() const
    {
        return SlotRef{pane, boxIdx[paneIdx()], cursor[paneIdx()]};
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
            // Keep only the icons of the visible box (+ the summary's) in memory.
            std::unordered_set<int> keep;
            for (const CellInfo& c : cells)
            {
                keep.insert(c.iconKey);
            }
            keep.insert(summary.iconKey);
            icons.retainOnly(keep);
        }
    }

    void App::render()
    {
        if (state == State::BoxView || state == State::ConfirmExit ||
            (state == State::Message && messageReturn != State::GameSelect))
        {
            refreshCaches();
        }

        gfx.beginFrame();

        gfx.drawTop();
        if (state == State::Credits)
        {
            renderCredits();
        }
        else if (state == State::GameSelect ||
                 (state == State::Message && messageReturn == State::GameSelect))
        {
            renderGameList();
        }
        else
        {
            renderSummary();
        }
        renderStatusBar();
        if (state == State::ConfirmExit) renderConfirmExit();
        if (state == State::Message) renderMessage();

        gfx.drawBottom();
        if (state == State::Credits)
        {
            gfx.text(BOTTOM_W / 2, 110, 0.5f, col::DIM, "B: back", Align::Center);
        }
        else if (state == State::GameSelect ||
                 (state == State::Message && messageReturn == State::GameSelect))
        {
            gfx.text(BOTTOM_W / 2, 70, 0.55f, col::TEXT, "Choose a save", Align::Center);
            gfx.text(BOTTOM_W / 2, 100, 0.42f, col::DIM,
                "A: open   Y: rescan   X: credits   START: exit", Align::Center);
            gfx.text(BOTTOM_W / 2, 140, 0.4f, col::DIM, "GBA / DS saves: put .sav files in",
                Align::Center);
            gfx.text(BOTTOM_W / 2, 156, 0.4f, col::DIM, "sdmc:/3ds/DB/saves/", Align::Center);
        }
        else
        {
            renderGrid();
        }

        gfx.endFrame();
    }

    void App::renderGameList()
    {
        gfx.rect(0, 0, TOP_W, TOP_HEADER_H, col::PANEL);
        gfx.text(8, 2, 0.5f, col::TEXT, "Devious Bank");
        gfx.text(TOP_W - 8, 3, 0.42f, col::DIM,
            std::to_string(saves.size()) + " save(s) found", Align::Right);

        const int entries = (int)saves.size() + 1;
        const int first   = std::clamp(listCursor - LIST_ROWS / 2, 0,
                std::max(0, entries - LIST_ROWS));
        for (int i = 0; i < LIST_ROWS && first + i < entries; i++)
        {
            const int idx = first + i;
            const int y   = TOP_HEADER_H + 4 + i * 18;
            if (idx == listCursor)
            {
                gfx.rect(4, y, TOP_W - 8, 18, col::CELL_FULL);
            }
            const std::string label = idx == 0 ? "Bank only (no game save)" : saves[idx - 1].label;
            gfx.text(12, y + 2, 0.48f, idx == 0 ? col::DIM : col::TEXT, label, Align::Left,
                TOP_W - 24);
        }
    }

    void App::renderSummary()
    {
        // Header: which game + unsaved marker
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

        // Sprite: 64x56 icon at 1.25x (80x70) in the top-right corner.
        if (const C2D_Image* img = icons.get(summary.iconKey))
        {
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
        if (!status.empty())
        {
            gfx.text(6, STATUS_Y + 3, 0.42f, statusColor, status, Align::Left, TOP_W - 12);
        }
        else if (state == State::BoxView)
        {
            gfx.text(6, STATUS_Y + 3, 0.4f, col::DIM,
                "A pick/place  B cancel  L/R box  SELECT save/bank  START exit", Align::Left,
                TOP_W - 12);
        }
    }

    void App::renderGrid()
    {
        if (!storage || storage->boxCount(pane) == 0)
        {
            return;
        }
        const int bi   = boxIdx[paneIdx()];
        const Box& box = storage->box(pane, bi);

        // Header: < [SAVE|BANK] box name (n/N) >
        gfx.rect(0, 0, BOTTOM_W, HEADER_H, pane == Pane::Save ? col::SAVE_HDR : col::BANK_HDR);
        gfx.text(10, 4, 0.55f, col::TEXT, "<");
        gfx.text(BOTTOM_W - 10, 4, 0.55f, col::TEXT, ">", Align::Right);
        const std::string title = std::string(pane == Pane::Save ? "SAVE  " : "BANK  ") + box.name +
                                  "  (" + std::to_string(bi + 1) + "/" +
                                  std::to_string(storage->boxCount(pane)) + ")";
        gfx.text(BOTTOM_W / 2, 5, 0.48f, col::TEXT, title, Align::Center, BOTTOM_W - 60);

        for (int i = 0; i < BOX_SLOTS; i++)
        {
            const int x       = GRID_X + (i % BOX_COLS) * CELL_W;
            const int y       = GRID_Y + (i / BOX_COLS) * CELL_H;
            const CellInfo& c = cells[i];
            const bool isHeldOrigin =
                held && held->pane == pane && held->box == bi && held->slot == i;

            u32 bg = col::CELL;
            if (c.bad) bg = col::CELL_BAD;
            else if (isHeldOrigin) bg = col::CELL_HELD;
            else if (c.present) bg = col::CELL_FULL;
            gfx.rect(x + 1, y + 1, CELL_W - 2, CELL_H - 2, bg);

            if (c.present)
            {
                if (const C2D_Image* img = icons.get(c.iconKey))
                {
                    // 64x56 icon at 0.66 = 42x37, centered in the 48x40 cell.
                    gfx.image(*img, x + 3, y + 1, ICON_SCALE, isHeldOrigin ? 0.35f : 1.0f);
                }
                else
                {
                    // No icon file: fall back to the species name.
                    gfx.text(x + CELL_W / 2, y + 8, 0.38f, isHeldOrigin ? col::DIM : col::TEXT,
                        c.name, Align::Center, CELL_W - 4);
                }
                if (!c.egg)
                {
                    gfx.text(x + CELL_W - 3, y + CELL_H - 12, 0.3f, col::TEXT,
                        std::to_string(c.level), Align::Right);
                }
                if (pane == Pane::Bank)
                {
                    gfx.text(x + 3, y + CELL_H - 12, 0.3f, col::DIM,
                        "G" + std::to_string((int)c.format));
                }
                if (c.shiny)
                {
                    gfx.text(x + 3, y + 1, 0.34f, col::SHINY, "★");
                }
            }
        }

        // Cursor: yellow normally, green while holding a Pokemon.
        const int cur = cursor[paneIdx()];
        gfx.frame(GRID_X + (cur % BOX_COLS) * CELL_W, GRID_Y + (cur / BOX_COLS) * CELL_H, CELL_W,
            CELL_H, 2, held ? col::GOOD : col::CURSOR);
    }

    void App::renderConfirmExit()
    {
        gfx.rect(0, 0, TOP_W, SCREEN_H, col::OVERLAY);
        gfx.rect(40, 60, TOP_W - 80, 120, col::PANEL);
        gfx.text(TOP_W / 2, 72, 0.55f, col::TEXT, "You have unsaved changes", Align::Center);
        gfx.text(TOP_W / 2, 104, 0.48f, col::GOOD, "A: Save everything and exit", Align::Center);
        gfx.text(TOP_W / 2, 124, 0.48f, col::WARN, "X: Exit WITHOUT saving", Align::Center);
        gfx.text(TOP_W / 2, 144, 0.48f, col::DIM, "B: Back", Align::Center);
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
            "  github.com/kwsch/PKHeX",
            "",
            "Pokemon and all related names are trademarks of",
            "Nintendo, Game Freak and Creatures Inc.",
        };
        gfx.rect(0, 0, TOP_W, TOP_HEADER_H, col::PANEL);
        gfx.text(8, 2, 0.5f, col::TEXT, "Credits");
        int y = TOP_HEADER_H + 6;
        for (const char* line : LINES)
        {
            gfx.text(10, y, 0.42f, line[0] == ' ' ? col::DIM : col::TEXT, line, Align::Left,
                TOP_W - 20);
            y += 15;
        }
    }

    void App::renderMessage()
    {
        const auto lines = wrap(message, 52);
        const int h      = 40 + (int)lines.size() * 16;
        const int y0     = std::max(10, (SCREEN_H - h) / 2);
        gfx.rect(0, 0, TOP_W, SCREEN_H, col::OVERLAY);
        gfx.rect(20, y0, TOP_W - 40, h, col::PANEL);
        gfx.frame(20, y0, TOP_W - 40, h, 2, messageError ? col::WARN : col::GOOD);
        int y = y0 + 10;
        for (const auto& line : lines)
        {
            gfx.text(30, y, 0.45f, col::TEXT, line, Align::Left, TOP_W - 60);
            y += 16;
        }
        gfx.text(TOP_W / 2, y + 4, 0.42f, col::DIM, "A: OK", Align::Center);
    }
}
