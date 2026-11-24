// SD card file helpers: RAII file handle, directory creation, CRC32 and
// crash-safe atomic file replacement.
#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace db::fs
{
    // Root folder for everything Devious Bank writes to the SD card.
    inline constexpr const char* ROOT_DIR    = "sdmc:/3ds/DB";
    inline constexpr const char* BACKUP_DIR  = "sdmc:/3ds/DB/backups";
    inline constexpr const char* SAVES_DIR   = "sdmc:/3ds/DB/saves";
    inline constexpr const char* BANK_PATH   = "sdmc:/3ds/DB/bank.dbk";

    // Closes the FILE* when it goes out of scope, so early returns never leak handles.
    class File
    {
    public:
        File() = default;
        File(const std::string& path, const char* mode) : f(std::fopen(path.c_str(), mode)) {}
        ~File() { close(); }
        File(const File&)            = delete;
        File& operator=(const File&) = delete;

        explicit operator bool() const { return f != nullptr; }
        FILE* get() const { return f; }
        // Returns false if the final flush failed (data may not be on the card).
        bool close()
        {
            if (!f)
            {
                return true;
            }
            bool ok = std::fclose(f) == 0;
            f       = nullptr;
            return ok;
        }

    private:
        FILE* f = nullptr;
    };

    uint32_t crc32(const uint8_t* data, size_t len);

    bool exists(const std::string& path);
    bool isDirectory(const std::string& path);
    // mkdir -p
    bool mkdirs(const std::string& path);
    std::optional<std::vector<uint8_t>> readFile(const std::string& path, size_t maxSize = 0x200000);
    // Writes the file, closes it, then reads it back and compares CRC32.
    bool writeFileVerified(const std::string& path, const uint8_t* data, size_t len);
    // Writes <path>.tmp, verifies it, keeps the previous file as <path>.bak, then
    // renames .tmp into place. A crash at any point leaves either the old or the new
    // file intact (plus a .bak).
    bool writeFileAtomic(const std::string& path, const uint8_t* data, size_t len);

    // "YYYYMMDD_HHMMSS" from the console clock.
    std::string timestamp();
    // Replaces anything that is not [A-Za-z0-9-] with '_' so it is safe in a path.
    std::string sanitize(const std::string& s);
}
