// Thin citro2d wrapper: owns the render targets and the per-frame text buffer.
#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <string>

namespace db::ui
{
    inline constexpr int TOP_W    = 400;
    inline constexpr int BOTTOM_W = 320;
    inline constexpr int SCREEN_H = 240;

    // citro2d colors are ABGR in memory (C2D_Color32), built at compile time here.
    constexpr u32 rgba(u8 r, u8 g, u8 b, u8 a = 0xFF)
    {
        return r | (g << 8) | (b << 16) | ((u32)a << 24);
    }

    namespace col
    {
        inline constexpr u32 BG        = rgba(0x33, 0x33, 0x33); // 20% gray
        inline constexpr u32 PANEL     = rgba(0x2A, 0x2C, 0x3A);
        inline constexpr u32 CELL      = rgba(0x38, 0x3B, 0x4E);
        inline constexpr u32 CELL_FULL = rgba(0x4A, 0x5A, 0x7A);
        inline constexpr u32 CELL_BAD  = rgba(0x8A, 0x2E, 0x2E);
        inline constexpr u32 CELL_HELD = rgba(0x2A, 0x2C, 0x3A);
        inline constexpr u32 SAVE_HDR  = rgba(0x2D, 0x6C, 0xB0);
        inline constexpr u32 BANK_HDR  = rgba(0x7A, 0x3F, 0xA8);
        inline constexpr u32 CURSOR    = rgba(0xFF, 0xD2, 0x3F);
        inline constexpr u32 TEXT      = rgba(0xF2, 0xF2, 0xF5);
        inline constexpr u32 DIM       = rgba(0x9A, 0x9C, 0xAD);
        inline constexpr u32 SHINY     = rgba(0xFF, 0xC4, 0x3D);
        inline constexpr u32 WARN      = rgba(0xFF, 0x7A, 0x6B);
        inline constexpr u32 GOOD      = rgba(0x7A, 0xE0, 0x8E);
        inline constexpr u32 OVERLAY   = rgba(0x00, 0x00, 0x00, 0xB0);

        // Box grid
        inline constexpr u32 TRAY       = rgba(0x26, 0x28, 0x36);
        inline constexpr u32 CELL_HOVER = rgba(0x5E, 0x72, 0x9A);
        inline constexpr u32 SHADOW     = rgba(0x00, 0x00, 0x00, 0x60);

        // Cursor colors, one per mode
        inline constexpr u32 MODE_POINT  = CURSOR;                     // yellow: pointing
        inline constexpr u32 MODE_SELECT = rgba(0x4F, 0xC3, 0xF7);     // cyan: selecting
        inline constexpr u32 SELECT_TINT = rgba(0x4F, 0xC3, 0xF7, 0x50);
        inline constexpr u32 MODE_HOLD   = GOOD;                       // green: holding one
        inline constexpr u32 MODE_GROUP  = rgba(0xFF, 0x6F, 0xB5);     // pink: holding a group

        // Game select / snow
        inline constexpr u32 SKY_TOP    = rgba(0x14, 0x1F, 0x3B);
        inline constexpr u32 SKY_MID    = rgba(0x22, 0x36, 0x5C);
        inline constexpr u32 SKY_BOTTOM = rgba(0x31, 0x4C, 0x78);
        inline constexpr u32 CARD       = rgba(0x23, 0x27, 0x3A);
        inline constexpr u32 CARD_SEL   = rgba(0x31, 0x37, 0x52);

        // Generation accents
        inline constexpr u32 GEN[8] = {
            rgba(0x9B, 0x6B, 0xD6), // 0: bank
            rgba(0xE0, 0x5A, 0x5A), // Gen 1 red
            rgba(0xD9, 0xA9, 0x3A), // Gen 2 gold
            rgba(0x4F, 0xB3, 0x6B), // Gen 3 green
            rgba(0x4F, 0x8F, 0xD9), // Gen 4 blue
            rgba(0x8A, 0x8F, 0xA0), // Gen 5 gray
            rgba(0x3F, 0xB8, 0xB0), // Gen 6 teal
            rgba(0xE9, 0x87, 0x3A), // Gen 7 orange
        };
    }

    enum class Align
    {
        Left,
        Center,
        Right,
    };

    class Gfx
    {
    public:
        bool init();
        void fini(); // safe to call more than once

        void beginFrame();
        void endFrame();
        void drawTop();    // switch to the top screen and clear it
        void drawBottom(); // switch to the bottom screen and clear it

        void rect(float x, float y, float w, float h, u32 color);
        void frame(float x, float y, float w, float h, float t, u32 color);
        // Vertical gradient, top color to bottom color.
        void gradient(float x, float y, float w, float h, u32 top, u32 bottom);
        void circle(float cx, float cy, float r, u32 color);
        void ellipse(float x, float y, float w, float h, u32 color);
        // alpha < 1 draws the image faded (used for a picked-up Pokemon's origin slot).
        void image(const C2D_Image& img, float x, float y, float scale, float alpha = 1.0f);
        // maxW > 0 squeezes the text horizontally to fit.
        void text(float x, float y, float scale, u32 color, const std::string& s,
            Align align = Align::Left, float maxW = 0);

    private:
        C3D_RenderTarget* top    = nullptr;
        C3D_RenderTarget* bottom = nullptr;
        C2D_TextBuf textBuf      = nullptr;
        bool gfxUp = false, c3dUp = false, c2dUp = false;
    };
}
