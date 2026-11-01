// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * sound_ahi.h - Boing Twist's sounds through ahi.device: made at start-up
 * (no files), played one-shot. No AHI just means silence.
 */
#ifndef SOUND_AHI_H
#define SOUND_AHI_H

enum { SND_THUD, SND_CLICK, SND_FLIP, SND_SOLVED, SND_COUNT };

int  snd_open(void);          /* 0: no sound */
void snd_close(void);
void snd_play(int id);
void snd_update(void);        /* once a frame: collect finished requests */
extern int snd_volume;        /* 0..64 */

#endif
