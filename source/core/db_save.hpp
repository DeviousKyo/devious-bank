// Game save access: discovery, load (+ mandatory verified backup) and write-back.
//
// Two places a save can come from in this version:
//   - TitleArchive: a 3DS title's save archive (Gen 6/7 retail carts and digital copies,
//                   Gen 1/2 Virtual Console). Read/written through FS:USER.
//   - SdFile:       a .sav file in sdmc:/3ds/DB/saves/ (Gen 3 GBA dumps, Gen 4/5 DS saves
//                   from flashcarts/emulators, or any of the above).
// Reading DS cartridges directly over SPI is planned but not implemented yet.
#pragma once

#include "core/db_slot.hpp"
#include "sav/Sav.hpp"

#include <3ds.h>
#include <memory>
#include <string>
#include <vector>

namespace db
{
    enum class SaveMedium : uint8_t
    {
        TitleArchive,
        SdFile,
    };

    struct SaveLocation
    {
        SaveMedium medium     = SaveMedium::SdFile;
        FS_MediaType media    = MEDIATYPE_SD; // TitleArchive only
        uint64_t titleId      = 0;            // TitleArchive only
        std::string path;                     // file inside the archive, or full SD path
        std::string label;                    // shown in the game list
    };

    // Lists every save PKSM-Core recognizes as Gen 1-7. Slow-ish: reads each candidate.
    std::vector<SaveLocation> scanSaves();

    class SaveSource
    {
    public:
        // Reads the save, parses it, and creates a verified backup. If the backup fails the
        // save still opens, but read-only.
        static std::unique_ptr<SaveSource> open(const SaveLocation& loc, std::string& err);

        [[nodiscard]] const SaveLocation& location() const { return loc; }
        [[nodiscard]] PkxFormat format() const { return fmt; }
        [[nodiscard]] std::string gameName() const;
        [[nodiscard]] int boxCount() const { return (int)boxes.size(); }
        [[nodiscard]] Box& box(int i) { return boxes[i]; }
        [[nodiscard]] bool readOnly() const { return !roReason.empty(); }
        [[nodiscard]] const std::string& readOnlyReason() const { return roReason; }
        [[nodiscard]] const std::string& backupPath() const { return backup; }
        [[nodiscard]] bool dirty() const;

        // Writes every dirty slot into the save, recomputes checksums/signatures
        // (PKSM-Core finishEditing), writes it back, and verifies by re-reading.
        bool commit(std::string& err);

    private:
        SaveSource() = default;
        void rebuildBoxes();

        SaveLocation loc;
        std::shared_ptr<u8[]> buffer;
        size_t size = 0;
        std::unique_ptr<pksm::Sav> sav;
        PkxFormat fmt = PkxFormat::Empty;
        std::vector<Box> boxes;
        std::string roReason;
        std::string backup;
    };
}
