#include "ui/db_gameart.hpp"

#include <cstdio>

namespace db::ui
{
    std::string gameArtKeyFor(pksm::GameVersion v)
    {
        struct KeyFor
        {
            pksm::GameVersion version;
            const char* key;
        };
        // Gen 1-2 are the 3DS Virtual Console releases. PKSM-Core names Silver "SV".
        static const KeyFor table[] = {
            {pksm::GameVersion::RD, "RD"}, {pksm::GameVersion::GN, "GN"},
            {pksm::GameVersion::BU, "BU"}, {pksm::GameVersion::YW, "YW"},
            {pksm::GameVersion::GD, "GD"}, {pksm::GameVersion::SV, "SI"},
            {pksm::GameVersion::C, "C"},   {pksm::GameVersion::R, "R"},
            {pksm::GameVersion::S, "S"},   {pksm::GameVersion::E, "E"},
            {pksm::GameVersion::FR, "FR"}, {pksm::GameVersion::LG, "LG"},
            {pksm::GameVersion::D, "D"},   {pksm::GameVersion::P, "P"},
            {pksm::GameVersion::Pt, "PT"}, {pksm::GameVersion::HG, "HG"},
            {pksm::GameVersion::SS, "SS"}, {pksm::GameVersion::B, "B"},
            {pksm::GameVersion::W, "W"},   {pksm::GameVersion::B2, "B2"},
            {pksm::GameVersion::W2, "W2"}, {pksm::GameVersion::X, "X"},
            {pksm::GameVersion::Y, "Y"},   {pksm::GameVersion::OR, "OR"},
            {pksm::GameVersion::AS, "AS"}, {pksm::GameVersion::SN, "SN"},
            {pksm::GameVersion::MN, "MN"}, {pksm::GameVersion::US, "US"},
            {pksm::GameVersion::UM, "UM"},
        };
        for (const KeyFor& k : table)
        {
            if (v == k.version)
            {
                return k.key;
            }
        }
        return {};
    }

    GameArtCache::Entry::~Entry()
    {
        if (t3x)
        {
            Tex3DS_TextureFree(t3x); // subtexture table
            C3D_TexDelete(&tex);     // texture memory
        }
    }

    const C2D_Image* GameArtCache::get(const std::string& key)
    {
        if (key.empty())
        {
            return nullptr;
        }
        if (auto it = entries.find(key); it != entries.end())
        {
            return it->second->ok ? &it->second->image : nullptr;
        }

        auto e                 = std::make_unique<Entry>();
        const std::string path = "romfs:/gfx/gameart/" + key + ".t3x";
        if (FILE* f = std::fopen(path.c_str(), "rb"))
        {
            // vram = false: textures go to linear memory, which the Old 3DS has more of.
            e->t3x = Tex3DS_TextureImportStdio(f, &e->tex, nullptr, false);
            std::fclose(f);
        }
        if (e->t3x)
        {
            C3D_TexSetFilter(&e->tex, GPU_LINEAR, GPU_LINEAR); // smoother when scaled
            e->image = C2D_Image{&e->tex, Tex3DS_GetSubTexture(e->t3x, 0)};
            e->ok    = true;
        }
        // Misses are cached too, so a missing file isn't re-opened every frame.
        const C2D_Image* out = e->ok ? &e->image : nullptr;
        entries.emplace(key, std::move(e));
        return out;
    }

    void GameArtCache::retainOnly(const std::unordered_set<std::string>& keep)
    {
        for (auto it = entries.begin(); it != entries.end();)
        {
            it = keep.count(it->first) ? std::next(it) : entries.erase(it);
        }
    }

    void GameArtCache::clear()
    {
        entries.clear();
    }
}
