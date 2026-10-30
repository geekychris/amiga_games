// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Amiga Paula (four channel sample playback) on AmigaOS 4, mixed in a
 * task of its own and played through ahi.device (os4c_paula.c).  The
 * interface is the Atari ST ports' paula.h, so their ptplayer.c runs on
 * it unchanged.
 *
 * `custom` is a structure in RAM with the real register layout.  As on
 * Paula, enabling a channel latches ac_ptr/ac_len and plays from the
 * sample's start; at the end of the block the (possibly new) ac_ptr and
 * ac_len are reloaded, which is how one-shot and looping samples work.
 * ac_per and ac_vol take effect at once.  Channels 0 and 3 are on the
 * left, 1 and 2 on the right.
 */
#ifndef OS4C_PAULA_H
#define OS4C_PAULA_H

#include <exec/types.h>
#include <hardware/custom.h>

extern struct Custom custom;

/* a write to dmacon, taking effect at once (the layer's own callers) */
void paula_dmacon(UWORD v);

/* starts the mixer if needed; tick (may be NULL) is called from the
 * mixer task tick_hz times a second.  Returns 0 if sound plays. */
int  paula_init(void (*tick)(void), int tick_hz);
void paula_exit(void);
void paula_set_rate(int hz);
void paula_set_tick_rate(int num, int den);

/* bracket code that touches state the tick also uses (recursive) */
UWORD paula_lock(void);
void paula_unlock(UWORD sr);

#endif
