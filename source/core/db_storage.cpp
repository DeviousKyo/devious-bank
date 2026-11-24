#include "core/db_storage.hpp"

#include <algorithm>
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

    bool Storage::moveGroup(
        const std::vector<SlotRef>& from, const std::vector<SlotRef>& to, std::string& why)
    {
        if (from.size() != to.size() || from.empty())
        {
            why = "Nothing to move";
            return false;
        }
        auto contains = [](const std::vector<SlotRef>& v, const SlotRef& r)
        { return std::find(v.begin(), v.end(), r) != v.end(); };

        // ---- validate everything before touching anything
        for (size_t i = 0; i < from.size(); i++)
        {
            const SlotRef& a = from[i];
            const SlotRef& b = to[i];
            if (!hasPane(a.pane) || !hasPane(b.pane))
            {
                why = "No save loaded";
                return false;
            }
            if (paneReadOnly(a.pane, &why) || paneReadOnly(b.pane, &why))
            {
                return false;
            }
            for (size_t j = 0; j < i; j++)
            {
                if (from[j] == a || to[j] == b)
                {
                    why = "Internal error: duplicate slot in group";
                    return false;
                }
            }
            const Slot& src = at(a);
            const Slot& dst = at(b);
            if (!src.movable())
            {
                why = "A Pokemon in the group is locked or damaged";
                return false;
            }
            if (!dst.writable() || (!dst.empty() && !contains(from, b)))
            {
                why = "Not enough empty spots there";
                return false;
            }
            if (!accepts(b.pane, src.format, &why))
            {
                return false;
            }
        }

        // ---- copy out, clear the sources, then place: no Pokemon is ever in two slots
        std::vector<Slot> moving;
        moving.reserve(from.size());
        for (const SlotRef& a : from)
        {
            moving.push_back(at(a));
        }
        for (const SlotRef& a : from)
        {
            at(a) = Slot{};
            markDirty(a);
        }
        for (size_t i = 0; i < to.size(); i++)
        {
            at(to[i]) = moving[i];
            markDirty(to[i]);
        }
        return true;
    }

    bool Storage::tradeReplace(
        const SlotRef& where, const Slot& outgoing, const Slot& incoming, std::string& why)
    {
        if (!hasPane(where.pane) || paneReadOnly(where.pane, &why))
        {
            if (why.empty()) why = "No save loaded";
            return false;
        }
        Slot& s = at(where);
        if (s.format != outgoing.format || s.length != outgoing.length ||
            !std::equal(s.data.begin(), s.data.begin() + s.length, outgoing.data.begin()))
        {
            why = "The offered Pokemon is no longer in its slot";
            return false;
        }
        if (!s.movable() || !accepts(where.pane, incoming.format, &why))
        {
            if (why.empty()) why = "That slot can't take the received Pokemon";
            return false;
        }
        s       = incoming;
        s.flags = 0;
        markDirty(where);
        return true;
    }

    std::vector<SlotRef> Storage::findEmpty(Pane p, int count, int startBox)
    {
        std::vector<SlotRef> out;
        const int boxes = boxCount(p);
        for (int n = 0; n < boxes && (int)out.size() < count; n++)
        {
            const int b = (startBox + n) % boxes;
            for (int s = 0; s < BOX_SLOTS && (int)out.size() < count; s++)
            {
                const Slot& slot = box(p, b).slots[s];
                if (slot.empty() && slot.writable())
                {
                    out.push_back(SlotRef{p, b, s});
                }
            }
        }
        return out;
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
