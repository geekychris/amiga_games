#include "render.h"
#include "terrain.h"
#include "game.h"
#include "pilots.h"

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
 * We generate the sine table without libm: sin(x) ≈ 4x(pi-x) / (5pi²-4x(pi-x))
 * (Bhaskara I approximation, accurate to ~0.16%). Then mirror for the
 * second half and negate.
 */
void render_init_math()
{
    for (LONG i = 0; i < ANGLE_FULL; i++) {
        LONG a = i;                       /* 0..ANGLE_FULL-1 */
        LONG sign = 1;
        if (a >= ANGLE_HALF) { a -= ANGLE_HALF; sign = -1; }
        /* Approximation input: t = a * pi / ANGLE_HALF. Do it in fixed
         * point without ever computing pi explicitly. */
        LONG x = a;                             /* 0..ANGLE_HALF */
        LONG y = ANGLE_HALF - a;
        LONG num = 4L * x * y;                  /* fits: 2048*2048*4 = 16M */
        LONG den = (5L * ANGLE_HALF * ANGLE_HALF - num) / TRIG_ONE;
        if (den == 0) den = 1;
        LONG s = num / den;                     /* TRIG_ONE-scaled */
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
    cur_buf = 0;
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

    /* Render every other column and duplicate — halves the raycaster
     * cost. Fractal terrain doesn't need sub-pixel horizontal precision
     * to feel right; two-pixel-wide fills are indistinguishable from
     * per-pixel at LORES. */
    const int COL_STEP = 2;

    for (int col = 0; col < R_VIEW_W; col += COL_STEP) {
        LONG dcol = (LONG)col - (R_VIEW_W >> 1);
        LONG ray_yaw = (cam_yaw + (dcol * fov_span) / R_VIEW_W) & ANGLE_MASK;
        LONG rdx = isin(ray_yaw);
        LONG rdz = icos(ray_yaw);

        /* Painter's algo, near-to-far. y_top tracks the top of what
         * we've filled; only strips ABOVE it need painting. */
        int y_top = R_VIEW_Y2 + 1;

        LONG dist = 8;
        LONG step = 2;

        int cx = R_VIEW_X + col;
        int cx2 = cx + COL_STEP - 1;
        if (cx2 > R_VIEW_X2) cx2 = R_VIEW_X2;

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
                UBYTE pen = (UBYTE)(PAL_TERRAIN_BASE + h_bin * 8 + d_bin);
                SetAPen(rp, pen);
                RectFill(rp, cx, screen_y, cx2, y_top - 1);
                y_top = screen_y;
            }

            dist += step;
            if (dist >   64) step = 3;
            if (dist >  200) step = 6;
            if (dist >  500) step = 12;
            if (dist > 1000) step = 24;
        }

        if (y_top >= R_VIEW_Y2) {
            SetAPen(rp, (UBYTE)(PAL_TERRAIN_BASE + 7));
            RectFill(rp, cx, horizon_y + 1, cx2, R_VIEW_Y2);
        }
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
    WaitBlit();
    if (ChangeScreenBuffer(screen, sbuf[cur_buf])) {
        if (!first_flip) {
            /* Consume the safe message so we know the last buffer
             * finished displaying before we scribble on it again. */
            struct Message *m;
            while (!(m = GetMsg(safe_port))) WaitPort(safe_port);
        }
        first_flip = 0;
        WaitTOF();
        cur_buf ^= 1;
    } else {
        WaitTOF();
    }
}

void Renderer::render(const GameState &gs, const Terrain &world,
                      const PilotList &pilots)
{
    struct RastPort *rp = &rp_buf[cur_buf];
    rp->BitMap = sbuf[cur_buf]->sb_BitMap;

    draw_sky(rp);
    /* During rescue overlays the terrain is fully covered by the
     * dialogue banner / jumpscare — skipping the raycaster there
     * gives us multiple frames-per-second even when the flying
     * loop drops below one. */
    if (gs.rescue_state == RS_FLYING) {
        draw_terrain(rp, gs, world);
    } else {
        /* Backfill below-horizon with the fog colour so the overlay
         * sits on a coherent scene instead of the raw sky ramp. */
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
