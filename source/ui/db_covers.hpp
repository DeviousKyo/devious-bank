// Game box art covers, streamed from romfs:/gfx/covers/<key>.t3x on demand.
//
// Old 3DS memory budget: only the covers of the game-select tiles on screen are kept.
// After the tiles are drawn, retainOnly() frees every other texture, so at most ~8 covers
// (128x128 RGB565, 32 KB each, ~256 KB) are resident at once.
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "enums/GameVersion.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace db::ui
{
    // Cover key for a Gen 1-7 game ("D", "HG", ...), or "" if there is no cover for it.
    std::string coverKeyFor(pksm::GameVersion v);

    class CoverCache
    {
    public:
        CoverCache() = default;
        ~CoverCache() { clear(); }
        CoverCache(const CoverCache&)            = delete;
        CoverCache& operator=(const CoverCache&) = delete;

        // Loads on first use. Null if the key is empty or the file is missing.
        const C2D_Image* get(const std::string& key);
        // Frees every texture whose key is not in `keep`.
        void retainOnly(const std::unordered_set<std::string>& keep);
        // Frees all textures. Must run before citro2d/citro3d shut down.
        void clear();
        [[nodiscard]] size_t loaded() const { return entries.size(); }

    private:
        struct Entry
        {
            C3D_Tex tex{};
            Tex3DS_Texture t3x = nullptr;
            C2D_Image image{};
            bool ok = false;
            ~Entry();
        };
        std::unordered_map<std::string, std::unique_ptr<Entry>> entries;
    };
}
