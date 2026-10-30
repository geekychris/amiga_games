// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * sound_ahi.h - the shared engine's sound on AmigaOS 4 (from Terra
 * Strike): the music rendered by modplay.c, the effects through
 * ahi.device, described as ptplayer's SfxStructure is on the classic
 * Amiga.
 */
#ifndef SOUND_AHI_H
#define SOUND_AHI_H

#include <exec/types.h>

typedef struct {
    APTR  sfx_ptr;   /* sample start */
    WORD  sfx_len;   /* length in words */
    WORD  sfx_per;   /* Paula period */
    WORD  sfx_vol;   /* volume 0..64 */
    BYTE  sfx_cha;
    BYTE  sfx_pri;   /* a playing effect is only cut by one of equal or higher priority */
} SfxStructure;

int  ahi_open(void);                     /* 0: no ahi.device, so no sound */
void ahi_close(void);
void ahi_sfx(const SfxStructure *s);
int  ahi_music(const void *mod, ULONG len);   /* 0: not a module modplay can play */
void ahi_update(void);                   /* once a frame: keeps the music queued */

#endif
