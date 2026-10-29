// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * sound.h - ScAIry Fun's sound: effects synthesised at start-up, and the
 * autopilot's voice (scairy.voc, made by tools/modgen/scairy_voices.py).
 * Everything plays through ptplayer on the two channels the music leaves.
 */
#ifndef SOUND_H
#define SOUND_H

#include <exec/types.h>
#include "ptplayer.h"

#define CUSTOM_BASE ((void *)0xdff000)

enum { SFX_CRASH, SFX_SPLASH, SFX_SCRAPE, SFX_CHIME_ON, SFX_CHIME_OFF, SFX_CONE,
       SFX_SAVE, SFX_ALERT, SFX_SIREN, SFX_BLIP, SFX_COUNT };

void sound_init(void);
void sound_cleanup(void);
void sfx(int which);

int  voice_load(const char *path);      /* number of clips, 0 = none */
void voice_say(int msg);                /* MSG_*: its clip, if there is one */
int  voice_clips(void);

#endif
