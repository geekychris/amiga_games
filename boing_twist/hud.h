// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * hud.h - text and panels drawn straight into the A R G B frame, over the
 * 3D (topaz 8 from the ROM, scaled up by whole numbers).
 */
#ifndef HUD_H
#define HUD_H

#include <stdint.h>

int  hud_init(void);                                   /* 0: no topaz, no text */
void hud_target(uint32_t *pix, int w, int h);
void hud_text(int x, int y, const char *s, int scale, uint32_t rgb);     /* with a drop shadow */
void hud_ctext(int y, const char *s, int scale, uint32_t rgb);          /* centred */
int  hud_width(const char *s, int scale);
void hud_shade(int x0, int y0, int x1, int y1, int pct);                /* darken to pct % */
void hud_fill(int x0, int y0, int x1, int y1, uint32_t rgb);
void hud_frame(int x0, int y0, int x1, int y1, int t, uint32_t rgb);    /* an outline t thick */
/* an R G B picture (sw x sh) shrunk into the box */
void hud_picture(const uint8_t *rgb, int sw, int sh, int x0, int y0, int w, int h);

#endif
