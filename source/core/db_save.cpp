#include "core/db_save.hpp"
#include "core/db_backup.hpp"
#include "core/db_fs.hpp"

#include "sav/SaveRecognition.hpp"
#include "utils/i18n.hpp"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace db
{
    namespace
    {
        // ---- 3DS save archive I/O ------------------------------------------------------

        // Closes the archive when it goes out of scope.
        class Archive
        {
        public:
            Archive(FS_MediaType media, uint64_t titleId)
            {
                pathData[0] = (u32)media;
                pathData[1] = (u32)(titleId & 0xFFFFFFFF);
                pathData[2] = (u32)(titleId >> 32);
                FS_Path p   = {PATH_BINARY, sizeof(pathData), pathData};
                res         = FSUSER_OpenArchive(&handle, ARCHIVE_USER_SAVEDATA, p);
            }
            ~Archive()
            {
                if (R_SUCCEEDED(res))
                {
                    FSUSER_CloseArchive(handle);
                }
            }
            Archive(const Archive&)            = delete;
            Archive& operator=(const Archive&) = delete;

            explicit operator bool() const { return R_SUCCEEDED(res); }
            FS_Archive get() const { return handle; }

        private:
            u32 pathData[3];
            FS_Archive handle = 0;
            Result res        = -1;
        };

        // Closes the file handle when it goes out of scope.
        struct ArchiveFile
        {
            Handle h = 0;
            ~ArchiveFile()
            {
                if (h)
                {
                    FSFILE_Close(h);
                }
            }
        };

        std::u16string toU16(const std::string& s)
        {
            return std::u16string(s.begin(), s.end()); // archive paths are ASCII
        }

        FS_Path u16Path(const std::u16string& s)
        {
            return FS_Path{PATH_UTF16, (s.size() + 1) * 2, s.c_str()};
        }

        bool archiveRead(FS_Archive arch, const std::string& path, std::vector<uint8_t>& out)
        {
            const std::u16string p = toU16(path);
            ArchiveFile f;
            if (R_FAILED(FSUSER_OpenFile(&f.h, arch, u16Path(p), FS_OPEN_READ, 0)))
            {
                return false;
            }
            u64 size = 0;
            if (R_FAILED(FSFILE_GetSize(f.h, &size)) || size == 0 || size > 0x100000)
            {
                return false;
            }
            out.resize(size);
            u32 read = 0;
            return R_SUCCEEDED(FSFILE_Read(f.h, &read, 0, out.data(), (u32)size)) && read == size;
        }

        bool archiveWrite(FS_Archive arch, const std::string& path, const uint8_t* data, size_t len)
        {
            const std::u16string p = toU16(path);
            {
                ArchiveFile f;
                if (R_FAILED(FSUSER_OpenFile(&f.h, arch, u16Path(p), FS_OPEN_WRITE, 0)))
                {
                    return false;
                }
                u32 written = 0;
                if (R_FAILED(FSFILE_Write(f.h, &written, 0, data, (u32)len, FS_WRITE_FLUSH)) ||
                    written != len)
                {
                    return false;
                }
            }
            // Save archives are transactional: nothing is persisted until committed.
            return R_SUCCEEDED(
                FSUSER_ControlArchive(arch, ARCHIVE_ACTION_COMMIT_SAVE_DATA, nullptr, 0, nullptr, 0));
        }

        // Sizes of the save files Gen 1-7 games keep on a 3DS (VC GB saves, XY, ORAS, SM, USUM).
        bool candidateSize(u64 size)
        {
            return size == pksm::SaveSize::GB_INT || size == pksm::SaveSize::GB_JPN ||
                   size == 0x65600 || size == 0x76000 || size == 0x6BE00 || size == 0x6CC00;
        }

        // Parses a file image. Returns the save only if it is Gen 1-7.
        std::unique_ptr<pksm::Sav> parse(
            const std::vector<uint8_t>& bytes, std::shared_ptr<u8[]>& bufOut)
        {
            bufOut = std::shared_ptr<u8[]>(new u8[bytes.size()]);
            std::copy(bytes.begin(), bytes.end(), bufOut.get());
            std::unique_ptr<pksm::Sav> sav;
            try
            {
                sav = pksm::Sav::getSave(bufOut, bytes.size());
            }
            catch (...)
            {
                return nullptr;
            }
            PkxFormat f;
            if (!sav || !formatFromGeneration(sav->generation(), f))
            {
                return nullptr;
            }
            return sav;
        }

        std::string gameLabel(const pksm::Sav& sav)
        {
            return i18n::game(pksm::Language::ENG, sav.version());
        }

        // Fills the display fields of a location found by a scan.
        void describe(SaveLocation& loc, const pksm::Sav& sav, const std::string& tag)
        {
            loc.game    = gameLabel(sav);
            loc.tag     = tag;
            loc.version = sav.version();
            loc.label = loc.game + " [" + tag + "]";
            (void)formatFromGeneration(sav.generation(), loc.format);
        }

        void scanTitles(FS_MediaType media, std::vector<SaveLocation>& out)
        {
            u32 count = 0;
            if (R_FAILED(AM_GetTitleCount(media, &count)) || count == 0)
            {
                return;
            }
            std::vector<u64> ids(count);
            u32 read = 0;
            if (R_FAILED(AM_GetTitleList(&read, media, count, ids.data())))
            {
                return;
            }
            ids.resize(read);

            for (u64 tid : ids)
            {
                if ((tid >> 32) != 0x00040000) // applications only
                {
                    continue;
                }
                Archive arch(media, tid);
                if (!arch)
                {
                    continue;
                }

                Handle dir = 0;
                const std::u16string root = u"/";
                if (R_FAILED(FSUSER_OpenDirectory(&dir, arch.get(), u16Path(root))))
                {
                    continue;
                }
                std::vector<std::string> files;
                FS_DirectoryEntry entry;
                u32 n = 0;
                while (R_SUCCEEDED(FSDIR_Read(dir, &n, 1, &entry)) && n == 1)
                {
                    if (!(entry.attributes & FS_ATTRIBUTE_DIRECTORY) &&
                        candidateSize(entry.fileSize))
                    {
                        std::string name = "/";
                        for (int i = 0; i < 0x106 && entry.name[i]; i++)
                        {
                            name += (char)entry.name[i];
                        }
                        files.push_back(name);
                    }
                }
                FSDIR_Close(dir);

                for (const std::string& file : files)
                {
                    std::vector<uint8_t> bytes;
                    std::shared_ptr<u8[]> buf;
                    if (!archiveRead(arch.get(), file, bytes))
                    {
                        continue;
                    }
                    auto sav = parse(bytes, buf);
                    if (!sav)
                    {
                        continue;
                    }
                    SaveLocation loc;
                    loc.medium  = SaveMedium::TitleArchive;
                    loc.media   = media;
                    loc.titleId = tid;
                    loc.path    = file;
                    PkxFormat f = PkxFormat::Empty;
                    (void)formatFromGeneration(sav->generation(), f);
                    const bool vc = f <= PkxFormat::PK2;
                    describe(loc, *sav,
                        media == MEDIATYPE_GAME_CARD ? "3DS Cart"
                                                     : (vc ? "Virtual Console" : "Digital"));                    out.push_back(loc);
                    break; // one save per title
                }
            }
        }

        void scanSdSaves(std::vector<SaveLocation>& out)
        {
            fs::mkdirs(fs::SAVES_DIR);
            DIR* d = opendir(fs::SAVES_DIR);
            if (!d)
            {
                return;
            }
            std::vector<std::string> paths;
            while (dirent* e = readdir(d))
            {
                std::string name = e->d_name;
                if (name == "." || name == "..")
                {
                    continue;
                }
                // writeFileAtomic leaves <name>.bak (previous version) and, after a failed
                // write, <name>.tmp next to each save; they are not saves of their own.
                const auto endsWith = [&name](const char* ext)
                {
                    const size_t n = std::strlen(ext);
                    return name.size() >= n && name.compare(name.size() - n, n, ext) == 0;
                };
                if (endsWith(".bak") || endsWith(".tmp"))
                {
                    continue;
                }
                std::string full = std::string(fs::SAVES_DIR) + "/" + name;
                struct stat st;
                if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 &&
                    st.st_size <= 0x100000)
                {
                    paths.push_back(full);
                }
            }
            closedir(d);

            for (const std::string& p : paths)
            {
                auto bytes = fs::readFile(p, 0x100000);
                std::shared_ptr<u8[]> buf;
                if (!bytes)
                {
                    continue;
                }
                auto sav = parse(*bytes, buf);
                if (!sav)
                {
                    continue;
                }
                SaveLocation loc;
                loc.medium = SaveMedium::SdFile;
                loc.path   = p;
                describe(loc, *sav, "SD: " + p.substr(p.find_last_of('/') + 1));
                out.push_back(loc);
            }
        }

        void scanDsCart(std::vector<SaveLocation>& out)
        {
            dscart::CartInfo cart;
            std::string err;
            if (!dscart::detect(cart, err))
            {
                return; // no cart, a 3DS cart, or not a Gen 4/5 Pokemon game
            }
            std::vector<uint8_t> bytes;
            if (!dscart::read(cart, bytes, err))
            {
                return;
            }
            std::shared_ptr<u8[]> buf;
            auto sav = parse(bytes, buf);
            if (!sav)
            {
                return;
            }
            SaveLocation loc;
            loc.medium = SaveMedium::DsCart;
            loc.cart   = cart;
            loc.path   = cart.gameCode; // also names the backup file
            describe(loc, *sav, "DS Cart");
            out.push_back(loc);
        }

        bool readLocation(const SaveLocation& loc, std::vector<uint8_t>& out)
        {
            if (loc.medium == SaveMedium::SdFile)
            {
                auto bytes = fs::readFile(loc.path, 0x100000);
                if (!bytes)
                {
                    return false;
                }
                out = std::move(*bytes);
                return true;
            }
            if (loc.medium == SaveMedium::DsCart)
            {
                // Re-identify first: the cart in the slot may not be the one that was scanned.
                dscart::CartInfo now;
                std::string err;
                return dscart::detect(now, err) && now.gameCode == loc.cart.gameCode &&
                       now.chipType == loc.cart.chipType && dscart::read(now, out, err);
            }
            Archive arch(loc.media, loc.titleId);
            return arch && archiveRead(arch.get(), loc.path, out);
        }

        // `expected`: what the medium held when last read (DS carts check it before writing,
        // and update it to what the cart really holds after a write that failed partway).
        bool writeLocation(const SaveLocation& loc, const uint8_t* data, size_t len,
            std::vector<uint8_t>& expected, std::string& err)
        {
            if (loc.medium == SaveMedium::SdFile)
            {
                return fs::writeFileAtomic(loc.path, data, len);
            }
            if (loc.medium == SaveMedium::DsCart)
            {
                return dscart::write(loc.cart, data, len, expected, err);
            }
            Archive arch(loc.media, loc.titleId);
            return arch && archiveWrite(arch.get(), loc.path, data, len);
        }
    }

    std::vector<SaveLocation> scanSaves()
    {
        std::vector<SaveLocation> out;
        scanDsCart(out);
        scanTitles(MEDIATYPE_GAME_CARD, out);
        scanTitles(MEDIATYPE_SD, out);
        scanSdSaves(out);
        return out;
    }

    std::unique_ptr<SaveSource> SaveSource::open(const SaveLocation& loc, std::string& err)
    {
        std::vector<uint8_t> bytes;
        if (!readLocation(loc, bytes))
        {
            err = "Could not read save: " + loc.path;
            return nullptr;
        }

        std::unique_ptr<SaveSource> src(new SaveSource());
        src->loc      = loc;
        src->size     = bytes.size();
        src->onMedium = bytes;
        src->sav  = parse(bytes, src->buffer);
        if (!src->sav)
        {
            err = "Not a Gen 1-7 Pokemon save: " + loc.path;
            return nullptr;
        }
        (void)formatFromGeneration(src->sav->generation(), src->fmt);

        // Backup the bytes exactly as read from the medium, before anything can change.
        BackupResult b = createBackup(gameLabel(*src->sav), loc.path, bytes.data(), bytes.size());
        if (b.ok)
        {
            src->backup = b.path;
        }
        else
        {
            src->roReason = "Backup failed, opened read-only: " + b.error;
        }

        src->party = snapshotParty(*src->sav);
        src->rebuildBoxes();
        return src;
    }

    SaveSource::PartySnapshot SaveSource::snapshotParty(const pksm::Sav& s)
    {
        PartySnapshot p;
        p.count = s.partyCount();
        for (u8 i = 0; i < 6; i++)
        {
            auto pk = s.pkm(i);
            if (pk)
            {
                auto raw = pk->rawData();
                p.mons.emplace_back(raw.begin(), raw.end());
            }
            else
            {
                p.mons.emplace_back();
            }
        }
        return p;
    }

    std::string SaveSource::gameName() const
    {
        return sav ? gameLabel(*sav) : "?";
    }

    int SaveSource::slotsPerBox() const
    {
        // International Gen 1/2 boxes hold 20 (Japanese 30); PKSM-Core's box writes silently
        // ignore anything past that, so those slots must never take a Pokemon.
        const int boxesInSave = sav->maxBoxes();
        const int perBox      = boxesInSave > 0 ? sav->maxSlot() / boxesInSave : 0;
        return std::clamp(perBox, 0, BOX_SLOTS);
    }

    bool SaveSource::canHold(const Slot& s, std::string& why) const
    {
        if (s.empty())
        {
            return true;
        }
        if (!sav || s.format != fmt)
        {
            why = std::string(formatName(s.format)) + " can't go into a " + formatName(fmt) +
                  " save";
            return false;
        }
        if (!validLength(s.format, s.length))
        {
            why = "That Pokemon's data is damaged";
            return false;
        }
        if (fmt <= PkxFormat::PK2)
        {
            // Japanese and international Gen 1/2 records differ in name length, and PKSM-Core
            // rewrites the nickname and OT when they cross over. Only same-family records
            // are written byte for byte.
            const size_t want = sav->emptyPkm()->rawData().size();
            if (s.length != want)
            {
                why = s.length < want
                          ? "A Japanese Gen 1/2 Pokemon can't go into an international save"
                          : "An international Gen 1/2 Pokemon can't go into a Japanese save";
                return false;
            }
        }
        return true;
    }

    void SaveSource::rebuildBoxes()
    {
        const int count    = sav->maxBoxes();
        const int capacity = slotsPerBox();
        boxes.assign(count, Box{});
        for (int b = 0; b < count; b++)
        {
            boxes[b].name = sav->boxName(b);
            if (boxes[b].name.empty())
            {
                boxes[b].name = "Box " + std::to_string(b + 1);
            }
            for (int s = 0; s < BOX_SLOTS; s++)
            {
                if (s >= capacity)
                {
                    // Not part of the game's box: shown as unusable so nothing goes there.
                    boxes[b].slots[s].flags = SLOT_LOCKED;
                    continue;
                }
                auto pk = sav->pkm((u8)b, (u8)s);
                if (pk)
                {
                    boxes[b].slots[s] = Slot::fromPkx(*pk);
                }
            }
        }
    }

    bool SaveSource::dirty() const
    {
        for (const Box& b : boxes)
        {
            for (bool d : b.dirty)
            {
                if (d)
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool SaveSource::commit(std::string& err)
    {
        if (!dirty())
        {
            return true;
        }
        if (readOnly() || backup.empty())
        {
            err = roReason.empty() ? "No verified backup; refusing to write" : roReason;
            return false;
        }

        // ---- validate every dirty slot before touching the save
        const int capacity = slotsPerBox();
        std::vector<std::unique_ptr<pksm::PKX>> parsed(boxCount() * BOX_SLOTS);
        for (int b = 0; b < boxCount(); b++)
        {
            for (int s = 0; s < BOX_SLOTS; s++)
            {
                if (!boxes[b].dirty[s])
                {
                    continue;
                }
                if (s >= capacity)
                {
                    // PKSM-Core would silently drop it: the Pokemon would vanish on save.
                    err = "Internal error: box " + std::to_string(b + 1) + " slot " +
                          std::to_string(s + 1) + " is past the game's box size; nothing "
                          "was written";
                    return false;
                }
                const Slot& slot = boxes[b].slots[s];
                if (slot.empty())
                {
                    continue;
                }
                std::string why;
                if (!canHold(slot, why))
                {
                    err = "Box " + std::to_string(b + 1) + ": " + why + "; nothing was written";
                    return false;
                }
                auto pk = slot.toPkx();
                if (!pk)
                {
                    err = "Could not parse Pokemon for box " + std::to_string(b + 1) +
                          "; nothing was written";
                    return false;
                }
                parsed[b * BOX_SLOTS + s] = std::move(pk);
            }
        }

        // The save exactly as it is before this attempt. Any failure from here on puts it
        // back, so the in-memory save matches `boxes` again (Gen 1/2 finishEditing compacts
        // the box lists, which would shift every index) and a retry re-applies the same
        // moves from the dirty flags, which are left as they are.
        // Taken from `onMedium` (the exact file last read from / written to the medium)
        // rather than from `buffer`: the in-memory save is never changed outside commit(),
        // so the two describe the same save, but `onMedium` is guaranteed to be in on-disk
        // form and therefore always re-parses. Copied now, before writeLocation() can update
        // `onMedium` to a partly written DS cart.
        const std::vector<uint8_t> before = onMedium;
        const auto restore = [this, &before]()
        {
            std::shared_ptr<u8[]> restoredBuf;
            auto restored = parse(before, restoredBuf);
            if (!restored)
            {
                // Can't happen (these bytes parsed before); never write from a bad state.
                roReason = "Could not restore the save after a failed write. Reopen the save.";
                return;
            }
            // The Sav keeps its own reference to its buffer, so the order doesn't matter.
            sav    = std::move(restored);
            buffer = std::move(restoredBuf);
        };

        for (int b = 0; b < boxCount(); b++)
        {
            for (int s = 0; s < BOX_SLOTS; s++)
            {
                if (!boxes[b].dirty[s])
                {
                    continue;
                }
                const auto& pk = parsed[b * BOX_SLOTS + s];
                // applyTrade = false: the Pokemon's bytes are written exactly as stored.
                sav->pkm(pk ? *pk : *sav->emptyPkm(), (u8)b, (u8)s, false);
            }
        }

        // Party guard 1: the box writes above must not have touched the party.
        if (!(snapshotParty(*sav) == party))
        {
            restore();
            roReason = "Party check failed before saving; nothing was written. Reopen the save.";
            err      = roReason;
            return false;
        }

        sav->finishEditing(); // checksums, Gen 7 signature, Gen 1/2 box compaction
        const size_t len   = sav->getEntireLengthIncludingFooter();
        const uint32_t crc = fs::crc32(buffer.get(), len);

        // Party guard 2: parse the exact image about to be written, from a separate copy,
        // and compare its party with the one recorded at load.
        {
            std::vector<uint8_t> image(buffer.get(), buffer.get() + len);
            std::shared_ptr<u8[]> checkBuf;
            auto check = parse(image, checkBuf);
            if (!check || !(snapshotParty(*check) == party))
            {
                restore();
                roReason = "Party check failed on the final save image; nothing was written. "
                           "Reopen the save.";
                err = roReason;
                return false;
            }
        }

        std::string writeErr;
        bool ok = writeLocation(loc, buffer.get(), len, onMedium, writeErr);
        if (ok)
        {
            std::vector<uint8_t> check;
            const bool readBack = readLocation(loc, check);
            ok = readBack && check.size() == len && fs::crc32(check.data(), len) == crc;
            if (readBack)
            {
                onMedium = std::move(check); // whatever is really there now
            }
            if (!ok)
            {
                err = "Save written but verification failed. Retry saving; if that also "
                      "fails, restore the backup: " + backup;
            }
        }
        else
        {
            err = (writeErr.empty() ? std::string("Writing the save failed") : writeErr) +
                  ". Backup: " + backup;
        }

        if (!ok)
        {
            restore(); // boxes and dirty flags untouched: a retry writes the same moves
            return false;
        }
        sav->beginEditing();
        rebuildBoxes(); // reflects Gen 1/2 compaction; clears dirty flags
        return true;
    }
}
