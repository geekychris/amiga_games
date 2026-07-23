/*
 * ham_test - HAM8 3D demo, cloned from aga3d.
 *
 * Same rendering pipeline as aga3d (flat-shaded icosahedron over a
 * starfield, mouse-driven rotation, integer math), but the display
 * runs in Hold-And-Modify HAM8 mode instead of 8bpp indexed.
 *
 * WHAT HAM BUYS US FOR SOLID-POLY 3D
 *
 *   HAM8 gives us a 64-entry BASE palette plus per-pixel "modify"
 *   codes that override one RGB channel of the previous pixel. In
 *   theory 262 144 simultaneous colours; in practice, because
 *   AreaFill picks ONE pen index per triangle, we can only use
 *   base-palette entries for face fills — so the win here is the
 *   palette shape, not per-pixel variation.
 *
 *   Concretely: aga3d has to share 256 pens across (a) a 64-slot
 *   sky gradient, (b) a 64-slot object ramp, (c) stars, (d) menu
 *   chrome. It ends up with 64 shades to draw the ship with.
 *   HAM8 lets us paint the entire base palette (64 slots) as one
 *   long temperature-style ramp — deep blue → purple → red →
 *   orange → yellow → white — and use all 64 shades as face
 *   colours. Same shade count, but the ramp spans a much wider
 *   colour space so lit faces read as WARM and unlit as COOL,
 *   not just "brighter grey" / "darker grey".
 *
 *   The horizontal-streak artefact HAM is famous for shows up
 *   only at hard object-edges-against-sky: because we clamped the
 *   backdrop to a single dark colour, streaks resolve within one
 *   pixel of any edge. If we tried a HAM6 vertical gradient
 *   backdrop with the object ON TOP, we'd see the ship edges
 *   drag colour bleed. Chose the simpler backdrop deliberately.
 *
 * Exit with ESC. Mouse rotates the object. All math is integer.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <graphics/displayinfo.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"
#include "sound.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

#define VERSION "2.0"

/* fixed-point shift for the rotation matrix (sin/cos scaled by 4096) */
#define FP 12
#define ONE (1 << FP)

#define DIST 5000              /* camera distance in object units */

/*
 * Palette layout (HAM8 base palette is only 64 slots — pens
 * 64..255 are NOT solid colours; they're per-pixel "modify"
 * codes we deliberately don't use so AreaFill stays sane).
 *
 *   0        deep space backdrop
 *   1..48    object shading ramp (48 shades, temperature style)
 *   49..53   starfield twinkles
 *   54..63   menu chrome
 */
#define BACKDROP_PEN  0
#define OBJ_BASE      1
#define OBJ_COUNT    48
#define STAR_BASE    49
#define STAR_COUNT    5
#define MENU_BG_PEN  54
#define MENU_TX_PEN  55

#define NUM_STARS  140
#define MAX_W 640
#define MAX_H 512

static LONG frame_count = 0;
static LONG running = 1;
static LONG bridge_ok = 0;

/* current display geometry (set by open_display) */
static UWORD W = 320, H = 256;
static LONG  CXv = 160, CYv = 128, FOVv = 340;

/* ---- selectable display modes --------------------------------------- */
/*
 * HAM_KEY sets the Hold-And-Modify bit. With SA_Depth 8, AGA
 * automatically routes to HAM8 (6 modify bits per pixel) rather
 * than HAM6. The base mode ID (LORES/LACE) picks resolution.
 */
struct ModeDef { const char *name; ULONG modeID; UWORD w, h; };
#define NUM_MODES 2
static struct ModeDef modes[NUM_MODES] = {
    { "HAM8  320 x 256",        LORES_KEY     | HAM_KEY, 320, 256 },
    { "HAM8 Lace 320 x 512",    LORESLACE_KEY | HAM_KEY, 320, 512 },
};

/* ---- integer sine table (degrees, value scaled by ONE) --------------- */
static WORD sintab[360];

static LONG isin_deg(LONG d)
{
    LONG sign = 1, t;
    d %= 360;
    if (d < 0) d += 360;
    if (d >= 180) { d -= 180; sign = -1; }
    t = d * (180 - d);
    return sign * ((4 * t * ONE) / (40500 - t));
}

static void build_sintab(void)
{
    LONG i;
    for (i = 0; i < 360; i++) sintab[i] = (WORD)isin_deg(i);
}

#define SIN(a) ((LONG)sintab[((a) % 360 + 360) % 360])
#define COS(a) ((LONG)sintab[(((a) + 90) % 360 + 360) % 360])

/* ---- pluggable geometry -------------------------------------------- */
/*
 * MAX_VERTS / MAXFACES cover the biggest shape we build (boing
 * sphere with 5 rings × 12 meridians + 2 poles = 62 verts, 120
 * faces). Icosahedron only uses 12/20, cube uses 8/12.
 *
 * face_ramp[i] is the palette ramp BASE for face i:
 *   RAMP_RED   0    boing red squares
 *   RAMP_WHITE 16   boing white squares
 *   RAMP_TEMP  32   temperature (default for icosahedron / cube)
 * The shading intensity is added on top: pen = face_ramp[i] + shade.
 */
#define MAX_VERTS 96
#define MAXFACES  160
/* Ramp bases live at 1..48, leaving pen 0 for the backdrop (which
 * HAM8 also uses for the display border). */
#define RAMP_RED    1
#define RAMP_WHITE 17
#define RAMP_TEMP  33
#define RAMP_SHADES 16

static LONG  vbase[MAX_VERTS][3];
static int   face[MAXFACES][3];
static LONG  fnorm[MAXFACES][3];
static UBYTE face_ramp[MAXFACES];
static int   num_verts = 0;
static int   num_faces = 0;

/* Shape descriptor. Each build_* function populates the globals
 * above and returns a name for the HUD. */
typedef void (*ShapeBuilder)(void);
typedef struct {
    const char *name;
    ShapeBuilder build;
} ShapeDef;
static int current_shape = 0;   /* index into g_shapes[] */

static LONG idist2(LONG *a, LONG *b)
{
    LONG dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2];
    return dx*dx + dy*dy + dz*dz;
}

static LONG isqrt(LONG v)
{
    LONG r = 0, bit = 1L << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/*
 * Compute face normals from the current vbase[] + face[] state.
 * Assumes each triangle's winding already faces outward (builders
 * must handle that). Called by build_shape after populating.
 */
static void compute_normals(void)
{
    int f;
    for (f = 0; f < num_faces; f++) {
        LONG *a = vbase[face[f][0]];
        LONG *b = vbase[face[f][1]];
        LONG *c = vbase[face[f][2]];
        LONG ax = b[0]-a[0], ay = b[1]-a[1], az = b[2]-a[2];
        LONG bx = c[0]-a[0], by = c[1]-a[1], bz = c[2]-a[2];
        LONG nx = (ay*bz - az*by) / 1000;
        LONG ny = (az*bx - ax*bz) / 1000;
        LONG nz = (ax*by - ay*bx) / 1000;
        LONG len = isqrt(nx*nx + ny*ny + nz*nz);
        if (len < 1) len = 1;
        fnorm[f][0] = (nx * ONE) / len;
        fnorm[f][1] = (ny * ONE) / len;
        fnorm[f][2] = (nz * ONE) / len;
    }
}

/* ---- Shape builders ----------------------------------------------- */

/* Icosahedron — kept from the original aga3d. Retains its
 * temperature-ramp shading. */
static const LONG ICOSA_V[12][3] = {
    {    0,  1000,  1618 }, {    0,  1000, -1618 },
    {    0, -1000,  1618 }, {    0, -1000, -1618 },
    { 1000,  1618,     0 }, {-1000,  1618,     0 },
    { 1000, -1618,     0 }, {-1000, -1618,     0 },
    { 1618,     0,  1000 }, { 1618,     0, -1000 },
    {-1618,     0,  1000 }, {-1618,     0, -1000 }
};

static void build_icosahedron(void)
{
    const LONG EDGE2 = 4000000;
    const LONG TOL   = EDGE2 + EDGE2/8;
    int i, j, k;

    num_verts = 12;
    for (i = 0; i < 12; i++) {
        vbase[i][0] = ICOSA_V[i][0];
        vbase[i][1] = ICOSA_V[i][1];
        vbase[i][2] = ICOSA_V[i][2];
    }
    num_faces = 0;
    for (i = 0; i < 12; i++)
        for (j = i+1; j < 12; j++) {
            if (idist2(vbase[i], vbase[j]) > TOL) continue;
            for (k = j+1; k < 12; k++) {
                LONG nx, ny, nz;
                LONG ax, ay, az, bx, by, bz, cx, cy, cz;
                if (idist2(vbase[j], vbase[k]) > TOL) continue;
                if (idist2(vbase[i], vbase[k]) > TOL) continue;

                ax = vbase[j][0]-vbase[i][0];
                ay = vbase[j][1]-vbase[i][1];
                az = vbase[j][2]-vbase[i][2];
                bx = vbase[k][0]-vbase[i][0];
                by = vbase[k][1]-vbase[i][1];
                bz = vbase[k][2]-vbase[i][2];
                nx = (ay*bz - az*by) / 1000;
                ny = (az*bx - ax*bz) / 1000;
                nz = (ax*by - ay*bx) / 1000;

                cx = vbase[i][0]+vbase[j][0]+vbase[k][0];
                cy = vbase[i][1]+vbase[j][1]+vbase[k][1];
                cz = vbase[i][2]+vbase[j][2]+vbase[k][2];

                face[num_faces][0] = i;
                if (nx*cx + ny*cy + nz*cz < 0) {
                    face[num_faces][1] = k;
                    face[num_faces][2] = j;
                } else {
                    face[num_faces][1] = j;
                    face[num_faces][2] = k;
                }
                face_ramp[num_faces] = RAMP_TEMP;
                num_faces++;
            }
        }
    compute_normals();
}

/* Axis-aligned cube — 8 verts, 12 triangles (6 quad faces split).
 * Uses the temperature ramp; alternating faces show off shading. */
static void build_cube(void)
{
    static const LONG CV[8][3] = {
        {-1400,-1400,-1400}, { 1400,-1400,-1400},
        { 1400, 1400,-1400}, {-1400, 1400,-1400},
        {-1400,-1400, 1400}, { 1400,-1400, 1400},
        { 1400, 1400, 1400}, {-1400, 1400, 1400}
    };
    /* Faces wound counter-clockwise when viewed from outside so
     * the cross product yields outward normals. */
    static const int CF[12][3] = {
        {0,3,2},{0,2,1},           /* -Z (back) */
        {5,6,7},{5,7,4},           /* +Z (front) */
        {4,7,3},{4,3,0},           /* -X (left) */
        {1,2,6},{1,6,5},           /* +X (right) */
        {3,7,6},{3,6,2},           /* +Y (top) */
        {4,0,1},{4,1,5},           /* -Y (bottom) */
    };
    int i;
    num_verts = 8;
    for (i = 0; i < 8; i++) {
        vbase[i][0] = CV[i][0]; vbase[i][1] = CV[i][1]; vbase[i][2] = CV[i][2];
    }
    num_faces = 12;
    for (i = 0; i < 12; i++) {
        face[i][0] = CF[i][0]; face[i][1] = CF[i][1]; face[i][2] = CF[i][2];
        face_ramp[i] = RAMP_TEMP;
    }
    compute_normals();
}

/*
 * Boing ball — classic Amiga red/white checkered sphere. Built by
 * latitude/longitude tessellation:
 *   N_LAT = 6 rings between the two poles (7 quad rows total)
 *   N_LON = 12 meridians
 * Each quad splits into two triangles; the pole caps are triangle
 * fans. Faces get RAMP_RED or RAMP_WHITE based on (lat+lon)&1.
 * Radius 1400 to match the cube visually.
 */
#define BOING_LAT 6
#define BOING_LON 12
#define BOING_R   1400

static void build_boing(void)
{
    int lat, lon, f;
    num_verts = 0;
    num_faces = 0;

    /* Pole vertices first. */
    int north = num_verts++;
    vbase[north][0] = 0; vbase[north][1] =  BOING_R; vbase[north][2] = 0;
    int south = num_verts++;
    vbase[south][0] = 0; vbase[south][1] = -BOING_R; vbase[south][2] = 0;

    /* Ring vertices: lat = 1..BOING_LAT-1, each with N_LON verts.
     * Latitude 0 = north pole, BOING_LAT = south pole. */
    int ring_start[BOING_LAT];
    for (lat = 1; lat < BOING_LAT; lat++) {
        int theta_deg = (lat * 180) / BOING_LAT;  /* 0..180, 0=top */
        LONG s_th = SIN(theta_deg);   /* fixed-point ONE-scaled */
        LONG c_th = COS(theta_deg);
        ring_start[lat] = num_verts;
        for (lon = 0; lon < BOING_LON; lon++) {
            int phi_deg = (lon * 360) / BOING_LON;
            LONG s_ph = SIN(phi_deg);
            LONG c_ph = COS(phi_deg);
            /* r = R * sin(theta); y = R * cos(theta). */
            LONG r  = (BOING_R * s_th) >> FP;
            LONG x  = (r * s_ph) >> FP;
            LONG z  = (r * c_ph) >> FP;
            LONG y  = (BOING_R * c_th) >> FP;
            vbase[num_verts][0] = x;
            vbase[num_verts][1] = y;
            vbase[num_verts][2] = z;
            num_verts++;
        }
    }

    /* Top cap — triangles from north pole to ring 1. */
    for (lon = 0; lon < BOING_LON; lon++) {
        int a = ring_start[1] + lon;
        int b = ring_start[1] + ((lon + 1) % BOING_LON);
        face[num_faces][0] = north;
        face[num_faces][1] = a;
        face[num_faces][2] = b;
        face_ramp[num_faces] = (lon & 1) ? RAMP_RED : RAMP_WHITE;
        num_faces++;
    }
    /* Middle rings — each quad = two triangles, checker per quad. */
    for (lat = 1; lat < BOING_LAT - 1; lat++) {
        for (lon = 0; lon < BOING_LON; lon++) {
            int a  = ring_start[lat]     + lon;
            int b  = ring_start[lat]     + ((lon + 1) % BOING_LON);
            int c  = ring_start[lat + 1] + ((lon + 1) % BOING_LON);
            int d  = ring_start[lat + 1] + lon;
            UBYTE ramp = ((lat + lon) & 1) ? RAMP_RED : RAMP_WHITE;
            face[num_faces][0] = a; face[num_faces][1] = c; face[num_faces][2] = b;
            face_ramp[num_faces++] = ramp;
            face[num_faces][0] = a; face[num_faces][1] = d; face[num_faces][2] = c;
            face_ramp[num_faces++] = ramp;
        }
    }
    /* Bottom cap. */
    for (lon = 0; lon < BOING_LON; lon++) {
        int a = ring_start[BOING_LAT - 1] + lon;
        int b = ring_start[BOING_LAT - 1] + ((lon + 1) % BOING_LON);
        face[num_faces][0] = south;
        face[num_faces][1] = b;
        face[num_faces][2] = a;
        face_ramp[num_faces] = ((BOING_LAT + lon) & 1) ? RAMP_RED : RAMP_WHITE;
        num_faces++;
    }
    compute_normals();
    /* Each middle-ring quad became two triangles (poles stayed as
     * single triangles). Average the two triangles' normals so the
     * shared diagonal edge lights identically on both sides —
     * otherwise every "square" shows a visible V-crease as the two
     * fshade[] values disagree. Poles are single triangles; skip. */
    int quad_start = BOING_LON;   /* skip top-cap fans */
    int quad_end   = num_faces - BOING_LON;  /* skip bottom-cap fans */
    for (f = quad_start; f < quad_end; f += 2) {
        LONG ax = (fnorm[f][0] + fnorm[f+1][0]) / 2;
        LONG ay = (fnorm[f][1] + fnorm[f+1][1]) / 2;
        LONG az = (fnorm[f][2] + fnorm[f+1][2]) / 2;
        fnorm[f  ][0] = fnorm[f+1][0] = ax;
        fnorm[f  ][1] = fnorm[f+1][1] = ay;
        fnorm[f  ][2] = fnorm[f+1][2] = az;
    }
}

static const ShapeDef g_shapes[] = {
    { "Icosahedron", build_icosahedron },
    { "Cube",        build_cube        },
    { "Boing Ball",  build_boing       },
};
#define NUM_SHAPES (sizeof(g_shapes) / sizeof(g_shapes[0]))

static void build_shape(int idx)
{
    if (idx < 0 || idx >= (int)NUM_SHAPES) return;
    current_shape = idx;
    g_shapes[idx].build();
}

/* ---- starfield ------------------------------------------------------- */
static WORD star_x[NUM_STARS], star_y[NUM_STARS];
static UBYTE star_pen[NUM_STARS];

static ULONG rngseed = 0x1234567UL;
static ULONG xrand(void)
{
    rngseed = rngseed * 1103515245UL + 12345UL;
    return (rngseed >> 8);
}

static void build_stars(void)
{
    int i;
    rngseed = 0x1234567UL;
    for (i = 0; i < NUM_STARS; i++) {
        star_x[i] = (WORD)(xrand() % W);
        star_y[i] = (WORD)(xrand() % H);
        star_pen[i] = (UBYTE)(STAR_BASE + (xrand() % STAR_COUNT));
    }
}

/* ---- palette --------------------------------------------------------- */
static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* 6-bit RGB lookup per palette pen, populated by set_palette. Lets
 * the draw loop synthesize per-face-vertex colour by adding vertex
 * intensity to the face's ramp base and reading the RGB directly. */
static UBYTE pal_r6[64], pal_g6[64], pal_b6[64];

/* Set pen to RGB (0..255 each) and record the 6-bit RGB in the
 * pal_*6 lookup tables so the draw loop can synthesize per-face-vertex
 * colour without touching the ViewPort. */
static void put_pen(struct ViewPort *vp, int pen, int r, int g, int b)
{
    r = clamp255(r); g = clamp255(g); b = clamp255(b);
    SetRGB32(vp, pen, (ULONG)r << 24, (ULONG)g << 24, (ULONG)b << 24);
    if (pen >= 0 && pen < 64) {
        pal_r6[pen] = (UBYTE)(r >> 2);
        pal_g6[pen] = (UBYTE)(g >> 2);
        pal_b6[pen] = (UBYTE)(b >> 2);
    }
}

static void set_palette(struct ViewPort *vp)
{
    int i;
    /* Backdrop — deep space blue. */
    put_pen(vp, BACKDROP_PEN, 6, 3, 18);
    /*
     * Object palette is three 16-shade ramps, so faces can pick
     * one of three "materials" (a face colour base) and add a
     * lighting-derived shade offset within that material.
     *
     *   Pens 0..15   RAMP_RED   dark cherry → bright red → pink
     *   Pens 16..31  RAMP_WHITE dark grey  → pure white
     *   Pens 32..47  RAMP_TEMP  indigo → purple → red → yellow → white
     *
     * All three ramps end near-white so a fully-lit face reads as
     * highlight regardless of material. This is what lets the
     * Boing ball's red/white checker keep its identity under
     * varying lighting without a bespoke shader.
     */
    for (i = 0; i < RAMP_SHADES; i++) {
        int t = (i * 255) / (RAMP_SHADES - 1);   /* 0..255 */
        /* Red ramp — dark cherry → saturated pure red (classic Boing). */
        int rr = 60 + (t * 195) / 255;
        int rg = (t * 40)  / 255;
        int rb = (t * 40)  / 255;
        put_pen(vp, RAMP_RED + i, rr, rg, rb);
        /* White ramp — dark grey → pure white (no colour tint). */
        int w = 30 + (t * 225) / 255;
        put_pen(vp, RAMP_WHITE + i, w, w, w);
        /* Temperature ramp — same formula as before but over 16 shades */
        int tr, tg, tb;
        if (t < 64) {
            tr =  20 + (t * 80) / 63;
            tg =   5 + (t * 15) / 63;
            tb =  40 + (t * 90) / 63;
        } else if (t < 128) {
            int u = t - 64;
            tr = 100 + (u * 140) / 63;
            tg =  20 + (u *  40) / 63;
            tb = 130 - (u *  90) / 63;
        } else if (t < 192) {
            int u = t - 128;
            tr = 240;
            tg =  60 + (u * 180) / 63;
            tb =  40 - (u *  30) / 63;
        } else {
            int u = t - 192;
            tr = 240 + (u *  15) / 63;
            tg = 240 + (u *  15) / 63;
            tb =  10 + (u * 240) / 63;
        }
        put_pen(vp, RAMP_TEMP + i, tr, tg, tb);
    }
    /* menu pens */
    put_pen(vp, MENU_BG_PEN, 200, 205, 220);
    put_pen(vp, MENU_TX_PEN,  10,  10,  25);
    /* stars */
    for (i = 0; i < STAR_COUNT; i++) {
        int v = 150 + i * 18;
        put_pen(vp, STAR_BASE + i, v, v, v + 20);
    }
}

static void twinkle_palette(struct ViewPort *vp, LONG frame)
{
    int i;
    for (i = 0; i < STAR_COUNT; i++) {
        LONG b = (SIN((frame * 6) + i * 60) + ONE) >> 1;   /* 0..ONE */
        int v = clamp255(120 + (int)((b * 135) >> FP));
        SetRGB32(vp, STAR_BASE + i, (ULONG)v << 24, (ULONG)v << 24, (ULONG)v << 24);
    }
}

/* ---- intuition resolution menu -------------------------------------- */
#define ITEM_H 12
#define ITEM_W 180
static struct Menu     menus[2];
static struct MenuItem mitem[NUM_MODES];
static struct IntuiText mtext[NUM_MODES];
static struct MenuItem  qitem;
static struct IntuiText qtext;

static void build_menus(void)
{
    int i;
    for (i = 0; i < NUM_MODES; i++) {
        mtext[i].FrontPen = MENU_TX_PEN;
        mtext[i].BackPen  = MENU_BG_PEN;
        mtext[i].DrawMode = JAM2;
        mtext[i].LeftEdge = 6;
        mtext[i].TopEdge  = 1;
        mtext[i].ITextFont = NULL;
        mtext[i].IText    = (UBYTE *)modes[i].name;
        mtext[i].NextText = NULL;

        mitem[i].NextItem      = (i < NUM_MODES-1) ? &mitem[i+1] : NULL;
        mitem[i].LeftEdge      = 0;
        mitem[i].TopEdge       = i * ITEM_H;
        mitem[i].Width         = ITEM_W;
        mitem[i].Height        = ITEM_H;
        mitem[i].Flags         = ITEMTEXT | ITEMENABLED | HIGHCOMP;
        mitem[i].MutualExclude = 0;
        mitem[i].ItemFill      = (APTR)&mtext[i];
        mitem[i].SelectFill    = NULL;
        mitem[i].Command       = 0;
        mitem[i].SubItem       = NULL;
        mitem[i].NextSelect    = 0;
    }

    qtext.FrontPen = MENU_TX_PEN;
    qtext.BackPen  = MENU_BG_PEN;
    qtext.DrawMode = JAM2;
    qtext.LeftEdge = 6;
    qtext.TopEdge  = 1;
    qtext.ITextFont = NULL;
    qtext.IText    = (UBYTE *)"Quit";
    qtext.NextText = NULL;

    qitem.NextItem      = NULL;
    qitem.LeftEdge      = 0;
    qitem.TopEdge       = 0;
    qitem.Width         = ITEM_W;
    qitem.Height        = ITEM_H;
    qitem.Flags         = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    qitem.MutualExclude = 0;
    qitem.ItemFill      = (APTR)&qtext;
    qitem.SelectFill    = NULL;
    qitem.Command       = 0;
    qitem.SubItem       = NULL;
    qitem.NextSelect    = 0;

    menus[0].NextMenu  = &menus[1];
    menus[0].LeftEdge  = 0;   menus[0].TopEdge = 0;
    menus[0].Width     = 110; menus[0].Height  = 10;
    menus[0].Flags     = MENUENABLED;
    menus[0].MenuName  = (BYTE *)"Screen";
    menus[0].FirstItem = &mitem[0];

    menus[1].NextMenu  = NULL;
    menus[1].LeftEdge  = 120; menus[1].TopEdge = 0;
    menus[1].Width     = 70;  menus[1].Height  = 10;
    menus[1].Flags     = MENUENABLED;
    menus[1].MenuName  = (BYTE *)"Demo";
    menus[1].FirstItem = &qitem;
}

/* ---- display resources (one open screen + double buffer) ------------ */
static struct {
    struct Screen      *scr;
    struct Window      *win;
    struct ViewPort    *vp;
    struct ScreenBuffer *sb[2];
    struct RastPort     mrp;
    struct AreaInfo     areaInfo;
    struct TmpRas       tmpRas;
    UBYTE              *areaBuf;
    PLANEPTR            tmpPlane;
    int                 cur;
} D;

static void close_display(void)
{
    if (D.win) ClearMenuStrip(D.win);

    if (D.tmpPlane) { FreeRaster(D.tmpPlane, W, H); D.tmpPlane = NULL; }
    if (D.areaBuf)  { FreeMem(D.areaBuf, 8L * 5L);  D.areaBuf  = NULL; }

    /* settle the display onto the screen's own bitmap (sb[0]) before freeing */
    if (D.sb[0]) {
        int tries = 0;
        while (!ChangeScreenBuffer(D.scr, D.sb[0]) && ++tries < 5) WaitTOF();
        WaitTOF(); WaitTOF();
    }
    if (D.sb[1]) { FreeScreenBuffer(D.scr, D.sb[1]); D.sb[1] = NULL; }
    if (D.sb[0]) { FreeScreenBuffer(D.scr, D.sb[0]); D.sb[0] = NULL; }

    if (D.win) { CloseWindow(D.win); D.win = NULL; }
    if (D.scr) { CloseScreen(D.scr); D.scr = NULL; }
}

/* returns 0 on success */
static int open_display(int idx)
{
    memset(&D, 0, sizeof(D));

    W    = modes[idx].w;
    H    = modes[idx].h;
    CXv  = W / 2;
    CYv  = H / 2;
    /* Keep the object's bounding sphere (radius ~1902, DIST 5000) fully on
     * screen for ANY rotation: max projected offset = R*FOV/(DIST-R) must stay
     * below H/2 with margin.  FOV 185 -> ~114px for a 256-tall screen (~120
     * budget), and it scales with H so wider/taller modes stay safe too. */
    FOVv = (H * 185) / 256;
    D.cur = 0;

    D.scr = OpenScreenTags(NULL,
        SA_Width,     W,
        SA_Height,    H,
        SA_Depth,     8,
        SA_DisplayID, modes[idx].modeID,
        SA_Title,     (ULONG)"aga3d",
        SA_ShowTitle, FALSE,
        SA_DetailPen, MENU_TX_PEN,
        SA_BlockPen,  MENU_BG_PEN,
        SA_Type,      CUSTOMSCREEN,
        TAG_DONE);
    if (!D.scr) { AB_E("OpenScreen failed (mode %ld)", (long)idx); return 1; }
    D.vp = &D.scr->ViewPort;

    D.win = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)D.scr,
        WA_Left, 0, WA_Top, 0,
        WA_Width, W, WA_Height, H,
        WA_Borderless, TRUE, WA_Backdrop, TRUE,
        WA_Activate, TRUE, WA_ReportMouse, TRUE,
        WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEMOVE | IDCMP_MENUPICK,
        TAG_DONE);
    if (!D.win) { AB_E("OpenWindow failed"); close_display(); return 1; }

    SetMenuStrip(D.win, &menus[0]);

    D.sb[0] = AllocScreenBuffer(D.scr, NULL, SB_SCREEN_BITMAP);
    D.sb[1] = AllocScreenBuffer(D.scr, NULL, 0);
    if (!D.sb[0] || !D.sb[1]) { AB_E("AllocScreenBuffer failed"); close_display(); return 1; }

    InitRastPort(&D.mrp);
    D.areaBuf  = AllocMem(8L * 5L, MEMF_CLEAR);
    D.tmpPlane = (PLANEPTR)AllocRaster(W, H);
    if (!D.areaBuf || !D.tmpPlane) { AB_E("workspace alloc failed"); close_display(); return 1; }
    InitArea(&D.areaInfo, D.areaBuf, 7);
    InitTmpRas(&D.tmpRas, D.tmpPlane, RASSIZE(W, H));
    D.mrp.AreaInfo = &D.areaInfo;
    D.mrp.TmpRas   = &D.tmpRas;

    set_palette(D.vp);
    build_stars();

    AB_I("display open: %ldx%ld modeID $%08lx", (long)W, (long)H,
         (unsigned long)modes[idx].modeID);
    return 0;
}

/* ---- per-frame render globals --------------------------------------- */
static WORD px[MAX_VERTS], py[MAX_VERTS];
static LONG vz[MAX_VERTS];
static int  vshade[MAX_VERTS];         /* per-vertex INTENSITY 0..RAMP_SHADES-1 */
static int  fvis[MAXFACES];
static int  fshade[MAXFACES];          /* per-face intensity for AreaFill */
static int  forder[MAXFACES];

/* Rasterizer selection — main loop cycles this on TAB. */
#include "rasterizer.h"
static const Rasterizer *g_raster_list[] = {
    &rasterizer_areafill,
    &rasterizer_gouraud,
    &rasterizer_ham_gouraud,
};
#define NUM_RASTERIZERS (sizeof(g_raster_list) / sizeof(g_raster_list[0]))
static int g_raster_idx = 0;

/* Boing bounce state — toggled by 'B'. When on, main() drives the
 * screen-space (bounce_x, bounce_y) offset per frame and passes it
 * to render_frame(). */
static int g_bounce = 0;

static void render_frame(LONG ax, LONG ay, LONG az, WORD bounce_x, WORD bounce_y)
{
    struct BitMap *bm = D.sb[D.cur]->sb_BitMap;
    struct RastPort *rp = &D.mrp;
    LONG cx = COS(ax), sx = SIN(ax);
    LONG cy = COS(ay), sy = SIN(ay);
    LONG cz = COS(az), sz = SIN(az);
    int f, n;
    LONG i;

    rp->BitMap = bm;

    /* background: single deep-space colour. HAM does support
     * gradients here too — HAM6 was famous for them — but the
     * modify-code streak on ship edges is very visible when the
     * backdrop varies, so we keep it flat and avoid the artefact. */
    SetAPen(rp, BACKDROP_PEN);
    RectFill(rp, 0, 0, W - 1, H - 1);
    /* starfield */
    for (i = 0; i < NUM_STARS; i++) {
        SetAPen(rp, star_pen[i]);
        WritePixel(rp, star_x[i], star_y[i]);
    }
    twinkle_palette(D.vp, frame_count);

    /* transform vertices. Also compute a per-vertex shade for
     * Gouraud — for shapes whose vertex positions sit on the surface
     * outward from origin (all three current shapes do), vbase[i]
     * doubles as an approximate normal at that vertex. Rotated Z
     * component then feeds a lighting intensity. */
    for (i = 0; i < num_verts; i++) {
        LONG x = vbase[i][0], y = vbase[i][1], z = vbase[i][2];
        LONG y1 = (y * cx - z * sx) >> FP;
        LONG z1 = (y * sx + z * cx) >> FP;
        LONG x2 = (x * cy + z1 * sy) >> FP;
        LONG z2 = (-x * sy + z1 * cy) >> FP;
        LONG x3 = (x2 * cz - y1 * sz) >> FP;
        LONG y3 = (x2 * sz + y1 * cz) >> FP;
        LONG zc = z2 + DIST;
        LONG sxp, syp;
        if (zc < 1) zc = 1;
        sxp = CXv + (x3 * FOVv) / zc + bounce_x;
        syp = CYv - (y3 * FOVv) / zc + bounce_y;
        /* Per-vertex INTENSITY only (0..RAMP_SHADES-1). Ramp base
         * is added at draw time based on each face's material. */
        int inten = (int)((-z2 * (RAMP_SHADES - 1)) / 1902);
        if (inten < 0)                  inten = 0;
        if (inten > RAMP_SHADES - 1)    inten = RAMP_SHADES - 1;
        vshade[i] = inten;   /* NB: now intensity, not a pen index */
        /* hard clamp: a layer-less RastPort does NOT clip, so out-of-bounds
         * AreaFill/TmpRas writes would corrupt chip RAM.  FOV keeps us in
         * bounds; this is the safety net. */
        if (sxp < 0) sxp = 0; else if (sxp > (LONG)W - 1) sxp = W - 1;
        if (syp < 0) syp = 0; else if (syp > (LONG)H - 1) syp = H - 1;
        px[i] = (WORD)sxp;
        py[i] = (WORD)syp;
        vz[i] = z2;
    }

    /* shade + backface cull. fshade[] is now INTENSITY (0..RAMP_SHADES-1);
     * the ramp base is added at draw time from face_ramp[]. */
    n = 0;
    for (f = 0; f < num_faces; f++) {
        LONG nx = fnorm[f][0], ny = fnorm[f][1], nz = fnorm[f][2];
        LONG nz1 = (ny * sx + nz * cx) >> FP;
        LONG rnz = (-nx * sy + nz1 * cy) >> FP;
        int light;
        if (rnz >= 0) { fvis[f] = 0; continue; }
        fvis[f] = 1;
        light = (int)(((-rnz) * (RAMP_SHADES - 1)) >> FP);
        if (light < 0) light = 0;
        if (light > RAMP_SHADES - 1) light = RAMP_SHADES - 1;
        fshade[f] = light;
        forder[n++] = f;
    }

    /* painter's sort: far (large z) first */
    for (f = 1; f < n; f++) {
        int key = forder[f], g = f - 1;
        LONG kz = vz[face[key][0]] + vz[face[key][1]] + vz[face[key][2]];
        while (g >= 0) {
            int gf = forder[g];
            LONG gz = vz[face[gf][0]] + vz[face[gf][1]] + vz[face[gf][2]];
            if (gz >= kz) break;
            forder[g + 1] = forder[g];
            g--;
        }
        forder[g + 1] = key;
    }

    /* draw faces — pluggable rasterizer. Each face picks its ramp base
     * (RAMP_RED / RAMP_WHITE / RAMP_TEMP) and adds the per-vertex
     * intensity to form a per-face-vertex pen. RGB lookups come from
     * the pal_*6 tables populated by set_palette. */
    const Rasterizer *R = g_raster_list[g_raster_idx];
    for (f = 0; f < n; f++) {
        int ff = forder[f];
        int a = face[ff][0], b = face[ff][1], c = face[ff][2];
        UBYTE base = face_ramp[ff];
        UBYTE pa = (UBYTE)(base + vshade[a]);
        UBYTE pb = (UBYTE)(base + vshade[b]);
        UBYTE pc = (UBYTE)(base + vshade[c]);
        RTri va = { px[a], py[a], pa, pal_r6[pa], pal_g6[pa], pal_b6[pa] };
        RTri vb = { px[b], py[b], pb, pal_r6[pb], pal_g6[pb], pal_b6[pb] };
        RTri vc = { px[c], py[c], pc, pal_r6[pc], pal_g6[pc], pal_b6[pc] };
        /* Flat backends only look at va.pen, so seed it with the
         * face's own intensity + ramp for a solid shade. */
        va.pen = (UBYTE)(base + fshade[ff]);
        R->draw(rp, &va, &vb, &vc);
    }

    /* Live indicator: which rasterizer + which frame. */
    SetAPen(rp, MENU_TX_PEN);
    SetBPen(rp, MENU_BG_PEN);
    SetDrMd(rp, JAM2);
    Move(rp, 4, 12);
    Text(rp, (STRPTR)R->name, strlen(R->name));
    Move(rp, 4, 24);
    {
        const char *sn = g_shapes[current_shape].name;
        Text(rp, (STRPTR)sn, strlen(sn));
    }
    Move(rp, 4, 36);
    Text(rp, (STRPTR)"TAB=raster S=shape B=bounce", 27);

    /* flip (blitter idle before showing the buffer) */
    WaitBlit();
    if (ChangeScreenBuffer(D.scr, D.sb[D.cur])) {
        WaitTOF();
        D.cur ^= 1;
    } else {
        /* swap rejected: still sync to vblank, keep D.cur so we redraw same buffer */
        WaitTOF();
    }
}

int main(void)
{
    int cur_mode = 0, req_mode = -1;
    LONG ax = 0, ay = 0, az = 0;     /* current angles, mouse-driven */

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    if (!IntuitionBase) return 1;
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!GfxBase) { CloseLibrary((struct Library *)IntuitionBase); return 1; }

    printf("aga3d v%s\n", VERSION);
    if (ab_init("aga3d") != 0) { printf("  Bridge: NOT FOUND\n"); bridge_ok = 0; }
    else { printf("  Bridge: CONNECTED\n"); bridge_ok = 1; }
    AB_I("aga3d v%s starting", VERSION);
    BOOL sound_ok = sound_init();
    if (!sound_ok) AB_W("audio.device unavailable — bounce will be silent");
    ab_register_var("frame_count", AB_TYPE_I32, &frame_count);
    ab_register_var("running", AB_TYPE_I32, &running);

    build_sintab();
    build_shape(0);
    build_menus();
    AB_I("geometry: %s — %ld verts, %ld faces",
         g_shapes[current_shape].name,
         (long)num_verts, (long)num_faces);

    if (open_display(cur_mode)) {
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    while (running) {
        struct IntuiMessage *msg;
        ULONG sig = SetSignal(0L, 0L);
        if (sig & SIGBREAKF_CTRL_C) { running = 0; break; }

        while ((msg = (struct IntuiMessage *)GetMsg(D.win->UserPort))) {
            ULONG cls = msg->Class;
            UWORD code = msg->Code;
            WORD  mx = msg->MouseX, my = msg->MouseY;
            ReplyMsg((struct Message *)msg);

            if (cls == IDCMP_RAWKEY) {
                if (code == 0x45) running = 0;                       /* ESC */
                else if (code == 0x40) req_mode = (cur_mode + 1) % NUM_MODES; /* SPACE: cycle res */
                else if (code == 0x42)                              /* TAB: cycle rasterizer */
                    g_raster_idx = (g_raster_idx + 1) % NUM_RASTERIZERS;
                else if (code == 0x21) {                            /* S: cycle shape */
                    build_shape((current_shape + 1) % NUM_SHAPES);
                    AB_I("shape -> %s (%ld verts, %ld faces)",
                         g_shapes[current_shape].name,
                         (long)num_verts, (long)num_faces);
                }
                else if (code == 0x35) {                            /* B: toggle bounce */
                    g_bounce = !g_bounce;
                    AB_I("bounce = %ld", (long)g_bounce);
                }
            } else if (cls == IDCMP_MOUSEMOVE) {
                if (mx < 0) mx = 0; else if (mx >= (WORD)W) mx = W - 1;
                if (my < 0) my = 0; else if (my >= (WORD)H) my = H - 1;
                ay = ((LONG)mx * 360) / W;               /* yaw   from X */
                ax = ((LONG)my * 360) / H;               /* pitch from Y */
            } else if (cls == IDCMP_MENUPICK) {
                UWORD mc = code;
                while (mc != MENUNULL) {
                    struct MenuItem *mi = ItemAddress(&menus[0], mc);
                    if (MENUNUM(mc) == 0) req_mode = ITEMNUM(mc);
                    else if (MENUNUM(mc) == 1) running = 0;
                    if (!mi) break;
                    mc = mi->NextSelect;
                }
            }
        }

        if (!running) break;

        /* resolution change requested -> reinitialise the display */
        if (req_mode >= 0 && req_mode != cur_mode) {
            int old = cur_mode;
            close_display();
            if (open_display(req_mode) != 0) {
                AB_E("mode %ld failed, reverting", (long)req_mode);
                if (open_display(old) != 0) { running = 0; break; }
            } else {
                cur_mode = req_mode;
            }
            req_mode = -1;
            continue;
        }
        req_mode = -1;

        WORD bx = 0, by = 0;
        static WORD prev_bx = 0, prev_dx = 0;
        if (g_bounce) {
            /* Classic Boing motion: sinusoidal horizontal drift with
             * a parabolic vertical bounce. abs(sin) gives the "hits
             * floor" cusps at frequency 2×; horizontal at half that
             * so each landing alternates side. */
            LONG hx = ((LONG)W * 30) / 100;      /* 30% of screen width */
            LONG hy = ((LONG)H * 25) / 100;      /* 25% of screen height */
            LONG sh = SIN((frame_count * 2) % 360);            /* -ONE..ONE */
            LONG sv = SIN((frame_count * 4) % 360);
            if (sv < 0) sv = -sv;                              /* abs → bounce */
            bx = (WORD)((sh * hx) >> FP);
            by = (WORD)(hy - ((sv * hy) >> FP));               /* drop from top */
            /* Spin the ball while bouncing so the checker rolls. */
            az = (frame_count * 4) % 360;

            /* Clang when horizontal motion reverses — the ball has
             * reached its leftmost / rightmost extent, i.e. "hit the
             * side". Detect by watching dx change sign. */
            WORD dx = bx - prev_bx;
            if (prev_dx != 0 && ((prev_dx > 0 && dx <= 0) || (prev_dx < 0 && dx >= 0)))
                sound_play_boing();
            prev_bx = bx;
            if (dx != 0) prev_dx = dx;
        } else {
            prev_bx = 0; prev_dx = 0;
        }
        render_frame(ax, ay, az, bx, by);
        frame_count++;
        if (bridge_ok) ab_poll();
    }

    AB_I("aga3d shutting down (%ld frames)", (long)frame_count);
    close_display();
    sound_cleanup();
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    printf("aga3d finished.\n");
    return 0;
}
