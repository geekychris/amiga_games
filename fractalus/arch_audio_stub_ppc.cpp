// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins

/*
 * PPC audio stub for fractalus.
 *
 * The 68k build's modplay.c drives Paula directly via <hardware/custom.h>
 * at $DFF000. sam460ex under OS4 has no chip RAM and no Paula, so the
 * whole file is unusable there. We swap in these no-op stubs on PPC.
 *
 * sfx.cpp is compiled on both arches: its sample-generation is pure
 * integer math, and its Sfx::play() dispatches through modplay_sfx()
 * below, which becomes a no-op — so the game runs silently on OS4.
 *
 * To wire real audio later on OS4: implement on top of ahi.device
 * (see AHI SDK) and drop this file. No caller-side changes needed.
 */

#include "modplay.h"

extern "C" {

int  modplay_init(void)                              { return 0; }
void modplay_start(void)                             {}
void modplay_start_song(int id)                      { (void)id; }
void modplay_stop(void)                              {}
void modplay_cleanup(void)                           {}
void modplay_tick(void)                              {}

void modplay_sfx(BYTE *data, UWORD len_words,
                 UWORD period, UWORD volume)
{
    (void)data; (void)len_words; (void)period; (void)volume;
}

}  /* extern "C" */
