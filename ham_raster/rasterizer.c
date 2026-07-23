#include "rasterizer.h"

#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <proto/graphics.h>

/*
 * Rasterizer backends. See rasterizer.h for the shared interface.
 *
 * BACKEND 1: AreaFill (baseline)
 *   graphics.library AreaMove/AreaDraw/AreaEnd. One flat pen for
 *   the whole triangle (pen[0]). Blitter-accelerated. This is
 *   what aga3d/ham_test use.
 *
 * BACKEND 2: Manual scanline Gouraud
 *   Sort vertices by y. Walk two edges from top to bottom,
 *   tracking (x, pen) as fixed-point on each. For each scanline
 *   between them, interpolate pen linearly across the span and
 *   WritePixel each pixel. Because our palette is a smooth
 *   temperature ramp, walking pen linearly gives smoothly
 *   changing colour — cheap approximation of Gouraud shading
 *   without touching HAM modify codes.
 *
 *   Cost: ~50-100x slower than AreaFill because we're going
 *   through WritePixel per pixel. For a 30-face icosahedron
 *   filling ~10000 pixels total that's a few hundred thousand
 *   pixel writes — noticeable but still animates. The point of
 *   this backend is to SHOW smooth shading, not to be fast.
 */

/* ---- Backend 1: AreaFill ------------------------------------- */

static void draw_areafill(struct RastPort *rp,
                          const RTri *a, const RTri *b, const RTri *c)
{
    SetAPen(rp, a->pen);
    AreaMove(rp, a->x, a->y);
    AreaDraw(rp, b->x, b->y);
    AreaDraw(rp, c->x, c->y);
    AreaEnd(rp);
}

const Rasterizer rasterizer_areafill = {
    "AreaFill (blit)", draw_areafill
};

/* ---- Backend 2: Gouraud scanline ----------------------------- */

/*
 * Fixed-point for edge interpolation. 16.16 gives ~1/65k pixel
 * precision — plenty for a 320-wide screen.
 */
#define FP    16
#define ONE_F (1L << FP)

static int fmin3(int a, int b, int c) {
    int m = a < b ? a : b; return m < c ? m : c;
}
static int fmax3(int a, int b, int c) {
    int m = a > b ? a : b; return m > c ? m : c;
}

/*
 * Fill a horizontal span y from x0..x1 with linearly interpolated
 * pen values (pen0 at x0, pen1 at x1). Uses WritePixel which is
 * slow but correct — a real HAM Gouraud would batch write into
 * the bitplanes directly.
 */
static void span(struct RastPort *rp, int y, int x0, int x1,
                 LONG pen0_fp, LONG pen1_fp)
{
    if (x1 < x0) {
        int tx = x0; x0 = x1; x1 = tx;
        LONG tp = pen0_fp; pen0_fp = pen1_fp; pen1_fp = tp;
    }
    int span_w = x1 - x0;
    if (span_w == 0) return;
    LONG dpen = (pen1_fp - pen0_fp) / span_w;
    LONG cur  = pen0_fp;
    UBYTE last_pen = 255;
    for (int x = x0; x < x1; x++) {
        UBYTE p = (UBYTE)(cur >> FP);
        if (p != last_pen) { SetAPen(rp, p); last_pen = p; }
        WritePixel(rp, (WORD)x, (WORD)y);
        cur += dpen;
    }
}

/*
 * Triangle scanline. Sort vertices top-to-bottom. Handle the
 * general case (no assumption about flat top or flat bottom) by
 * splitting at the middle vertex — one half has flat bottom, the
 * other flat top.
 */
static void draw_gouraud(struct RastPort *rp,
                         const RTri *A, const RTri *B, const RTri *C)
{
    /* Sort by y so A.y <= B.y <= C.y. */
    const RTri *v[3] = {A, B, C};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2 - i; j++)
            if (v[j]->y > v[j+1]->y) {
                const RTri *t = v[j]; v[j] = v[j+1]; v[j+1] = t;
            }
    const RTri *v0 = v[0], *v1 = v[1], *v2 = v[2];

    /* Trivial reject */
    if (v0->y == v2->y) return;

    /* Screen bounds — we don't clip individual pixels since our
     * viewport clamp in main.c already keeps everything in range.
     * But bail if entirely off top/bottom. */
    (void)fmin3; (void)fmax3;

    /* Fixed-point x and pen for the long edge (v0..v2) and the
     * two short edges (v0..v1, v1..v2). */
    LONG dy_long  = v2->y - v0->y;
    LONG dx_long  = ((LONG)(v2->x - v0->x) << FP) / dy_long;
    LONG dp_long  = ((LONG)(v2->pen - v0->pen) << FP) / dy_long;
    LONG x_long   = (LONG)v0->x << FP;
    LONG p_long   = (LONG)v0->pen << FP;

    LONG dy_top   = v1->y - v0->y;
    LONG dx_top   = 0, dp_top = 0;
    LONG x_top    = (LONG)v0->x << FP;
    LONG p_top    = (LONG)v0->pen << FP;
    if (dy_top > 0) {
        dx_top = ((LONG)(v1->x - v0->x) << FP) / dy_top;
        dp_top = ((LONG)(v1->pen - v0->pen) << FP) / dy_top;
    }

    /* Top half: v0.y..v1.y */
    for (int y = v0->y; y < v1->y; y++) {
        span(rp, y, (int)(x_long >> FP), (int)(x_top >> FP),
             p_long, p_top);
        x_long += dx_long; p_long += dp_long;
        x_top  += dx_top;  p_top  += dp_top;
    }

    /* Second half: v1.y..v2.y using v1..v2 as the short edge. */
    LONG dy_bot = v2->y - v1->y;
    LONG dx_bot = 0, dp_bot = 0;
    LONG x_bot  = (LONG)v1->x << FP;
    LONG p_bot  = (LONG)v1->pen << FP;
    if (dy_bot > 0) {
        dx_bot = ((LONG)(v2->x - v1->x) << FP) / dy_bot;
        dp_bot = ((LONG)(v2->pen - v1->pen) << FP) / dy_bot;
    }
    for (int y = v1->y; y < v2->y; y++) {
        span(rp, y, (int)(x_long >> FP), (int)(x_bot >> FP),
             p_long, p_bot);
        x_long += dx_long; p_long += dp_long;
        x_bot  += dx_bot;  p_bot  += dp_bot;
    }
}

const Rasterizer rasterizer_gouraud = {
    "Gouraud (scanline)", draw_gouraud
};
