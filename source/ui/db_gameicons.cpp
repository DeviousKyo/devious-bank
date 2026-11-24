#include "ui/db_gameicons.hpp"

#include <cstring>

namespace db::ui
{
    const C2D_Image* GameIconCache::get(int index, const SaveLocation& loc)
    {
        if (auto it = entries.find(index); it != entries.end())
        {
            return it->second->ok ? &it->second->image : nullptr;
        }

        auto e = std::make_unique<Entry>();
        if (loc.iconKind == IconKind::Smdh48 && loc.icon.size() == 48 * 48 * 2)
        {
            // 48x48 RGB565 icon into a 64x64 texture. The SMDH data is already in GPU tile
            // order, so it is copied one row of 8x8 tiles at a time; the icon sits in the
            // texture's lower 48 rows, which the subtexture coordinates point at.
            if ((e->texInit = C3D_TexInit(&e->tex, 64, 64, GPU_RGB565)))
            {
                std::memset(e->tex.data, 0, e->tex.size);
                u16* dst       = (u16*)e->tex.data + (64 - 48) * 64;
                const u16* src = (const u16*)loc.icon.data();
                for (int row = 0; row < 48; row += 8)
                {
                    std::memcpy(dst, src, 48 * 8 * sizeof(u16));
                    src += 48 * 8;
                    dst += 64 * 8;
                }
                e->sub = Tex3DS_SubTexture{48, 48, 0.0f, 48 / 64.0f, 48 / 64.0f, 0.0f};
                C3D_TexSetFilter(&e->tex, GPU_LINEAR, GPU_LINEAR); // smooth art, scaled 1.33x
                e->ok = true;
            }
        }
        else if (loc.iconKind == IconKind::Ds32 && loc.icon.size() == 32 * 32 * 4)
        {
            // 32x32 RGBA8, already tiled by the decoder: fills the whole texture.
            if ((e->texInit = C3D_TexInit(&e->tex, 32, 32, GPU_RGBA8)))
            {
                std::memcpy(e->tex.data, loc.icon.data(), loc.icon.size());
                e->sub = Tex3DS_SubTexture{32, 32, 0.0f, 1.0f, 1.0f, 0.0f};
                C3D_TexSetFilter(&e->tex, GPU_NEAREST, GPU_NEAREST); // crisp pixels at 2x
                e->ok = true;
            }
        }

        if (e->ok)
        {
            C3D_TexFlush(&e->tex); // written by the CPU; make sure the GPU sees it
            e->image = C2D_Image{&e->tex, &e->sub};
        }
        const C2D_Image* out = e->ok ? &e->image : nullptr;
        entries.emplace(index, std::move(e));
        return out;
    }
}
