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

/* ---- icosahedron geometry ------------------------------------------- */
static LONG vbase[12][3] = {
    {    0,  1000,  1618 }, {    0,  1000, -1618 },
    {    0, -1000,  1618 }, {    0, -1000, -1618 },
    { 1000,  1618,     0 }, {-1000,  1618,     0 },
    { 1000, -1618,     0 }, {-1000, -1618,     0 },
    { 1618,     0,  1000 }, { 1618,     0, -1000 },
    {-1618,     0,  1000 }, {-1618,     0, -1000 }
};

#define MAXFACES 20
static int   face[MAXFACES][3];
static LONG  fnorm[MAXFACES][3];
static int   num_faces = 0;

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

static void build_faces(void)
{
    const LONG EDGE2 = 4000000;
    const LONG TOL   = EDGE2 + EDGE2/8;
    int i, j, k;

    for (i = 0; i < 12; i++)
        for (j = i+1; j < 12; j++) {
            if (idist2(vbase[i], vbase[j]) > TOL) continue;
            for (k = j+1; k < 12; k++) {
                LONG nx, ny, nz, len;
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
                    nx = -nx; ny = -ny; nz = -nz;
                } else {
                    face[num_faces][1] = j;
                    face[num_faces][2] = k;
                }

                len = isqrt(nx*nx + ny*ny + nz*nz);
                if (len < 1) len = 1;
                fnorm[num_faces][0] = (nx * ONE) / len;
                fnorm[num_faces][1] = (ny * ONE) / len;
                fnorm[num_faces][2] = (nz * ONE) / len;
                num_faces++;
            }
        }
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

static void set_palette(struct ViewPort *vp)
{
    int i;
    /* Backdrop — deep space blue. */
    SetRGB32(vp, BACKDROP_PEN,
             (ULONG)6 << 24, (ULONG)3 << 24, (ULONG)18 << 24);
    /*
     * Object shading ramp — 48 shades on a "cool → hot" temperature
     * gradient. Unlit faces sit at index 0 (deep indigo), lit faces
     * climb through purple → magenta → red → orange → yellow → white.
     * The full ramp spans a much wider colour space than aga3d's
     * single-hue lighten-only ramp because HAM8 doesn't constrain
     * us to pens 0..255 as our ONLY colours — the extra breathing
     * room in the palette layout is what we're using here.
     */
    for (i = 0; i < OBJ_COUNT; i++) {
        int t = (i * 255) / (OBJ_COUNT - 1);   /* 0..255 */
        int r, g, b;
        if (t < 64) {
            /* Indigo → purple: R and B climb, G asleep */
            r =  20 + (t * 80) / 63;
            g =   5 + (t * 15) / 63;
            b =  40 + (t * 90) / 63;
        } else if (t < 128) {
            int u = t - 64;
            /* Purple → red: R keeps climbing, B falls */
            r = 100 + (u * 140) / 63;
            g =  20 + (u * 40)  / 63;
            b = 130 - (u * 90)  / 63;
        } else if (t < 192) {
            int u = t - 128;
            /* Red → orange → yellow: G climbs */
            r = 240;
            g =  60 + (u * 180) / 63;
            b =  40 - (u * 30)  / 63;
        } else {
            int u = t - 192;
            /* Yellow → white: B climbs, R/G already near max */
            r = 240 + (u * 15)  / 63;
            g = 240 + (u * 15)  / 63;
            b =  10 + (u * 240) / 63;
        }
        SetRGB32(vp, OBJ_BASE + i,
                 (ULONG)clamp255(r) << 24,
                 (ULONG)clamp255(g) << 24,
                 (ULONG)clamp255(b) << 24);
    }
    /* menu pens */
    SetRGB32(vp, MENU_BG_PEN, (ULONG)200 << 24, (ULONG)205 << 24, (ULONG)220 << 24);
    SetRGB32(vp, MENU_TX_PEN, (ULONG)10  << 24, (ULONG)10  << 24, (ULONG)25  << 24);
    /* stars */
    for (i = 0; i < STAR_COUNT; i++) {
        int v = 150 + i * 18;
        SetRGB32(vp, STAR_BASE + i, (ULONG)v << 24, (ULONG)v << 24, (ULONG)clamp255(v+20) << 24);
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
static WORD px[12], py[12];
static LONG vz[12];
static int  fvis[MAXFACES];
static int  fshade[MAXFACES];
static int  forder[MAXFACES];

static void render_frame(LONG ax, LONG ay, LONG az)
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

    /* transform vertices */
    for (i = 0; i < 12; i++) {
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
        sxp = CXv + (x3 * FOVv) / zc;
        syp = CYv - (y3 * FOVv) / zc;
        /* hard clamp: a layer-less RastPort does NOT clip, so out-of-bounds
         * AreaFill/TmpRas writes would corrupt chip RAM.  FOV keeps us in
         * bounds; this is the safety net. */
        if (sxp < 0) sxp = 0; else if (sxp > (LONG)W - 1) sxp = W - 1;
        if (syp < 0) syp = 0; else if (syp > (LONG)H - 1) syp = H - 1;
        px[i] = (WORD)sxp;
        py[i] = (WORD)syp;
        vz[i] = z2;
    }

    /* shade + backface cull */
    n = 0;
    for (f = 0; f < num_faces; f++) {
        LONG nx = fnorm[f][0], ny = fnorm[f][1], nz = fnorm[f][2];
        LONG nz1 = (ny * sx + nz * cx) >> FP;
        LONG rnz = (-nx * sy + nz1 * cy) >> FP;
        int light;
        if (rnz >= 0) { fvis[f] = 0; continue; }
        fvis[f] = 1;
        light = (int)(((-rnz) * (OBJ_COUNT - 1)) >> FP);
        if (light < 0) light = 0;
        if (light > OBJ_COUNT - 1) light = OBJ_COUNT - 1;
        fshade[f] = OBJ_BASE + light;
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

    /* draw faces */
    for (f = 0; f < n; f++) {
        int ff = forder[f];
        int a = face[ff][0], b = face[ff][1], c = face[ff][2];
        SetAPen(rp, (UBYTE)fshade[ff]);
        AreaMove(rp, px[a], py[a]);
        AreaDraw(rp, px[b], py[b]);
        AreaDraw(rp, px[c], py[c]);
        AreaEnd(rp);
    }

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
    ab_register_var("frame_count", AB_TYPE_I32, &frame_count);
    ab_register_var("running", AB_TYPE_I32, &running);

    build_sintab();
    build_faces();
    build_menus();
    AB_I("geometry: %ld faces", (long)num_faces);

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

        render_frame(ax, ay, az);
        frame_count++;
        if (bridge_ok) ab_poll();
    }

    AB_I("aga3d shutting down (%ld frames)", (long)frame_count);
    close_display();
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    printf("aga3d finished.\n");
    return 0;
}
