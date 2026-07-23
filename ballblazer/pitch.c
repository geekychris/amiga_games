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
 * Direct-to-bitplane per-row checker fill. The prior implementation
 * used graphics.library Move+Draw per cell band (10-30 calls per
 * scanline × 85 rows × 2 panes = ~4000 calls/frame) which pays the
 * gfx.library setup cost on every call. That single change dropped
 * the frame rate to single digits.
 *
 * Here we compute one 40-byte mask per row (bit = 1 means checker
 * cell A) and blast it into 5 bitplanes directly. Pen A=3=0b00011
 * so planes 0/1 = mask, plane 2 = ~mask; pen B=4=0b00100 fills the
 * inverse cells. Planes 3/4 are already zeroed for these two pens.
 *
 * Cost per frame: ~85 rows × 5 plane byte-writes × 40 bytes × 2
 * panes = ~34 KB memory writes. Trivial on 68020.
 *
 * Camera restriction: only the two Ballblazer angles (0 / 180) so
 * we can hard-code the fwd_sign instead of running a full 2D rotate
 * per pixel.
 */
static void draw_checker_floor(struct RastPort *rp, int pane_y0,
                               LONG cam_x, LONG cam_z, LONG cam_angle)
{
    struct BitMap *bm = rp->BitMap;
    if (!bm) return;
    LONG bpr = bm->BytesPerRow;
    UBYTE *plane0 = (UBYTE *)bm->Planes[0];
    UBYTE *plane1 = (UBYTE *)bm->Planes[1];
    UBYTE *plane2 = (UBYTE *)bm->Planes[2];
    UBYTE *plane3 = (SCR_DEPTH > 3) ? (UBYTE *)bm->Planes[3] : NULL;
    UBYTE *plane4 = (SCR_DEPTH > 4) ? (UBYTE *)bm->Planes[4] : NULL;

    int fwd_sign = (cam_angle == 0) ? +1 : -1;
    LONG grid_i  = GRID_STEP >> FP;   /* 5 */
    LONG hover_i = HOVER_H  >> FP;    /* 2 */
    LONG cam_x_i = cam_x    >> FP;
    LONG cam_z_i = cam_z    >> FP;

    /* Row-scratch mask: 40 bytes = 320 pixels. */
    UBYTE mask[40];

    int y;
    for (y = pane_y0 + HORIZON_Y + 1; y < pane_y0 + PANE_H; y++) {
        int dy = y - (pane_y0 + HORIZON_Y);
        LONG zdist = (hover_i * FOCAL) / dy;
        if (zdist < 1) zdist = 1;
        LONG worldx = cam_x_i + fwd_sign * zdist;
        LONG cellx  = (worldx >= 0) ? (worldx / grid_i)
                                    : -((-worldx + grid_i - 1) / grid_i);
        int row_parity = ((int)(cellx & 1));

        /* Build mask: bit set = pixel is in an A cell.
         * worldz(px) = cam_z_i - fwd_sign * (px - pane_cx) * zdist / FOCAL */
        int px;
        UBYTE cur = 0;
        for (px = 0; px < SCR_W; px++) {
            LONG dx = px - SCR_W / 2;
            LONG worldz_num = (LONG)dx * zdist;
            LONG worldz = cam_z_i - fwd_sign * (worldz_num / FOCAL);
            LONG cellz = (worldz >= 0) ? (worldz / grid_i)
                                       : -((-worldz + grid_i - 1) / grid_i);
            int is_a = (((int)((cellz + cellx) & 1)) == 0);   /* even = A */
            (void)row_parity;
            /* pack MSB-first across the byte */
            int bit = 7 - (px & 7);
            if (bit == 7) cur = 0;
            if (is_a) cur |= (1 << bit);
            if (bit == 0) mask[px >> 3] = cur;
        }

        /* Blast to 5 bitplanes at this Y. */
        UBYTE *r0 = plane0 + y * bpr;
        UBYTE *r1 = plane1 + y * bpr;
        UBYTE *r2 = plane2 + y * bpr;
        UBYTE *r3 = plane3 ? plane3 + y * bpr : NULL;
        UBYTE *r4 = plane4 ? plane4 + y * bpr : NULL;
        int b;
        for (b = 0; b < 40; b++) {
            UBYTE m = mask[b];
            r0[b] = m;
            r1[b] = m;
            r2[b] = (UBYTE)~m;
            if (r3) r3[b] = 0;
            if (r4) r4[b] = 0;
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
