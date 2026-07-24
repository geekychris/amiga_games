#ifndef HAM_RASTER_H
#define HAM_RASTER_H

#include <exec/types.h>

/*
 * Pluggable triangle rasterizer interface.
 *
 * Each rasterizer takes three 2D screen-space vertices and a pen
 * index per vertex. Flat-shading implementations use only pen[0].
 * Gouraud-style implementations interpolate the pen across the
 * triangle so per-pixel colour comes from the palette gradient.
 *
 * Screen coords are integer pixel positions; pen indices are
 * base-palette entries (0..63 in HAM8, but this file is agnostic).
 */

struct RastPort;

typedef struct {
    WORD  x, y;
    UBYTE pen;      /* palette index for flat backends       */
    UBYTE r, g, b;  /* 6-bit (0..63) RGB for HAM backend      */
} RTri;

typedef struct {
    const char *name;
    void (*draw)(struct RastPort *rp, const RTri *a,
                 const RTri *b, const RTri *c);
} Rasterizer;

/* Backends. Extern so main.c can build the list. */
extern const Rasterizer rasterizer_areafill;    /* graphics.library AreaFill, flat pen */
extern const Rasterizer rasterizer_gouraud;     /* scanline w/ interpolated palette pen */
extern const Rasterizer rasterizer_ham_gouraud; /* scanline w/ per-pixel HAM modify codes */

#endif
