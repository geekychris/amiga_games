/*
 * Split-screen pitch renderer.
 *
 * Each call renders the full pane (sky + horizon + checkered ground
 * grid + goal beams + ball) from ONE camera viewpoint. Coordinates
 * are all 16.16 fixed-point, but screen math falls back to integer
 * pixels once we divide by depth.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <proto/graphics.h>

#include "ballblazer.h"

/* ---- integer sin / cos --------------------------------------------- */
static LONG sintab[360];

static LONG isin_deg(LONG d)
{
    LONG sign = 1, t;
    d %= 360; if (d < 0) d += 360;
    if (d >= 180) { d -= 180; sign = -1; }
    t = d * (180 - d);
    return sign * ((4 * t * ONE) / (40500 - t));
}

void math_init(void)
{
    LONG i;
    for (i = 0; i < 360; i++) sintab[i] = isin_deg(i);
}

LONG math_sin(LONG deg) { deg %= 360; if (deg < 0) deg += 360; return sintab[deg]; }
LONG math_cos(LONG deg) { return math_sin(deg + 90); }

/* ---- projection ---------------------------------------------------- */
#define FOCAL       180L        /* pixels, ~85° horizontal FOV */
#define NEAR_CLIP   (2L * ONE)  /* world units */

/* Project a world point (wx, wy=optional, wz) into pane coords.
 * Returns 0 if behind near plane; else fills *sx, *sy. */
static int project(LONG wx, LONG wy_offset,   /* wy_offset = wy - HOVER_H */
                   LONG wz,
                   LONG cx, LONG cz,
                   LONG cos_a, LONG sin_a,
                   int pane_y0,
                   WORD *sx, WORD *sy)
{
    LONG dx = wx - cx;
    LONG dz = wz - cz;
    /* z_cam (forward) = dx*cos + dz*sin  — dot with camera forward */
    LONG zc = ((dx >> 8) * (cos_a >> 8) + (dz >> 8) * (sin_a >> 8));
    if (zc < NEAR_CLIP) return 0;
    /* x_cam (right)   = dx*sin - dz*cos */
    LONG xc = ((dx >> 8) * (sin_a >> 8) - (dz >> 8) * (cos_a >> 8));

    LONG pane_cx = SCR_W / 2;
    /* Screen X: pane_cx + F * xc / zc — shift xc back into full FP for /zc */
    LONG rx = (xc * FOCAL) / (zc >> FP);   /* xc still FP; result in pixels */
    LONG ry = (wy_offset * FOCAL) / (zc >> FP);
    LONG sxp = pane_cx + (rx >> FP);
    LONG syp = pane_y0 + HORIZON_Y - (ry >> FP);

    /* Safety clamp to the pane rect; caller can further clip if needed. */
    if (sxp < 0)              sxp = 0;
    if (sxp > SCR_W - 1)      sxp = SCR_W - 1;
    if (syp < pane_y0)              syp = pane_y0;
    if (syp > pane_y0 + PANE_H - 1) syp = pane_y0 + PANE_H - 1;

    *sx = (WORD)sxp; *sy = (WORD)syp;
    return 1;
}

/* Line-draw a world-space segment. If either endpoint is behind the
 * near plane, walk toward the visible end in small steps until it
 * becomes projectable. Crude but avoids per-segment clip algebra. */
static void world_line(struct RastPort *rp,
                       LONG x0, LONG z0, LONG x1, LONG z1,
                       LONG cx, LONG cz, LONG ca, LONG sa,
                       int pane_y0)
{
    WORD sx0, sy0, sx1, sy1;
    int a_ok = project(x0, -HOVER_H, z0, cx, cz, ca, sa, pane_y0, &sx0, &sy0);
    int b_ok = project(x1, -HOVER_H, z1, cx, cz, ca, sa, pane_y0, &sx1, &sy1);
    if (!a_ok && !b_ok) return;

    /* If one end is offscreen-behind, pull it toward the visible end
     * along the segment until project() succeeds. */
    if (!a_ok || !b_ok) {
        LONG *fx = a_ok ? &x1 : &x0;
        LONG *fz = a_ok ? &z1 : &z0;
        LONG  gx = a_ok ? x0  : x1;
        LONG  gz = a_ok ? z0  : z1;
        int tries = 8;
        while (tries--) {
            *fx = (*fx + gx) / 2;
            *fz = (*fz + gz) / 2;
            WORD tx, ty;
            if (project(*fx, -HOVER_H, *fz, cx, cz, ca, sa, pane_y0, &tx, &ty)) {
                if (!a_ok) { sx0 = tx; sy0 = ty; }
                else       { sx1 = tx; sy1 = ty; }
                break;
            }
        }
    }
    Move(rp, sx0, sy0);
    Draw(rp, sx1, sy1);
}

/* Fill an axis-aligned rectangle band with a solid pen. */
static void band(struct RastPort *rp, int y0, int y1, UBYTE pen)
{
    SetAPen(rp, pen);
    RectFill(rp, 0, y0, SCR_W - 1, y1);
}

/* ---- chequered floor ---------------------------------------------- */
/*
 * Per-row raycast: for each screen row below horizon, the perspective
 * pin-hole model gives a single world-Z depth (for a camera facing
 * +X or -X, with head-height HOVER_H above ground). Along that row,
 * screen columns map linearly to world-Z positions, so cells project
 * as equally-spaced coloured runs. Cell parity = (cellx + cellz) & 1.
 *
 * Restriction: because we only care about the two ballblazer camera
 * angles (0 and 180), we can hard-code the axis assumption instead
 * of doing full 2D rotation per pixel. Costs one branch per pane
 * for the sign of forward.
 */
static void draw_checker_floor(struct RastPort *rp, int pane_y0,
                               LONG cam_x, LONG cam_z, LONG cam_angle)
{
    int fwd_sign = (cam_angle == 0) ? +1 : -1;
    int y;
    LONG grid_i = GRID_STEP >> FP;   /* 5 world units, plain int */
    LONG hover_i = HOVER_H  >> FP;   /* 2 */
    LONG cam_x_i = cam_x    >> FP;
    LONG cam_z_i = cam_z    >> FP;

    for (y = pane_y0 + HORIZON_Y + 1; y < pane_y0 + PANE_H; y++) {
        int dy = y - (pane_y0 + HORIZON_Y);
        /* zdist (world units) = HOVER_H * F / dy   — perspective */
        LONG zdist = (hover_i * FOCAL) / dy;
        if (zdist < 1) zdist = 1;
        /* world_x under camera at this row */
        LONG worldx = cam_x_i + fwd_sign * zdist;
        LONG cellx  = worldx / grid_i;
        int row_parity = ((cellx % 2) + 2) % 2;

        /* Cell width in pixels along this row */
        LONG cell_w = (grid_i * FOCAL) / zdist;
        if (cell_w < 1) cell_w = 1;

        /* worldz at the pane centre (pane_cx column) */
        LONG worldz_centre = cam_z_i;  /* independent of fwd_sign at centre */
        /* pixel where the nearest cell boundary to the LEFT of pane_cx sits */
        LONG cellz_centre = worldz_centre / grid_i;
        LONG boundary_z   = cellz_centre * grid_i;
        LONG offset_wz    = worldz_centre - boundary_z;
        LONG offset_px    = (offset_wz * FOCAL) / zdist;
        /* leftmost boundary is offset_px pixels LEFT of pane_cx */
        LONG start_px = (SCR_W / 2) - offset_px;
        /* Walk left/right filling alternating bands. */
        int band_start = (int)start_px;
        LONG cellz     = cellz_centre;   /* cell to the RIGHT of boundary */
        /* First: extend leftward from start_px */
        int px = band_start;
        int wall = 0;
        while (px > 0 && wall++ < 40) {
            int px2 = px - (int)cell_w;
            if (px2 < 0) px2 = 0;
            int parity = ((((cellz - 1) % 2) + 2) % 2 + row_parity) & 1;
            SetAPen(rp, parity ? PEN_GRID_B : PEN_GRID_A);
            Move(rp, px2, y); Draw(rp, px - 1, y);
            px = px2;
            cellz--;
        }
        /* Rightward */
        px    = band_start;
        cellz = cellz_centre;
        wall  = 0;
        while (px < SCR_W && wall++ < 40) {
            int px2 = px + (int)cell_w;
            if (px2 > SCR_W - 1) px2 = SCR_W - 1;
            int parity = ((((cellz) % 2) + 2) % 2 + row_parity) & 1;
            SetAPen(rp, parity ? PEN_GRID_B : PEN_GRID_A);
            Move(rp, px, y); Draw(rp, px2, y);
            px = px2 + 1;
            cellz++;
        }
    }
}

/* ---- shaded ball --------------------------------------------------- */
static void draw_ball_sphere(struct RastPort *rp, int pane_y0,
                             int cx, int cy, int r)
{
    if (r < 1) r = 1;
    /* Three-tone shading: dark outer, mid, bright inner-offset. */
    int hi_dx = -r / 3, hi_dy = -r / 3;   /* highlight upper-left */
    int r2   = r * r;
    int mr   = (r * 5) / 6;                /* mid radius */
    int mr2  = mr * mr;
    int hr   = r / 3;                      /* highlight radius */
    int hr2  = hr * hr;
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < pane_y0 || y >= pane_y0 + PANE_H) continue;
        int hw_out = r2 - dy * dy; if (hw_out <= 0) continue;
        int hw_mid = mr2 - dy * dy;
        int w_out = 0; while ((w_out+1)*(w_out+1) <= hw_out) w_out++;
        int w_mid = 0; if (hw_mid > 0) while ((w_mid+1)*(w_mid+1) <= hw_mid) w_mid++;
        /* Outer ring: PEN_BALL_DARK */
        SetAPen(rp, PEN_BALL_DARK);
        int xL = cx - w_out, xR = cx + w_out;
        if (xL < 0)         xL = 0;
        if (xR > SCR_W - 1) xR = SCR_W - 1;
        Move(rp, xL, y); Draw(rp, xR, y);
        /* Mid: PEN_BALL */
        if (w_mid > 0) {
            SetAPen(rp, PEN_BALL);
            int mL = cx - w_mid, mR = cx + w_mid;
            if (mL < 0) mL = 0; if (mR > SCR_W - 1) mR = SCR_W - 1;
            Move(rp, mL, y); Draw(rp, mR, y);
        }
        /* Highlight: PEN_BALL_HI, offset up-left */
        int hdy = dy - hi_dy;
        int hw_hi = hr2 - hdy * hdy;
        if (hw_hi > 0) {
            int w_hi = 0; while ((w_hi+1)*(w_hi+1) <= hw_hi) w_hi++;
            SetAPen(rp, PEN_BALL_HI);
            int hL = cx + hi_dx - w_hi, hR = cx + hi_dx + w_hi;
            if (hL < 0) hL = 0; if (hR > SCR_W - 1) hR = SCR_W - 1;
            Move(rp, hL, y); Draw(rp, hR, y);
        }
    }
}

/* ---- public entry point ------------------------------------------- */
void pitch_render(struct RastPort *rp, int pane_y0,
                  LONG cam_x, LONG cam_z, LONG cam_angle,
                  const Ball *ball)
{
    LONG ca = math_cos(cam_angle);
    LONG sa = math_sin(cam_angle);

    /* Sky above horizon. */
    UBYTE sky_pen = (pane_y0 == PANE_P1_Y0) ? PEN_SKY_BOT : PEN_SKY_TOP;
    band(rp, pane_y0, pane_y0 + HORIZON_Y - 1, sky_pen);

    /* Chequered ground. */
    draw_checker_floor(rp, pane_y0, cam_x, cam_z, cam_angle);

    /* Horizon divider — one-pixel high black. */
    SetAPen(rp, PEN_BLACK);
    Move(rp, 0, pane_y0 + HORIZON_Y);
    Draw(rp, SCR_W - 1, pane_y0 + HORIZON_Y);

    /* Goal beams: two posts at each end of the pitch. */
    LONG z;
    for (z = -PITCH_WIDTH; z <= PITCH_WIDTH; z += PITCH_WIDTH) {
        WORD sx, sy_bot, sy_top;
        SetAPen(rp, PEN_GOAL_P2);
        if (project(PITCH_LENGTH, -HOVER_H, z, cam_x, cam_z, ca, sa, pane_y0, &sx, &sy_bot)) {
            WORD sxt; int ok_top = project(PITCH_LENGTH, HOVER_H * 4, z,
                                           cam_x, cam_z, ca, sa, pane_y0, &sxt, &sy_top);
            if (ok_top) { Move(rp, sx, sy_bot); Draw(rp, sxt, sy_top); }
        }
        SetAPen(rp, PEN_GOAL_P1);
        if (project(-PITCH_LENGTH, -HOVER_H, z, cam_x, cam_z, ca, sa, pane_y0, &sx, &sy_bot)) {
            WORD sxt; int ok_top = project(-PITCH_LENGTH, HOVER_H * 4, z,
                                           cam_x, cam_z, ca, sa, pane_y0, &sxt, &sy_top);
            if (ok_top) { Move(rp, sx, sy_bot); Draw(rp, sxt, sy_top); }
        }
    }

    /* Ball as a shaded sphere. */
    if (ball) {
        LONG dx = ball->x - cam_x;
        LONG dz = ball->z - cam_z;
        LONG zc = ((dx >> 8) * (ca >> 8) + (dz >> 8) * (sa >> 8));
        if (zc > NEAR_CLIP) {
            WORD bsx, bsy;
            if (project(ball->x, 0, ball->z, cam_x, cam_z, ca, sa, pane_y0, &bsx, &bsy)) {
                LONG r_px = (BALL_RADIUS * FOCAL) / (zc >> FP);
                int  r    = (int)(r_px >> FP);
                if (r < 3)  r = 3;
                if (r > 30) r = 30;
                draw_ball_sphere(rp, pane_y0, bsx, bsy, r);
            }
        }
    }
}
