#include "ui/db_snow.hpp"

#include <cmath>
#include <cstdlib>

namespace db::ui
{
    namespace
    {
        constexpr float WORLD_W  = 400;
        constexpr float WORLD_H  = 480;
        constexpr float BOTTOM_X = 40; // bottom screen's left edge in world coordinates

        float frand(float lo, float hi)
        {
            return lo + (hi - lo) * (std::rand() / (float)RAND_MAX);
        }
    }

    Snow::Snow()
    {
        std::srand((unsigned)osGetTime());
        for (Flake& f : flakes)
        {
            respawn(f, true);
        }
    }

    void Snow::respawn(Flake& f, bool anywhere)
    {
        // Small flakes fall slower and fainter, which reads as depth.
        f.r     = frand(0.8f, 2.4f);
        f.speed = 0.25f + f.r * 0.35f;
        f.sway  = frand(0.2f, 0.7f);
        f.phase = frand(0, 6.283f);
        f.alpha = (u8)(110 + f.r * 55);
        f.x     = frand(0, WORLD_W);
        f.y     = anywhere ? frand(0, WORLD_H) : frand(-12, -2);
    }

    void Snow::update()
    {
        frame++;
        for (Flake& f : flakes)
        {
            f.y += f.speed;
            f.x += std::sin(f.phase + frame * 0.02f) * f.sway * 0.5f;
            if (f.x < -4) f.x += WORLD_W + 8;
            if (f.x > WORLD_W + 4) f.x -= WORLD_W + 8;
            if (f.y > WORLD_H + 4)
            {
                respawn(f, false);
            }
        }
    }

    void Snow::drawTop(Gfx& gfx)
    {
        gfx.gradient(0, 0, TOP_W, SCREEN_H, col::SKY_TOP, col::SKY_MID);
        for (const Flake& f : flakes)
        {
            if (f.y < SCREEN_H + 3)
            {
                gfx.circle(f.x, f.y, f.r, rgba(0xFF, 0xFF, 0xFF, f.alpha));
            }
        }
    }

    void Snow::drawBottom(Gfx& gfx)
    {
        gfx.gradient(0, 0, BOTTOM_W, SCREEN_H, col::SKY_MID, col::SKY_BOTTOM);
        for (const Flake& f : flakes)
        {
            const float x = f.x - BOTTOM_X;
            const float y = f.y - SCREEN_H;
            if (y > -3 && x > -3 && x < BOTTOM_W + 3)
            {
                gfx.circle(x, y, f.r, rgba(0xFF, 0xFF, 0xFF, f.alpha));
            }
        }
    }
}
