#include "render.h"
#include "terrain.h"
#include "game.h"
#include "pilots.h"
#include "combat.h"

#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/displayinfo.h>
#include <graphics/text.h>
#include <intuition/intuition.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <string.h>

extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase       *GfxBase;

/* ------------------------------------------------------------------ */
/* Shared integer sin/cos — TRIG_ONE scaled, ANGLE_FULL entries.        */
LONG sin_table[ANGLE_FULL];

LONG isin(LONG a) { return sin_table[a & ANGLE_MASK]; }
LONG icos(LONG a) { return sin_table[(a + ANGLE_QUART) & ANGLE_MASK]; }

/*
 * Bhaskara I sine approximation, accurate to ~0.16%:
 *   sin(x) ≈ 16 x (π - x) / (5π² - 4 x (π - x))    for x in [0, π]
 *
 * In our units x is [0, ANGLE_HALF] representing [0, π]. Result is
 * scaled by TRIG_ONE (4096). Denominator is pre-divided by TRIG_ONE
 * so the final division stays in 32-bit even without long-long.
 *
 * (Earlier version had a factor-of-4 bug in the numerator — every
 * rotation in the game was 1/4 of intended, which showed up first as
 * saucer spawn distances collapsing to a quarter of the requested
 * range. Fixed here.)
 */
void render_init_math()
{
    for (LONG i = 0; i < ANGLE_FULL; i++) {
        LONG a = i;
        LONG sign = 1;
        if (a >= ANGLE_HALF) { a -= ANGLE_HALF; sign = -1; }
        LONG x = a;
        LONG y = ANGLE_HALF - a;
        LONG xy  = x * y;                        /* 0..1M */
        LONG num = 16L * xy;                     /* 0..16.7M */
        LONG den = (5L * ANGLE_HALF * ANGLE_HALF - 4L * xy) / TRIG_ONE;
        if (den == 0) den = 1;
        LONG s = num / den;                      /* TRIG_ONE-scaled 0..4096 */
        sin_table[i] = (LONG)(s * sign);
    }
}

/* ------------------------------------------------------------------ */
/* Palette generator.                                                   */

static void put_rgb(struct ViewPort *vp, UWORD pen, int r, int g, int b)
{
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    SetRGB32(vp, pen, (ULONG)r << 24, (ULONG)g << 24, (ULONG)b << 24);
}

void Renderer::install_palette()
{
    struct ViewPort *vp = &screen->ViewPort;
    int i;

    /* Sky: deep indigo at top → dusty orange at horizon. Fractalus's
     * planet was mostly seen at twilight; this palette mirrors that. */
    for (i = 0; i < PAL_SKY_COUNT; i++) {
        int t = i;
        int r =  20 + (t * 220) / (PAL_SKY_COUNT - 1);
        int g =  10 + (t * 100) / (PAL_SKY_COUNT - 1);
        int b =  90 - (t *  70) / (PAL_SKY_COUNT - 1);
        put_rgb(vp, PAL_SKY_BASE + i, r, g, b);
    }

    /* Terrain: 8 height bins × 8 fog bins.
     *   height ramp:  dark green (low) → sandy tan (peaks)
     *   fog: mix with horizon sky colour as distance grows */
    static const struct { int r, g, b; } height_ramp[8] = {
        {  20,  40,  16 },   /* deep valley */
        {  30,  70,  22 },
        {  45, 100,  30 },
        {  60, 130,  36 },
        {  95, 150,  44 },
        { 140, 160,  52 },
        { 180, 155,  70 },
        { 210, 175,  95 },   /* sun-lit peak */
    };
    int horizon_r = 240, horizon_g = 110, horizon_b = 20;
    for (int h = 0; h < 8; h++) {
        for (int d = 0; d < 8; d++) {
            /* d=0 near (crisp), d=7 far (heavily fogged) */
            int mix = d * 32;   /* 0..224 */
            int r = height_ramp[h].r + ((horizon_r - height_ramp[h].r) * mix) / 256;
            int g = height_ramp[h].g + ((horizon_g - height_ramp[h].g) * mix) / 256;
            int b = height_ramp[h].b + ((horizon_b - height_ramp[h].b) * mix) / 256;
            put_rgb(vp, PAL_TERRAIN_BASE + h * 8 + d, r, g, b);
        }
    }

    /* Cockpit: dull metallic browns/greys. */
    for (i = 0; i < PAL_COCKPIT_COUNT; i++) {
        int v = 40 + i * 10;
        put_rgb(vp, PAL_COCKPIT_BASE + i, v, v - 5, v - 15);
    }

    /* HUD: classic phosphor greens. */
    for (i = 0; i < PAL_HUD_COUNT; i++) {
        int v = 30 + i * 15;
        put_rgb(vp, PAL_HUD_BASE + i, v / 4, v, v / 3);
    }

    /* MISC pens for FX (jumpscare flash, teeth, blood tones, etc). */
    put_rgb(vp, PAL_MISC_BASE + 0, 30, 30, 30);          /* dark grey  */
    put_rgb(vp, PAL_MISC_BASE + 1, 220, 220, 220);       /* light grey */
    put_rgb(vp, PAL_MISC_BASE + 2, 140, 20, 10);         /* dark red   */
    put_rgb(vp, PAL_MISC_BASE + 3, 220, 60, 30);         /* mid red    */
    put_rgb(vp, PAL_MISC_BASE + 4, 250, 200, 40);        /* bright     */
    put_rgb(vp, PAL_MISC_BASE + 5, 20, 200, 40);         /* laser grn  */
    put_rgb(vp, PAL_MISC_BASE + 6, 250, 250, 220);       /* pilot suit */
    put_rgb(vp, PAL_MISC_BASE + 7, 255, 255, 210);       /* teeth      */
}

/* ------------------------------------------------------------------ */
/* Display setup.                                                       */

int Renderer::open_display()
{
    memset(&rp_buf[0], 0, sizeof(rp_buf[0]));
    memset(&rp_buf[1], 0, sizeof(rp_buf[1]));

    screen = OpenScreenTags(NULL,
        SA_Width,     R_SCREEN_W,
        SA_Height,    R_SCREEN_H,
        SA_Depth,     8,
        SA_DisplayID, LORES_KEY,
        SA_Title,     (ULONG)"Fractalus",
        SA_ShowTitle, FALSE,
        SA_Quiet,     TRUE,
        SA_Type,      CUSTOMSCREEN,
        TAG_DONE);
    if (!screen) return 1;

    window = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)screen,
        WA_Left, 0, WA_Top, 0,
        WA_Width, R_SCREEN_W, WA_Height, R_SCREEN_H,
        WA_Borderless, TRUE, WA_Backdrop, TRUE,
        WA_Activate, TRUE,
        WA_IDCMP, IDCMP_RAWKEY,
        TAG_DONE);
    if (!window) { CloseScreen(screen); screen = NULL; return 2; }

    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) return 3;

    safe_port = CreateMsgPort();
    if (!safe_port) return 4;
    sbuf[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;
    sbuf[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;

    InitRastPort(&rp_buf[0]); rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]); rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    install_palette();
    /* Start with cur_buf=1 so frame 1 draws to sbuf[1] (allocated
     * off-screen), then flips to it — visible bitmap sbuf[0] is
     * untouched until frame 2 draws to it (now off-screen after the
     * flip). Prevents the visible left-to-right build on frame 1. */
    cur_buf = 1;
    first_flip = 1;
    return 0;
}

void Renderer::close_display()
{
    if (sbuf[0]) {
        int t = 0;
        while (!ChangeScreenBuffer(screen, sbuf[0]) && ++t < 5) WaitTOF();
        WaitTOF(); WaitTOF();
    }
    if (sbuf[1]) { FreeScreenBuffer(screen, sbuf[1]); sbuf[1] = NULL; }
    if (sbuf[0]) { FreeScreenBuffer(screen, sbuf[0]); sbuf[0] = NULL; }
    if (safe_port) { DeleteMsgPort(safe_port); safe_port = NULL; }
    if (window) { CloseWindow(window); window = NULL; }
    if (screen) { CloseScreen(screen); screen = NULL; }
}

struct MsgPort *Renderer::user_port() const
{
    return window ? window->UserPort : NULL;
}

/* ------------------------------------------------------------------ */
/* Frame render.                                                        */

void Renderer::draw_sky(struct RastPort *rp)
{
    /* Vertical gradient across the whole viewport. Above the horizon
     * we show the sky; below, we'll get overwritten by terrain. */
    for (int i = 0; i < PAL_SKY_COUNT; i++) {
        int y0 = R_VIEW_Y + (i * R_VIEW_H) / PAL_SKY_COUNT;
        int y1 = R_VIEW_Y + ((i + 1) * R_VIEW_H) / PAL_SKY_COUNT - 1;
        SetAPen(rp, (UBYTE)(PAL_SKY_BASE + i));
        RectFill(rp, R_VIEW_X, y0, R_VIEW_X2, y1);
    }
}

/*
 * Column-by-column heightfield renderer. For each visible column of the
 * viewport we cast a ray in the XZ plane, sample the terrain at each
 * step, project to screen Y, and fill the newly-exposed pixels above
 * whatever we've drawn already. Painter's algorithm, front-to-back.
 */
void Renderer::draw_terrain(struct RastPort *rp, const GameState &gs,
                            const Terrain &world)
{
    const ShipState &s = gs.ship;

    /* FOV: ~60° across R_VIEW_W. Pre-compute the per-column yaw offset. */
    const LONG fov_span = 512;   /* ~45° (out of 4096 = 360°) */

    LONG cam_yaw   = s.yaw;
    LONG cam_y     = s.y;
    LONG cam_x_int = FX16_TOINT(s.x);
    LONG cam_z_int = FX16_TOINT(s.z);

    /* Pitch shift: nose down = look further down = horizon moves up on
     * screen. 380 pitch units ≈ 33°, translates to ~+40 pixels. */
    LONG horizon_shift = -(s.pitch * 40) / SHIP_PITCH_MAX;
    LONG horizon_y = R_HORIZON_Y + horizon_shift;
    if (horizon_y < R_VIEW_Y) horizon_y = R_VIEW_Y;
    if (horizon_y > R_VIEW_Y2) horizon_y = R_VIEW_Y2;

    /* Projection scale (focal length in pixels). Chosen to give ~28°
     * vertical FOV over R_VIEW_H — tight enough to feel telephoto and
     * make distant mountains satisfyingly large. */
    const LONG PROJ = 400;
    const LONG MAX_DIST = 1600;

    /* Sky already fills above horizon; below-horizon starts empty and
     * gets painted by the terrain loop. Anything the ray-march didn't
     * reach we backstop with the horizon-fog terrain colour. */

    /* --- Fast path -----------------------------------------------------
     * Old code fired a RectFill per (col, strip) — ~8000 blitter setups
     * per frame at 320x160. Now we raycast, find the topmost visible
     * terrain pixel per column, and issue ONE blitter line per column
     * (Move+Draw uses the blitter's line mode). Depth cueing survives
     * as horizontal fog BANDS drawn once each — the eye reads distance
     * from vertical position on screen, not from per-sample tinting.
     * -----------------------------------------------------------------*/

    const int COL_STEP = 4;                     /* 4-wide chunky columns */
    const int NCOLS    = R_VIEW_W / COL_STEP;   /* 72 */
    static WORD col_ytop[R_VIEW_W / 4 + 4];     /* per-4px column y_top  */
    static UBYTE col_pen[R_VIEW_W / 4 + 4];     /* pen at that y_top     */

    for (int ci = 0; ci < NCOLS; ci++) {
        int col = ci * COL_STEP;
        LONG dcol = (LONG)col - (R_VIEW_W >> 1);
        LONG ray_yaw = (cam_yaw + (dcol * fov_span) / R_VIEW_W) & ANGLE_MASK;
        LONG rdx = isin(ray_yaw);
        LONG rdz = icos(ray_yaw);

        int  y_top    = R_VIEW_Y2 + 1;
        UBYTE best_pen = PAL_TERRAIN_BASE + 4 * 8 + 4;   /* mid green */

        LONG dist = 16;
        LONG step = 4;

        while (dist < MAX_DIST && y_top > R_VIEW_Y) {
            LONG wx = cam_x_int + ((rdx * dist) >> TRIG_SHIFT);
            LONG wz = cam_z_int + ((rdz * dist) >> TRIG_SHIFT);
            LONG h  = world.height_at_world(wx, wz);

            LONG dy = h - cam_y;
            LONG screen_y = horizon_y - (dy * PROJ) / dist;

            if (screen_y > R_VIEW_Y2) screen_y = R_VIEW_Y2;
            if (screen_y < R_VIEW_Y)  screen_y = R_VIEW_Y;

            if (screen_y < y_top) {
                int h_bin = (int)((h * 8) / (TERRAIN_MAX_HEIGHT + 1));
                if (h_bin < 0) h_bin = 0;
                else if (h_bin > 7) h_bin = 7;
                int d_bin = (int)(dist / (MAX_DIST / 8));
                if (d_bin > 7) d_bin = 7;
                best_pen = (UBYTE)(PAL_TERRAIN_BASE + h_bin * 8 + d_bin);
                y_top = (int)screen_y;
            }

            dist += step;
            if (dist >  120) step = 12;
            if (dist >  400) step = 32;
            if (dist > 1000) step = 64;
        }
        col_ytop[ci] = (WORD)y_top;
        col_pen[ci]  = best_pen;
    }

    /* Fog fill: single big blit for everything below horizon. Columns
     * that were visible will get overpainted next. */
    SetAPen(rp, (UBYTE)(PAL_TERRAIN_BASE + 7));
    if (horizon_y + 1 <= R_VIEW_Y2)
        RectFill(rp, R_VIEW_X, horizon_y + 1, R_VIEW_X2, R_VIEW_Y2);

    /* Emit one blitter-line per column-band. Coalesce adjacent columns
     * that share the same pen AND top-y into a single wider RectFill —
     * with 4px steps and 8 distance bins, ~50-70% of neighbours match. */
    int i = 0;
    while (i < NCOLS) {
        int j = i + 1;
        while (j < NCOLS
               && col_pen[j] == col_pen[i]
               && col_ytop[j] == col_ytop[i]) j++;
        int cx1 = R_VIEW_X + i * COL_STEP;
        int cx2 = R_VIEW_X + j * COL_STEP - 1;
        if (cx2 > R_VIEW_X2) cx2 = R_VIEW_X2;
        int y0 = col_ytop[i];
        if (y0 <= R_VIEW_Y2) {
            SetAPen(rp, col_pen[i]);
            RectFill(rp, cx1, y0, cx2, R_VIEW_Y2);
        }
        i = j;
    }
}

/*
 * Static cockpit frame. Two dark bands on either side of the flying
 * viewport plus a chunky dashboard beneath. Later phases hang gauges
 * and pilot indicators off this.
 */
void Renderer::draw_cockpit(struct RastPort *rp, const GameState &)
{
    /* Left/right pillars around the viewport. */
    SetAPen(rp, PAL_COCKPIT_BASE + 3);
    RectFill(rp, 0, 0, R_VIEW_X - 1, R_SCREEN_H - 1);
    RectFill(rp, R_VIEW_X2 + 1, 0, R_SCREEN_W - 1, R_SCREEN_H - 1);

    /* Top strip. */
    RectFill(rp, R_VIEW_X, 0, R_VIEW_X2, R_VIEW_Y - 1);

    /* Bottom dashboard. */
    SetAPen(rp, PAL_COCKPIT_BASE + 2);
    RectFill(rp, 0, R_VIEW_Y2 + 1, R_SCREEN_W - 1, R_SCREEN_H - 1);

    /* Inner highlight lines to frame the viewport. */
    SetAPen(rp, PAL_COCKPIT_BASE + 8);
    Move(rp, R_VIEW_X - 1, R_VIEW_Y - 1);
    Draw(rp, R_VIEW_X2 + 1, R_VIEW_Y - 1);
    Draw(rp, R_VIEW_X2 + 1, R_VIEW_Y2 + 1);
    Draw(rp, R_VIEW_X - 1, R_VIEW_Y2 + 1);
    Draw(rp, R_VIEW_X - 1, R_VIEW_Y - 1);
}

static void draw_bar(struct RastPort *rp, int x, int y, int w, int h,
                     int filled_w, UBYTE bg, UBYTE fg)
{
    SetAPen(rp, bg);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
    if (filled_w > 0) {
        SetAPen(rp, fg);
        RectFill(rp, x, y, x + filled_w - 1, y + h - 1);
    }
    /* Border */
    SetAPen(rp, PAL_HUD_BASE + 14);
    Move(rp, x - 1, y - 1);
    Draw(rp, x + w, y - 1);
    Draw(rp, x + w, y + h);
    Draw(rp, x - 1, y + h);
    Draw(rp, x - 1, y - 1);
}

/* Compass arrow to the nearest active pilot. Drawn as a small
 * triangle at (cx, cy) pointing along (nx, nz) — where nx/nz is the
 * unit vector from the ship to the pilot in world space, ROTATED into
 * the ship's local yaw frame so "up" on screen means "ahead of us". */
static void draw_arrow(struct RastPort *rp, int cx, int cy,
                       LONG local_dx, LONG local_dz, UBYTE pen)
{
    /* Normalise to a fixed radius so all arrows look the same size. */
    LONG len2 = local_dx * local_dx + local_dz * local_dz;
    if (len2 == 0) return;
    LONG len = 0, r = 0, bit = 1L << 30, v = len2;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    len = r ? r : 1;
    const LONG R = 12;
    LONG ux = (local_dx * R) / len;
    LONG uz = (local_dz * R) / len;
    LONG px = -uz;              /* perpendicular for the tail width */
    LONG pz = ux;
    SetAPen(rp, pen);
    /* Head at (cx+ux, cy-uz)  — screen +y = world -z (looking up on
     * the radar means "ahead"). */
    Move(rp, cx + (int)(px / 2), cy - (int)(pz / 2));
    Draw(rp, cx + (int)ux,       cy - (int)uz);
    Draw(rp, cx - (int)(px / 2), cy + (int)(pz / 2));
}

void Renderer::draw_hud(struct RastPort *rp, const GameState &gs,
                        const PilotList &pilots)
{
    /* Dashboard occupies y = R_VIEW_Y2+2 .. R_SCREEN_H-1 (~70 tall). */
    int dash_y = R_VIEW_Y2 + 6;

    /* Fuel bar */
    {
        int filled = (int)((gs.fuel * 60) / 1000);
        draw_bar(rp, 40, dash_y, 60, 8, filled,
                 PAL_HUD_BASE + 2, PAL_HUD_BASE + 12);
    }
    /* Shield bar */
    {
        int filled = (int)((gs.shield * 60) / 1000);
        draw_bar(rp, 40, dash_y + 16, 60, 8, filled,
                 PAL_HUD_BASE + 2, PAL_HUD_BASE + 10);
    }

    /* Text labels + numeric readouts */
    SetAPen(rp, PAL_HUD_BASE + 15);
    SetDrMd(rp, JAM1);
    Move(rp, 8, dash_y + 7);       Text(rp, (STRPTR)"FUEL", 4);
    Move(rp, 8, dash_y + 23);      Text(rp, (STRPTR)"SHLD", 4);

    /* Compass / heading readout */
    char buf[32];
    LONG deg = ((gs.ship.yaw * 360) / ANGLE_FULL);
    if (deg < 0) deg += 360;
    sprintf(buf, "HDG %03ld", (long)deg);
    Move(rp, 200, dash_y + 7);     Text(rp, (STRPTR)buf, 7);

    LONG alt_units = gs.ship.y;
    sprintf(buf, "ALT %04ld", (long)alt_units);
    Move(rp, 200, dash_y + 23);    Text(rp, (STRPTR)buf, 8);

    sprintf(buf, "SPD %02ld", (long)gs.ship.speed);
    Move(rp, 260, dash_y + 39);    Text(rp, (STRPTR)buf, 6);

    sprintf(buf, "SCORE %05ld", (long)gs.score);
    Move(rp, 8, dash_y + 39);      Text(rp, (STRPTR)buf, 11);

    sprintf(buf, "SAVED %ld/%ld",
            (long)gs.pilots_rescued, (long)MAX_PILOTS);
    Move(rp, 108, dash_y + 39);    Text(rp, (STRPTR)buf, 10);

    /* Keys hint bottom-right — jog the player's memory each frame. */
    SetAPen(rp, PAL_HUD_BASE + 9);
    Move(rp, 200, dash_y + 55); Text(rp, (STRPTR)"UP/DN THRUST", 12);
    Move(rp, 200, dash_y + 65); Text(rp, (STRPTR)"LR TURN  L LAND", 15);

    /* Radar dot in the middle of the dashboard, arrow to nearest pilot. */
    int rx = 155, ry = dash_y + 25;
    SetAPen(rp, PAL_HUD_BASE + 4);
    RectFill(rp, rx - 14, ry - 14, rx + 14, ry + 14);
    SetAPen(rp, PAL_HUD_BASE + 14);
    Move(rp, rx - 15, ry - 15); Draw(rp, rx + 15, ry - 15);
    Draw(rp, rx + 15, ry + 15); Draw(rp, rx - 15, ry + 15);
    Draw(rp, rx - 15, ry - 15);

    LONG pdist = 0;
    LONG pi = pilots.find_nearest(FX16_TOINT(gs.ship.x),
                                  FX16_TOINT(gs.ship.z),
                                  4000, &pdist);
    if (pi >= 0) {
        /* World delta */
        LONG wdx = pilots[pi].x - FX16_TOINT(gs.ship.x);
        LONG wdz = pilots[pi].z - FX16_TOINT(gs.ship.z);
        /* Rotate into ship frame — yaw 0 = +Z ahead, so local_z
         * component wants to be the sin/cos rotation of the world vec. */
        LONG cy = icos(gs.ship.yaw);
        LONG sy = isin(gs.ship.yaw);
        LONG local_x = (wdx * cy - wdz * sy) >> TRIG_SHIFT;
        LONG local_z = (wdx * sy + wdz * cy) >> TRIG_SHIFT;
        draw_arrow(rp, rx, ry, local_x, local_z, PAL_HUD_BASE + 12);
    }
}

/* Project a world point (wx, wy, wz) into the flying viewport using
 * the same math the terrain raycaster uses. Returns 0 if the point is
 * behind the camera or off-screen. If in-view, fills sx/sy and sz
 * (depth for size scaling). */
static int project(LONG wx, LONG wy, LONG wz,
                   LONG cam_x, LONG cam_y, LONG cam_z, LONG cam_yaw,
                   LONG horizon_y, LONG proj,
                   int *sx, int *sy, LONG *sz_out)
{
    LONG dx = wx - cam_x;
    LONG dz = wz - cam_z;
    LONG sy_yaw = isin(cam_yaw);
    LONG cy_yaw = icos(cam_yaw);
    LONG local_z = ((dx * sy_yaw + dz * cy_yaw) >> TRIG_SHIFT);
    if (local_z < 16) return 0;   /* behind or too close */
    LONG local_x = ((dx * cy_yaw - dz * sy_yaw) >> TRIG_SHIFT);
    LONG dy = wy - cam_y;
    LONG screen_x = R_VIEW_X + (R_VIEW_W >> 1) + (local_x * proj) / local_z;
    LONG screen_y = horizon_y - (dy * proj) / local_z;
    /* Only cull sprites that are wildly off-screen; anything within
     * ~150px overhang still gets a partially-clipped draw so mid-
     * altitude enemies close to the horizon read as approaching. */
    if (screen_x < R_VIEW_X - 150 || screen_x > R_VIEW_X2 + 150) return 0;
    if (screen_y < R_VIEW_Y - 150 || screen_y > R_VIEW_Y2 + 150) return 0;
    *sx = (int)screen_x;
    *sy = (int)screen_y;
    *sz_out = local_z;
    return 1;
}

static void draw_saucer(struct RastPort *rp, int sx, int sy, int size,
                        UBYTE dying)
{
    /* Saucer body is an oval-ish 3-band shape drawn with RectFills.
     * Size scales with distance; kept clipped so nothing writes off
     * the viewport bounds. */
    if (size < 3) size = 3;
    if (size > 60) size = 60;
    int hw = size;             /* half-width */
    int hh = size >> 1;        /* half-height */
    UBYTE body = dying ? PAL_MISC_BASE + 4 : PAL_MISC_BASE + 3;   /* yellow / red */
    UBYTE dark = dying ? PAL_MISC_BASE + 3 : PAL_MISC_BASE + 2;
    UBYTE glass = dying ? PAL_MISC_BASE + 7 : PAL_MISC_BASE + 5;  /* teeth-white / laser-green */

    int x0 = sx - hw, x1 = sx + hw;
    int y0 = sy - hh, y1 = sy + hh;
    if (x0 < R_VIEW_X) x0 = R_VIEW_X;
    if (x1 > R_VIEW_X2) x1 = R_VIEW_X2;
    if (y0 < R_VIEW_Y) y0 = R_VIEW_Y;
    if (y1 > R_VIEW_Y2) y1 = R_VIEW_Y2;
    if (x0 >= x1 || y0 >= y1) return;

    /* Wide middle band. */
    SetAPen(rp, body);
    RectFill(rp, x0, sy - 1, x1, sy + 1);
    /* Thinner top/bottom rims. */
    int rim = hw - (hw >> 2);
    SetAPen(rp, dark);
    int rx0 = sx - rim, rx1 = sx + rim;
    if (rx0 < R_VIEW_X) rx0 = R_VIEW_X;
    if (rx1 > R_VIEW_X2) rx1 = R_VIEW_X2;
    if (rx0 < rx1) {
        RectFill(rp, rx0, sy - 3, rx1, sy - 2);
        RectFill(rp, rx0, sy + 2, rx1, sy + 3);
    }
    /* Cockpit blister on top. */
    int dome = hw >> 2;
    if (dome < 2) dome = 2;
    int dx0 = sx - dome, dx1 = sx + dome;
    if (dx0 < R_VIEW_X) dx0 = R_VIEW_X;
    if (dx1 > R_VIEW_X2) dx1 = R_VIEW_X2;
    if (dx0 < dx1) {
        SetAPen(rp, glass);
        RectFill(rp, dx0, sy - 5, dx1, sy - 4);
    }
}

static void draw_bullet_dot(struct RastPort *rp, int sx, int sy,
                            UBYTE owner)
{
    if (sx < R_VIEW_X + 1 || sx > R_VIEW_X2 - 1) return;
    if (sy < R_VIEW_Y + 1 || sy > R_VIEW_Y2 - 1) return;
    UBYTE pen = (owner == BO_PLAYER) ? PAL_MISC_BASE + 5   /* laser green */
                                     : PAL_MISC_BASE + 3;  /* enemy red */
    SetAPen(rp, pen);
    RectFill(rp, sx - 1, sy - 1, sx + 1, sy + 1);
}

void Renderer::draw_sprites(struct RastPort *rp, const GameState &gs,
                            const Combat &combat)
{
    const LONG PROJ = 400;
    LONG cam_x = FX16_TOINT(gs.ship.x);
    LONG cam_z = FX16_TOINT(gs.ship.z);
    LONG cam_y = gs.ship.y;
    LONG horizon_y = R_HORIZON_Y
                   + (-(gs.ship.pitch * 40) / SHIP_PITCH_MAX);
    if (horizon_y < R_VIEW_Y) horizon_y = R_VIEW_Y;
    if (horizon_y > R_VIEW_Y2) horizon_y = R_VIEW_Y2;

    /* Saucers. Size scales inverse to distance. */
    for (LONG i = 0; i < combat.saucer_count(); i++) {
        const Saucer &s = combat.saucer(i);
        if (s.state == SS_INACTIVE) continue;
        int sx, sy; LONG sz;
        if (!project(s.x, s.y, s.z, cam_x, cam_y, cam_z, gs.ship.yaw,
                     horizon_y, PROJ, &sx, &sy, &sz)) continue;
        /* Saucer projected size — grows dramatically as it closes.
         * Clamped so a very close one doesn't consume the viewport. */
        int size = (int)(8000 / (sz + 40));
        if (size < 6) size = 6;
        UBYTE dying = (s.state == SS_DYING) ? 1 : 0;
        draw_saucer(rp, sx, sy, size, dying);
    }

    /* Bullets — draw last so they sit on top of everything. */
    for (LONG i = 0; i < combat.bullet_count(); i++) {
        const Bullet &b = combat.bullet(i);
        if (b.owner == BO_INACTIVE) continue;
        int sx, sy; LONG sz;
        if (!project(b.x, b.y, b.z, cam_x, cam_y, cam_z, gs.ship.yaw,
                     horizon_y, PROJ, &sx, &sy, &sz)) continue;
        draw_bullet_dot(rp, sx, sy, b.owner);
    }
}

/* Overlays drawn on top of the terrain during the rescue sequence. */
void Renderer::draw_overlay(struct RastPort *rp, const GameState &gs)
{
    if (gs.rescue_state == RS_FLYING) return;

    /* Semi-opaque banner across the middle of the viewport. */
    int by = R_HORIZON_Y - 12;
    int by2 = R_HORIZON_Y + 20;
    SetAPen(rp, PAL_COCKPIT_BASE + 1);
    RectFill(rp, R_VIEW_X + 20, by, R_VIEW_X2 - 20, by2);
    SetAPen(rp, PAL_HUD_BASE + 14);
    Move(rp, R_VIEW_X + 20, by); Draw(rp, R_VIEW_X2 - 20, by);
    Draw(rp, R_VIEW_X2 - 20, by2); Draw(rp, R_VIEW_X + 20, by2);
    Draw(rp, R_VIEW_X + 20, by);

    SetAPen(rp, PAL_HUD_BASE + 15);
    SetDrMd(rp, JAM1);
    int tx = R_VIEW_X + 40;
    int ty = by + 20;
    switch (gs.rescue_state) {
    case RS_LANDING:
        Move(rp, tx, ty); Text(rp, (STRPTR)"LANDING...", 10);
        break;
    case RS_AIRLOCK:
        Move(rp, tx, ty); Text(rp, (STRPTR)"AIRLOCK CYCLING", 15);
        break;
    case RS_REVEAL:
        SetAPen(rp, PAL_HUD_BASE + 12);
        Move(rp, tx, ty); Text(rp, (STRPTR)"PILOT ABOARD!", 13);
        break;
    case RS_TAKEOFF:
        Move(rp, tx, ty); Text(rp, (STRPTR)"LIFTING OFF", 11);
        break;
    default: break;
    }

    /* The jump-scare: full-viewport red flash + Jaggi face.
     * Alternate between two shades on odd/even frames for a strobe. */
    if (gs.rescue_state == RS_JUMPSCARE) {
        UBYTE flash = (gs.state_timer & 1) ? PAL_MISC_BASE + 4
                                           : PAL_MISC_BASE + 2;
        SetAPen(rp, flash);
        RectFill(rp, R_VIEW_X, R_VIEW_Y, R_VIEW_X2, R_VIEW_Y2);
        /* Crude Jaggi face — big green head, black eyes, sharp teeth. */
        int fx = R_VIEW_X + R_VIEW_W / 2;
        int fy = R_VIEW_Y + R_VIEW_H / 2;
        SetAPen(rp, PAL_TERRAIN_BASE + 4 * 8 + 0);   /* bright green */
        RectFill(rp, fx - 60, fy - 40, fx + 60, fy + 40);
        SetAPen(rp, 0);
        RectFill(rp, fx - 40, fy - 20, fx - 15, fy - 5);   /* eye L */
        RectFill(rp, fx + 15, fy - 20, fx + 40, fy - 5);   /* eye R */
        SetAPen(rp, PAL_MISC_BASE + 7);              /* teeth pen */
        for (int t = 0; t < 6; t++) {
            int tx0 = fx - 40 + t * 16;
            RectFill(rp, tx0, fy + 10, tx0 + 6, fy + 30);
        }
        SetAPen(rp, PAL_HUD_BASE + 15);
        Move(rp, fx - 40, fy + 55); Text(rp, (STRPTR)"JAGGI!!", 7);
    }
}

void Renderer::flip()
{
    /* Wait for the blitter to settle before swapping so the display
     * doesn't chase a half-written bitplane. Render time is currently
     * far longer than a vblank so the previously-displayed buffer is
     * guaranteed safe to reuse — no safe-message dance needed. */
    WaitBlit();
    if (ChangeScreenBuffer(screen, sbuf[cur_buf])) {
        WaitTOF();
        cur_buf ^= 1;      /* next frame draws to what is now off-screen */
        first_flip = 0;
    } else {
        WaitTOF();         /* rejected — try again next frame */
    }
}

void Renderer::render(const GameState &gs, const Terrain &world,
                      const PilotList &pilots, const Combat &combat)
{
    struct RastPort *rp = &rp_buf[cur_buf];
    rp->BitMap = sbuf[cur_buf]->sb_BitMap;

    draw_sky(rp);
    if (gs.rescue_state == RS_FLYING) {
        draw_terrain(rp, gs, world);
        draw_sprites(rp, gs, combat);
    } else {
        SetAPen(rp, (UBYTE)(PAL_TERRAIN_BASE + 7));
        if (R_HORIZON_Y + 1 <= R_VIEW_Y2)
            RectFill(rp, R_VIEW_X, R_HORIZON_Y + 1,
                         R_VIEW_X2, R_VIEW_Y2);
    }
    draw_overlay(rp, gs);
    draw_cockpit(rp, gs);
    draw_hud(rp, gs, pilots);

    flip();
}
