// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef MINER_METEOR_AUDIO_H
#define MINER_METEOR_AUDIO_H

/*
 * Sound effects: short 8-bit samples rendered from the Atari Lynx port's
 * Mikey effects by tools/gen_sfx.py (sfx_data.cpp). Played through
 * audio.device on classic 68k and ahi.device on AmigaOS 4 (sam460ex has
 * no Paula); see audio.cpp. No device just means silence.
 *
 * Each trigger also logs "sfx: <name>" to the bridge.
 */

void audio_init(void);
void audio_shutdown(void);

/* Non-blocking SFX triggers. */
void sfx_jump(void);
void sfx_land(void);
void sfx_key(void);
void sfx_death(void);
void sfx_levelwin(void);    /* a cavern cleared, more to go */
void sfx_gameover(void);
void sfx_win(void);         /* the last cavern cleared */

/* Plays an effect by its name ("jump" ... "win"); 0 if no such name. */
int  sfx_by_name(const char *name);

#endif
