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

/* ---- Backend 3: HAM Gouraud (real modify codes) ---------------
 *
 * HAM8 pixel encoding (8-bit pen index):
 *   bits 7:6 = control
 *     00  base-palette index  (bits 5:0 pick palette entry 0..63)
 *     01  modify BLUE  channel to (bits 5:0) << 2
 *     10  modify RED   channel to (bits 5:0) << 2
 *     11  modify GREEN channel to (bits 5:0) << 2
 *
 * Strategy per scanline:
 *   1. At the leftmost pixel, emit a base-palette pen that best
 *      approximates the target RGB (we can't rely on whatever the
 *      previous triangle / backdrop left in the "held" register).
 *   2. Track the currently-held R, G, B.
 *   3. For each subsequent pixel, compare target vs held and emit
 *      the modify code for whichever channel needs the biggest
 *      change. Update held to reflect what we just wrote.
 *   4. Interpolate R, G, B linearly along both edges + span in
 *      16.16 fixed point.
 *
 * The 48-entry palette from main.c is a temperature ramp that
 * roughly samples the R/G/B cube; nearest-match at the left edge
 * gets us close enough that the first 1-2 modify codes finish the
 * transition invisibly.
 *
 * All 6-bit RGB internally; hardware SetRGB32 in main.c uses 8-bit
 * so multiply by 4 to compare.
 */

/* Palette formula lifted from main.c — pen 0..47 -> RGB (0..252
 * each), reduced to 6-bit (0..63) for HAM domain. */
static UBYTE pal_r[48], pal_g[48], pal_b[48];
static UBYTE pal_ready = 0;

static void build_pal_table(void)
{
    for (int i = 0; i < 48; i++) {
        int t = (i * 255) / 47;
        int r, g, b;
        if (t < 64) {
            r =  20 + (t * 80) / 63;
            g =   5 + (t * 15) / 63;
            b =  40 + (t * 90) / 63;
        } else if (t < 128) {
            int u = t - 64;
            r = 100 + (u * 140) / 63;
            g =  20 + (u *  40) / 63;
            b = 130 - (u *  90) / 63;
        } else if (t < 192) {
            int u = t - 128;
            r = 240;
            g =  60 + (u * 180) / 63;
            b =  40 - (u *  30) / 63;
        } else {
            int u = t - 192;
            r = 240 + (u *  15) / 63;
            g = 240 + (u *  15) / 63;
            b =  10 + (u * 240) / 63;
        }
        if (r < 0) r = 0; if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        if (b < 0) b = 0; if (b > 255) b = 255;
        pal_r[i] = (UBYTE)(r >> 2);
        pal_g[i] = (UBYTE)(g >> 2);
        pal_b[i] = (UBYTE)(b >> 2);
    }
    pal_ready = 1;
}

/* Nearest palette pen to a target 6-bit RGB — linear scan is fine,
 * 48 entries per starting pixel of each scanline. */
static UBYTE pal_nearest(UBYTE r, UBYTE g, UBYTE b)
{
    int best_i = 0, best_d = 0x7FFFFFFF;
    for (int i = 0; i < 48; i++) {
        int dr = pal_r[i] - r, dg = pal_g[i] - g, db = pal_b[i] - b;
        int d = dr * dr + dg * dg + db * db;
        if (d < best_d) { best_d = d; best_i = i; }
    }
    return (UBYTE)best_i;
}

/* HAM modify pen encodings. Given a 6-bit channel value, produce
 * the pen that will hold-and-modify that channel to it. */
#define HAM_MOD_B(v) (UBYTE)(0x40 | ((v) & 0x3F))
#define HAM_MOD_R(v) (UBYTE)(0x80 | ((v) & 0x3F))
#define HAM_MOD_G(v) (UBYTE)(0xC0 | ((v) & 0x3F))

static void span_ham(struct RastPort *rp, int y, int x0, int x1,
                     int r0, int g0, int b0,
                     int r1, int g1, int b1)
{
    if (x1 < x0) {
        int t = x0; x0 = x1; x1 = t;
        t = r0; r0 = r1; r1 = t;
        t = g0; g0 = g1; g1 = t;
        t = b0; b0 = b1; b1 = t;
    }
    int w = x1 - x0;
    if (w == 0) return;
    /* Fixed-point 16.16 per channel across the span. */
    LONG dr = ((LONG)(r1 - r0) << 16) / w;
    LONG dg = ((LONG)(g1 - g0) << 16) / w;
    LONG db = ((LONG)(b1 - b0) << 16) / w;
    LONG cr = (LONG)r0 << 16;
    LONG cg = (LONG)g0 << 16;
    LONG cb = (LONG)b0 << 16;

    /* Leftmost pixel: nearest palette pen so the held register is
     * primed with something close to the target. */
    UBYTE tr = (UBYTE)(cr >> 16);
    UBYTE tg = (UBYTE)(cg >> 16);
    UBYTE tb = (UBYTE)(cb >> 16);
    UBYTE first_pen = pal_nearest(tr, tg, tb);
    SetAPen(rp, first_pen);
    WritePixel(rp, (WORD)x0, (WORD)y);
    UBYTE held_r = pal_r[first_pen];
    UBYTE held_g = pal_g[first_pen];
    UBYTE held_b = pal_b[first_pen];

    cr += dr; cg += dg; cb += db;

    /* Remaining pixels: pick the channel with biggest error, emit
     * the modify pen, update held. */
    for (int x = x0 + 1; x < x1; x++) {
        UBYTE ttr = (UBYTE)(cr >> 16);
        UBYTE ttg = (UBYTE)(cg >> 16);
        UBYTE ttb = (UBYTE)(cb >> 16);
        int er = (int)ttr - (int)held_r; if (er < 0) er = -er;
        int eg = (int)ttg - (int)held_g; if (eg < 0) eg = -eg;
        int eb = (int)ttb - (int)held_b; if (eb < 0) eb = -eb;
        UBYTE pen;
        if (er >= eg && er >= eb) {
            pen = HAM_MOD_R(ttr);
            held_r = ttr;
        } else if (eg >= eb) {
            pen = HAM_MOD_G(ttg);
            held_g = ttg;
        } else {
            pen = HAM_MOD_B(ttb);
            held_b = ttb;
        }
        SetAPen(rp, pen);
        WritePixel(rp, (WORD)x, (WORD)y);
        cr += dr; cg += dg; cb += db;
    }
}

static void draw_ham_gouraud(struct RastPort *rp,
                             const RTri *A, const RTri *B, const RTri *C)
{
    if (!pal_ready) build_pal_table();

    /* Sort by y. */
    const RTri *v[3] = {A, B, C};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2 - i; j++)
            if (v[j]->y > v[j+1]->y) {
                const RTri *t = v[j]; v[j] = v[j+1]; v[j+1] = t;
            }
    const RTri *v0 = v[0], *v1 = v[1], *v2 = v[2];
    if (v0->y == v2->y) return;

    LONG dy_long  = v2->y - v0->y;
    LONG dx_long  = ((LONG)(v2->x - v0->x) << 16) / dy_long;
    LONG dr_long  = ((LONG)(v2->r - v0->r) << 16) / dy_long;
    LONG dg_long  = ((LONG)(v2->g - v0->g) << 16) / dy_long;
    LONG db_long  = ((LONG)(v2->b - v0->b) << 16) / dy_long;
    LONG x_long   = (LONG)v0->x << 16;
    LONG r_long   = (LONG)v0->r << 16;
    LONG g_long   = (LONG)v0->g << 16;
    LONG b_long   = (LONG)v0->b << 16;

    LONG dy_top   = v1->y - v0->y;
    LONG dx_top = 0, dr_top = 0, dg_top = 0, db_top = 0;
    LONG x_top = (LONG)v0->x << 16;
    LONG r_top = (LONG)v0->r << 16;
    LONG g_top = (LONG)v0->g << 16;
    LONG b_top = (LONG)v0->b << 16;
    if (dy_top > 0) {
        dx_top = ((LONG)(v1->x - v0->x) << 16) / dy_top;
        dr_top = ((LONG)(v1->r - v0->r) << 16) / dy_top;
        dg_top = ((LONG)(v1->g - v0->g) << 16) / dy_top;
        db_top = ((LONG)(v1->b - v0->b) << 16) / dy_top;
    }
    for (int y = v0->y; y < v1->y; y++) {
        span_ham(rp, y,
                 (int)(x_long >> 16), (int)(x_top >> 16),
                 (int)(r_long >> 16), (int)(g_long >> 16), (int)(b_long >> 16),
                 (int)(r_top  >> 16), (int)(g_top  >> 16), (int)(b_top  >> 16));
        x_long += dx_long; r_long += dr_long; g_long += dg_long; b_long += db_long;
        x_top  += dx_top;  r_top  += dr_top;  g_top  += dg_top;  b_top  += db_top;
    }

    LONG dy_bot = v2->y - v1->y;
    LONG dx_bot = 0, dr_bot = 0, dg_bot = 0, db_bot = 0;
    LONG x_bot = (LONG)v1->x << 16;
    LONG r_bot = (LONG)v1->r << 16;
    LONG g_bot = (LONG)v1->g << 16;
    LONG b_bot = (LONG)v1->b << 16;
    if (dy_bot > 0) {
        dx_bot = ((LONG)(v2->x - v1->x) << 16) / dy_bot;
        dr_bot = ((LONG)(v2->r - v1->r) << 16) / dy_bot;
        dg_bot = ((LONG)(v2->g - v1->g) << 16) / dy_bot;
        db_bot = ((LONG)(v2->b - v1->b) << 16) / dy_bot;
    }
    for (int y = v1->y; y < v2->y; y++) {
        span_ham(rp, y,
                 (int)(x_long >> 16), (int)(x_bot >> 16),
                 (int)(r_long >> 16), (int)(g_long >> 16), (int)(b_long >> 16),
                 (int)(r_bot  >> 16), (int)(g_bot  >> 16), (int)(b_bot  >> 16));
        x_long += dx_long; r_long += dr_long; g_long += dg_long; b_long += db_long;
        x_bot  += dx_bot;  r_bot  += dr_bot;  g_bot  += dg_bot;  b_bot  += db_bot;
    }
}

const Rasterizer rasterizer_ham_gouraud = {
    "HAM Gouraud (modify)", draw_ham_gouraud
};
