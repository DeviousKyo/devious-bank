#include "core/db_backup.hpp"
#include "core/db_fs.hpp"

namespace db
{
    BackupResult createBackup(
        const std::string& tag, const std::string& fileName, const uint8_t* data, size_t len)
    {
        BackupResult res;
        if (data == nullptr || len == 0)
        {
            res.error = "Nothing to back up";
            return res;
        }

        std::string dir = std::string(fs::BACKUP_DIR) + "/" + fs::sanitize(tag) + "_" +
                          fs::timestamp();
        // Two backups in the same second get a numeric suffix instead of overwriting.
        std::string candidate = dir;
        for (int n = 2; fs::exists(candidate) && n < 100; n++)
        {
            candidate = dir + "_" + std::to_string(n);
        }
        if (fs::exists(candidate))
        {
            // Every suffix is taken: writing into an existing folder could replace an
            // older backup, so give up instead.
            res.error = "Too many backups this second: " + dir;
            return res;
        }
        dir = candidate;

        if (!fs::mkdirs(dir))
        {
            res.error = "Could not create " + dir;
            return res;
        }

        std::string name = fileName;
        if (auto slash = name.find_last_of('/'); slash != std::string::npos)
        {
            name = name.substr(slash + 1);
        }
        if (name.empty())
        {
            name = "main";
        }

        const std::string path = dir + "/" + name;
        if (!fs::writeFileVerified(path, data, len))
        {
            res.error = "Backup write/verify failed: " + path;
            return res;
        }

        res.ok   = true;
        res.path = path;
        return res;
    }
}
