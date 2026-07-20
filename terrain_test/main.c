/*
 * terrain_test - minimal principled voxel-space landscape flyer.
 *
 * ONE file. NO gameplay, NO HUD, NO combat. Just:
 *   - Fractal terrain (128x128 midpoint displacement, wraps)
 *   - Camera that auto-flies forward with slow yaw drift
 *   - Column-based voxel raycaster (Comanche/Fractalus style)
 *   - Bridge heartbeat so we can measure FPS from the host
 *
 * The point is to prove the RENDER is right in isolation, then port
 * the working algorithm back into the game later.
 *
 * Design decisions, called out because they matter:
 *
 * 1. Camera altitude is FIXED above the tallest possible terrain
 *    point (cam_y = 700 vs max_h = 510). This eliminates the
 *    "camera-inside-mountain" degeneracy where the very first ray
 *    sample fills the whole column with one colour and everything
 *    else is occluded. In a real game you'd fly lower and let peaks
 *    poke above the horizon; this demo just wants the algorithm to
 *    be visibly correct first.
 *
 * 2. Ray march runs to FAR_DIST = 6000 world units with an
 *    aggressive log-ish step schedule. When flying above terrain,
 *    the projected screen y climbs UP toward the horizon SLOWLY as
 *    dist grows — a sample at dist=200 might project to y=300
 *    (below screen), a sample at dist=3000 lands at y=140 (near
 *    horizon). If the ray stops early we never fill the top of the
 *    ground band and the sky bleeds through. Prior versions
 *    stopped at 512 which is why the screen looked empty.
 *
 * 3. Palette: 32 sky pens (0..31, indigo->warm), 64 terrain pens
 *    (32..95, 8 heights x 8 fog distances). Colour picked per
 *    sample not per column, so vertical bands of colour give the
 *    depth cueing the eye reads as distance.
 *
 * 4. Every strip is a plain RectFill. It's the blitter, it's fast
 *    enough. No exotic C2P.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/displayinfo.h>
#include <dos/dos.h>
#include <dos/dosextens.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

/* ================================================================== */
/* Screen                                                             */
/* ================================================================== */

#define SCREEN_W     320
#define SCREEN_H     256
#define HORIZON_Y    128            /* row of the true horizon on screen */

struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;

/* ================================================================== */
/* Fractal terrain — 128x128 heightfield, wrap                        */
/* ================================================================== */

#define TERRAIN_SIZE       128
#define TERRAIN_MASK       127
#define TERRAIN_CELL_UNITS 16       /* world units per cell */
#define HEIGHT_SHIFT       1        /* raw byte 0..255 -> world 0..510  */
#define TERRAIN_MAX_HEIGHT (255 << HEIGHT_SHIFT)

static UBYTE terrain[TERRAIN_SIZE * TERRAIN_SIZE];

static ULONG rng_s;
static ULONG rng_next(void)  { rng_s = rng_s * 1664525UL + 1013904223UL; return rng_s; }
static LONG  jitter(LONG amp) { return amp <= 0 ? 0 : (LONG)(rng_next() % (2 * amp + 1)) - amp; }
static LONG  wrap(LONG v)     { return v & TERRAIN_MASK; }

static UBYTE gget(LONG z, LONG x)
{
    return terrain[wrap(z) * TERRAIN_SIZE + wrap(x)];
}
static void gset(LONG z, LONG x, LONG v)
{
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    terrain[wrap(z) * TERRAIN_SIZE + wrap(x)] = (UBYTE)v;
}

static void terrain_generate(ULONG seed)
{
    LONG i, z, x, step, half, amp;
    rng_s = seed ? seed : 0xC0FFEE01UL;
    for (i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++) terrain[i] = 0;
    gset(0, 0, 128);                /* wrap-corner seed */

    step = TERRAIN_SIZE;
    amp  = 96;
    while (step > 1) {
        half = step >> 1;

        /* Diamond: centre of each square = avg of 4 corners */
        for (z = 0; z < TERRAIN_SIZE; z += step) {
            for (x = 0; x < TERRAIN_SIZE; x += step) {
                LONG a = gget(z,        x);
                LONG b = gget(z,        x + step);
                LONG c = gget(z + step, x);
                LONG d = gget(z + step, x + step);
                gset(z + half, x + half, ((a+b+c+d) >> 2) + jitter(amp));
            }
        }

        /* Square: west + north edges (east + south owned by neighbours via wrap) */
        for (z = 0; z < TERRAIN_SIZE; z += step) {
            for (x = 0; x < TERRAIN_SIZE; x += step) {
                LONG a, b, c, d;
                /* West edge midpoint (z+half, x) */
                a = gget(z,          x);
                b = gget(z + step,   x);
                c = gget(z + half,   x - half);
                d = gget(z + half,   x + half);
                gset(z + half, x, ((a+b+c+d) >> 2) + jitter(amp));
                /* North edge midpoint (z, x+half) */
                a = gget(z,          x);
                b = gget(z,          x + step);
                c = gget(z - half,   x + half);
                d = gget(z + half,   x + half);
                gset(z, x + half, ((a+b+c+d) >> 2) + jitter(amp));
            }
        }
        step = half;
        amp  = (amp * 5) >> 3;      /* roughness = 0.625 per octave */
    }

    /* 3x3 smooth once — kills single-pixel spikes from midpoint noise */
    {
        static UBYTE tmp[TERRAIN_SIZE * TERRAIN_SIZE];
        for (z = 0; z < TERRAIN_SIZE; z++) {
            for (x = 0; x < TERRAIN_SIZE; x++) {
                LONG s = 0, dz, dx;
                for (dz = -1; dz <= 1; dz++)
                    for (dx = -1; dx <= 1; dx++)
                        s += gget(z + dz, x + dx);
                tmp[z * TERRAIN_SIZE + x] = (UBYTE)(s / 9);
            }
        }
        for (i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++) terrain[i] = tmp[i];
    }
}

/* Bilinear world-space sample -> world-unit height. */
static LONG terrain_height_at(LONG wx, LONG wz)
{
    /* Cell = 16 world units. Sub-cell fraction in 0..255. */
    LONG cx = wx / TERRAIN_CELL_UNITS;
    LONG cz = wz / TERRAIN_CELL_UNITS;
    LONG fx = ((wx - cx * TERRAIN_CELL_UNITS) * 256) / TERRAIN_CELL_UNITS;
    LONG fz = ((wz - cz * TERRAIN_CELL_UNITS) * 256) / TERRAIN_CELL_UNITS;
    if (fx < 0) { fx += 256; cx--; }
    if (fz < 0) { fz += 256; cz--; }
    {
        LONG h00 = (LONG)gget(cz,     cx)     << HEIGHT_SHIFT;
        LONG h10 = (LONG)gget(cz,     cx + 1) << HEIGHT_SHIFT;
        LONG h01 = (LONG)gget(cz + 1, cx)     << HEIGHT_SHIFT;
        LONG h11 = (LONG)gget(cz + 1, cx + 1) << HEIGHT_SHIFT;
        LONG a = h00 + ((h10 - h00) * fx) / 256;
        LONG b = h01 + ((h11 - h01) * fx) / 256;
        return a + ((b - a) * fz) / 256;
    }
}

/* ================================================================== */
/* Integer sin/cos (Bhaskara I, corrected)                            */
/* ================================================================== */

#define ANGLE_FULL  4096
#define ANGLE_MASK  4095
#define ANGLE_HALF  2048
#define ANGLE_QUART 1024
#define TRIG_SHIFT  12
#define TRIG_ONE    4096

static LONG sintab[ANGLE_FULL];

static void sintab_build(void)
{
    LONG i;
    for (i = 0; i < ANGLE_FULL; i++) {
        LONG a = i, sign = 1;
        if (a >= ANGLE_HALF) { a -= ANGLE_HALF; sign = -1; }
        {
            LONG x   = a;
            LONG y   = ANGLE_HALF - a;
            LONG xy  = x * y;
            LONG num = 16L * xy;
            LONG den = (5L * ANGLE_HALF * ANGLE_HALF - 4L * xy) / TRIG_ONE;
            if (den == 0) den = 1;
            sintab[i] = (num / den) * sign;
        }
    }
}

static LONG isin_a(LONG a) { return sintab[a & ANGLE_MASK]; }
static LONG icos_a(LONG a) { return sintab[(a + ANGLE_QUART) & ANGLE_MASK]; }

/* ================================================================== */
/* Camera                                                             */
/* ================================================================== */

/* Fixed altitude guaranteed above any terrain: max terrain = 510. */
static LONG cam_x   = 4096;
static LONG cam_z   = 4096;
static LONG cam_y   = 700;
static LONG cam_yaw = 0;

/* Per-tick motion: 8 units forward, tiny yaw drift so the view
 * changes even if you stare at it. */
#define CAM_SPEED    8
#define CAM_YAW_RATE 3

static void camera_tick(void)
{
    cam_x += (isin_a(cam_yaw) * CAM_SPEED) >> TRIG_SHIFT;
    cam_z += (icos_a(cam_yaw) * CAM_SPEED) >> TRIG_SHIFT;
    cam_yaw = (cam_yaw + CAM_YAW_RATE) & ANGLE_MASK;
}

/* ================================================================== */
/* Palette                                                            */
/* ================================================================== */

#define PAL_SKY_BASE       0
#define PAL_SKY_COUNT      32
#define PAL_TERRAIN_BASE   32
#define PAL_TERRAIN_COUNT  64            /* 8 heights x 8 fog bins */

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void set_rgb(struct ViewPort *vp, UWORD pen, int r, int g, int b)
{
    r = clampi(r, 0, 255);
    g = clampi(g, 0, 255);
    b = clampi(b, 0, 255);
    SetRGB32(vp, pen, (ULONG)r << 24, (ULONG)g << 24, (ULONG)b << 24);
}

static void install_palette(struct ViewPort *vp)
{
    int i, h, d;
    /* Sky: dawn indigo above -> warm orange at the horizon. */
    for (i = 0; i < PAL_SKY_COUNT; i++) {
        int t = i;
        int r =  25 + (t * 220) / (PAL_SKY_COUNT - 1);
        int g =  15 + (t *  95) / (PAL_SKY_COUNT - 1);
        int b =  95 - (t *  75) / (PAL_SKY_COUNT - 1);
        set_rgb(vp, PAL_SKY_BASE + i, r, g, b);
    }
    /* Terrain: 8 height ramp shading toward the horizon colour with
     * distance. Ramp is deep valley green -> sun-lit tan peaks. */
    {
        static const struct { int r, g, b; } ramp[8] = {
            {  18,  40,  16 },
            {  30,  70,  22 },
            {  45, 105,  30 },
            {  60, 135,  40 },
            {  95, 155,  50 },
            { 140, 165,  55 },
            { 180, 155,  70 },
            { 210, 175,  95 },
        };
        int hr = 240, hg = 110, hb = 25;         /* horizon fog colour */
        for (h = 0; h < 8; h++) {
            for (d = 0; d < 8; d++) {
                int mix = d * 32;                /* 0..224 */
                int r = ramp[h].r + ((hr - ramp[h].r) * mix) / 256;
                int g = ramp[h].g + ((hg - ramp[h].g) * mix) / 256;
                int b = ramp[h].b + ((hb - ramp[h].b) * mix) / 256;
                set_rgb(vp, PAL_TERRAIN_BASE + h * 8 + d, r, g, b);
            }
        }
    }
}

/* ================================================================== */
/* Renderer — voxel column raycaster                                  */
/* ================================================================== */

/*
 * Parameters. FOV wide enough to feel airy; PROJ chosen so a mountain
 * at distance 400 with h=cam_y+200 subtends ~110 px vertically. Max
 * distance MUST be large enough that a ground sample projects up to
 * the horizon — 6000 covers cam_y up to ~1000 with PROJ 220.
 */
#define PROJ        220
#define FOV_SPAN    900       /* out of ANGLE_FULL=4096 -> ~79 deg */
#define NEAR_DIST   6
#define FAR_DIST    6000
#define COL_STEP    4

static void draw_sky(struct RastPort *rp)
{
    int i;
    for (i = 0; i < PAL_SKY_COUNT; i++) {
        int y0 = (i * SCREEN_H) / PAL_SKY_COUNT;
        int y1 = ((i + 1) * SCREEN_H) / PAL_SKY_COUNT - 1;
        SetAPen(rp, (UBYTE)(PAL_SKY_BASE + i));
        RectFill(rp, 0, y0, SCREEN_W - 1, y1);
    }
}

static void draw_terrain(struct RastPort *rp)
{
    int col;
    for (col = 0; col < SCREEN_W; col += COL_STEP) {
        LONG dcol    = (LONG)col - (SCREEN_W >> 1);
        LONG ray_yaw = (cam_yaw + (dcol * FOV_SPAN) / SCREEN_W) & ANGLE_MASK;
        LONG rdx     = isin_a(ray_yaw);
        LONG rdz     = icos_a(ray_yaw);
        int  cx1     = col;
        int  cx2     = col + COL_STEP - 1;
        if (cx2 >= SCREEN_W) cx2 = SCREEN_W - 1;

        /* y_top starts one past the bottom of the viewport. As dist
         * grows, each new sample projects HIGHER on screen (smaller y)
         * whenever it exposes new pixels; only then do we paint. */
        int y_top = SCREEN_H;

        LONG dist = NEAR_DIST;
        LONG step = 2;

        while (dist < FAR_DIST && y_top > 0) {
            LONG wx = cam_x + ((rdx * dist) >> TRIG_SHIFT);
            LONG wz = cam_z + ((rdz * dist) >> TRIG_SHIFT);
            LONG h  = terrain_height_at(wx, wz);

            /* Projected screen y for this world point. dy>0 means the
             * point is above camera (screen y < horizon = higher on
             * screen); dy<0 means below (screen y > horizon). */
            LONG dy       = h - cam_y;
            LONG projected = HORIZON_Y - (dy * PROJ) / dist;

            /* Clip to viewport bounds. Off-screen top and bottom both
             * become boundary values so painter's algo can still see
             * the transition through them. */
            if (projected < 0)            projected = 0;
            if (projected > SCREEN_H - 1) projected = SCREEN_H - 1;

            if (projected < y_top) {
                /* Colour lookup: 8 height bins x 8 fog bins. */
                int h_bin = (int)((h * 8) / (TERRAIN_MAX_HEIGHT + 1));
                int d_bin = (int)((dist * 8) / FAR_DIST);
                if (h_bin < 0) h_bin = 0; else if (h_bin > 7) h_bin = 7;
                if (d_bin < 0) d_bin = 0; else if (d_bin > 7) d_bin = 7;
                SetAPen(rp, (UBYTE)(PAL_TERRAIN_BASE + h_bin * 8 + d_bin));
                RectFill(rp, cx1, (int)projected, cx2, y_top - 1);
                y_top = (int)projected;
            }

            /* Log-ish step schedule. Near samples need fine resolution
             * because a small dist change moves the projection a lot;
             * far samples can be coarse because they compress toward
             * the horizon. Tune here if the ground looks banded. */
            dist += step;
            if (dist >   40) step = 4;
            if (dist >  120) step = 10;
            if (dist >  400) step = 30;
            if (dist > 1200) step = 80;
            if (dist > 3000) step = 200;
        }
    }
}

/* ================================================================== */
/* Amiga screen setup + main loop                                     */
/* ================================================================== */

static struct Screen       *screen;
static struct Window       *window;
static struct ScreenBuffer *sbuf[2];
static struct RastPort      rp_buf[2];
static UWORD                cur_buf = 1;   /* frame 1 draws to sbuf[1] off-screen */

static int open_display(void)
{
    memset(&rp_buf[0], 0, sizeof(rp_buf[0]));
    memset(&rp_buf[1], 0, sizeof(rp_buf[1]));

    screen = OpenScreenTags(NULL,
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     8,
        SA_DisplayID, LORES_KEY,
        SA_Title,     (ULONG)"terrain_test",
        SA_ShowTitle, FALSE,
        SA_Quiet,     TRUE,
        SA_Type,      CUSTOMSCREEN,
        TAG_DONE);
    if (!screen) return 1;

    window = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)screen,
        WA_Left, 0, WA_Top, 0,
        WA_Width, SCREEN_W, WA_Height, SCREEN_H,
        WA_Borderless, TRUE, WA_Backdrop, TRUE,
        WA_Activate, TRUE,
        WA_IDCMP, IDCMP_RAWKEY,
        TAG_DONE);
    if (!window) return 2;

    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) return 3;

    InitRastPort(&rp_buf[0]); rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]); rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    install_palette(&screen->ViewPort);
    return 0;
}

static void close_display(void)
{
    if (sbuf[0]) {
        int t = 0;
        while (!ChangeScreenBuffer(screen, sbuf[0]) && ++t < 5) WaitTOF();
        WaitTOF(); WaitTOF();
    }
    if (sbuf[1]) { FreeScreenBuffer(screen, sbuf[1]); sbuf[1] = NULL; }
    if (sbuf[0]) { FreeScreenBuffer(screen, sbuf[0]); sbuf[0] = NULL; }
    if (window)  { CloseWindow(window); window = NULL; }
    if (screen)  { CloseScreen(screen); screen = NULL; }
}

static void flip(void)
{
    WaitBlit();
    if (ChangeScreenBuffer(screen, sbuf[cur_buf])) {
        WaitTOF();
        cur_buf ^= 1;
    } else {
        WaitTOF();
    }
}

int main(void)
{
    ULONG frame = 0;
    int bridge_ok, running = 1;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary(
        (CONST_STRPTR)"intuition.library", 39);
    if (!IntuitionBase) { printf("no intuition\n"); return 20; }
    GfxBase = (struct GfxBase *)OpenLibrary(
        (CONST_STRPTR)"graphics.library", 39);
    if (!GfxBase) { CloseLibrary((struct Library *)IntuitionBase); return 20; }

    bridge_ok = (ab_init("terrain_test") == 0);
    if (bridge_ok) AB_I("terrain_test starting");

    sintab_build();
    terrain_generate(0xC0FFEE01UL);

    if (bridge_ok) {
        LONG mn = 255, mx = 0, i;
        ULONG sum = 0;
        for (i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++) {
            if (terrain[i] < mn) mn = terrain[i];
            if (terrain[i] > mx) mx = terrain[i];
            sum += terrain[i];
        }
        AB_I("terrain min=%ld max=%ld mean=%ld cam_y=%ld",
             (long)mn, (long)mx,
             (long)(sum / (TERRAIN_SIZE * TERRAIN_SIZE)),
             (long)cam_y);
    }

    if (open_display()) {
        printf("open_display failed\n");
        if (bridge_ok) ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 20;
    }

    while (running) {
        struct IntuiMessage *msg;
        struct RastPort     *rp;
        ULONG sig = SetSignal(0L, 0L);
        if (sig & SIGBREAKF_CTRL_C) break;

        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);
            if (msg->Class == IDCMP_RAWKEY && (code & 0x7F) == 0x45) running = 0;
        }
        if (!running) break;

        camera_tick();

        rp = &rp_buf[cur_buf];
        rp->BitMap = sbuf[cur_buf]->sb_BitMap;
        draw_sky(rp);
        draw_terrain(rp);
        flip();

        frame++;
        if (bridge_ok) ab_poll();

        /* Real-clock heartbeat every 30 frames — fps + camera pos so
         * we can tell from outside that frames are being generated. */
        if (bridge_ok && (frame % 30) == 0) {
            struct DateStamp now;
            static struct DateStamp prev = { 0, 0, 0 };
            static ULONG prev_frame = 0;
            LONG dticks, dframes, fps10;
            DateStamp(&now);
            dticks  = (now.ds_Days   - prev.ds_Days)   * 24 * 60 * 3000
                    + (now.ds_Minute - prev.ds_Minute) * 3000
                    + (now.ds_Tick   - prev.ds_Tick);
            dframes = (LONG)(frame - prev_frame);
            fps10 = (dticks > 0) ? (500L * dframes) / dticks : 0;
            AB_I("hb frame=%ld fps=%ld.%ld cam=(%ld,%ld,%ld) yaw=%ld",
                 (long)frame,
                 (long)(fps10 / 10), (long)(fps10 % 10),
                 (long)cam_x, (long)cam_y, (long)cam_z,
                 (long)cam_yaw);
            prev = now;
            prev_frame = frame;
        }
    }

    close_display();
    if (bridge_ok) { AB_I("terrain_test shutting down (%lu frames)", frame); ab_cleanup(); }
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
