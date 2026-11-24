#include "ui/db_gfx.hpp"

namespace db::ui
{
    bool Gfx::init()
    {
        gfxInitDefault();
        gfxUp = true;
        if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
        {
            return false;
        }
        c3dUp = true;
        if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS))
        {
            return false;
        }
        c2dUp = true;
        C2D_Prepare();

        top     = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        bottom  = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
        textBuf = C2D_TextBufNew(4096);
        return top && bottom && textBuf;
    }

    void Gfx::fini()
    {
        // Reverse order of init; every pointer is nulled so a second call is harmless.
        if (textBuf)
        {
            C2D_TextBufDelete(textBuf);
            textBuf = nullptr;
        }
        if (top)
        {
            C3D_RenderTargetDelete(top);
            top = nullptr;
        }
        if (bottom)
        {
            C3D_RenderTargetDelete(bottom);
            bottom = nullptr;
        }
        if (c2dUp)
        {
            C2D_Fini();
            c2dUp = false;
        }
        if (c3dUp)
        {
            C3D_Fini();
            c3dUp = false;
        }
        if (gfxUp)
        {
            gfxExit();
            gfxUp = false;
        }
    }

    void Gfx::beginFrame()
    {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(textBuf);
    }

    void Gfx::endFrame()
    {
        C3D_FrameEnd(0);
    }

    void Gfx::drawTop()
    {
        C2D_TargetClear(top, col::BG);
        C2D_SceneBegin(top);
    }

    void Gfx::drawBottom()
    {
        C2D_TargetClear(bottom, col::BG);
        C2D_SceneBegin(bottom);
    }

    void Gfx::rect(float x, float y, float w, float h, u32 color)
    {
        C2D_DrawRectSolid(x, y, 0.5f, w, h, color);
    }

    void Gfx::frame(float x, float y, float w, float h, float t, u32 color)
    {
        rect(x, y, w, t, color);
        rect(x, y + h - t, w, t, color);
        rect(x, y, t, h, color);
        rect(x + w - t, y, t, h, color);
    }

    void Gfx::image(const C2D_Image& img, float x, float y, float scale, float alpha)
    {
        if (alpha >= 1.0f)
        {
            C2D_DrawImageAt(img, x, y, 0.5f, nullptr, scale, scale);
            return;
        }
        C2D_ImageTint tint;
        C2D_AlphaImageTint(&tint, alpha);
        C2D_DrawImageAt(img, x, y, 0.5f, &tint, scale, scale);
    }

    void Gfx::text(float x, float y, float scale, u32 color, const std::string& s, Align align,
        float maxW)
    {
        if (s.empty())
        {
            return;
        }
        C2D_Text t;
        if (!C2D_TextParse(&t, textBuf, s.c_str()))
        {
            return; // text buffer full this frame
        }
        C2D_TextOptimize(&t);

        float sx = scale;
        float w  = 0, h = 0;
        C2D_TextGetDimensions(&t, scale, scale, &w, &h);
        if (maxW > 0 && w > maxW)
        {
            sx = scale * maxW / w;
            w  = maxW;
        }
        if (align == Align::Center)
        {
            x -= w / 2;
        }
        else if (align == Align::Right)
        {
            x -= w;
        }
        C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, sx, scale, color);
    }
}
