// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.h - the frame loop's game side: input, the fixed 25 Hz simulation,
 * what to draw, sound and voice for the simulation's events, and
 * everything exposed over the AmigaBridge.
 */
#ifndef GAME_H
#define GAME_H

#include "sc.h"
#include "render.h"

extern World world;
extern int paused;

/* Supplied by the platform's main */
int32_t  plat_ticks(void);              /* 1/50 s since some start */
uint32_t plat_clock(void);              /* a fast counter, for stage timings */
int32_t  plat_us_since(uint32_t t0);    /* microseconds since plat_clock() was t0 */
void     plat_palette(int route, int glare);
void     plat_sky(int route, int view); /* the copper sky gradient */

unsigned game_key_bit(unsigned rawkey); /* IN_* for a raw key code (no up bit) */
void game_init(void);                   /* the world, the bridge vars and hooks */
int  game_input(unsigned in);           /* pause / view / Esc; 0 = quit */
void game_update(unsigned in);          /* run the simulation up to now */
void game_draw(Canvas *cv, int buf);    /* buf: which screen buffer cv is (HUD cache) */
void game_after_frame(void);            /* bridge mirrors, fps, ab_poll, self-test */

#endif
