// Builds the display text for one Pokemon (top screen) and for a grid cell.
// Parsing goes through PKSM-Core on a copy of the slot bytes; nothing is written back.
#pragma once

#include "core/db_slot.hpp"

#include <string>
#include <vector>

namespace db::ui
{
    struct CellInfo
    {
        bool present = false;
        bool bad     = false;
        bool shiny   = false;
        bool egg     = false;
        bool locked  = false; // SLOT_LOCKED, e.g. slots 21-30 of international Gen 1/2 boxes
        int level    = 0;
        int iconKey  = -1; // species number, ICON_EGG, or ICON_NONE
        std::string name;
        PkxFormat format = PkxFormat::Empty;
    };

    struct Summary
    {
        bool present = false;
        int iconKey  = -1;
        std::string title;
        std::vector<std::string> left; // origin / trainer data
        // Stat table (drawn in fixed columns), order HP Atk Def SpA SpD Spe
        bool hasStats = false;
        std::string ivLabel, evLabel;
        int iv[6] = {};
        int ev[6] = {};
        std::string moves[4];
    };

    CellInfo buildCell(const Slot& slot);
    Summary buildSummary(const Slot& slot);
}
