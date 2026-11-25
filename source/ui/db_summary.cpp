#include "ui/db_summary.hpp"
#include "ui/db_icons.hpp"

#include "enums/Ribbon.hpp"
#include "utils/i18n.hpp"

#include <cstdarg>
#include <cstdio>

namespace db::ui
{
    namespace
    {
        constexpr pksm::Language LANG = pksm::Language::ENG;

        std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
        std::string fmt(const char* f, ...)
        {
            char buf[160];
            va_list ap;
            va_start(ap, f);
            vsnprintf(buf, sizeof(buf), f, ap);
            va_end(ap);
            return buf;
        }

        std::string dateStr(const Date& d)
        {
            if (d.year() == 0 && d.month() == 0 && d.day() == 0)
            {
                return "--";
            }
            return fmt("%04lu-%02u-%02u", (unsigned long)d.year(), d.month(), d.day());
        }

        const char* genderSym(pksm::Gender g)
        {
            if (g == pksm::Gender::Male) return "♂";
            if (g == pksm::Gender::Female) return "♀";
            return "";
        }

        int genOf(PkxFormat f) { return (int)f; }

        std::string orDash(const std::string& s) { return s.empty() ? "--" : s; }

        // Highest national dex number each stored format can hold.
        int maxSpeciesFor(PkxFormat f)
        {
            switch (f)
            {
                case PkxFormat::PK1: return 151;
                case PkxFormat::PK2: return 251;
                case PkxFormat::PK3: return 386;
                case PkxFormat::PK4: return 493;
                case PkxFormat::PK5: return 649;
                case PkxFormat::PK6: return 721;
                case PkxFormat::PK7: return 809;
                default: return 0;
            }
        }

        // Level, gender, ability, nature, base stats, all index PKSM-Core's personal
        // tables by species without a bounds check, so a damaged slot or a hostile trade
        // offer with species 0 / past the generation's range would read out of bounds.
        // Forms need no check here: formSpecies() only uses a form below the table's own
        // formCount() for that (now in-range) species.
        bool speciesInRange(const pksm::PKX& pk, PkxFormat f)
        {
            try
            {
                const int species = (int)u16(pk.species());
                return species >= 1 && species <= maxSpeciesFor(f);
            }
            catch (...)
            {
                return false; // can't even read the species: treat as bad
            }
        }

        int iconKeyFor(const pksm::PKX& pk)
        {
            if (pk.egg())
            {
                return ICON_EGG;
            }
            const int species = (int)u16(pk.species());
            return (species >= 1 && species <= 809) ? species : 0; // 0 = placeholder icon
        }
    }

    CellInfo buildCell(const Slot& slot)
    {
        CellInfo c;
        c.format = slot.format;
        c.locked = (slot.flags & SLOT_LOCKED) != 0;
        if (slot.empty())
        {
            return c;
        }
        c.present = true;
        c.bad     = (slot.flags & SLOT_CORRUPT) != 0;
        try
        {
            auto pk = slot.toPkx();
            if (!pk)
            {
                c.bad  = true;
                c.name = "???";
                return c;
            }
            if (!speciesInRange(*pk, slot.format))
            {
                c.bad  = true;
                c.name = "???";
                return c;
            }
            c.egg     = pk->egg();
            c.shiny   = pk->shiny();
            c.level   = pk->level();
            c.name    = c.egg ? "Egg" : i18n::species(LANG, pk->species());
            c.iconKey = iconKeyFor(*pk);
        }
        catch (...)
        {
            c.bad  = true;
            c.name = "???";
        }
        return c;
    }

    Summary buildSummary(const Slot& slot)
    {
        Summary s;
        if (slot.empty())
        {
            return s;
        }
        s.present = true;

        std::unique_ptr<pksm::PKX> pk;
        try
        {
            pk = slot.toPkx();
        }
        catch (...)
        {
        }
        if (!pk)
        {
            s.title = "Unreadable Pokemon data";
            s.left.push_back(std::string("Format: ") + formatName(slot.format) +
                             fmt("  (%u bytes)", slot.length));
            s.left.push_back("The bytes are kept as-is and this slot is locked.");
            return s;
        }
        if (!speciesInRange(*pk, slot.format))
        {
            // Everything past this point may touch the personal tables; show only what
            // is safe to read from the raw bytes.
            s.title = "???";
            s.left.push_back(std::string("Format: ") + formatName(slot.format) +
                             fmt("  (%u bytes)", slot.length));
            s.left.push_back("Its species number is not valid for this format.");
            return s;
        }

        const int gen = genOf(slot.format);
        try
        {
            // Inside the try: a lookup that throws must not escape into the render loop.
            const std::string species = i18n::species(LANG, pk->species());
            s.iconKey                 = iconKeyFor(*pk);

            // ---- Title: nickname (species) Lv.X  gender  shiny
            std::string title = pk->egg() ? "Egg" : pk->nickname();
            if (!pk->egg() && pk->nickname() != species)
            {
                title += " (" + species + ")";
            }
            title += fmt("  Lv.%u ", pk->level());
            if (gen >= 2)
            {
                title += genderSym(pk->gender());
            }
            if (pk->shiny())
            {
                title += " ★";
            }
            s.title = title;

            // ---- Left column: identity + origin
            std::string fmtLine = std::string("Format: ") + formatName(slot.format);
            if (gen >= 3)
            {
                fmtLine += "   Origin: " + orDash(i18n::game(LANG, pk->version()));
            }
            s.left.push_back(fmtLine);
            if (slot.flags & SLOT_CORRUPT)
            {
                s.left.push_back("!! Checksum mismatch: slot locked");
            }

            std::string ot = "OT: " + pk->otName();
            if (gen >= 3)
            {
                ot += std::string(" ") + genderSym(pk->otGender());
            }
            s.left.push_back(ot);
            if (gen >= 3)
            {
                s.left.push_back(fmt("TID: %lu   SID: %lu", (unsigned long)pk->formatTID(),
                    (unsigned long)pk->formatSID()));
            }
            else
            {
                s.left.push_back(fmt("TID: %05u", pk->TID()));
            }

            if (gen >= 3)
            {
                s.left.push_back("Nature: " + orDash(pk->nature().localize(LANG)) +
                                 "   Ability: " + orDash(pk->ability().localize(LANG)));
                s.left.push_back("Ball: " + orDash(pk->ball().localize(LANG)));
            }
            if (gen >= 2)
            {
                s.left.push_back("Item: " + (pk->heldItem() ? orDash(i18n::item(LANG, pk->heldItem()))
                                                            : std::string("None")));
            }

            // Met data: Gen 2 only in Crystal (location 0 = not recorded), Gen 3+ always.
            if (gen >= 3 || pk->metLocation() != 0)
            {
                std::string met = "Met: " +
                                  orDash(i18n::location(LANG, pk->generation(), pk->metLocation())) +
                                  fmt(" @ Lv.%u", pk->metLevel());
                s.left.push_back(met);
            }
            if (gen >= 4)
            {
                s.left.push_back("Met date: " + dateStr(pk->metDate()));
                if (pk->eggLocation() != 0)
                {
                    s.left.push_back("Egg: " +
                                     orDash(i18n::location(LANG, pk->generation(), pk->eggLocation())) +
                                     "  " + dateStr(pk->eggDate()));
                }
            }

            if (gen >= 3)
            {
                int ribbons = 0;
                for (u32 i = 0; i <= u32(pksm::Ribbon::Partner); i++)
                {
                    pksm::Ribbon r(i);
                    if (pk->hasRibbon(r) && pk->ribbon(r))
                    {
                        ribbons++;
                    }
                }
                s.left.push_back(fmt("Ribbons: %d", ribbons));
                s.left.push_back(fmt("PID: %08lX", (unsigned long)pk->PID()) +
                                 (gen >= 6 ? fmt("  EC: %08lX", (unsigned long)pk->encryptionConstant())
                                           : std::string()));
            }

            // ---- Right column: stats and moves
            using pksm::Stat;
            const Stat order[6] = {Stat::HP, Stat::ATK, Stat::DEF, Stat::SPATK, Stat::SPDEF, Stat::SPD};
            s.hasStats = true;
            s.ivLabel  = gen <= 2 ? "DV" : "IV";
            s.evLabel  = gen <= 2 ? "StatExp" : "EV";
            for (int i = 0; i < 6; i++)
            {
                s.iv[i] = pk->iv(order[i]);
                s.ev[i] = pk->ev(order[i]);
            }
            for (u8 m = 0; m < 4; m++)
            {
                s.moves[m] = orDash(pk->move(m).localize(LANG));
            }
        }
        catch (...)
        {
            s.left.push_back("(some fields could not be read)");
        }
        return s;
    }
}
