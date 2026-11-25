// Core storage units: one Pokemon slot and one 30-slot box.
//
// A Slot holds the Pokemon's raw, DECRYPTED bytes exactly as PKSM-Core / PKHeX export
// them (.pk1 ... .pk7). Nothing is ever rebuilt from parsed fields, so every value the
// game recorded (OT, IDs, origin game, met/egg location and date, IVs/DVs, EVs/stat exp,
// ribbons, memories, markings, PID/EC, ...) survives a round trip byte for byte.
#pragma once

#include "pkx/PKX.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace db
{
    // Stored in the bank file: values must never change.
    enum class PkxFormat : uint8_t
    {
        Empty = 0,
        PK1   = 1,
        PK2   = 2,
        PK3   = 3,
        PK4   = 4,
        PK5   = 5,
        PK6   = 6,
        PK7   = 7,
    };

    // Largest stored record: PK6/PK7 box data (0xE8). Gen 1/2 records include the OT
    // name and nickname (59/69 and 63/73 bytes, JP/international).
    inline constexpr size_t SLOT_DATA_MAX = 232;
    inline constexpr int BOX_SLOTS        = 30;
    inline constexpr int BOX_COLS         = 6;
    inline constexpr int BOX_ROWS         = 5;

    enum SlotFlags : uint8_t
    {
        SLOT_CORRUPT = 1 << 0, // checksum/length mismatch: shown, but never moved or overwritten
        SLOT_LOCKED  = 1 << 1, // reserved for game-locked slots
    };

    struct Slot
    {
        PkxFormat format = PkxFormat::Empty;
        uint8_t flags    = 0;
        uint16_t length  = 0;
        std::array<uint8_t, SLOT_DATA_MAX> data{};

        [[nodiscard]] bool empty() const { return format == PkxFormat::Empty; }
        [[nodiscard]] bool movable() const
        {
            return !empty() && !(flags & (SLOT_CORRUPT | SLOT_LOCKED));
        }
        // A slot that may be overwritten (empty or a normal Pokemon).
        [[nodiscard]] bool writable() const { return !(flags & (SLOT_CORRUPT | SLOT_LOCKED)); }

        // Parses a copy of the bytes for display. Null for empty or unparseable slots.
        [[nodiscard]] std::unique_ptr<pksm::PKX> toPkx() const;
        // Copies the decrypted bytes of a Pokemon read from a save. Species None -> Empty only
        // if the record is genuinely blank; a damaged one is kept and flagged SLOT_CORRUPT.
        [[nodiscard]] static Slot fromPkx(const pksm::PKX& pk);
    };

    struct Box
    {
        std::string name;
        std::array<Slot, BOX_SLOTS> slots{};
        // Not persisted: which slots changed since the last write.
        std::array<bool, BOX_SLOTS> dirty{};
    };

    [[nodiscard]] const char* formatName(PkxFormat f);
    [[nodiscard]] bool formatFromGeneration(pksm::Generation g, PkxFormat& out);
    [[nodiscard]] pksm::Generation formatToGeneration(PkxFormat f);
    // True if `length` is a valid stored size for that format.
    [[nodiscard]] bool validLength(PkxFormat f, size_t length);
}
