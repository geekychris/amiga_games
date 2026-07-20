/*
 * modgen.h - Procedural ProTracker MOD generator
 * Builds an original heroic space-theme tune in chip RAM.
 */
#ifndef MODGEN_H
#define MODGEN_H

#include <exec/types.h>

/* Generate a MOD file in chip RAM.
 * Produces an original, heroic Bb-major space-flight theme with
 * melody, bass and pad channels. No third-party material referenced.
 * Returns pointer to chip RAM buffer (caller must FreeMem).
 * Sets *out_size to the total size of the MOD data.
 * Returns NULL on failure.
 */
UBYTE *generate_space_theme_mod(ULONG *out_size);

#endif
