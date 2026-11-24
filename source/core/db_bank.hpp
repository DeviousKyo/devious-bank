// The Devious Bank storage file (sdmc:/3ds/DB/bank.dbk).
//
// Binary layout, all integers little-endian:
//
//   Header (32 bytes)
//     0x00 char[4]  magic "DVBK"
//     0x04 u16      version (1)
//     0x06 u16      boxCount
//     0x08 u32      slot record size (236)
//     0x0C u32      CRC32 of everything after the header
//     0x10 u8[16]   reserved (0)
//   Box (repeated boxCount times)
//     0x00 char[32] box name, UTF-8, NUL padded
//     then 30 slot records of 236 bytes:
//       0x00 u8       format (0 empty, 1-7 = PK1..PK7)
//       0x01 u8       flags (SlotFlags)
//       0x02 u16      length of data actually used
//       0x04 u8[232]  raw decrypted Pokemon bytes (PKHeX .pkN layout), zero padded
//
// Any record can be copied out as a standalone .pk1-.pk7 file by taking `length` bytes.
#pragma once

#include "core/db_slot.hpp"

#include <string>
#include <vector>

namespace db
{
    class Bank
    {
    public:
        static constexpr uint32_t MAGIC          = 0x4B425644; // "DVBK"
        static constexpr uint16_t VERSION        = 1;
        static constexpr size_t HEADER_SIZE      = 32;
        static constexpr size_t BOX_NAME_SIZE    = 32;
        static constexpr size_t SLOT_RECORD_SIZE = 4 + SLOT_DATA_MAX; // 236
        static constexpr size_t BOX_RECORD_SIZE  = BOX_NAME_SIZE + BOX_SLOTS * SLOT_RECORD_SIZE;
        static constexpr int DEFAULT_BOXES       = 50;
        static constexpr int MAX_BOXES           = 500;

        // Loads the bank, falling back to <path>.bak if the main file is damaged.
        // Creates a fresh bank only if neither file exists. If a file exists but cannot be
        // parsed, the bank opens read-only so it is never overwritten.
        bool load(const std::string& path, std::string& err);
        // Backs up the previous bank file once per session, then writes atomically.
        bool save(std::string& err);

        [[nodiscard]] int boxCount() const { return (int)boxes.size(); }
        [[nodiscard]] Box& box(int i) { return boxes[i]; }
        [[nodiscard]] const Box& box(int i) const { return boxes[i]; }
        [[nodiscard]] bool dirty() const { return isDirty; }
        void markDirty() { isDirty = true; }
        [[nodiscard]] bool readOnly() const { return isReadOnly; }
        [[nodiscard]] const std::string& readOnlyReason() const { return roReason; }

        [[nodiscard]] std::vector<uint8_t> serialize() const;
        // Parses a full file image. Returns false (and leaves *this untouched) on any error.
        bool deserialize(const std::vector<uint8_t>& file, std::string& err);

    private:
        void createDefault(int count);

        std::vector<Box> boxes;
        std::string path;
        bool isDirty         = false;
        bool isReadOnly      = false;
        bool backedUpSession = false;
        std::string roReason;
    };
}
