#ifndef HAM_RASTER_SOUND_H
#define HAM_RASTER_SOUND_H

#include <exec/types.h>

/* Ported from examples/boing_ball — a synthesized metallic clang.
 * sound_init returns FALSE if audio.device / chip memory couldn't be
 * grabbed; in that case sound_play_boing() and sound_cleanup() are
 * still safe (they no-op / free what got allocated). */
BOOL sound_init(void);
void sound_play_boing(void);
void sound_cleanup(void);

#endif
