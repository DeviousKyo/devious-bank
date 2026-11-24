// Timestamped, verified save backups on the SD card.
#pragma once

#include <cstdint>
#include <string>

namespace db
{
    struct BackupResult
    {
        bool ok = false;
        std::string path;  // full path of the backup file when ok
        std::string error; // human-readable reason when !ok
    };

    // Writes sdmc:/3ds/DB/backups/<tag>_<YYYYMMDD_HHMMSS>/<fileName>, then re-reads it and
    // compares CRC32 against `data`. Only an ok result allows the caller to write the save.
    BackupResult createBackup(
        const std::string& tag, const std::string& fileName, const uint8_t* data, size_t len);
}
