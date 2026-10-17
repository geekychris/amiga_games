// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* render.h - Terra Strike renderer (Amiga graphics.library) */
#ifndef RENDER_H
#define RENDER_H

#include <graphics/rastport.h>
#include "ts.h"

typedef struct { int32_t tiles, models, polys, dropped; } RenderStats;
extern RenderStats render_stats;

int  render_init(void);
void render_attach(struct RastPort *rp);
void render_cleanup(void);
void camera_follow(Cam *c, const World *w);
void render_build(const World *w, Cam *c);   /* transform, clip, sort */
void render_draw(struct RastPort *rp);           /* fill the sorted polygons */
void render_hud(struct RastPort *rp, int buf, const World *w, int fps, int paused);
void render_hud_reset(void);
void render_title(struct RastPort *rp, const World *w);
void render_title_image(struct RastPort *rp, int frame);   /* main screen artwork */

#endif
