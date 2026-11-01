// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * hud.c - Boing Twist's text and panels, drawn in C into the frame after
 * the GL ball: topaz 8 read out of the ROM font once (as planet_chomp
 * does), scaled by whole numbers, with a drop shadow so it reads on any
 * theme.
 */
#include <exec/types.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <string.h>
#include "hud.h"

static unsigned char glyph[256][8];
static uint32_t *fb;
static int FW, FH;

int hud_init(void)
{
    struct TextAttr ta = { (STRPTR)"topaz.font", 8, 0, 0 };
    struct TextFont *tf = OpenFont(&ta);
    int c;
    if (!tf) return 0;
    for (c = 32; c < 256; c++) {
        int idx = c, r, b, off, w;
        const ULONG *loc = (const ULONG *)tf->tf_CharLoc;
        const UBYTE *data = (const UBYTE *)tf->tf_CharData;
        if (idx < tf->tf_LoChar || idx > tf->tf_HiChar) idx = tf->tf_HiChar + 1;   /* "no glyph" */
        idx -= tf->tf_LoChar;
        off = (int)(loc[idx] >> 16);
        w = (int)(loc[idx] & 0xFFFF);
        if (w > 8) w = 8;
        for (r = 0; r < 8 && r < tf->tf_YSize; r++) {
            unsigned char bits = 0;
            for (b = 0; b < w; b++) {
                int bit = off + b;
                if (data[r * tf->tf_Modulo + (bit >> 3)] & (0x80 >> (bit & 7))) bits |= 0x80 >> b;
            }
            glyph[c][r] = bits;
        }
    }
    CloseFont(tf);
    return 1;
}

void hud_target(uint32_t *pix, int w, int h)
{
    fb = pix;
    FW = w;
    FH = h;
}

void hud_fill(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    uint32_t c = 0xFF000000u | rgb;
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > FW - 1) x1 = FW - 1;
    if (y1 > FH - 1) y1 = FH - 1;
    for (y = y0; y <= y1; y++) {
        uint32_t *row = fb + (size_t)y * FW;
        for (x = x0; x <= x1; x++) row[x] = c;
    }
}

void hud_shade(int x0, int y0, int x1, int y1, int pct)
{
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > FW - 1) x1 = FW - 1;
    if (y1 > FH - 1) y1 = FH - 1;
    for (y = y0; y <= y1; y++) {
        uint32_t *row = fb + (size_t)y * FW;
        for (x = x0; x <= x1; x++) {
            uint32_t c = row[x];
            row[x] = 0xFF000000u | (((c >> 16) & 255) * pct / 100) << 16 | (((c >> 8) & 255) * pct / 100) << 8 |
                     ((c & 255) * pct / 100);
        }
    }
}

void hud_frame(int x0, int y0, int x1, int y1, int t, uint32_t rgb)
{
    hud_fill(x0, y0, x1, y0 + t - 1, rgb);
    hud_fill(x0, y1 - t + 1, x1, y1, rgb);
    hud_fill(x0, y0, x0 + t - 1, y1, rgb);
    hud_fill(x1 - t + 1, y0, x1, y1, rgb);
}

static void text_raw(int x, int y, const char *s, int scale, uint32_t rgb)
{
    for (; *s; s++, x += 8 * scale) {
        const unsigned char *gl = glyph[(unsigned char)*s];
        int r, b;
        for (r = 0; r < 8; r++)
            if (gl[r])
                for (b = 0; b < 8; b++)
                    if (gl[r] & (0x80 >> b))
                        hud_fill(x + b * scale, y + r * scale, x + b * scale + scale - 1, y + r * scale + scale - 1, rgb);
    }
}

int hud_width(const char *s, int scale) { return 8 * scale * (int)strlen(s); }

void hud_text(int x, int y, const char *s, int scale, uint32_t rgb)
{
    int d = scale > 1 ? scale / 2 + 1 : 1;
    text_raw(x + d, y + d, s, scale, 0x101018);
    text_raw(x, y, s, scale, rgb);
}

void hud_ctext(int y, const char *s, int scale, uint32_t rgb)
{
    hud_text((FW - hud_width(s, scale)) / 2, y, s, scale, rgb);
}

void hud_picture(const uint8_t *rgb, int sw, int sh, int x0, int y0, int w, int h)
{
    int x, y;
    if (!rgb) return;
    for (y = 0; y < h; y++) {
        int yy = y0 + y, sy = y * sh / h;
        uint32_t *row;
        if (yy < 0 || yy >= FH) continue;
        row = fb + (size_t)yy * FW;
        for (x = 0; x < w; x++) {
            int xx = x0 + x;
            const uint8_t *t = rgb + ((size_t)sy * sw + x * sw / w) * 3;
            if (xx < 0 || xx >= FW) continue;
            row[xx] = 0xFF000000u | (uint32_t)t[0] << 16 | (uint32_t)t[1] << 8 | t[2];
        }
    }
}
