#include "core/db_storage.hpp"

#include <utility>

namespace db
{
    int Storage::boxCount(Pane p) const
    {
        return p == Pane::Bank ? bank.boxCount() : (save ? save->boxCount() : 0);
    }

    Box& Storage::box(Pane p, int i)
    {
        return p == Pane::Bank ? bank.box(i) : save->box(i);
    }

    Slot& Storage::at(const SlotRef& r)
    {
        return box(r.pane, r.box).slots[r.slot];
    }

    bool Storage::paneReadOnly(Pane p, std::string* why) const
    {
        if (p == Pane::Bank && bank.readOnly())
        {
            if (why) *why = bank.readOnlyReason();
            return true;
        }
        if (p == Pane::Save && (!save || save->readOnly()))
        {
            if (why) *why = save ? save->readOnlyReason() : "No save loaded";
            return true;
        }
        return false;
    }

    bool Storage::accepts(Pane p, PkxFormat f, std::string* why) const
    {
        if (f == PkxFormat::Empty || p == Pane::Bank)
        {
            return true; // the bank holds every generation side by side
        }
        if (save && f == save->format())
        {
            return true;
        }
        if (why)
        {
            *why = std::string(formatName(f)) + " can't go into a " +
                   formatName(save ? save->format() : PkxFormat::Empty) + " save";
        }
        return false;
    }

    bool Storage::swap(const SlotRef& a, const SlotRef& b, std::string& why)
    {
        if (a == b)
        {
            return true;
        }
        if (!hasPane(a.pane) || !hasPane(b.pane))
        {
            why = "No save loaded";
            return false;
        }
        if (paneReadOnly(a.pane, &why) || paneReadOnly(b.pane, &why))
        {
            return false;
        }

        Slot& sa = at(a);
        Slot& sb = at(b);
        if (!sa.writable() || !sb.writable())
        {
            why = "That slot is locked or damaged";
            return false;
        }
        if (!accepts(b.pane, sa.format, &why) || !accepts(a.pane, sb.format, &why))
        {
            return false;
        }

        std::swap(sa, sb);
        markDirty(a);
        markDirty(b);
        return true;
    }

    void Storage::markDirty(const SlotRef& r)
    {
        box(r.pane, r.box).dirty[r.slot] = true;
        if (r.pane == Pane::Bank)
        {
            bank.markDirty();
        }
    }

    bool Storage::dirty() const
    {
        return bank.dirty() || (save && save->dirty());
    }
}
