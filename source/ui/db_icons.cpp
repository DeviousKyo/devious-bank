#include "ui/db_icons.hpp"

#include <cstdio>
#include <string>

namespace db::ui
{
    IconCache::Entry::~Entry()
    {
        if (t3x)
        {
            Tex3DS_TextureFree(t3x); // subtexture table
            C3D_TexDelete(&tex);     // texture memory
        }
    }

    const C2D_Image* IconCache::get(int key)
    {
        if (key == ICON_NONE)
        {
            return nullptr;
        }
        if (auto it = entries.find(key); it != entries.end())
        {
            return it->second->ok ? &it->second->image : nullptr;
        }

        auto e = std::make_unique<Entry>();
        const std::string path =
            "romfs:/gfx/pkm/" + (key == ICON_EGG ? std::string("egg") : std::to_string(key)) +
            ".t3x";
        if (FILE* f = std::fopen(path.c_str(), "rb"))
        {
            // vram = false: textures go to linear memory, which the Old 3DS has more of.
            e->t3x = Tex3DS_TextureImportStdio(f, &e->tex, nullptr, false);
            std::fclose(f);
        }
        if (e->t3x)
        {
            C3D_TexSetFilter(&e->tex, GPU_LINEAR, GPU_LINEAR); // smoother when scaled down
            e->image = C2D_Image{&e->tex, Tex3DS_GetSubTexture(e->t3x, 0)};
            e->ok    = true;
        }
        // Misses are cached too, so a missing file isn't re-opened every frame.
        const C2D_Image* out = e->ok ? &e->image : nullptr;
        entries.emplace(key, std::move(e));
        return out;
    }

    void IconCache::retainOnly(const std::unordered_set<int>& keep)
    {
        for (auto it = entries.begin(); it != entries.end();)
        {
            it = keep.count(it->first) ? std::next(it) : entries.erase(it);
        }
    }

    void IconCache::clear()
    {
        entries.clear();
    }
}
