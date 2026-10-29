// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* render.h - ScAIry Fun's road renderer, HUD and screens.
 *
 * Everything is drawn by the CPU straight into the five bitplanes of a
 * RastPort (text too, from the topaz 8 bitmap), except the big sky and
 * ground fills at the start of a frame, which the blitter does. */
#ifndef RENDER_H
#define RENDER_H

#include <graphics/rastport.h>
#include "sc.h"

#define SCREEN_W 320
#define SCREEN_H 256

typedef struct RastPort Canvas;

typedef struct { int32_t segs, models, polys, dropped; } RenderStats;
extern RenderStats render_stats;
extern int32_t render_draw_dist;        /* segments drawn ahead (bridge-tunable) */

/* the colours: 32 pens per route, and the copper sky gradient */
extern const uint16_t route_palette[NUM_ROUTES][32];
extern const uint16_t route_sky[NUM_ROUTES][2];      /* top, horizon */

int  render_init(void);
void render_cleanup(void);
void camera_follow(Cam *c, const World *w);
void render_build(const World *w, Cam *c);           /* transform, clip, order */
void render_draw(Canvas *cv, const World *w, const Cam *c);     /* sky, skyline, sun, polygons */
void render_overlay(Canvas *cv, const World *w, const Cam *c);  /* AI boxes, cockpit, messages */
void render_hud(Canvas *cv, int buf, const World *w, int fps, int paused);
void render_hud_reset(void);
void render_update_screen(Canvas *cv, const World *w);          /* the software update */
void render_title(Canvas *cv, const World *w);                  /* over the attract mode */
#define HELP_PAGES 3
void render_help(Canvas *cv, int page);                         /* 1..HELP_PAGES: the rules */
int  render_horizon(int view);                                  /* for the copper sky */

#endif
