// Textures for the game-select tiles, built from each save's own icon (read from the
// console by the scan: DS cart banner icons and 3DS/Virtual Console HOME Menu icons).
// Built on first draw; cleared whenever the save list changes or the boxes open.
#pragma once

#include "core/db_save.hpp"

#include <3ds.h>
#include <citro2d.h>

#include <memory>
#include <unordered_map>

namespace db::ui
{
    class GameIconCache
    {
    public:
        GameIconCache() = default;
        ~GameIconCache() { clear(); }
        GameIconCache(const GameIconCache&)            = delete;
        GameIconCache& operator=(const GameIconCache&) = delete;

        // Texture for the save at `index` in the scanned list. Null if it has no icon.
        const C2D_Image* get(int index, const SaveLocation& loc);
        // Drawn size in pixels for that icon kind (both kinds draw at 64x64).
        static float scaleFor(IconKind kind) { return kind == IconKind::Ds32 ? 2.0f : 64.0f / 48; }
        // Frees all textures. Must run before citro2d/citro3d shut down.
        void clear() { entries.clear(); }

    private:
        struct Entry
        {
            C3D_Tex tex{};
            Tex3DS_SubTexture sub{};
            C2D_Image image{};
            bool texInit = false;
            bool ok      = false;
            ~Entry()
            {
                if (texInit)
                {
                    C3D_TexDelete(&tex);
                }
            }
        };
        std::unordered_map<int, std::unique_ptr<Entry>> entries;
    };
}
