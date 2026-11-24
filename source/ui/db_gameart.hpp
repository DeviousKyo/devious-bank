// Game icons for the game-select tiles, streamed from romfs:/gfx/gameart/<key>.t3x on
// demand. The art comes from the SteamGridDB "Pokemon Games" icon collection, except
// Black 2, which uses the icon supplied by the project owner (see CREDITS.md).
//
// Old 3DS memory budget: only the icons of the tiles on screen are kept. After the tiles
// are drawn, retainOnly() frees every other texture, so at most ~8 icons (128x128 RGB565,
// 32 KB each, ~256 KB) are resident at once.
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
    // Icon key for a Gen 1-7 game ("D", "HG", ...); "" if unknown. A key whose file is
    // missing loads as null and the tile shows the game's name.
    std::string gameArtKeyFor(pksm::GameVersion v);

    class GameArtCache
    {
    public:
        GameArtCache() = default;
        ~GameArtCache() { clear(); }
        GameArtCache(const GameArtCache&)            = delete;
        GameArtCache& operator=(const GameArtCache&) = delete;

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
