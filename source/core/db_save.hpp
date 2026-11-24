// Game save access: discovery, load (+ mandatory verified backup) and write-back.
//
// Three places a save can come from:
//   - TitleArchive: a 3DS title's save archive (Gen 6/7 retail carts and digital copies,
//                   Gen 1/2 Virtual Console). Read/written through FS:USER.
//   - DsCart:       a Gen 4/5 DS cartridge in the game card slot, over SPI (db_dscart).
//   - SdFile:       a .sav file in sdmc:/3ds/DB/saves/ (Gen 3 GBA dumps, Gen 4/5 DS saves
//                   from flashcarts/emulators, or any of the above).
//
// Party Pokemon are never edited: only box slots are written, and commit() refuses to
// write a save image whose party differs from the one recorded when it was opened.
#pragma once

#include "core/db_dscart.hpp"
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
        DsCart,
        SdFile,
    };

    // The game's own icon, read from the console (no bundled art):
    //   Smdh48 - 3DS titles and Virtual Console: the HOME Menu icon, 48x48 RGB565, already
    //            in the GPU's 8x8-tile Morton order (0x1200 bytes)
    //   Ds32   - DS cartridges: the banner icon, decoded to 32x32 RGBA8 in GPU tile order
    //            (4096 bytes)
    // SD .sav files have no icon anywhere on the console: None.
    enum class IconKind : uint8_t
    {
        None,
        Smdh48,
        Ds32,
    };

    struct SaveLocation
    {
        SaveMedium medium     = SaveMedium::SdFile;
        FS_MediaType media    = MEDIATYPE_SD; // TitleArchive only
        uint64_t titleId      = 0;            // TitleArchive only
        dscart::CartInfo cart;                // DsCart only
        std::string path;                     // file inside the archive, SD path, or game code
        std::string label;                    // full "<game> [<tag>]" text for messages
        std::string game;                     // game name, e.g. "HeartGold"
        std::string tag;                      // where it lives, e.g. "DS Cart", "Digital"
        PkxFormat format = PkxFormat::Empty;  // generation
        pksm::GameVersion version;            // which game
        IconKind iconKind = IconKind::None;
        std::vector<uint8_t> icon;            // pixel data, see IconKind
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
        // The party (count + the raw bytes of each party Pokemon) as stored in the save.
        // Devious Bank never edits the party; commit() refuses to write any image whose
        // party differs from the one recorded when the save was opened.
        struct PartySnapshot
        {
            unsigned count = 0;
            std::vector<std::vector<uint8_t>> mons;
            bool operator==(const PartySnapshot&) const = default;
        };
        static PartySnapshot snapshotParty(const pksm::Sav& s);

        SaveSource() = default;
        void rebuildBoxes();
        PartySnapshot party;

        SaveLocation loc;
        std::vector<uint8_t> onMedium; // exact bytes last read from / written to the medium
        std::shared_ptr<u8[]> buffer;
        size_t size = 0;
        std::unique_ptr<pksm::Sav> sav;
        PkxFormat fmt = PkxFormat::Empty;
        std::vector<Box> boxes;
        std::string roReason;
        std::string backup;
    };
}
