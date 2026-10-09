/*
 * The planet in OpenGL (Mesa's software rasteriser via OSMesa), in place
 * of the 3DO version's cel engine (render.c + cels.c there). Same camera
 * as PlanetCamera.cs, but with a depth buffer, so walls, crumbs and the
 * billboarded chomper, spooks and keys hide each other properly; the
 * planet is a lit disc under them all.
 *
 * Every vertex is costly: QEMU emulates the PowerPC FPU in software, and
 * Mesa's transform and triangle set-up are all floating point. So the
 * planet is a 49-vertex silhouette fan (not a sphere mesh), walls share
 * their corners through indexed arrays (6 vertices for the two faces
 * that can be seen, as on the 3DO), and crumbs are points.
 *
 * The game keeps its fixed point (Q14 directions, Q8 world units); this
 * file converts to float at the edge.
 */
#include <GL/gl.h>
#include <GL/osmesa.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pc.h"
#include "gl_render.h"

#define FOCAL     246.0f        /* px: 52 deg vertical field of view on 240 lines */
#define NEAR_Z    0.6f          /* rd_project's near plane, as on the 3DO */
#define GL_NEAR   0.25f
#define GL_FAR    220.0f
#define TILT      0.58f
#define LOOKAHEAD 0.08f
#define RADIUS    12.0f
#define CRUMB_H   0.5f
#define CRUMB_R   0.13f

int rd_stats_walls, rd_stats_cels;
/* optional profiling: main sets rd_clock; rd_prof[] collects microseconds
 * for clear+stars, planet, walls+nest, crumbs, sprites */
unsigned long long (*rd_clock)(void);
unsigned long long rd_prof[5];
static unsigned long long prof_t;
static void prof(int i)
{
    unsigned long long t;
    if (!rd_clock) return;
    glFlush();
    t = rd_clock();
    if (i >= 0) rd_prof[i] += t - prof_t;
    prof_t = t;
}
int tex_key_id = -1;

typedef struct { float x, y, z; } F3;

static F3 f3q14(V3 v) { F3 r; r.x = v.x / 16384.0f; r.y = v.y / 16384.0f; r.z = v.z / 16384.0f; return r; }
static F3 f3q8(V3 v)  { F3 r; r.x = v.x / 256.0f;   r.y = v.y / 256.0f;   r.z = v.z / 256.0f;   return r; }
static F3 add(F3 a, F3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
static F3 sub(F3 a, F3 b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
static F3 mul(F3 a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }
static float dot(F3 a, F3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static F3 cross(F3 a, F3 b)
{
    F3 r;
    r.x = a.y * b.z - a.z * b.y; r.y = a.z * b.x - a.x * b.z; r.z = a.x * b.y - a.y * b.x;
    return r;
}
static F3 norm(F3 a)
{
    float l = sqrtf(dot(a, a));
    return l > 0 ? mul(a, 1.0f / l) : a;
}

/* ---- context ---- */

static OSMesaContext ctx;
static unsigned char *pix;
static int W, H, S;

int gl_open(int scale)
{
    S = scale < 1 ? 1 : scale;
    W = LOGICAL_W * S;
    H = LOGICAL_H * S;
    pix = (unsigned char *)malloc((size_t)W * H * 4);
    if (!pix) return 0;
    ctx = OSMesaCreateContextExt(OSMESA_ARGB, 16, 0, 0, NULL);
    if (!ctx) return 0;
    if (!OSMesaMakeCurrent(ctx, pix, GL_UNSIGNED_BYTE, W, H)) return 0;
    OSMesaPixelStore(OSMESA_Y_UP, 0);            /* row 0 at the top, like a bitmap */
    glViewport(0, 0, W, H);
    glDisable(GL_DITHER);
    glDisable(GL_CULL_FACE);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    /* GL_LESS (with 16 depth bits and nothing else on) is what lets OSMesa
     * use its own fast flat/smooth z-triangle rasterisers: 5x faster */
    glDepthFunc(GL_LESS);
    glAlphaFunc(GL_GREATER, 0.5f);
    glClearColor(4 / 255.0f, 6 / 255.0f, 16 / 255.0f, 1.0f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    return 1;
}

void gl_close(void)
{
    if (ctx) OSMesaDestroyContext(ctx);
    ctx = 0;
    free(pix);
    pix = 0;
}

unsigned char *gl_pixels(void) { return pix; }
int gl_scale(void) { return S; }
const char *gl_renderer_name(void) { return (const char *)glGetString(GL_RENDERER); }

/* ---- textures (sprites.c makes them: RGB15, 0 = transparent) ---- */

#define MAX_TEX 40
static GLuint tex_name[MAX_TEX];
static int    tex_w[MAX_TEX], tex_h[MAX_TEX], ntex;

static GLuint upload(int w, int h, const unsigned char *rgba)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return t;
}

int pc_tex_create(int w, int h, unsigned short *pixels)
{
    unsigned char *rgba;
    int i;
    if (ntex >= MAX_TEX || !ctx) return -1;
    rgba = (unsigned char *)malloc((size_t)w * h * 4);
    if (!rgba) return -1;
    for (i = 0; i < w * h; i++) {
        unsigned p = pixels[i];
        int r = (p >> 10) & 31, g = (p >> 5) & 31, b = p & 31;
        rgba[i * 4 + 0] = (unsigned char)((r << 3) | (r >> 2));
        rgba[i * 4 + 1] = (unsigned char)((g << 3) | (g >> 2));
        rgba[i * 4 + 2] = (unsigned char)((b << 3) | (b >> 2));
        rgba[i * 4 + 3] = p ? 255 : 0;
    }
    tex_name[ntex] = upload(w, h, rgba);
    free(rgba);
    tex_w[ntex] = w;
    tex_h[ntex] = h;
    return ntex++;
}

/* ---- scenery made once ---- */

#define NSTARS 320
static GLfloat star_v[NSTARS][3];
static GLubyte star_c[NSTARS][4];

void rd_init(void)
{
    unsigned long r = 7;
    int i;
    for (i = 0; i < NSTARS; i++) {
        F3 d;
        int b;
        do {
            r = r * 1103515245UL + 12345UL; d.x = (float)((r >> 16) & 1023) / 511.5f - 1;
            r = r * 1103515245UL + 12345UL; d.y = (float)((r >> 16) & 1023) / 511.5f - 1;
            r = r * 1103515245UL + 12345UL; d.z = (float)((r >> 16) & 1023) / 511.5f - 1;
        } while (dot(d, d) > 1 || dot(d, d) < 0.01f);
        d = mul(norm(d), 150.0f);
        star_v[i][0] = d.x; star_v[i][1] = d.y; star_v[i][2] = d.z;
        r = r * 1103515245UL + 12345UL;
        b = 110 + (int)((r >> 16) % 146);
        star_c[i][0] = star_c[i][1] = (GLubyte)b;
        star_c[i][2] = (GLubyte)(b > 200 ? b : b + 20);
        star_c[i][3] = 255;
    }
}

/* ---- camera ---- */

static F3 campos, right, cup, fwd, lightv;

static void camera(const Camera *cam)
{
    float h = cam->height / 256.0f;
    F3 f = norm(f3q14(cam->focus)), up = norm(f3q14(cam->up)), target;
    campos = sub(mul(f, RADIUS + h), mul(up, h * TILT));
    target = add(mul(f, RADIUS), mul(up, h * LOOKAHEAD));
    fwd = norm(sub(target, campos));
    right = norm(cross(up, fwd));
    cup = cross(fwd, right);
    /* the sun: behind the camera, high and a little to the left */
    lightv = norm(add(add(mul(fwd, -0.55f), mul(cup, 0.75f)), mul(right, -0.35f)));
}

int rd_project(V3 p, long *sx, long *sy)
{
    F3 r = sub(f3q8(p), campos);
    float zc = dot(r, fwd), s;
    if (zc < NEAR_Z) return 0;
    s = FOCAL / zc;
    *sx = (long)((160.0f + dot(r, right) * s) * 65536.0f);
    *sy = (long)((128.0f - dot(r, cup) * s) * 65536.0f);
    return 1;
}

static void load_matrices(void)
{
    GLfloat m[16];
    float n = GL_NEAR;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-160.0f / FOCAL * n, 160.0f / FOCAL * n, -128.0f / FOCAL * n, 128.0f / FOCAL * n,
              n, GL_FAR);
    m[0] = right.x; m[4] = right.y; m[8]  = right.z; m[12] = -dot(right, campos);
    m[1] = cup.x;   m[5] = cup.y;   m[9]  = cup.z;   m[13] = -dot(cup, campos);
    m[2] = -fwd.x;  m[6] = -fwd.y;  m[10] = -fwd.z;  m[14] = dot(fwd, campos);
    m[3] = 0;       m[7] = 0;       m[11] = 0;       m[15] = 1;
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
}

static GLubyte clamp255(float v) { return (GLubyte)(v > 255 ? 255 : v < 0 ? 0 : v); }

static void colour(int r, int g, int b, float k)
{
    glColor3ub(clamp255(r * k), clamp255(g * k), clamp255(b * k));
}

static void colour4(GLubyte *c, int r, int g, int b, float k)
{
    c[0] = clamp255(r * k); c[1] = clamp255(g * k); c[2] = clamp255(b * k); c[3] = 255;
}

static float lit(F3 n, float amb, float dif)
{
    float d = dot(n, lightv);
    return amb + dif * (d > 0 ? d : 0);
}

static void vtx(F3 p) { glVertex3f(p.x, p.y, p.z); }

/* ---- the planet: its silhouette as a fan of rings, lit per vertex ----
 *
 * The rings run from the point nearest the camera out to the horizon, and
 * the faces between them are chords, a little inside the sphere. Their
 * depth is what hides things round the far side, so the chords must stay
 * close to the surface: with too few rings (it had two) a spook just over
 * the horizon pokes up through the cut-off bulge. Six rings, closer
 * together toward the horizon, keep the chords within ~0.05 units. */

#define RIM   24
#define RINGS 6

static void draw_planet(void)
{
    float d = sqrtf(dot(campos, campos)), c = RADIUS * RADIUS / d, rr = sqrtf(RADIUS * RADIUS - c * c);
    F3 chat = mul(campos, 1.0f / d);
    F3 e1 = norm(sub(right, mul(chat, dot(right, chat)))), e2 = cross(chat, e1);
    F3 centre = mul(chat, c), top = mul(chat, RADIUS);
    static F3 ring[RINGS][RIM + 1];
    static GLubyte col[RINGS][RIM + 1][4];
    GLubyte ctop[4];
    int i, k;
    for (i = 0; i <= RIM; i++) {
        float a = 2 * (float)M_PI * (i % RIM) / RIM;
        F3 rim = add(centre, add(mul(e1, rr * cosf(a)), mul(e2, rr * sinf(a))));
        for (k = 0; k < RINGS; k++) {
            float u = (float)(k + 1) / RINGS, t = 1 - (1 - u) * (1 - u);   /* denser near the rim */
            F3 p = k == RINGS - 1 ? rim : mul(norm(add(mul(top, 1 - t), mul(rim, t))), RADIUS);
            float l = lit(mul(p, 1.0f / RADIUS), 0.6f, 0.7f) * (k == RINGS - 1 ? 0.85f : 1.0f);
            ring[k][i] = p;
            colour4(col[k][i], 60, 66, 104, l);
        }
    }
    colour4(ctop, 60, 66, 104, lit(chat, 0.6f, 0.7f));
    glShadeModel(GL_SMOOTH);
    glBegin(GL_TRIANGLE_FAN);
    glColor4ubv(ctop);
    vtx(top);
    for (i = 0; i <= RIM; i++) { glColor4ubv(col[0][i]); vtx(ring[0][i]); }
    glEnd();
    for (k = 1; k < RINGS; k++) {
        glBegin(GL_QUAD_STRIP);
        for (i = 0; i <= RIM; i++) {
            glColor4ubv(col[k - 1][i]); vtx(ring[k - 1][i]);
            glColor4ubv(col[k][i]);     vtx(ring[k][i]);
        }
        glEnd();
    }
    glShadeModel(GL_FLAT);
    rd_stats_cels += RIM * (2 * RINGS - 1);
}

/* ---- walls: float copies made once per maze; each frame the visible
 * ones put 6 corners into the arrays (the long side facing the camera
 * and the top: 2 quads) ---- */

static F3  wf[MAX_WALLS][8], wmid[MAX_WALLS], wmidp[MAX_WALLS], wside[MAX_WALLS];
static int wf_n = -1;
static V3  wf_key;
static GLfloat wall_v[MAX_WALLS * 6][3];
static GLubyte wall_c[MAX_WALLS * 6][4];
static GLushort wall_i[MAX_WALLS * 8];

static void wall_cache(void)
{
    int i, k;
    if (wf_n == mz_nwalls && mz_nwalls > 0 && !memcmp(&wf_key, &mz_wall[0].v[0], sizeof(V3))) return;
    for (i = 0; i < mz_nwalls; i++) {
        for (k = 0; k < 8; k++) wf[i][k] = f3q8(mz_wall[i].v[k]);
        wmid[i] = f3q14(mz_wall[i].mid);
        wmidp[i] = f3q8(mz_wall[i].midp);
        wside[i] = f3q14(mz_wall[i].side);
    }
    wf_n = mz_nwalls;
    if (mz_nwalls > 0) wf_key = mz_wall[0].v[0];
}

static void draw_walls(int flash)
{
    /* corner order per side, and the quads as local indices: flat
     * shading takes a quad's colour from its last vertex, so the side's
     * colour sits on corner 1 and the top's on corner 4 */
    static const int plus_c[6] = { 1, 2, 6, 5, 4, 7 }, minus_c[6] = { 0, 3, 7, 4, 5, 6 };
    static const int plus_q[8] = { 2, 3, 0, 1, 3, 2, 5, 4 }, minus_q[8] = { 2, 3, 0, 1, 5, 2, 3, 4 };
    int i, k, nv = 0, ni = 0;
    float horizon = RADIUS - 0.6f;
    wall_cache();
    for (i = 0; i < mz_nwalls; i++) {
        F3 tocam;
        const int *cs, *qs;
        int plus;
        if (dot(wmid[i], campos) < horizon) continue;               /* over the horizon */
        tocam = sub(campos, wmidp[i]);
        if (dot(tocam, fwd) > 1.0f) continue;                        /* behind the camera */
        plus = dot(wside[i], tocam) > 0;
        cs = plus ? plus_c : minus_c;
        qs = plus ? plus_q : minus_q;
        for (k = 0; k < 6; k++) {
            F3 p = wf[i][cs[k]];
            wall_v[nv + k][0] = p.x; wall_v[nv + k][1] = p.y; wall_v[nv + k][2] = p.z;
        }
        if (flash) {
            colour4(wall_c[nv + 1], 235, 240, 255, 1);
            colour4(wall_c[nv + 4], 255, 255, 255, 1);
        } else {
            colour4(wall_c[nv + 1], 50, 80, 220, lit(plus ? wside[i] : mul(wside[i], -1), 0.5f, 0.8f));
            colour4(wall_c[nv + 4], 130, 180, 255, lit(wmid[i], 0.75f, 0.35f));
        }
        for (k = 0; k < 8; k++) wall_i[ni + k] = (GLushort)(nv + qs[k]);
        nv += 6;
        ni += 8;
        rd_stats_walls++;
    }
    if (!ni) return;
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, wall_v);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, wall_c);
    glDrawElements(GL_QUADS, ni, GL_UNSIGNED_SHORT, wall_i);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    rd_stats_cels += ni / 4;
}

static void draw_nest(void)
{
    int k;
    F3 n = f3q14(mz_dir[mz_nest]), t1 = norm(f3q14(mz_tan[mz_nest][0])), t2;
    F3 ctr = mul(n, RADIUS + 0.05f);
    if (dot(n, campos) < RADIUS) return;
    t1 = norm(sub(t1, mul(n, dot(t1, n))));
    t2 = cross(n, t1);
    colour(240, 110, 190, lit(n, 0.7f, 0.4f));
    glBegin(GL_TRIANGLE_FAN);
    vtx(ctr);
    for (k = 0; k <= 12; k++) {
        float a = 2 * (float)M_PI * k / 12;
        vtx(add(ctr, add(mul(t1, 1.25f * cosf(a)), mul(t2, 1.25f * sinf(a)))));
    }
    glEnd();
    rd_stats_cels++;
}

/* ---- crumbs: round points, binned by their size on screen ---- */

#define CRUMB_BINS 8
static GLfloat crumb_v[CRUMB_BINS][CELLS][3];
static int     crumb_n[CRUMB_BINS];

static void draw_crumbs(const unsigned char *crumb)
{
    int c, b;
    float px = 2 * CRUMB_R * FOCAL * S;           /* diameter in pixels times z */
    for (b = 0; b < CRUMB_BINS; b++) crumb_n[b] = 0;
    for (c = 0; c < CELLS; c++) {
        F3 d;
        float z;
        if (!crumb[c]) continue;
        d = f3q14(mz_dir[c]);
        if (dot(d, campos) < RADIUS) continue;
        d = mul(d, RADIUS + CRUMB_H);
        z = dot(sub(d, campos), fwd);
        if (z < NEAR_Z) continue;
        b = (int)(px / z + 0.5f) - 1;
        if (b < 0) b = 0;
        if (b >= CRUMB_BINS) b = CRUMB_BINS - 1;
        crumb_v[b][crumb_n[b]][0] = d.x;
        crumb_v[b][crumb_n[b]][1] = d.y;
        crumb_v[b][crumb_n[b]][2] = d.z;
        crumb_n[b]++;
    }
    glColor3ub(255, 214, 170);
    glEnableClientState(GL_VERTEX_ARRAY);
    for (b = 0; b < CRUMB_BINS; b++) {
        if (!crumb_n[b]) continue;
        /* smooth (round) points from 3 px up, coverage cut by the alpha test */
        if (b >= 2) { glEnable(GL_POINT_SMOOTH); glEnable(GL_ALPHA_TEST); }
        glPointSize((float)(b + 1));
        glVertexPointer(3, GL_FLOAT, 0, crumb_v[b]);
        glDrawArrays(GL_POINTS, 0, crumb_n[b]);
        if (b >= 2) { glDisable(GL_POINT_SMOOTH); glDisable(GL_ALPHA_TEST); }
        rd_stats_cels += crumb_n[b];
    }
    glDisableClientState(GL_VERTEX_ARRAY);
}

/* ---- keys and characters: textured billboards ---- */

static void draw_sprite(int t, F3 c, float hw)
{
    F3 rx, uy;
    if (t < 0 || t >= ntex) return;
    if (dot(sub(c, campos), fwd) < NEAR_Z) return;
    rx = mul(right, hw);
    uy = mul(cup, hw * tex_h[t] / tex_w[t]);
    glBindTexture(GL_TEXTURE_2D, tex_name[t]);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); vtx(add(sub(c, rx), uy));
    glTexCoord2f(1, 0); vtx(add(add(c, rx), uy));
    glTexCoord2f(1, 1); vtx(sub(add(c, rx), uy));
    glTexCoord2f(0, 1); vtx(sub(sub(c, rx), uy));
    glEnd();
    rd_stats_cels++;
}

void rd_frame(const Camera *cam, const unsigned char *crumb, const unsigned char *key,
              const RSprite *spr, int nspr, int flags)
{
    int i, c;
    rd_stats_walls = rd_stats_cels = 0;
    prof(-1);
    camera(cam);
    load_matrices();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* stars: far away, behind everything */
    glPointSize((float)S);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, star_v);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, star_c);
    glPushMatrix();
    glTranslatef(campos.x, campos.y, campos.z);    /* they don't move with the camera */
    glDrawArrays(GL_POINTS, 0, NSTARS);
    glPopMatrix();
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    prof(0);

    glEnable(GL_DEPTH_TEST);
    draw_planet();
    prof(1);
    draw_walls(flags & RD_WALL_FLASH);
    draw_nest();
    prof(2);
    draw_crumbs(crumb);
    prof(3);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    for (c = 0; c < 4; c++) {
        int kc = mz_keys[c];
        if (!key[kc]) continue;
        draw_sprite(tex_key_id, mul(f3q14(mz_dir[kc]), RADIUS + 0.45f), 0.42f);
    }
    for (i = 0; i < nspr; i++)
        draw_sprite(spr[i].tex, f3q8(spr[i].pos), spr[i].half / 256.0f);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glFinish();
    prof(4);
}
