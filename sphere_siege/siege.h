// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* siege.h - Sphere Siege's game shell, shared by game.c and render.c */
#ifndef SIEGE_H
#define SIEGE_H

#include "a3d_draw.h"
#include "ss.h"

enum { GS_TITLE, GS_PLAY, GS_OVER };

#define MAX_BURSTS 16
typedef struct { int t, size, pen; int32_t x, y, z; } Burst;   /* whole units */

typedef struct {
    int state, state_timer;
    int start_wave, autoplay, paused, help;
    char flash[32];
    int flash_timer, flash_pen;
    int hurt_timer, new_best;
    int32_t best;
    Burst burst[MAX_BURSTS];
} Siege;

extern Siege sg;
extern SSGame game;
extern int grid_detail;                 /* 0 no grid, 1 rings and meridians (bridge: grid) */
extern const uint16_t siege_palette[32];

void render_init(void);
void render_game(Canvas *cv, const SSGame *g);
void render_title(Canvas *cv, const SSGame *g);
void render_help(Canvas *cv, int page);

#endif
