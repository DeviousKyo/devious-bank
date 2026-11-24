#include "core/db_bank.hpp"
#include "core/db_backup.hpp"
#include "core/db_fs.hpp"

#include <algorithm>
#include <cstring>

namespace db
{
    namespace
    {
        void wr16(uint8_t* p, uint16_t v)
        {
            p[0] = v & 0xFF;
            p[1] = v >> 8;
        }
        void wr32(uint8_t* p, uint32_t v)
        {
            for (int i = 0; i < 4; i++)
            {
                p[i] = (v >> (8 * i)) & 0xFF;
            }
        }
        uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
        uint32_t rd32(const uint8_t* p)
        {
            return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
        }
    }

    void Bank::createDefault(int count)
    {
        boxes.assign(count, Box{});
        for (int i = 0; i < count; i++)
        {
            boxes[i].name = "Bank " + std::to_string(i + 1);
        }
    }

    std::vector<uint8_t> Bank::serialize() const
    {
        std::vector<uint8_t> out(HEADER_SIZE + boxes.size() * BOX_RECORD_SIZE, 0);
        wr32(&out[0x00], MAGIC);
        wr16(&out[0x04], VERSION);
        wr16(&out[0x06], (uint16_t)boxes.size());
        wr32(&out[0x08], SLOT_RECORD_SIZE);

        uint8_t* p = out.data() + HEADER_SIZE;
        for (const Box& b : boxes)
        {
            std::memcpy(p, b.name.data(), std::min(b.name.size(), BOX_NAME_SIZE - 1));
            p += BOX_NAME_SIZE;
            for (const Slot& s : b.slots)
            {
                p[0] = (uint8_t)s.format;
                p[1] = s.flags;
                wr16(p + 2, s.empty() ? 0 : s.length);
                if (!s.empty())
                {
                    std::memcpy(p + 4, s.data.data(), SLOT_DATA_MAX);
                }
                p += SLOT_RECORD_SIZE;
            }
        }
        wr32(&out[0x0C], fs::crc32(out.data() + HEADER_SIZE, out.size() - HEADER_SIZE));
        return out;
    }

    bool Bank::deserialize(const std::vector<uint8_t>& file, std::string& err)
    {
        if (file.size() < HEADER_SIZE || rd32(&file[0]) != MAGIC)
        {
            err = "Not a Devious Bank file";
            return false;
        }
        if (rd16(&file[4]) != VERSION)
        {
            err = "Unsupported bank version " + std::to_string(rd16(&file[4]));
            return false;
        }
        const uint16_t count = rd16(&file[6]);
        if (rd32(&file[8]) != SLOT_RECORD_SIZE || count == 0 || count > MAX_BOXES)
        {
            err = "Bank header is invalid";
            return false;
        }
        if (file.size() != HEADER_SIZE + count * BOX_RECORD_SIZE)
        {
            err = "Bank file size is wrong (truncated?)";
            return false;
        }
        if (rd32(&file[0x0C]) != fs::crc32(file.data() + HEADER_SIZE, file.size() - HEADER_SIZE))
        {
            err = "Bank CRC mismatch (file damaged)";
            return false;
        }

        std::vector<Box> loaded(count);
        const uint8_t* p = file.data() + HEADER_SIZE;
        for (Box& b : loaded)
        {
            b.name.assign((const char*)p, strnlen((const char*)p, BOX_NAME_SIZE));
            p += BOX_NAME_SIZE;
            for (Slot& s : b.slots)
            {
                const uint8_t fmt = p[0];
                s.flags           = p[1];
                s.length          = rd16(p + 2);
                std::memcpy(s.data.data(), p + 4, SLOT_DATA_MAX);
                if (fmt > (uint8_t)PkxFormat::PK7)
                {
                    // Unknown format from a future version: keep bytes, lock the slot.
                    s.format = PkxFormat::PK7;
                    s.flags |= SLOT_CORRUPT;
                }
                else
                {
                    s.format = (PkxFormat)fmt;
                    if (!s.empty() && !validLength(s.format, s.length))
                    {
                        s.flags |= SLOT_CORRUPT;
                    }
                }
                if (s.length > SLOT_DATA_MAX)
                {
                    s.length = SLOT_DATA_MAX;
                    s.flags |= SLOT_CORRUPT;
                }
                p += SLOT_RECORD_SIZE;
            }
        }
        boxes = std::move(loaded);
        return true;
    }

    bool Bank::load(const std::string& filePath, std::string& err)
    {
        path       = filePath;
        isDirty    = false;
        isReadOnly = false;
        roReason.clear();

        const bool mainExists = fs::exists(path);
        const bool bakExists  = fs::exists(path + ".bak");
        if (!mainExists && !bakExists)
        {
            createDefault(DEFAULT_BOXES);
            isDirty = true; // written on first save
            return true;
        }

        std::string mainErr;
        if (mainExists)
        {
            auto data = fs::readFile(path, HEADER_SIZE + MAX_BOXES * BOX_RECORD_SIZE);
            if (data && deserialize(*data, mainErr))
            {
                return true;
            }
            if (!data)
            {
                mainErr = "Could not read bank file";
            }
        }

        std::string bakErr;
        if (bakExists)
        {
            auto data = fs::readFile(path + ".bak", HEADER_SIZE + MAX_BOXES * BOX_RECORD_SIZE);
            if (data && deserialize(*data, bakErr))
            {
                // Recovered from the previous version. Keep the damaged main file untouched
                // until the user saves, and tell them.
                err = "bank.dbk damaged (" + mainErr + "); loaded bank.dbk.bak";
                isDirty = true;
                return true;
            }
        }

        // A bank file exists but nothing could be parsed: never overwrite it.
        createDefault(1);
        isReadOnly = true;
        roReason   = mainErr.empty() ? bakErr : mainErr;
        err        = "Bank could not be loaded: " + roReason;
        return false;
    }

    bool Bank::save(std::string& err)
    {
        if (isReadOnly)
        {
            err = "Bank is read-only: " + roReason;
            return false;
        }
        if (!fs::mkdirs(fs::ROOT_DIR))
        {
            err = "Could not create " + std::string(fs::ROOT_DIR);
            return false;
        }

        // First write of the session: keep a timestamped copy of the bank as it was.
        if (!backedUpSession && fs::exists(path))
        {
            auto old = fs::readFile(path, HEADER_SIZE + MAX_BOXES * BOX_RECORD_SIZE);
            if (!old)
            {
                err = "Could not read existing bank for backup";
                return false;
            }
            BackupResult b = createBackup("BANK", "bank.dbk", old->data(), old->size());
            if (!b.ok)
            {
                err = b.error;
                return false;
            }
            backedUpSession = true;
        }

        std::vector<uint8_t> image = serialize();
        if (!fs::writeFileAtomic(path, image.data(), image.size()))
        {
            err = "Writing bank failed (previous bank kept)";
            return false;
        }
        for (Box& b : boxes)
        {
            b.dirty.fill(false);
        }
        isDirty = false;
        return true;
    }
}
