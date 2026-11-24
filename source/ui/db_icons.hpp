// Pokemon box icons, streamed from romfs:/gfx/pkm/<species>.t3x on demand.
//
// Old 3DS memory budget: only the icons of the box on screen are kept. After a box is
// drawn, retainOnly() frees every other texture, so at most ~30 icons (64x64 RGBA5551,
// 8 KB each, ~240 KB) are resident at once instead of all 810.
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace db::ui
{
    inline constexpr int ICON_NONE = -1;   // empty slot / unreadable data
    inline constexpr int ICON_EGG  = 1000; // romfs:/gfx/pkm/egg.t3x

    class IconCache
    {
    public:
        IconCache() = default;
        ~IconCache() { clear(); }
        IconCache(const IconCache&)            = delete;
        IconCache& operator=(const IconCache&) = delete;

        // Loads on first use. Null if the key is ICON_NONE or the file is missing.
        const C2D_Image* get(int key);
        // Frees every texture whose key is not in `keep`.
        void retainOnly(const std::unordered_set<int>& keep);
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
        std::unordered_map<int, std::unique_ptr<Entry>> entries;
    };
}
