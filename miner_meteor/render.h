// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef MINER_METEOR_RENDER_H
#define MINER_METEOR_RENDER_H

#include <exec/types.h>

struct Screen;
struct RastPort;
struct GameState;

/* Open a 320x256 8bpp screen + double-buffered draw target.
 * Returns 0 on success, non-zero on error. */
LONG render_open(void);
void render_close(void);

/* Draw a single frame into the back buffer, then flip. */
void render_frame(const GameState &gs);

/* Draw title screen overlay. */
void render_title(const GameState &gs);

/* Draw end-of-game overlay. Uses mode from gs. */
void render_endscreen(const GameState &gs);

/* Simple SetAPen + Text wrapper used by title / end screens. */
void render_text(LONG x, LONG y, UBYTE pen, const char *text);

#endif
