/*
 * PPC audio stub — void_trader's modplay.c + sfx.c both bang directly
 * on the Paula chip registers via <hardware/custom.h>, which requires
 * classic Amiga chip RAM at $DFF000. sam460ex under OS4 has no such
 * memory and no such registers; the OS4-native audio path is AHI.
 *
 * Rather than port the MOD player + SFX engine to AHI (large job), we
 * swap this file in place of modplay.c and sfx.c on the PPC build.
 * The game runs silently. Every audio call becomes a no-op; return
 * values match the classic path's success cases so main.c doesn't
 * bail out during init.
 *
 * To wire real audio later: implement these functions on top of
 * ahi.device (see AHI SDK). No caller changes needed.
 */

#include "modplay.h"
#include "sfx.h"

/* ---- modplay stubs ---- */
int  modplay_init(void)          { return 1; }   /* pretend init succeeded */
void modplay_start(void)         {}
void modplay_start_song(int id)  { (void)id; }
void modplay_stop(void)          {}
void modplay_cleanup(void)       {}
void modplay_tick(void)          {}

/* ---- sfx stubs ---- */
int  vt_sfx_init(void)           { return 1; }
void vt_sfx_shutdown(void)       {}
void vt_sfx_play(int id)         { (void)id; }
