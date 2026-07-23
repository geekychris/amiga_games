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

/* ---- public entry point ------------------------------------------- */
void pitch_render(struct RastPort *rp, int pane_y0,
                  LONG cam_x, LONG cam_z, LONG cam_angle,
                  const Ball *ball)
{
    LONG ca = math_cos(cam_angle);
    LONG sa = math_sin(cam_angle);

    /* Sky + ground fill. Sky above horizon, single-hue ground below. */
    UBYTE sky_pen = (pane_y0 == PANE_P1_Y0) ? PEN_SKY_BOT : PEN_SKY_TOP;
    band(rp, pane_y0, pane_y0 + HORIZON_Y - 1, sky_pen);
    band(rp, pane_y0 + HORIZON_Y, pane_y0 + PANE_H - 1, PEN_GRID_A);

    /* Horizon divider — one-pixel high stripe of PEN_BLACK. */
    SetAPen(rp, PEN_BLACK);
    Move(rp, 0, pane_y0 + HORIZON_Y);
    Draw(rp, SCR_W - 1, pane_y0 + HORIZON_Y);

    /* Grid lines. Draw all lines within the pitch box; projection
     * naturally handles distance falloff. */
    SetAPen(rp, PEN_GRID_LINE);

    /* Lines of constant X (goal-to-goal direction), running along Z. */
    LONG x;
    for (x = -PITCH_LENGTH; x <= PITCH_LENGTH; x += GRID_STEP) {
        world_line(rp, x, -PITCH_WIDTH, x, PITCH_WIDTH,
                   cam_x, cam_z, ca, sa, pane_y0);
    }
    /* Lines of constant Z (sideline direction), running along X. */
    LONG z;
    for (z = -PITCH_WIDTH; z <= PITCH_WIDTH; z += GRID_STEP) {
        world_line(rp, -PITCH_LENGTH, z, PITCH_LENGTH, z,
                   cam_x, cam_z, ca, sa, pane_y0);
    }

    /* Goal beams: vertical stripes at the two pitch ends, drawn as
     * pairs of world-space "posts" projected + line-drawn. */
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

    /* Ball: project centre, draw a small filled disc whose radius
     * scales with 1/depth so it perspective-shrinks naturally. */
    if (ball) {
        LONG dx = ball->x - cam_x;
        LONG dz = ball->z - cam_z;
        LONG zc = ((dx >> 8) * (ca >> 8) + (dz >> 8) * (sa >> 8));
        if (zc > NEAR_CLIP) {
            WORD bsx, bsy;
            if (project(ball->x, 0, ball->z, cam_x, cam_z, ca, sa, pane_y0, &bsx, &bsy)) {
                LONG r_px = (BALL_RADIUS * FOCAL) / (zc >> FP);
                int  r    = (int)(r_px >> FP);
                if (r < 2)  r = 2;
                if (r > 20) r = 20;
                SetAPen(rp, PEN_BALL);
                int dy;
                for (dy = -r; dy <= r; dy++) {
                    int hw = (int)( ((long)r * r - (long)dy * dy) );
                    if (hw <= 0) continue;
                    /* integer sqrt via small loop — r is small */
                    int w = 0; while ((w+1)*(w+1) <= hw) w++;
                    int y = bsy + dy;
                    if (y < pane_y0 || y >= pane_y0 + PANE_H) continue;
                    int x0 = bsx - w, x1 = bsx + w;
                    if (x0 < 0) x0 = 0;
                    if (x1 > SCR_W - 1) x1 = SCR_W - 1;
                    Move(rp, x0, y); Draw(rp, x1, y);
                }
            }
        }
    }
}
