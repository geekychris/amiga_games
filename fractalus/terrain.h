#ifndef FRACTALUS_TERRAIN_H
#define FRACTALUS_TERRAIN_H

#include <exec/types.h>

/*
 * Fractal heightfield for planet Fractalus. Generated once via recursive
 * midpoint displacement with a fixed seed so the world is deterministic
 * (same play session = same mountains — makes the game debuggable).
 *
 * Grid is TERRAIN_SIZE x TERRAIN_SIZE cells, wraps at edges so flying
 * off one side comes back on the other. Each cell holds a UBYTE height
 * (0..255). World-space one cell = TERRAIN_CELL_UNITS.
 */

#define TERRAIN_BITS       7                    /* 128 x 128 grid */
#define TERRAIN_SIZE       (1 << TERRAIN_BITS)
#define TERRAIN_MASK       (TERRAIN_SIZE - 1)
#define TERRAIN_CELL_SHIFT 6                    /* one cell = 64 world units */
#define TERRAIN_CELL_UNITS (1 << TERRAIN_CELL_SHIFT)

/* Height scaling: raw byte 0..255 maps to 0..MAX_HEIGHT world units. */
#define TERRAIN_HEIGHT_SHIFT 3
#define TERRAIN_MAX_HEIGHT   (255 << TERRAIN_HEIGHT_SHIFT)

class Terrain
{
public:
    /* Generate a fresh heightfield using the given RNG seed. */
    void generate(ULONG seed);

    /* Sample the height at integer grid coordinates (wraps). */
    inline UBYTE at(LONG gx, LONG gz) const
    {
        return heights[(gz & TERRAIN_MASK) * TERRAIN_SIZE
                     + (gx & TERRAIN_MASK)];
    }

    /* Height at a world-space point (used for landing / pilot placement). */
    LONG height_at_world(LONG wx, LONG wz) const;

    /* Raw storage — render.cpp needs the pointer for tight inner loops. */
    const UBYTE *raw() const { return heights; }

private:
    UBYTE heights[TERRAIN_SIZE * TERRAIN_SIZE];

    void midpoint_displace(ULONG seed);
    void smooth();
};

#endif
