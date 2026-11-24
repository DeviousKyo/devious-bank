// Falling snow that crosses from the top screen onto the bottom screen.
//
// Flakes live in one 400x480 "world": y 0-239 is the top screen, y 240-479 is the bottom
// screen. The bottom screen is 320 wide and sits centered under the top one (world x 40-359),
// the same way the two physical screens line up, so a flake leaving the bottom of the top
// screen reappears at the top of the bottom screen.
#pragma once

#include "ui/db_gfx.hpp"

#include <array>

namespace db::ui
{
    class Snow
    {
    public:
        Snow();
        void update();             // advance one frame
        void drawTop(Gfx& gfx);    // sky + flakes on the top screen
        void drawBottom(Gfx& gfx); // sky + flakes on the bottom screen

    private:
        struct Flake
        {
            float x, y;  // world position
            float speed; // px per frame
            float sway;  // horizontal sway amplitude
            float phase; // sway phase
            float r;     // radius
            u8 alpha;
        };
        void respawn(Flake& f, bool anywhere);

        static constexpr int COUNT = 90;
        std::array<Flake, COUNT> flakes;
        u32 frame = 0;
    };
}
