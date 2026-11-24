#include "core/db_fs.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

namespace db::fs
{
    uint32_t crc32(const uint8_t* data, size_t len)
    {
        static uint32_t table[256];
        static bool init = false;
        if (!init)
        {
            for (uint32_t i = 0; i < 256; i++)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; k++)
                {
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                table[i] = c;
            }
            init = true;
        }
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; i++)
        {
            crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }

    bool exists(const std::string& path)
    {
        struct stat st;
        return stat(path.c_str(), &st) == 0;
    }

    bool isDirectory(const std::string& path)
    {
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    bool mkdirs(const std::string& path)
    {
        // Skip the "sdmc:/" device prefix when walking components.
        size_t start = path.find(":/");
        start        = (start == std::string::npos) ? 0 : start + 2;
        for (size_t pos = path.find('/', start); ; pos = path.find('/', pos + 1))
        {
            std::string part = path.substr(0, pos);
            if (!part.empty() && !isDirectory(part))
            {
                if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST)
                {
                    return false;
                }
            }
            if (pos == std::string::npos)
            {
                break;
            }
        }
        return isDirectory(path);
    }

    std::optional<std::vector<uint8_t>> readFile(const std::string& path, size_t maxSize)
    {
        File f(path, "rb");
        if (!f)
        {
            return std::nullopt;
        }
        if (std::fseek(f.get(), 0, SEEK_END) != 0)
        {
            return std::nullopt;
        }
        long size = std::ftell(f.get());
        if (size < 0 || (size_t)size > maxSize)
        {
            return std::nullopt;
        }
        std::rewind(f.get());
        std::vector<uint8_t> out((size_t)size);
        if (size > 0 && std::fread(out.data(), 1, out.size(), f.get()) != out.size())
        {
            return std::nullopt;
        }
        return out;
    }

    bool writeFileVerified(const std::string& path, const uint8_t* data, size_t len)
    {
        {
            File f(path, "wb");
            if (!f)
            {
                return false;
            }
            if (len > 0 && std::fwrite(data, 1, len, f.get()) != len)
            {
                return false;
            }
            if (std::fflush(f.get()) != 0 || !f.close())
            {
                return false;
            }
        }
        auto back = readFile(path, len + 1);
        return back && back->size() == len && crc32(back->data(), len) == crc32(data, len);
    }

    bool writeFileAtomic(const std::string& path, const uint8_t* data, size_t len)
    {
        const std::string tmp = path + ".tmp";
        const std::string bak = path + ".bak";

        if (!writeFileVerified(tmp, data, len))
        {
            std::remove(tmp.c_str());
            return false;
        }
        // FAT rename does not overwrite, so clear the way first.
        if (exists(path))
        {
            std::remove(bak.c_str());
            if (std::rename(path.c_str(), bak.c_str()) != 0)
            {
                std::remove(tmp.c_str());
                return false;
            }
        }
        if (std::rename(tmp.c_str(), path.c_str()) != 0)
        {
            // Put the previous version back so the path is never left empty.
            std::rename(bak.c_str(), path.c_str());
            return false;
        }
        return true;
    }

    std::string timestamp()
    {
        // The 3DS RTC is local time; libctru reports it as if it were UTC.
        time_t now = time(nullptr);
        struct tm* t = gmtime(&now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", t);
        return buf;
    }

    std::string sanitize(const std::string& s)
    {
        std::string out;
        for (char c : s)
        {
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '-';
            out += ok ? c : '_';
        }
        return out.empty() ? "save" : out;
    }
}
