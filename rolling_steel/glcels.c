/*
 * The 3DO cel engine's job done by OpenGL: Mesa's software rasteriser
 * through OSMesa, into an ARGB buffer that main_os4.c puts in a window.
 *
 * Everything arrives already projected (render.c), so the matrices are an
 * orthographic map of screen pixels plus depth. Opaque faces are batched
 * into one glDrawArrays a frame: with GL_LESS, 16 depth bits, flat shading
 * and nothing else on, OSMesa uses its fast z-buffered triangle code (QEMU
 * emulates the PowerPC FPU in software, so Mesa's generic paths are slow).
 */
#include <GL/gl.h>
#include <GL/osmesa.h>
#include <stdlib.h>
#include "glcels.h"

#define ZRANGE 256.0f                  /* depth covers +-256 units around the focus */
#define MAXQ   3000

static OSMesaContext ctx;
static unsigned char *pix;
static int S, nprims;

static GLfloat qv[MAXQ * 4][3];
static GLubyte qc[MAXQ * 4][4];
static int nq;

#define MAX_TEX 24
static GLuint tex_name[MAX_TEX];
static int ntex;

int glc_open(int scale)
{
    S = scale < 1 ? 1 : scale;
    pix = (unsigned char *)malloc((size_t)SCREEN_W * S * SCREEN_H * S * 4);
    if (!pix) return 0;
    ctx = OSMesaCreateContextExt(OSMESA_ARGB, 16, 0, 0, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, pix, GL_UNSIGNED_BYTE, SCREEN_W * S, SCREEN_H * S)) return 0;
    OSMesaPixelStore(OSMESA_Y_UP, 0);            /* row 0 at the top */
    glDisable(GL_DITHER);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);
    glDepthFunc(GL_LESS);                        /* GL_LEQUAL would lose the fast path */
    glAlphaFunc(GL_GREATER, 0.1f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glc_view(0, SCREEN_W);
    return 1;
}

void glc_close(void)
{
    if (ctx) OSMesaDestroyContext(ctx);
    ctx = 0;
    free(pix);
    pix = 0;
}

unsigned char *glc_pixels(void) { return pix; }
int glc_scale(void) { return S; }
int glc_count(void) { return nprims; }

static void rgb15f(unsigned short c, GLubyte *o)
{
    int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
    o[0] = (GLubyte)((r << 3) | (r >> 2));
    o[1] = (GLubyte)((g << 3) | (g >> 2));
    o[2] = (GLubyte)((b << 3) | (b >> 2));
    o[3] = 255;
}

void glc_begin(unsigned short bg)
{
    GLubyte c[4];
    rgb15f(bg, c);
    glc_view(0, SCREEN_W);
    glClearColor(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    nprims = 0;
}

void glc_view(int x0, int w)
{
    glc_flush();
    glViewport(x0 * S, 0, w * S, SCREEN_H * S);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, SCREEN_H, 0, -ZRANGE, ZRANGE);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void vert(GLfloat *v, long x, long y, long z)
{
    v[0] = x * (1.0f / 65536.0f);
    v[1] = y * (1.0f / 65536.0f);
    v[2] = z == GLC_NOZ ? 0.0f : z * (-1.0f / 4096.0f);   /* nearer = smaller dz = in front */
}

/* blending for a PIXC word; 0 = opaque */
static void mode(unsigned long pixc, long z, int textured, unsigned short rgb15)
{
    unsigned long h = pixc >> 16;
    GLubyte c[4];
    rgb15f(rgb15, c);
    if (z == GLC_NOZ) glDisable(GL_DEPTH_TEST);
    else glEnable(GL_DEPTH_TEST);
    if (!pixc) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        if (textured) glColor4ub(255, 255, 255, 255);
        else glColor4ubv(c);
        return;
    }
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    if (pixc == PIXC_GHOST) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (textured) glColor4ub(255, 255, 255, 128);
        else glColor4ub(c[0], c[1], c[2], 128);
    } else if (pixc == PIXC_ADD) {
        glBlendFunc(GL_ONE, GL_ONE);
        if (textured) glColor4ub(255, 255, 255, 255);
        else glColor4ubv(c);
    } else {                                  /* darken to k/8 (shadows, HUD boxes) */
        int k = (int)((h >> 10) & 7) + 1;
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        glColor4ub((GLubyte)(k * 32), (GLubyte)(k * 32), (GLubyte)(k * 32), 255);
    }
}

static void mode_end(void)
{
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void glc_quad(const long *x, const long *y, const long *z, unsigned short rgb15, unsigned long pixc)
{
    int k;
    nprims++;
    if (!pixc && z[0] != GLC_NOZ) {
        GLubyte c[4];
        if (nq >= MAXQ) glc_flush();
        rgb15f(rgb15, c);
        for (k = 0; k < 4; k++) {
            vert(qv[nq * 4 + k], x[k], y[k], z[k]);
            qc[nq * 4 + k][0] = c[0]; qc[nq * 4 + k][1] = c[1];
            qc[nq * 4 + k][2] = c[2]; qc[nq * 4 + k][3] = 255;
        }
        nq++;
        return;
    }
    mode(pixc, z[0], 0, rgb15);
    glBegin(GL_QUADS);
    for (k = 0; k < 4; k++) {
        GLfloat v[3];
        vert(v, x[k], y[k], z[k]);
        glVertex3fv(v);
    }
    glEnd();
    mode_end();
}

void glc_flush(void)
{
    if (!nq) return;
    glEnable(GL_DEPTH_TEST);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, qv);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, qc);
    glDrawArrays(GL_QUADS, 0, nq * 4);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    nq = 0;
}

int glc_tex(int w, int h, const unsigned short *px)
{
    unsigned char *rgba;
    int i;
    if (ntex >= MAX_TEX || !ctx) return -1;
    rgba = (unsigned char *)malloc((size_t)w * h * 4);
    if (!rgba) return -1;
    for (i = 0; i < w * h; i++) {
        rgb15f(px[i], rgba + i * 4);
        rgba[i * 4 + 3] = px[i] ? 255 : 0;
    }
    glGenTextures(1, &tex_name[ntex]);
    glBindTexture(GL_TEXTURE_2D, tex_name[ntex]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    return ntex++;
}

/* a texture centred on (x, y), hx x hy half size, all at depth z */
void glc_sprite(int t, long x, long y, long hx, long hy, long z, unsigned long pixc)
{
    GLfloat v[3];
    if (t < 0 || t >= ntex || hx <= 0 || hy <= 0) return;
    nprims++;
    mode(pixc, z, 1, 0x7FFF);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    glBindTexture(GL_TEXTURE_2D, tex_name[t]);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); vert(v, x - hx, y - hy, z); glVertex3fv(v);
    glTexCoord2f(1, 0); vert(v, x + hx, y - hy, z); glVertex3fv(v);
    glTexCoord2f(1, 1); vert(v, x + hx, y + hy, z); glVertex3fv(v);
    glTexCoord2f(0, 1); vert(v, x - hx, y + hy, z); glVertex3fv(v);
    glEnd();
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
    mode_end();
}

void glc_finish(void)
{
    glc_flush();
    glFinish();
}
