#include "core/db_save.hpp"
#include "core/db_backup.hpp"
#include "core/db_fs.hpp"

#include "sav/SaveRecognition.hpp"
#include "utils/i18n.hpp"

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
                    loc.label   = gameLabel(*sav) +
                                (media == MEDIATYPE_GAME_CARD ? " [Cart]" : " [Digital]");
                    out.push_back(loc);
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
                loc.label  = gameLabel(*sav) + " [SD: " + p.substr(p.find_last_of('/') + 1) + "]";
                out.push_back(loc);
            }
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
            Archive arch(loc.media, loc.titleId);
            return arch && archiveRead(arch.get(), loc.path, out);
        }

        bool writeLocation(const SaveLocation& loc, const uint8_t* data, size_t len)
        {
            if (loc.medium == SaveMedium::SdFile)
            {
                return fs::writeFileAtomic(loc.path, data, len);
            }
            Archive arch(loc.media, loc.titleId);
            return arch && archiveWrite(arch.get(), loc.path, data, len);
        }
    }

    std::vector<SaveLocation> scanSaves()
    {
        std::vector<SaveLocation> out;
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
        src->loc  = loc;
        src->size = bytes.size();
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

        src->rebuildBoxes();
        return src;
    }

    std::string SaveSource::gameName() const
    {
        return sav ? gameLabel(*sav) : "?";
    }

    void SaveSource::rebuildBoxes()
    {
        const int count = sav->maxBoxes();
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

        for (int b = 0; b < boxCount(); b++)
        {
            for (int s = 0; s < BOX_SLOTS; s++)
            {
                if (!boxes[b].dirty[s])
                {
                    continue;
                }
                const Slot& slot = boxes[b].slots[s];
                if (slot.empty())
                {
                    sav->pkm(*sav->emptyPkm(), (u8)b, (u8)s, false);
                    continue;
                }
                if (slot.format != fmt)
                {
                    err = "Internal error: wrong format in box " + std::to_string(b + 1);
                    return false;
                }
                auto pk = slot.toPkx();
                if (!pk)
                {
                    err = "Could not parse Pokemon for box " + std::to_string(b + 1);
                    return false;
                }
                // applyTrade = false: the Pokemon's bytes are written exactly as stored.
                sav->pkm(*pk, (u8)b, (u8)s, false);
            }
        }

        sav->finishEditing(); // checksums, Gen 7 signature, Gen 1/2 box compaction
        const size_t len   = sav->getEntireLengthIncludingFooter();
        const uint32_t crc = fs::crc32(buffer.get(), len);
        bool ok            = writeLocation(loc, buffer.get(), len);
        if (ok)
        {
            std::vector<uint8_t> check;
            ok = readLocation(loc, check) && check.size() == len &&
                 fs::crc32(check.data(), len) == crc;
            if (!ok)
            {
                err = "Save written but verification failed. Backup: " + backup;
            }
        }
        else
        {
            err = "Writing the save failed. Backup: " + backup;
        }
        sav->beginEditing();

        if (ok)
        {
            rebuildBoxes(); // reflects Gen 1/2 compaction; clears dirty flags
        }
        return ok;
    }
}
