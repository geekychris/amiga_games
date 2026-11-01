// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Boing Twist's ball and room in OpenGL, through Mesa's
 * software rasteriser (OSMesa 7.8.2, third_party/mesa-os4).
 *
 * The camera never moves, so the room (a box of up to five faces, each a
 * colour with a grid or a picture, from the theme) is drawn once into a
 * background buffer, and each frame starts by copying that and darkening
 * the ball's shadow into it in C. Then only the ball is drawn in GL.
 *
 * The ball is one quad mesh per tile, each tile cut into a few quads so
 * the sphere stays round, with the picture's tile its piece carries as the
 * texture (turned round if the piece is upside down). The tile edges are
 * drawn into a copy of the picture rather than as geometry, so the grooves
 * between pieces cost nothing. Tiles facing away are skipped before GL
 * sees them: QEMU's PowerPC FPU is emulated, so every vertex costs.
 *
 * Speed (OSMesa picks a triangle function by state; see s_triangle.c):
 * GL_REPLACE, GL_NEAREST, an RGB888 power-of-two texture, GL_FASTEST and
 * GL_LESS on a 16-bit depth buffer give simple_z_textured_triangle, which
 * steps the texture in fixed point. Anything else (GL_MODULATE for
 * shading, GL_LINEAR) falls to the general span code, whose per-pixel
 * floating point is 40 times slower under QEMU's emulated FPU (1.1 s for
 * the ball at 800 x 600, against 26 ms). So the ball is drawn unshaded and
 * shaded afterwards in C: the light is fixed to the eye, so the shade of a
 * pixel of the ball depends only on where it is in the ball's disc, and a
 * table gives it (diffuse and a highlight), applied where the depth
 * buffer says the ball was drawn.
 */
#include <GL/gl.h>
#include <GL/osmesa.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define FOVY     45.0f
#define NEAR_Z   0.5f
#define FAR_Z    40.0f
#define ROOM_Z   (-6.5f)        /* back wall */
#define ROOM_Y   (-1.5f)        /* floor: on the 0.5 grid */
#define BOX_X    3.0f           /* side walls, when the theme has them */
#define BOX_Y    2.2f           /* ceiling, when it has one */
#define PI_F     3.14159265f
#define MAXV     6144
#define MAXI     24576

int rd_lit = 1, rd_linear = 0, rd_detail = 32, rd_tris, rd_zbuf = 0;
uint32_t rd_us_shade;
uint32_t rd_us_bg, rd_us_ball;
unsigned long (*rd_clock)(void);

static OSMesaContext ctx;
static uint32_t *pix, *bg;
static int W, H;
static float focal;             /* pixels per unit at distance 1 */
static GLuint tex_pic;
static uint8_t *bordered;       /* the picture with tile edges, as uploaded */
static int pic_w, pic_h;
static RdTheme theme;

/* the shading table: (2N+1)^2 entries of (multiplier, highlight) over the disc */
#define SH_N 128
static uint8_t *shade_tab;
static int depth_flipped = -1;          /* the depth buffer's row 0 is the bottom */
static void project(float x, float y, float z, float *sx, float *sy);

/* the ball's mesh for the current size */
static int g_bands, g_segs, g_sx, g_sy, g_detail;
static float *gpos;             /* unit vectors, (bands*sy+1) x (segs*sx+1) */
static float va[MAXV * 3], ta[MAXV * 2];
static GLushort ia[MAXI];

static unsigned long clk(void) { return rd_clock ? rd_clock() : 0; }

int rd_width(void) { return W; }
int rd_height(void) { return H; }
uint32_t *rd_pixels(void) { return pix; }
const char *rd_renderer(void) { return ctx ? (const char *)glGetString(GL_RENDERER) : "none"; }

static void projection(void)
{
    float t = NEAR_Z * tanf(FOVY * PI_F / 360.0f), a = (float)W / (float)H;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-t * a, t * a, -t, t, NEAR_Z, FAR_Z);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static int build_shade(void)
{
    static const float L[3] = { -0.42f, 0.52f, 0.74f };   /* towards the light, eye space */
    float hx = L[0], hy = L[1], hz = L[2] + 1.0f, hl;
    int u, v;
    hl = sqrtf(hx * hx + hy * hy + hz * hz);
    hx /= hl; hy /= hl; hz /= hl;
    shade_tab = (uint8_t *)malloc((2 * SH_N + 1) * (2 * SH_N + 1) * 2);
    if (!shade_tab) return 0;
    for (v = -SH_N; v <= SH_N; v++)
        for (u = -SH_N; u <= SH_N; u++) {
            float x = (float)u / SH_N, y = -(float)v / SH_N, rr = x * x + y * y, z, d, s, m;
            uint8_t *e = shade_tab + 2 * ((v + SH_N) * (2 * SH_N + 1) + (u + SH_N));
            if (rr > 1) { float k = 1.0f / sqrtf(rr); x *= k; y *= k; rr = 1; }
            z = sqrtf(1.0f - rr);
            d = x * L[0] + y * L[1] + z * L[2];
            m = 0.30f + 0.80f * (d > 0 ? d : 0);
            s = x * hx + y * hy + z * hz;
            s = s > 0 ? s : 0;
            s = s * s; s = s * s; s = s * s; s = s * s; s = s * s;   /* ^32 */
            e[0] = (uint8_t)(m > 1 ? 255 : m * 255);
            e[1] = (uint8_t)(s * 150);
        }
    return 1;
}

/* which way up OSMesa keeps the depth buffer (OSMESA_Y_UP only turns the colour rows) */
static void probe_depth(void)
{
    GLint dw, dh, bpv;
    void *db;
    depth_flipped = 1;
    if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &db) || bpv != 2) return;
    glClear(GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, W, H, 0, -1, 1);                  /* y down: row 0 at the top */
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glColorMask(0, 0, 0, 0);
    glBegin(GL_QUADS);
    glVertex3f(0, 0, 0); glVertex3f((float)W, 0, 0); glVertex3f((float)W, 2, 0); glVertex3f(0, 2, 0);
    glEnd();
    glFinish();
    glColorMask(1, 1, 1, 1);
    glEnable(GL_CULL_FACE);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    depth_flipped = ((uint16_t *)db)[W / 2] == 0xFFFF;   /* the top rows weren't drawn at row 0 */
    glClear(GL_DEPTH_BUFFER_BIT);
}

/* the ball's outline on screen: the outline of a sphere seen in
 * perspective is an ellipse, stretched away from the middle of the
 * screen; its extents across and down come from the tangent lines in the
 * x-z and y-z planes */
static void ball_extent(const RdView *v, float *xl_, float *xr_, float *yt_, float *yb_)
{
    float xl, xr, yt, yb;
    {
        float dxz = sqrtf(v->x * v->x + v->dist * v->dist), dyz = sqrtf(v->y * v->y + v->dist * v->dist);
        float ax = atan2f(v->x, v->dist), ay = atan2f(v->y, v->dist);
        float hx = asinf(1.0f / dxz), hy = asinf(1.0f / dyz);
        xl = W * 0.5f + focal * tanf(ax - hx);
        xr = W * 0.5f + focal * tanf(ax + hx);
        yt = H * 0.5f - focal * tanf(ay + hy);
        yb = H * 0.5f - focal * tanf(ay - hy);
    }
    *xl_ = xl; *xr_ = xr; *yt_ = yt; *yb_ = yb;
}

/* what was under the ball (rd_zbuf 0): the pixels it changed are the ball */
static uint32_t *under;
static int ux0, uy0, ux1, uy1;

static void box_clip(const RdView *v, int *x0, int *y0, int *x1, int *y1)
{
    float xl, xr, yt, yb;
    ball_extent(v, &xl, &xr, &yt, &yb);
    *x0 = (int)xl - 3; *x1 = (int)xr + 3; *y0 = (int)yt - 3; *y1 = (int)yb + 3;
    if (*x0 < 0) *x0 = 0;
    if (*x1 > W - 1) *x1 = W - 1;
    if (*y0 < 0) *y0 = 0;
    if (*y1 > H - 1) *y1 = H - 1;
}

static void save_under(const RdView *v)
{
    int x, y, n = 0;
    box_clip(v, &ux0, &uy0, &ux1, &uy1);
    if (!under) under = (uint32_t *)malloc((size_t)W * H * 4);
    if (!under) return;
    for (y = uy0; y <= uy1; y++) {
        const uint32_t *s = pix + (size_t)y * W;
        for (x = ux0; x <= ux1; x++) under[n++] = s[x];
    }
}

/* shade the ball's pixels: where the depth buffer was written, or (no
 * depth test) where the frame changed */
static void shade_ball(const RdView *v)
{
    GLint dw, dh, bpv;
    void *db = 0;
    float xl, xr, yt, yb;
    int cx, cy, invx, invy, x0, x1, y0, y1, x, y;
    if (!shade_tab) return;
    if (rd_zbuf) {
        if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &db) || bpv != 2) return;
        if (depth_flipped < 0) probe_depth();
    } else if (!under) return;
    ball_extent(v, &xl, &xr, &yt, &yb);
    cx = (int)((xl + xr) * 0.5f);
    cy = (int)((yt + yb) * 0.5f);
    invx = (int)((SH_N << 16) / ((xr - xl) * 0.5f + 1.0f));
    invy = (int)((SH_N << 16) / ((yb - yt) * 0.5f + 1.0f));
    if (rd_zbuf) box_clip(v, &x0, &y0, &x1, &y1);
    else { x0 = ux0; y0 = uy0; x1 = ux1; y1 = uy1; }
    for (y = y0; y <= y1; y++) {
        const uint16_t *drow = db ? (const uint16_t *)db + (size_t)(depth_flipped ? H - 1 - y : y) * W : 0;
        const uint32_t *urow = drow ? 0 : under + (size_t)(y - y0) * (x1 - x0 + 1) - x0;
        uint32_t *row = pix + (size_t)y * W;
        int vv = ((y - cy) * invy) >> 16;
        const uint8_t *trow;
        if (vv < -SH_N) vv = -SH_N;
        if (vv > SH_N) vv = SH_N;
        trow = shade_tab + 2 * ((vv + SH_N) * (2 * SH_N + 1) + SH_N);
        for (x = x0; x <= x1; x++) {
            uint32_t c, m, a, rr, gg, bb;
            int uu;
            const uint8_t *e;
            if (drow ? drow[x] == 0xFFFF : urow[x] == row[x]) continue;
            uu = ((x - cx) * invx) >> 16;
            if (uu < -SH_N) uu = -SH_N;
            if (uu > SH_N) uu = SH_N;
            e = trow + 2 * uu;
            m = e[0];
            a = e[1];
            c = row[x];
            rr = (((c >> 16) & 255) * m >> 8) + a;
            gg = (((c >> 8) & 255) * m >> 8) + a;
            bb = ((c & 255) * m >> 8) + a;
            row[x] = 0xFF000000u | (rr > 255 ? 255 : rr) << 16 | (gg > 255 ? 255 : gg) << 8 | (bb > 255 ? 255 : bb);
        }
    }
}

int rd_open(int w, int h)
{
    W = w;
    H = h;
    focal = (H * 0.5f) / tanf(FOVY * PI_F / 360.0f);
    pix = (uint32_t *)malloc((size_t)W * H * 4);
    bg = (uint32_t *)malloc((size_t)W * H * 4);
    if (!pix || !bg) return 0;
    ctx = OSMesaCreateContextExt(OSMESA_ARGB, 16, 0, 0, NULL);
    if (!ctx) return 0;
    if (!OSMesaMakeCurrent(ctx, pix, GL_UNSIGNED_BYTE, W, H)) return 0;
    OSMesaPixelStore(OSMESA_Y_UP, 0);            /* row 0 at the top, like a bitmap */
    glViewport(0, 0, W, H);
    glDisable(GL_DITHER);
    glDisable(GL_LIGHTING);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glShadeModel(GL_SMOOTH);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    glDepthFunc(GL_LESS);                        /* with 16 bits: OSMesa's fast z paths */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    projection();
    glGenTextures(1, &tex_pic);
    return build_shade();
}

void rd_close(void)
{
    if (ctx) OSMesaDestroyContext(ctx);
    ctx = 0;
    free(pix);
    free(bg);
    free(bordered);
    free(gpos);
    free(under);
    under = 0;
    pix = bg = 0;
    bordered = 0;
    gpos = 0;
}

/* ---------------- the picture ---------------- */

static uint32_t sig_of(const uint8_t *rgb, int w, int h, int bands, int segs, int home, int turned)
{
    /* an 8 x 8 grid of samples inside the tile, read backwards for a turned piece:
     * tiles that look the same (a checkerboard) get the same signature */
    int hb = home / segs, hs = home % segs, i, j;
    uint32_t sum = 2166136261u;
    for (i = 0; i < 8; i++)
        for (j = 0; j < 8; j++) {
            int si = turned ? 7 - i : i, sj = turned ? 7 - j : j;
            float fu = (sj + 0.5f) / 8.0f, fv = (si + 0.5f) / 8.0f;
            int x = (int)((hs + fu) * w / segs), y = (int)((hb + fv) * h / bands);
            const uint8_t *t = rgb + ((size_t)y * w + x) * 3;
            sum = (sum ^ t[0]) * 16777619u;
            sum = (sum ^ t[1]) * 16777619u;
            sum = (sum ^ t[2]) * 16777619u;
        }
    return sum;
}

int rd_set_picture(const uint8_t *rgb, int w, int h, int bands, int segs, uint32_t sig[][2])
{
    int x, y, h_;
    float ex = 1.2f * segs / w, ey = 1.2f * bands / h;    /* edge half-width, in tiles */
    uint8_t *b;
    if (!ctx || !rgb || w < 8 || h < 8) return 0;
    b = (uint8_t *)malloc((size_t)w * h * 3);
    if (!b) return 0;
    for (y = 0; y < h; y++) {
        float fy = (y + 0.5f) * bands / h, dy = fy - floorf(fy + 0.5f);
        int edge_y = fabsf(dy) < ey;
        const uint8_t *s = rgb + (size_t)y * w * 3;
        uint8_t *d = b + (size_t)y * w * 3;
        for (x = 0; x < w; x++) {
            float fx = (x + 0.5f) * segs / w, dx = fx - floorf(fx + 0.5f);
            if (edge_y || fabsf(dx) < ex) {
                /* a groove that shows on dark pictures as well as light ones */
                d[0] = (uint8_t)((s[0] * 2 + 0x3A * 3) / 5);
                d[1] = (uint8_t)((s[1] * 2 + 0x3C * 3) / 5);
                d[2] = (uint8_t)((s[2] * 2 + 0x48 * 3) / 5);
            } else {
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2];      /* a loop: newlib's memcpy is unsafe for big copies */
            }
            s += 3;
            d += 3;
        }
    }
    free(bordered);
    bordered = b;
    pic_w = w;
    pic_h = h;
    glBindTexture(GL_TEXTURE_2D, tex_pic);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, bordered);
    if (sig)
        for (h_ = 0; h_ < bands * segs; h_++) {
            sig[h_][0] = sig_of(rgb, w, h, bands, segs, h_, 0);
            sig[h_][1] = sig_of(rgb, w, h, bands, segs, h_, 1);
        }
    return 1;
}

/* ---------------- the room ---------------- */

static void colour(uint32_t c)
{
    glColor3ub((GLubyte)(c >> 16), (GLubyte)(c >> 8), (GLubyte)c);
}

static void project(float x, float y, float z, float *sx, float *sy)
{
    *sx = W * 0.5f + focal * x / -z;
    *sy = H * 0.5f - focal * y / -z;
}

/* a face: the corners in order, its texture over them, and its grid
 * (lines across axis a and axis b, at multiples of the cell) */
static void face(const RdFace *f, const float c[4][3])
{
    int k;
    if (f->tex && f->tw > 0 && f->th > 0) {
        GLuint t;
        static const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, f->tw, f->th, 0, GL_RGB, GL_UNSIGNED_BYTE, f->tex);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        for (k = 0; k < 4; k++) { glTexCoord2f(uv[k][0], uv[k][1]); glVertex3fv(c[k]); }
        glEnd();
        glDisable(GL_TEXTURE_2D);
        glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
        glDeleteTextures(1, &t);
    } else {
        colour(f->col);
        glBegin(GL_QUADS);
        for (k = 0; k < 4; k++) glVertex3fv(c[k]);
        glEnd();
    }
    if (f->grid != NO_GRID && f->cell > 0.05f) {
        int axis;
        colour(f->grid);
        glLineWidth(W >= 1000 ? 3.0f : 2.0f);
        glBegin(GL_LINES);
        for (axis = 0; axis < 3; axis++) {
            float lo = c[0][axis], hi = c[0][axis], v;
            for (k = 1; k < 4; k++) {
                if (c[k][axis] < lo) lo = c[k][axis];
                if (c[k][axis] > hi) hi = c[k][axis];
            }
            if (hi - lo < 1e-3f) continue;                /* the face is flat in this axis */
            for (v = ceilf(lo / f->cell) * f->cell; v <= hi + 1e-4f; v += f->cell) {
                float p0[3], p1[3];
                int other = -1, o;
                for (o = 0; o < 3; o++)
                    if (o != axis) {
                        float l2 = c[0][o], h2 = c[0][o];
                        for (k = 1; k < 4; k++) { if (c[k][o] < l2) l2 = c[k][o]; if (c[k][o] > h2) h2 = c[k][o]; }
                        if (h2 - l2 > 1e-3f) other = o;
                        p0[o] = l2; p1[o] = h2;
                    }
                if (other < 0) continue;
                p0[axis] = p1[axis] = v;
                for (o = 0; o < 3; o++)                      /* the flat axis: on the face */
                    if (o != axis && o != other) p0[o] = p1[o] = c[0][o];
                glVertex3fv(p0);
                glVertex3fv(p1);
            }
        }
        glEnd();
    }
}

void rd_set_theme(const RdTheme *t)
{
    float xl, xr, yt, zn = -NEAR_Z * 1.01f;
    int i;
    theme = *t;
    if (!ctx) return;
    xl = t->face[FACE_LEFT].on ? -BOX_X : -20.0f;
    xr = t->face[FACE_RIGHT].on ? BOX_X : 20.0f;
    yt = t->face[FACE_CEIL].on ? BOX_Y : 20.0f;
    glClearColor(((t->bg >> 16) & 255) / 255.0f, ((t->bg >> 8) & 255) / 255.0f, (t->bg & 255) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);                 /* a box seen from inside: draw far to near */
    glLoadIdentity();
    {
        /* the visible part of the back wall and floor: where a texture is stretched */
        float th = tanf(FOVY * PI_F / 360.0f), hx = -ROOM_Z * th * W / H, hy = -ROOM_Z * th;
        float bx0 = xl > -hx ? xl : -hx, bx1 = xr < hx ? xr : hx, by1 = yt < hy ? yt : hy;
        float fz = ROOM_Y / th;               /* where the floor leaves the bottom of the screen */
        float back[4][3]  = { { bx0, ROOM_Y, ROOM_Z }, { bx1, ROOM_Y, ROOM_Z }, { bx1, by1, ROOM_Z }, { bx0, by1, ROOM_Z } };
        float floor_[4][3] = { { bx0 * fz / ROOM_Z, ROOM_Y, fz }, { bx1 * fz / ROOM_Z, ROOM_Y, fz }, { bx1, ROOM_Y, ROOM_Z }, { bx0, ROOM_Y, ROOM_Z } };
        float left[4][3]  = { { xl, ROOM_Y, zn }, { xl, ROOM_Y, ROOM_Z }, { xl, yt, ROOM_Z }, { xl, yt, zn } };
        float right[4][3] = { { xr, ROOM_Y, ROOM_Z }, { xr, ROOM_Y, zn }, { xr, yt, zn }, { xr, yt, ROOM_Z } };
        float ceil_[4][3] = { { xl, yt, ROOM_Z }, { xr, yt, ROOM_Z }, { xr, yt, zn }, { xl, yt, zn } };
        if (t->face[FACE_LEFT].on) face(&t->face[FACE_LEFT], left);
        if (t->face[FACE_RIGHT].on) face(&t->face[FACE_RIGHT], right);
        if (t->face[FACE_CEIL].on) face(&t->face[FACE_CEIL], ceil_);
        if (t->face[FACE_FLOOR].on) {
            if (!t->face[FACE_FLOOR].tex) {               /* the whole floor, out to the side walls */
                floor_[0][0] = xl; floor_[1][0] = xr; floor_[2][0] = xr; floor_[3][0] = xl;
                floor_[0][2] = floor_[1][2] = zn;
            }
            face(&t->face[FACE_FLOOR], floor_);
        }
        if (t->face[FACE_BACK].on) face(&t->face[FACE_BACK], back);
    }
    glFinish();
    for (i = 0; i < W * H; i++) bg[i] = pix[i];
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

/* the shadow: darken a disc of the background (in the frame) */
static void shadow(const RdView *v)
{
    float cx, cy, r, x0f, y0f, x1f, y1f, ry;
    int x, y, k = 100 - theme.shadow_pct, xa, xb, ya, yb, clip_y = H;
    if (theme.shadow_pct <= 0) return;
    if (theme.shadow_floor) {
        float sx0, sy0, sy1;
        project(v->x, ROOM_Y, -v->dist, &cx, &cy);
        project(v->x, ROOM_Y, -v->dist - 1.0f, &sx0, &sy0);
        project(v->x, ROOM_Y, -v->dist + 0.6f, &sx0, &sy1);
        r = focal * 1.0f / v->dist;
        ry = (sy1 - sy0) * 0.5f;
        cy = (sy0 + sy1) * 0.5f;
        /* fainter the higher the ball is */
        k = 100 - (int)(theme.shadow_pct * (1.0f - (v->y + 0.5f) / 4.0f));
        if (k > 95) return;
    } else {
        project(v->x + 0.85f, v->y - 0.15f, ROOM_Z, &cx, &cy);
        r = focal * 1.15f / -ROOM_Z;
        ry = r;
        project(0, ROOM_Y, ROOM_Z, &x0f, &y0f);
        clip_y = (int)y0f;                     /* only on the wall, not the floor */
    }
    x0f = cx - r; x1f = cx + r; y0f = cy - ry; y1f = cy + ry;
    xa = x0f < 0 ? 0 : (int)x0f;
    xb = x1f > W - 1 ? W - 1 : (int)x1f;
    ya = y0f < 0 ? 0 : (int)y0f;
    yb = y1f > clip_y - 1 ? clip_y - 1 : (int)y1f;
    if (k < 0) k = 0;
    for (y = ya; y <= yb; y++) {
        float dy = (y + 0.5f - cy) / ry, w;
        int lo, hi;
        uint32_t *row = pix + (size_t)y * W;
        if (dy * dy >= 1.0f) continue;
        w = r * sqrtf(1.0f - dy * dy);
        lo = (int)(cx - w);
        hi = (int)(cx + w);
        if (lo < xa) lo = xa;
        if (hi > xb) hi = xb;
        for (x = lo; x <= hi; x++) {
            uint32_t c = row[x];
            uint32_t rr = ((c >> 16) & 255) * k / 100, gg = ((c >> 8) & 255) * k / 100, bb = (c & 255) * k / 100;
            row[x] = 0xFF000000u | rr << 16 | gg << 8 | bb;
        }
    }
}

/* ---------------- the ball ---------------- */

typedef struct { float m[9]; } M3;              /* row major */

static M3 m3_mul(M3 a, M3 b)
{
    M3 r;
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
    return r;
}

static M3 rot_axis(float ax, float ay, float az, float deg)
{
    float a = deg * PI_F / 180.0f, c = cosf(a), s = sinf(a), t = 1 - c;
    M3 r;
    r.m[0] = t * ax * ax + c;      r.m[1] = t * ax * ay - s * az; r.m[2] = t * ax * az + s * ay;
    r.m[3] = t * ax * ay + s * az; r.m[4] = t * ay * ay + c;      r.m[5] = t * ay * az - s * ax;
    r.m[6] = t * ax * az - s * ay; r.m[7] = t * ay * az + s * ax; r.m[8] = t * az * az + c;
    return r;
}

/* the ball's turn: yaw about its axis, then the axis tipped (pitch) and leaned (roll) */
static M3 ball_rot(const RdView *v)
{
    return m3_mul(rot_axis(0, 0, 1, v->roll), m3_mul(rot_axis(1, 0, 0, v->pitch), rot_axis(0, 1, 0, v->yaw)));
}

static void m3_apply(const M3 *r, const float *p, float *o)
{
    o[0] = r->m[0] * p[0] + r->m[1] * p[1] + r->m[2] * p[2];
    o[1] = r->m[3] * p[0] + r->m[4] * p[1] + r->m[5] * p[2];
    o[2] = r->m[6] * p[0] + r->m[7] * p[1] + r->m[8] * p[2];
}

static int build_mesh(int bands, int segs)
{
    int rows, cols, i, j;
    int sx = rd_detail / segs, sy = (rd_detail / 2) / bands;
    if (sx < 1) sx = 1;
    if (sy < 1) sy = 1;
    if (bands == g_bands && segs == g_segs && sx == g_sx && sy == g_sy && gpos) return 1;
    rows = bands * sy + 1;
    cols = segs * sx + 1;
    free(gpos);
    gpos = (float *)malloc(sizeof(float) * 3 * rows * cols);
    if (!gpos) return 0;
    for (i = 0; i < rows; i++) {
        float lat = PI_F * 0.5f - PI_F * i / (rows - 1), cl = cosf(lat), sl = sinf(lat);
        for (j = 0; j < cols; j++) {
            float lon = 2 * PI_F * j / (cols - 1);
            float *g = gpos + 3 * (i * cols + j);
            g[0] = cl * sinf(lon);
            g[1] = sl;
            g[2] = cl * cosf(lon);
        }
    }
    g_bands = bands; g_segs = segs; g_sx = sx; g_sy = sy; g_detail = rd_detail;
    return 1;
}

static M3 anim_rot(int segs, int m, float t)
{
    if (BT_IS_FLIP(m)) {
        float a = (BT_HALF(m) + segs / 4.0f) * 2 * PI_F / segs;
        return rot_axis(sinf(a), 0, cosf(a), 180.0f * t);
    }
    return rot_axis(0, 1, 0, (BT_WEST(m) ? -360.0f : 360.0f) * t / segs);
}

/* the overlay lines: drawn after the ball with no depth test, so each
 * point is checked against the ball here and only the parts on the side
 * facing the eye are drawn (as GL_LINES pairs) */
static M3 ov_R;
static float ov_C[3], ov_lp[3];
static int ov_ok;

static int ov_facing(const float *p)
{
    float n[3];
    m3_apply(&ov_R, p, n);
    return -(n[0] * ov_C[0] + n[1] * ov_C[1] + n[2] * ov_C[2]) - (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) > 0;
}

static void ov_start(void) { ov_ok = 0; }

static void ov_to(float x, float y, float z)
{
    float p[3];
    int ok;
    p[0] = x; p[1] = y; p[2] = z;
    ok = ov_facing(p);
    if (ok && ov_ok) { glVertex3fv(ov_lp); glVertex3fv(p); }
    ov_lp[0] = x; ov_lp[1] = y; ov_lp[2] = z;
    ov_ok = ok;
}

static void ov_arc(float lat0, float lon0, float lat1, float lon1, int n)
{
    int k;
    ov_start();
    for (k = 0; k <= n; k++) {
        float la = lat0 + (lat1 - lat0) * k / n, lo = lon0 + (lon1 - lon0) * k / n;
        ov_to(1.012f * cosf(la) * sinf(lo), 1.012f * sinf(la), 1.012f * cosf(la) * cosf(lo));
    }
}

void rd_draw(const BtPuzzle *p, const RdView *v, int anim_move, float anim_t,
             int sel_band, int sel_half, int flags)
{
    unsigned long t0 = clk(), t1;
    int B = p->bands, S = p->segs, cols, nv = 0, ni = 0, slot, i, j, mv[BT_MAX_T], moving[BT_MAX_T];
    float C[3], inv_d, cos_skip;
    M3 R = ball_rot(v), A;
    GLfloat gm[16];

    if (!ctx || !build_mesh(B, S)) return;
    if (!(flags & RD_NO_ROOM)) {
        for (i = 0; i < W * H; i++) pix[i] = bg[i];   /* not memcpy: unsafe for big copies here */
        shadow(v);
    }
    t1 = clk();
    rd_us_bg = (uint32_t)(t1 - t0);

    C[0] = v->x; C[1] = v->y; C[2] = -v->dist;
    cols = S * g_sx + 1;
    memset(moving, 0, sizeof(moving));
    if (anim_move >= 0 && !(flags & RD_PEEK)) {
        int n = bt_moved_slots(p, anim_move, mv);
        for (i = 0; i < n; i++) moving[mv[i]] = 1;
        A = anim_rot(S, anim_move, anim_t);
    }

    {
        /* a point of the ball shows when the angle from its normal to the eye
         * is under acos(1/d); a tile reaches about tile_r from its middle */
        float d = sqrtf(C[0] * C[0] + C[1] * C[1] + C[2] * C[2]);
        float tile_r = 0.75f * (PI_F / B > PI_F / S ? PI_F / B : PI_F / S) + 0.05f, lim = acosf(1.0f / d) + tile_r;
        inv_d = 1.0f / d;
        cos_skip = lim >= PI_F ? -2.0f : cosf(lim);
    }
    for (slot = 0; slot < B * S && nv < MAXV - 300; slot++) {
        int b = slot / S, s = slot % S, home, orient, hb, hs, any = 0, base = nv;
        if (flags & RD_PEEK) { home = slot; orient = 0; }
        else { home = p->slot[slot].home; orient = p->slot[slot].orient; }
        hb = home / S;
        hs = home % S;
        {
            /* the tile's middle: if it faces far enough away, none of it shows */
            const float *g = gpos + 3 * ((b * g_sy + g_sy / 2) * cols + s * g_sx + g_sx / 2);
            float q[3], n[3], cosc;
            if (moving[slot]) m3_apply(&A, g, q); else { q[0] = g[0]; q[1] = g[1]; q[2] = g[2]; }
            m3_apply(&R, q, n);
            cosc = -(n[0] * C[0] + n[1] * C[1] + n[2] * C[2]) * inv_d;
            if (cosc < cos_skip) continue;
        }
        for (i = 0; i <= g_sy; i++)
            for (j = 0; j <= g_sx; j++) {
                const float *g = gpos + 3 * ((b * g_sy + i) * cols + s * g_sx + j);
                float q[3], n[3], fu = (float)j / g_sx, fv = (float)i / g_sy;
                float *o = va + 3 * nv, *tc = ta + 2 * nv;
                if (moving[slot]) m3_apply(&A, g, q); else { q[0] = g[0]; q[1] = g[1]; q[2] = g[2]; }
                o[0] = q[0]; o[1] = q[1]; o[2] = q[2];
                m3_apply(&R, q, n);                       /* the normal, in eye space */
                if (-(n[0] * C[0] + n[1] * C[1] + n[2] * C[2]) - 1.0f > -0.02f) any = 1;
                if (orient) { fu = 1 - fu; fv = 1 - fv; }
                /* half a texel in from the tile's edge, so GL_NEAREST stays inside it */
                tc[0] = (hs + 0.002f + fu * 0.996f) / S;
                tc[1] = (hb + 0.002f + fv * 0.996f) / B;
                nv++;
            }
        if (!any) { nv = base; continue; }                /* faces away: skip it */
        for (i = 0; i < g_sy; i++)
            for (j = 0; j < g_sx; j++) {
                int a = base + i * (g_sx + 1) + j, c = a + g_sx + 1;
                ia[ni++] = (GLushort)a; ia[ni++] = (GLushort)c; ia[ni++] = (GLushort)(c + 1);
                ia[ni++] = (GLushort)a; ia[ni++] = (GLushort)(c + 1); ia[ni++] = (GLushort)(a + 1);
            }
    }
    rd_tris = ni / 3;

    if (rd_lit && !rd_zbuf) save_under(v);
    if (rd_zbuf) { glClear(GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST); }
    else glDisable(GL_DEPTH_TEST);
    glLoadIdentity();
    glTranslatef(C[0], C[1], C[2]);
    gm[0] = R.m[0]; gm[4] = R.m[1]; gm[8] = R.m[2];  gm[12] = 0;
    gm[1] = R.m[3]; gm[5] = R.m[4]; gm[9] = R.m[5];  gm[13] = 0;
    gm[2] = R.m[6]; gm[6] = R.m[7]; gm[10] = R.m[8]; gm[14] = 0;
    gm[3] = 0; gm[7] = 0; gm[11] = 0; gm[15] = 1;
    glMultMatrixf(gm);

    glBindTexture(GL_TEXTURE_2D, tex_pic);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, rd_linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, rd_linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, va);
    glTexCoordPointer(2, GL_FLOAT, 0, ta);
    glColor3ub(255, 255, 255);
    if (ni) glDrawElements(GL_TRIANGLES, ni, GL_UNSIGNED_SHORT, ia);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_TEXTURE_2D);
    if (rd_lit) {
        unsigned long ts;
        glFinish();
        ts = clk();
        shade_ball(v);
        rd_us_shade = (uint32_t)(clk() - ts);
    } else rd_us_shade = 0;

    if ((flags & RD_SELECT) && anim_move < 0) {
        float bw = PI_F / B, sw = 2 * PI_F / S;
        ov_R = R;
        ov_C[0] = C[0]; ov_C[1] = C[1]; ov_C[2] = C[2];
        glDisable(GL_DEPTH_TEST);
        glLineWidth(W >= 1000 ? 3.0f : 2.0f);
        glBegin(GL_LINES);
        if (sel_half >= 0 && sel_half < S) {                /* the half a flip turns */
            float l0 = sel_half * sw, l1 = (sel_half + S / 2) * sw, a = (sel_half + S / 4.0f) * sw;
            float ux = sinf(a), uz = cosf(a);           /* the flip's axis, out through the half's middle */
            int k;
            glColor3ub(40, 230, 255);
            ov_arc(PI_F / 2, l0, -PI_F / 2, l0, 24);
            ov_arc(PI_F / 2, l1, -PI_F / 2, l1, 24);
            /* a ring round the axis, with two arrow heads: the half turns about it */
            for (k = 0; k < 2; k++) {
                int j, q;
                float r0 = 0.42f, c0 = sqrtf(1.0f - r0 * r0) * 1.012f;
                float t = (k * 180.0f + 160.0f) * PI_F / 180.0f, t2 = (k * 180.0f + 142.0f) * PI_F / 180.0f;
                float hx = c0 * ux + r0 * cosf(t) * uz, hy = r0 * sinf(t), hz = c0 * uz - r0 * cosf(t) * ux;
                float rr[2] = { r0 + 0.07f, r0 - 0.07f };
                ov_start();
                for (j = 0; j <= 16; j++) {
                    float tt = (k * 180.0f + 20.0f + j * 140.0f / 16) * PI_F / 180.0f;
                    /* round the axis u: across it are up (y) and u x y */
                    ov_to(c0 * ux + r0 * cosf(tt) * uz, r0 * sinf(tt), c0 * uz - r0 * cosf(tt) * ux);
                }
                for (q = 0; q < 2; q++) {
                    float cq = sqrtf(1.0f - rr[q] * rr[q]) * 1.012f;
                    ov_start();
                    ov_to(hx, hy, hz);
                    ov_to(cq * ux + rr[q] * cosf(t2) * uz, rr[q] * sinf(t2), cq * uz - rr[q] * cosf(t2) * ux);
                }
            }
        }
        if (sel_band >= 0 && sel_band < B) {                /* the band a twist turns */
            float la0 = PI_F / 2 - sel_band * bw, la1 = la0 - bw;
            glColor3ub(255, 220, 40);
            if (sel_band > 0) ov_arc(la0, 0, la0, 2 * PI_F, 64);
            if (sel_band < B - 1) ov_arc(la1, 0, la1, 2 * PI_F, 64);
        }
        glEnd();
    }
    glFinish();
    rd_us_ball = (uint32_t)(clk() - t1);
}

void rd_ball_screen(const RdView *v, int *cx, int *cy, int *r)
{
    float x, y;
    project(v->x, v->y, -v->dist, &x, &y);
    *cx = (int)x;
    *cy = (int)y;
    *r = (int)(focal / v->dist);
}

int rd_pick(const RdView *v, int mx, int my, float *lat, float *lon)
{
    float d[3], C[3], b, c, disc, t, p[3], q[3], len;
    M3 R = ball_rot(v), RT;
    int i, j;
    d[0] = (mx + 0.5f - W * 0.5f) / focal;
    d[1] = -(my + 0.5f - H * 0.5f) / focal;
    d[2] = -1.0f;
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (i = 0; i < 3; i++) d[i] /= len;
    C[0] = v->x; C[1] = v->y; C[2] = -v->dist;
    /* |t d - C| = 1 */
    b = d[0] * C[0] + d[1] * C[1] + d[2] * C[2];
    c = C[0] * C[0] + C[1] * C[1] + C[2] * C[2] - 1.0f;
    disc = b * b - c;
    if (disc < 0) return 0;
    t = b - sqrtf(disc);
    for (i = 0; i < 3; i++) p[i] = t * d[i] - C[i];
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) RT.m[i * 3 + j] = R.m[j * 3 + i];
    m3_apply(&RT, p, q);
    if (q[1] > 1) q[1] = 1;
    if (q[1] < -1) q[1] = -1;
    *lat = asinf(q[1]) * 180.0f / PI_F;
    *lon = atan2f(q[0], q[2]) * 180.0f / PI_F;
    if (*lon < 0) *lon += 360.0f;
    return 1;
}
