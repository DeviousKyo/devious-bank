// Box/slot state management shared by the save and the bank.
//
// Every move is a swap between two slots, done in one step. "Picking up" a Pokemon in the
// UI only remembers where it came from; nothing changes until it is placed, so a
// Pokemon can never be lost or duplicated.
#pragma once

#include "core/db_bank.hpp"
#include "core/db_save.hpp"

#include <string>
#include <vector>

namespace db
{
    enum class Pane : uint8_t
    {
        Save,
        Bank,
    };

    struct SlotRef
    {
        Pane pane = Pane::Bank;
        int box   = 0;
        int slot  = 0;

        bool operator==(const SlotRef&) const = default;
    };

    class Storage
    {
    public:
        Storage(Bank& bank, SaveSource* save) : bank(bank), save(save) {}

        [[nodiscard]] bool hasPane(Pane p) const { return p == Pane::Bank || save != nullptr; }
        [[nodiscard]] int boxCount(Pane p) const;
        [[nodiscard]] Box& box(Pane p, int i);
        [[nodiscard]] Slot& at(const SlotRef& r);
        [[nodiscard]] bool paneReadOnly(Pane p, std::string* why = nullptr) const;
        // Whether a Pokemon of this format may live in the pane.
        [[nodiscard]] bool accepts(Pane p, PkxFormat f, std::string* why = nullptr) const;

        // Exchanges the contents of two slots (either may be empty). Validates both
        // directions first; on failure nothing is changed and `why` says what was wrong.
        bool swap(const SlotRef& a, const SlotRef& b, std::string& why);

        // Moves several Pokemon in one step: from[i] goes to to[i]. Every target must be
        // empty or itself one of the sources (so a group can shift over its own spots).
        // All checks run first; either every Pokemon moves or none does.
        bool moveGroup(const std::vector<SlotRef>& from, const std::vector<SlotRef>& to,
            std::string& why);

        // Up to `count` empty, writable slots in a pane, scanning from `startBox` and
        // wrapping around. Returns fewer if the pane doesn't have room.
        [[nodiscard]] std::vector<SlotRef> findEmpty(Pane p, int count, int startBox);

        [[nodiscard]] bool dirty() const;

    private:
        void markDirty(const SlotRef& r);

        Bank& bank;
        SaveSource* save;
    };
}
