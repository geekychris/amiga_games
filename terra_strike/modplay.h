// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * modplay.h - a small 4-channel Protracker player that mixes in software,
 * for AmigaOS 4, which has no Paula for ptplayer. Plain C, no Amiga
 * headers, so the host tests can run it too.
 */
#ifndef MODPLAY_H
#define MODPLAY_H

#include <stdint.h>

/* An M.K. (or 4CHN / FLT4) module; data must stay valid while it plays.
 * Returns 0 if it isn't one. */
int  mod_load(const uint8_t *data, long len);
/* frames of interleaved stereo, signed 16-bit, at rate Hz; loops the song */
void mod_render(int16_t *out, int frames, int rate);
int  mod_position(void);                /* the song position playing (0..) */

#endif
