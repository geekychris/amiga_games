// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * os4c_screen.c - a classic game's custom screen on AmigaOS 4 (see
 * os4_compat.h for what the game sees).
 *
 * OpenScreenTags() makes a stand-in Screen: its bitmaps are planar
 * bitmaps in RAM, which graphics.library draws into in software and the
 * game may poke directly (Planes[]).  The screen is shown in a window on
 * the Workbench, scaled up by a whole number (2 if it fits): each time it
 * is shown (ChangeScreenBuffer, or WaitTOF for a game without double
 * buffering) the planes are turned into pen numbers, the pens into the
 * Workbench's pixel format through the palette, into a bitmap in RAM
 * like the Workbench's, which is then blitted into the window (drawing
 * into video memory is slow under QEMU; see amiga3d/os4_display.c).
 *
 * WaitTOF() waits for the next tick of a 50 Hz clock kept on
 * timer.device, as a PAL vertical blank would.
 *
 * The scale can be set with the environment variable OS4C_SCALE (1..4).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <proto/utility.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "paula.h"
#include "bridge_client.h"

struct CIA ciaa, ciab;

struct Message *oc_GetMsg(struct MsgPort *port);
void oc_present(void);
void oc_LoadRGB32(struct ViewPort *vp, const ULONG *t);
static void wait_tick(void);

#define MAXTAGS 64

struct oc_screen {
    struct Screen s;                    /* first: what the game gets */
    struct RasInfo ri;
    struct BitMap *bm0;                 /* the screen's own bitmap */
    int own_bm0;
    struct BitMap *front;               /* the bitmap shown */
    int w, h, depth;
    int dbuf;                           /* the game uses ChangeScreenBuffer */
};

struct oc_sbuf {
    struct ScreenBuffer sb;
    int own;
};

static struct oc_screen *cur;           /* the one stand-in screen shown */
static struct Window *win;              /* the Workbench window showing it */
static int win_given;                   /* the game's OpenWindow calls on it */
static ULONG win_idcmp;
static int scale = 2;
static char title[80];

static ULONG pal[256];                  /* 0x00RRGGBB */
static ULONG lut[256];                  /* in fb's pixel format */
static int lut_dirty = 1;

static struct BitMap *fb;               /* like the Workbench's, in RAM */
static int fb_fmt;                      /* 0 ARGB, 1 BGRA, 2 RGBA, 3 RGB565, 4 RGB565PC, -1 none */
static ULONG *argb;                     /* WritePixelArray fallback */

static UBYTE *line;                     /* pens of one row */
static int line_w;
static unsigned long long spread[256];  /* bit j of a byte -> byte (7 - j) */

extern volatile ULONG oc_audio_peak, oc_audio_blocks;

static struct TextFont *topaz;
static struct TextAttr topaz_attr = { (STRPTR)"topaz.font", 8, 0, 0 };

static struct MsgPort *tport;
static struct TimeRequest *treq;
struct TimerIFace *ITimer;
static unsigned long long t0;
static int waited;                      /* a tick waited for since the last ChangeScreenBuffer */

static int mouse_x, mouse_y, mouse_valid;
static unsigned long long stat_t0, stat_cost;   /* frame rate, logged through the bridge */
static ULONG stat_frames;
static unsigned long long stat_work, last_wake, stat_late;
static ULONG stat_waits, stat_missed;
static unsigned long long stat_sleep;   /* the game's time between waits */
static UBYTE joy0x, joy0y;

/* ---- time ---- */

static int timer_open(void)
{
    if (ITimer) return 1;
    tport = AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (!tport) return 0;
    treq = AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_ReplyPort, tport,
                              ASOIOR_Size, sizeof(struct TimeRequest), TAG_DONE);
    if (!treq) return 0;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)treq, 0) != 0) {
        FreeSysObject(ASOT_IOREQUEST, treq);
        treq = 0;
        return 0;
    }
    ITimer = (struct TimerIFace *)GetInterface((struct Library *)treq->Request.io_Device, "main", 1, NULL);
    return ITimer != 0;
}

static void timer_close(void)
{
    if (ITimer) DropInterface((struct Interface *)ITimer);
    ITimer = 0;
    if (treq) {
        if (treq->Request.io_Device) CloseDevice((struct IORequest *)treq);
        FreeSysObject(ASOT_IOREQUEST, treq);
    }
    treq = 0;
    if (tport) FreeSysObject(ASOT_PORT, tport);
    tport = 0;
}

static unsigned long long now_us(void)
{
    struct TimeVal tv;
    GetUpTime(&tv);
    return (unsigned long long)tv.Seconds * 1000000ULL + tv.Microseconds;
}

static void sleep_us(ULONG us)
{
    if (!us) return;
    treq->Request.io_Command = TR_ADDREQUEST;
    treq->Time.Seconds = us / 1000000;
    treq->Time.Microseconds = us % 1000000;
    DoIO((struct IORequest *)treq);
}

/* ---- bitmaps ---- */

static struct BitMap *alloc_planar(int w, int h, int depth)
{
    struct BitMap *bm = AllocVecTags(sizeof(struct BitMap), AVT_ClearWithValue, 0, TAG_DONE);
    int p;
    if (!bm) return 0;
    InitBitMap(bm, depth, w, h);
    for (p = 0; p < depth; p++) {
        bm->Planes[p] = AllocVecTags((ULONG)bm->BytesPerRow * h, AVT_Type, MEMF_SHARED,
                                     AVT_Alignment, 16, AVT_ClearWithValue, 0, TAG_DONE);
        if (!bm->Planes[p]) {
            while (--p >= 0) FreeVec(bm->Planes[p]);
            FreeVec(bm);
            return 0;
        }
    }
    return bm;
}

static void free_planar(struct BitMap *bm)
{
    int p;
    if (!bm) return;
    for (p = 0; p < bm->Depth; p++) if (bm->Planes[p]) FreeVec(bm->Planes[p]);
    FreeVec(bm);
}

/* The screen's bitmaps: planar, as on the Amiga (a game may write into
 * Planes[] itself); or, built with OS4C_CHUNKY=1, 8-bit chunky (CLUT)
 * bitmaps in RAM.  Under QEMU graphics.library drew into both at the same
 * speed (Jump Quest, Boing Ball).  Either kind is shown. */
#ifdef OS4C_CHUNKY
#define CHUNKY 1
#else
#define CHUNKY 0
#endif

static struct BitMap *alloc_screen_bm(int w, int h, int depth)
{
    if (!CHUNKY) return alloc_planar(w, h, depth);
    return AllocBitMapTags(w, h, 8, BMATags_PixelFormat, PIXF_CLUT, BMATags_Clear, TRUE,
                           BMATags_Displayable, FALSE, TAG_DONE);
}

static void free_screen_bm(struct BitMap *bm)
{
    if (!bm) return;
    if (CHUNKY) FreeBitMap(bm);
    else free_planar(bm);
}

static int is_chunky(struct BitMap *bm)
{
    return GetBitMapAttr(bm, BMA_ISRTG) && GetBitMapAttr(bm, BMA_BYTESPERPIXEL) == 1;
}

/* ---- the window and its picture ---- */

static void free_fb(void)
{
    if (fb) FreeBitMap(fb);
    fb = 0;
    free(argb);
    argb = 0;
}

static void make_fb(void)
{
    ULONG fmt;
    int bw = cur->w * scale, bh = cur->h * scale;
    free_fb();
    fb_fmt = -1;
    lut_dirty = 1;
    fb = AllocBitMapTags(bw, bh, GetBitMapAttr(win->RPort->BitMap, BMA_DEPTH),
                         BMATags_Friend, (ULONG)win->RPort->BitMap, BMATags_Displayable, FALSE, TAG_DONE);
    if (fb) {
        fmt = GetBitMapAttr(fb, BMA_PIXELFORMAT);
        if (fmt == PIXF_A8R8G8B8) fb_fmt = 0;
        else if (fmt == PIXF_B8G8R8A8) fb_fmt = 1;
        else if (fmt == PIXF_R8G8B8A8) fb_fmt = 2;
        else if (fmt == PIXF_R5G6B5) fb_fmt = 3;
        else if (fmt == PIXF_R5G6B5PC) fb_fmt = 4;
        else { FreeBitMap(fb); fb = 0; }
    }
    if (!fb) argb = malloc((size_t)bw * bh * 4);
}

/* a 0x00RRGGBB colour in fb's pixel format */
static ULONG fmt_color(ULONG c)
{
    ULONG r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255, v;
    switch (fb_fmt) {
    case 1: return b << 24 | g << 16 | r << 8 | 0xFF;
    case 2: return r << 24 | g << 16 | b << 8 | 0xFF;
    case 3: return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
    case 4: v = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3); return ((v & 0xFF) << 8) | (v >> 8);
    default: return 0xFF000000UL | c;
    }
}

static void make_lut(void)
{
    int i;
    int mask = cur ? (1 << cur->depth) - 1 : 255;    /* pens beyond the depth wrap, as in planes */
    for (i = 0; i < 256; i++) lut[i] = fmt_color(pal[i & mask]);
    lut_dirty = 0;
}

/* ---- user copper lists ---- */

#define COP_MAGIC 0x4F434350UL          /* "OCCP" */

struct oc_cop {
    ULONG magic;
    int n, cap, line;
    struct { WORD line; UWORD reg; UWORD val; } e[1];
};

void oc_CInit(struct UCopList *c, LONG n)
{
    struct oc_cop *cop;
    if (!c) return;
    if (n < 1) n = 1;
    cop = AllocVecTags(sizeof(*cop) + sizeof(cop->e[0]) * n, AVT_ClearWithValue, 0, TAG_DONE);
    if (cop) {
        cop->magic = COP_MAGIC;
        cop->cap = (int)n + 1;
    }
    c->FirstCopList = c->CopList = (struct CopList *)cop;
}

static struct oc_cop *cop_of(struct UCopList *c)
{
    struct oc_cop *cop = c ? (struct oc_cop *)c->FirstCopList : 0;
    return cop && cop->magic == COP_MAGIC ? cop : 0;
}

void oc_CWait(struct UCopList *c, LONG v, LONG h)
{
    struct oc_cop *cop = cop_of(c);
    (void)h;
    if (cop) cop->line = (int)v;
}

void oc_CMove(struct UCopList *c, volatile void *reg, LONG val)
{
    struct oc_cop *cop = cop_of(c);
    LONG off = (LONG)((volatile UBYTE *)reg - (volatile UBYTE *)&custom);
    if (!cop || cop->n >= cop->cap || off < 0 || off >= 0x200) return;
    cop->e[cop->n].line = (WORD)cop->line;
    cop->e[cop->n].reg = (UWORD)off;
    cop->e[cop->n].val = (UWORD)val;
    cop->n++;
}

void oc_FreeCopList(struct CopList *cl)
{
    struct oc_cop *cop = (struct oc_cop *)cl;
    if (cop && cop->magic == COP_MAGIC) { cop->magic = 0; FreeVec(cop); return; }
    if (cl) FreeCopList(cl);
}

static int pick_scale(int w, int h)
{
    char buf[8];
    int s = 2;
    struct Screen *wb;
    if (GetVar("OS4C_SCALE", buf, sizeof(buf), 0) > 0 && buf[0] >= '1' && buf[0] <= '4') s = buf[0] - '0';
    wb = LockPubScreen(NULL);
    if (wb) {
        while (s > 1 && (w * s > wb->Width - 16 || h * s > wb->Height - wb->BarHeight - 24)) s--;
        UnlockPubScreen(NULL, wb);
    }
    return s;
}

static int open_win(void)
{
    struct Screen *wb = LockPubScreen(NULL);
    int iw = cur->w * scale, ih = cur->h * scale, l = 0, t = 0;
    if (wb) {
        l = (wb->Width - iw) / 2 - 4;
        t = (wb->Height - ih) / 2 - wb->BarHeight;
        if (l < 0) l = 0;
        if (t < 0) t = 0;
    }
    win = OpenWindowTags(NULL, WA_Title, (ULONG)title, WA_Left, l, WA_Top, t,
                         WA_InnerWidth, iw, WA_InnerHeight, ih,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                         WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_GimmeZeroZero, TRUE,
                         WA_PubScreen, (ULONG)wb, WA_IDCMP, 0, TAG_DONE);
    if (wb) UnlockPubScreen(NULL, wb);
    if (!win) return 0;
    win_given = 0;
    win_idcmp = 0;
    make_fb();
    return 1;
}

/* the stand-in screen's front bitmap into the window */
static void present(void);

/* shows the picture; every 10 s logs the frames shown a second and what
 * showing them costs */
void oc_present(void)
{
    unsigned long long t = ITimer ? now_us() : 0;
    if (!cur || !win) return;
    present();
    if (!ITimer) return;
    stat_cost += now_us() - t;
    stat_frames++;
    if (!stat_t0) { stat_t0 = t; stat_frames = 0; stat_cost = 0; stat_work = 0; }
    else if (t - stat_t0 >= 10000000ULL) {
        ULONG el = (ULONG)((t - stat_t0) / 1000);
        char msg[200];
        snprintf(msg, sizeof(msg), "%ld.%ld fps, %ld us a frame to show (x%ld), %ld us of work, %ld.%ld waits of %ld us a frame, %ld us late (%ld missed); sound: %ld blocks, peak %ld",
                 (long)(stat_frames * 1000 / el), (long)(stat_frames * 10000 / el % 10),
                 (long)(stat_cost / (stat_frames ? stat_frames : 1)), (long)scale,
                 (long)(stat_work / (stat_frames ? stat_frames : 1)),
                 (long)(stat_waits * 10 / (stat_frames ? stat_frames : 1) / 10), (long)(stat_waits * 10 / (stat_frames ? stat_frames : 1) % 10),
                 (long)(stat_sleep / (stat_waits ? stat_waits : 1)), (long)(stat_late / (stat_waits ? stat_waits : 1)), (long)stat_missed,
                 (long)oc_audio_blocks, (long)oc_audio_peak);
        AB_I("os4c: %s", msg);
        SetVar("OS4C_FPS", msg, -1, GVF_GLOBAL_ONLY);   /* for games without the bridge */
        oc_audio_blocks = 0;
        oc_audio_peak = 0;
        stat_t0 = t;
        stat_frames = 0;
        stat_cost = 0;
        stat_work = 0;
        stat_late = 0;
        stat_sleep = 0;
        stat_waits = 0;
        stat_missed = 0;
    }
}

static void present(void)
{
    struct BitMap *bm;
    int w, h, rx, ry, y, x, k, bpr, ncols, x0, sx;
    APTR lock = 0, base = 0;
    ULONG mod = 0;
    struct oc_cop *cop;
    int ce = 0, chunky, bmw, rows, step = (1 << cur->depth);
    APTR slock = 0;
    UBYTE *sbase = 0;
    ULONG sbpr = 0;
    static ULONG flut[256];
    bm = cur->front;
    w = cur->w;
    h = cur->h;
    rx = cur->ri.RxOffset;
    ry = cur->ri.RyOffset;
    chunky = is_chunky(bm);
    bpr = bm->BytesPerRow;
    bmw = chunky ? (int)GetBitMapAttr(bm, BMA_WIDTH) : bpr * 8;
    rows = chunky ? (int)GetBitMapAttr(bm, BMA_HEIGHT) : bm->Rows;
    if (rx < 0) rx = 0;
    if (ry < 0) ry = 0;
    if (rx > bmw - w) rx = bmw - w;
    if (rx < 0) rx = 0;
    if (ry > rows - h) ry = rows - h;
    if (ry < 0) ry = 0;
    if (lut_dirty) make_lut();
    x0 = rx >> 3;
    ncols = ((rx + w + 7) >> 3) - x0;
    if (line_w < ncols * 8) {
        free(line);
        line = malloc((size_t)ncols * 8 + 8);
        line_w = line ? ncols * 8 : 0;
        if (!line) return;
    }
    if (fb) {
        lock = LockBitMapTags(fb, LBM_BaseAddress, (ULONG)&base, LBM_BytesPerRow, (ULONG)&mod, TAG_DONE);
        if (!lock || !base) { if (lock) UnlockBitMap(lock); return; }
    } else if (argb) {
        base = argb;
        mod = (ULONG)w * scale * 4;
    } else return;
    if (chunky) {
        slock = LockBitMapTags(bm, LBM_BaseAddress, (ULONG)&sbase, LBM_BytesPerRow, (ULONG)&sbpr, TAG_DONE);
        if (!slock || !sbase) {
            if (slock) UnlockBitMap(slock);
            if (lock) UnlockBitMap(lock);
            return;
        }
    }
    sx = rx & 7;
    cop = cop_of(cur->s.ViewPort.UCopIns);
    if (cop) for (k = 0; k < 256; k++) flut[k] = lut[k];
    for (y = 0; y < h && y + ry < rows; y++) {
        const ULONG *L = lut;
        if (cop) {                      /* the copper's colour moves down to this line */
            while (ce < cop->n && cop->e[ce].line <= y) {
                UWORD r = cop->e[ce].reg, v = cop->e[ce].val;
                if (r >= 0x180 && r < 0x1C0) {
                    ULONG c = fmt_color(((v >> 8) & 15) * 0x110000UL + ((v >> 4) & 15) * 0x1100UL + (v & 15) * 0x11UL);
                    for (k = (r - 0x180) >> 1; k < 256; k += step) flut[k] = c;
                }
                ce++;
            }
            L = flut;
        }
        const UBYTE *pens;
        if (chunky) pens = sbase + (ULONG)(y + ry) * sbpr + rx;
        else {                          /* planes to pens, 8 pixels at a time */
            unsigned long long *l64 = (unsigned long long *)line;
            ULONG off = (ULONG)(y + ry) * bpr + x0;
            int p, depth = bm->Depth > 8 ? 8 : bm->Depth;
            for (x = 0; x < ncols; x++) l64[x] = 0;
            for (p = 0; p < depth; p++) {
                const UBYTE *src = (const UBYTE *)bm->Planes[p];
                if (!src || src == (const UBYTE *)-1) continue;
                src += off;
                for (x = 0; x < ncols; x++) l64[x] |= spread[src[x]] << p;
            }
            pens = line + sx;
        }
        if (fb_fmt >= 3) {
            UWORD *row = (UWORD *)((UBYTE *)base + (ULONG)y * scale * mod), *d = row;
            if (scale == 2)
                for (x = 0; x < w; x++) { UWORD c = (UWORD)L[pens[x]]; d[0] = c; d[1] = c; d += 2; }
            else
                for (x = 0; x < w; x++) { UWORD c = (UWORD)L[pens[x]]; for (k = 0; k < scale; k++) *d++ = c; }
            for (k = 1; k < scale; k++) {
                ULONG *s32 = (ULONG *)row, *d32 = (ULONG *)((UBYTE *)row + k * mod);
                int n = w * scale / 2;
                for (x = 0; x < n; x++) d32[x] = s32[x];
            }
        } else {
            ULONG *row = (ULONG *)((UBYTE *)base + (ULONG)y * scale * mod), *d = row;
            if (scale == 2)
                for (x = 0; x < w; x++) { ULONG c = L[pens[x]]; d[0] = c; d[1] = c; d += 2; }
            else
                for (x = 0; x < w; x++) { ULONG c = L[pens[x]]; for (k = 0; k < scale; k++) *d++ = c; }
            for (k = 1; k < scale; k++) {
                ULONG *d2 = (ULONG *)((UBYTE *)row + k * mod);
                int n = w * scale;
                for (x = 0; x < n; x++) d2[x] = row[x];
            }
        }
    }
    if (slock) UnlockBitMap(slock);
    if (fb) {
        UnlockBitMap(lock);
        BltBitMapRastPort(fb, 0, 0, win->RPort, 0, 0, w * scale, h * scale, 0xC0);
    } else {
        WritePixelArray((uint8 *)argb, 0, 0, w * scale * 4, PIXF_A8R8G8B8, win->RPort, 0, 0, w * scale, h * scale);
    }
}

/* the mouse in the window, counted into joy0dat as the port would */
static void poll_mouse(void)
{
    int mx, my;
    if (!win) return;
    mx = win->GZZMouseX / scale;
    my = win->GZZMouseY / scale;
    if (mouse_valid) {
        joy0x = (UBYTE)(joy0x + (mx - mouse_x));
        joy0y = (UBYTE)(joy0y + (my - mouse_y));
        custom.joy0dat = (UWORD)(joy0y << 8 | joy0x);
    }
    mouse_x = mx;
    mouse_y = my;
    mouse_valid = 1;
}

/* ---- screens ---- */

static void set_pal(int n, ULONG r8, ULONG g8, ULONG b8)
{
    if (n < 0 || n > 255) return;
    pal[n] = (r8 & 255) << 16 | (g8 & 255) << 8 | (b8 & 255);
    lut_dirty = 1;
}

static int is_ours_vp(struct ViewPort *vp)
{
    return cur && vp == &cur->s.ViewPort;
}

static void hw_defaults(void)
{
    int i;
    ciaa.ciapra = 0xFF;                 /* fire buttons up (active low) */
    ciab.ciapra = 0xFF;
    custom.potinp = 0xFF00;             /* right buttons up */
    custom.joy0dat = custom.joy1dat = 0;
    for (i = 0; i < 256; i++) {
        unsigned long long v = 0;
        int j;
        for (j = 0; j < 8; j++) if (i & (1 << j)) v |= 1ULL << (8 * j);
        spread[i] = v;
    }
}

struct Screen *oc_OpenScreenTagList(const struct NewScreen *ns, const struct TagItem *tags)
{
    struct oc_screen *o;
    struct BitMap *game_bm = 0;
    struct TagItem *t, *state;
    ULONG id = 0;
    int w = 0, h = 0, depth = 1, i;
    const char *ti = 0;
    char prog[64];
    const struct ColorSpec *cs = 0;
    const ULONG *c32 = 0;
    const struct Rectangle *dclip = 0;
    int vw, vh;                         /* the part shown */

    if (cur) return 0;                  /* one at a time */
    if (ns) {
        w = ns->Width;
        h = ns->Height;
        depth = ns->Depth;
        id = ns->ViewModes;
        if (ns->DefaultTitle) ti = (const char *)ns->DefaultTitle;
        if (ns->Type & CUSTOMBITMAP) game_bm = ns->CustomBitMap;
    }
    state = (struct TagItem *)tags;
    while ((t = NextTagItem(&state)) != 0) {
        switch (t->ti_Tag) {
        case SA_Width: w = (int)t->ti_Data; break;
        case SA_Height: h = (int)t->ti_Data; break;
        case SA_Depth: depth = (int)t->ti_Data; break;
        case SA_DisplayID: id = t->ti_Data; break;
        case SA_Title: if (t->ti_Data) ti = (const char *)t->ti_Data; break;
        case SA_BitMap: game_bm = (struct BitMap *)t->ti_Data; break;
        case SA_Colors: cs = (const struct ColorSpec *)t->ti_Data; break;
        case SA_Colors32: c32 = (const ULONG *)t->ti_Data; break;
        case SA_DClip: dclip = (const struct Rectangle *)t->ti_Data; break;
        }
    }
    if (!ti) {                          /* no title: the program's name */
        char *e;
        ti = "Amiga";
        if (GetCliProgramName(prog, sizeof(prog)) && prog[0]) {
            ti = (const char *)FilePart(prog);
            if ((e = strstr(ti, "_os4")) != 0) *e = 0;
        }
    }
    if (w <= 0) w = (id & HIRES) ? 640 : 320;
    if (h <= 0) h = (id & LACE) ? 512 : 256;
    if (game_bm) {
        if (w > game_bm->BytesPerRow * 8) w = game_bm->BytesPerRow * 8;
        if (h > game_bm->Rows) h = game_bm->Rows;
        depth = game_bm->Depth;
    }
    if (depth < 1) depth = 1;
    if (depth > 8) depth = 8;
    /* a screen bigger than the display (for scrolling) shows the standard
     * PAL size for its mode, or its display clip, from its top left */
    vw = dclip ? dclip->MaxX - dclip->MinX + 1 : (id & HIRES) ? 640 : 320;
    vh = dclip ? dclip->MaxY - dclip->MinY + 1 : (id & LACE) ? 512 : 256;
    if (vw > w || vw <= 0) vw = w;
    if (vh > h || vh <= 0) vh = h;

    hw_defaults();
    DeleteVar("OS4C_FPS", GVF_GLOBAL_ONLY);
    stat_t0 = 0;
    if (!timer_open()) return 0;
    o = AllocVecTags(sizeof(*o), AVT_ClearWithValue, 0, TAG_DONE);
    if (!o) return 0;
    o->w = vw;
    o->h = vh;
    o->depth = depth;
    if (game_bm) o->bm0 = game_bm;
    else {
        o->bm0 = alloc_screen_bm(w, h, depth);
        o->own_bm0 = 1;
        if (!o->bm0) { FreeVec(o); return 0; }
    }
    o->front = o->bm0;
    if (!topaz) topaz = OpenFont(&topaz_attr);

    o->s.Width = w;
    o->s.Height = h;
    strncpy(title, ti, sizeof(title) - 1);
    o->s.Title = (STRPTR)title;
    o->s.DefaultTitle = (STRPTR)title;
    o->s.Font = &topaz_attr;
    o->s.BitMap = *o->bm0;
    InitRastPort(&o->s.RastPort);
    o->s.RastPort.BitMap = o->bm0;
    if (topaz) SetFont(&o->s.RastPort, topaz);
    InitVPort(&o->s.ViewPort);
    o->s.ViewPort.DWidth = vw;
    o->s.ViewPort.DHeight = vh;
    o->s.ViewPort.Modes = (UWORD)id;
    o->s.ViewPort.RasInfo = &o->ri;
    o->s.ViewPort.ColorMap = GetColorMap(256);
    o->ri.BitMap = o->bm0;

    /* the Workbench colours until the game sets its own */
    for (i = 0; i < 256; i++) pal[i] = 0;
    pal[0] = 0x959595; pal[1] = 0x000000; pal[2] = 0xFFFFFF; pal[3] = 0x3B67A2;
    for (; cs && cs->ColorIndex >= 0; cs++)
        set_pal(cs->ColorIndex, cs->Red * 17, cs->Green * 17, cs->Blue * 17);
    lut_dirty = 1;

    cur = o;
    scale = pick_scale(vw, vh);
    if (!open_win()) {
        cur = 0;
        if (o->s.ViewPort.ColorMap) FreeColorMap(o->s.ViewPort.ColorMap);
        if (o->own_bm0) free_screen_bm(o->bm0);
        FreeVec(o);
        return 0;
    }
    if (c32) oc_LoadRGB32(&o->s.ViewPort, c32);
    t0 = now_us();
    oc_present();
    return &o->s;
}

struct Screen *oc_OpenScreenTags(const struct NewScreen *ns, ...)
{
    struct TagItem tags[MAXTAGS];
    va_list ap;
    int n = 0;
    va_start(ap, ns);
    while (n < MAXTAGS - 1) {
        ULONG tag = va_arg(ap, ULONG);
        if (tag == TAG_DONE) break;
        tags[n].ti_Tag = tag;
        tags[n].ti_Data = va_arg(ap, ULONG);
        n++;
    }
    va_end(ap);
    tags[n].ti_Tag = TAG_DONE;
    tags[n].ti_Data = 0;
    return oc_OpenScreenTagList(ns, tags);
}

struct Screen *oc_OpenScreen(const struct NewScreen *ns)
{
    const struct TagItem *tags = 0;
    if (ns && (ns->Type & NS_EXTENDED)) tags = ((const struct ExtNewScreen *)ns)->Extension;
    return oc_OpenScreenTagList(ns, tags);
}

static void close_win(void)
{
    struct IntuiMessage *m;
    if (!win) return;
    if (win->UserPort)
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) ReplyMsg((struct Message *)m);
    CloseWindow(win);
    win = 0;
    free_fb();
}

BOOL oc_CloseScreen(struct Screen *s)
{
    struct oc_screen *o = (struct oc_screen *)s;
    if (!s) return TRUE;
    if (!cur || s != &cur->s) return CloseScreen(s);
    close_win();
    if (o->s.ViewPort.ColorMap) FreeColorMap(o->s.ViewPort.ColorMap);
    if (o->own_bm0) free_screen_bm(o->bm0);
    FreeVec(o);
    cur = 0;
    return TRUE;
}

/* ---- windows ---- */

static int on_ours(const struct NewWindow *nw, const struct TagItem *tags, ULONG *idcmp, int *activate)
{
    struct TagItem *t, *state;
    struct Screen *scr = 0;
    if (nw) {
        if ((nw->Type & SCREENTYPE) == CUSTOMSCREEN) scr = nw->Screen;
        *idcmp = nw->IDCMPFlags;
        *activate = (nw->Flags & WFLG_ACTIVATE) != 0;
    }
    state = (struct TagItem *)tags;
    while ((t = NextTagItem(&state)) != 0) {
        if (t->ti_Tag == WA_CustomScreen) scr = (struct Screen *)t->ti_Data;
        else if (t->ti_Tag == WA_IDCMP) *idcmp = t->ti_Data;
        else if (t->ti_Tag == WA_Activate) *activate = t->ti_Data != 0;
    }
    return cur && scr == &cur->s;
}

struct Window *oc_OpenWindowTagList(const struct NewWindow *nw, const struct TagItem *tags)
{
    ULONG idcmp = 0;
    int activate = 0;
    if (!on_ours(nw, tags, &idcmp, &activate)) return OpenWindowTagList(nw, tags);
    if (!win) return 0;
    win_idcmp |= idcmp;
    ModifyIDCMP(win, win_idcmp ? win_idcmp | IDCMP_CLOSEWINDOW : 0);
    win_given++;
    cur->s.FirstWindow = win;
    if (activate) ActivateWindow(win);
    return win;
}

struct Window *oc_OpenWindowTags(const struct NewWindow *nw, ...)
{
    struct TagItem tags[MAXTAGS];
    va_list ap;
    int n = 0;
    va_start(ap, nw);
    while (n < MAXTAGS - 1) {
        ULONG tag = va_arg(ap, ULONG);
        if (tag == TAG_DONE) break;
        tags[n].ti_Tag = tag;
        tags[n].ti_Data = va_arg(ap, ULONG);
        n++;
    }
    va_end(ap);
    tags[n].ti_Tag = TAG_DONE;
    tags[n].ti_Data = 0;
    return oc_OpenWindowTagList(nw, tags);
}

struct Window *oc_OpenWindow(const struct NewWindow *nw)
{
    const struct TagItem *tags = 0;
    if (nw && (nw->Flags & WFLG_NW_EXTENDED)) tags = ((const struct ExtNewWindow *)nw)->Extension;
    return oc_OpenWindowTagList(nw, tags);
}

void oc_CloseWindow(struct Window *w)
{
    if (!w) return;
    if (w != win) { CloseWindow(w); return; }
    if (win_given > 0 && --win_given == 0) {
        win_idcmp = 0;
        ModifyIDCMP(win, 0);
        if (cur) cur->s.FirstWindow = 0;
    }
}

BOOL oc_ModifyIDCMP(struct Window *w, ULONG flags)
{
    if (w != win || !win) return ModifyIDCMP(w, flags);
    win_idcmp = flags;
    return ModifyIDCMP(win, flags ? flags | IDCMP_CLOSEWINDOW : 0);
}

void oc_ActivateWindow(struct Window *w)
{
    if (w) ActivateWindow(w);
}

/* the window's close gadget reads as Esc, which quits the games */
struct Message *oc_GetMsg(struct MsgPort *port)
{
    struct Message *m = GetMsg(port);
    if (m && win && port == win->UserPort) {
        struct IntuiMessage *im = (struct IntuiMessage *)m;
        if (im->Class == IDCMP_CLOSEWINDOW) {
            im->Class = IDCMP_RAWKEY;
            im->Code = 0x45;
            im->Qualifier = 0;
        } else if (im->Class == IDCMP_MOUSEMOVE || im->Class == IDCMP_MOUSEBUTTONS) {
            im->MouseX = (WORD)(win->GZZMouseX / scale);
            im->MouseY = (WORD)(win->GZZMouseY / scale);
        }
    }
    return m;
}

/* ---- double buffering ---- */

struct ScreenBuffer *oc_AllocScreenBuffer(struct Screen *s, struct BitMap *bm, ULONG flags)
{
    struct oc_sbuf *b;
    if (!cur || s != &cur->s) return AllocScreenBuffer(s, bm, flags);
    b = AllocVecTags(sizeof(*b), AVT_ClearWithValue, 0, TAG_DONE);
    if (!b) return 0;
    b->sb.sb_DBufInfo = AllocVecTags(sizeof(struct DBufInfo), AVT_ClearWithValue, 0, TAG_DONE);
    if (!b->sb.sb_DBufInfo) { FreeVec(b); return 0; }
    if (flags & SB_SCREEN_BITMAP) b->sb.sb_BitMap = cur->bm0;
    else if (bm) b->sb.sb_BitMap = bm;
    else {
        int bw = (int)GetBitMapAttr(cur->bm0, BMA_WIDTH), bh = (int)GetBitMapAttr(cur->bm0, BMA_HEIGHT);
        b->sb.sb_BitMap = is_chunky(cur->bm0) ? alloc_screen_bm(bw, bh, cur->depth) : alloc_planar(bw, bh, cur->bm0->Depth);
        b->own = 1;
        if (!b->sb.sb_BitMap) { FreeVec(b->sb.sb_DBufInfo); FreeVec(b); return 0; }
        if (flags & SB_COPY_BITMAP) BltBitMap(cur->bm0, 0, 0, b->sb.sb_BitMap, 0, 0, bw, bh, 0xC0, 0xFF, NULL);
    }
    return &b->sb;
}

void oc_FreeScreenBuffer(struct Screen *s, struct ScreenBuffer *sb)
{
    struct oc_sbuf *b = (struct oc_sbuf *)sb;
    if (!sb) return;
    if (!cur || s != &cur->s) { FreeScreenBuffer(s, sb); return; }
    if (cur->front == sb->sb_BitMap) cur->front = cur->bm0;
    if (b->own) {
        if (is_chunky(sb->sb_BitMap)) FreeBitMap(sb->sb_BitMap);
        else free_planar(sb->sb_BitMap);
    }
    FreeVec(sb->sb_DBufInfo);
    FreeVec(b);
}

/* reply a double-buffer message unless it is still waiting at its port */
static void reply_db(struct Message *m)
{
    struct MsgPort *p = m->mn_ReplyPort;
    struct Node *n;
    if (!p) return;
    Disable();
    for (n = p->mp_MsgList.lh_Head; n->ln_Succ; n = n->ln_Succ)
        if (n == &m->mn_Node) { Enable(); return; }
    Enable();
    ReplyMsg(m);
}

ULONG oc_ChangeScreenBuffer(struct Screen *s, struct ScreenBuffer *sb)
{
    if (!cur || s != &cur->s) return ChangeScreenBuffer(s, sb);
    /* the new bitmap is shown at the next vertical blank, which is when
     * the game gets its messages (some games pace themselves on those) */
    if (!waited) wait_tick();
    waited = 0;
    cur->front = sb->sb_BitMap;
    cur->ri.BitMap = sb->sb_BitMap;
    cur->dbuf = 1;
    poll_mouse();
    oc_present();
    reply_db(&sb->sb_DBufInfo->dbi_SafeMessage);
    reply_db(&sb->sb_DBufInfo->dbi_DispMessage);
    return 1;
}

/* ---- colours ---- */

void oc_SetRGB4(struct ViewPort *vp, LONG n, ULONG r, ULONG g, ULONG b)
{
    if (!is_ours_vp(vp)) { SetRGB4(vp, n, r, g, b); return; }
    set_pal((int)n, (r & 15) * 17, (g & 15) * 17, (b & 15) * 17);
}

void oc_SetRGB32(struct ViewPort *vp, ULONG n, ULONG r, ULONG g, ULONG b)
{
    if (!is_ours_vp(vp)) { SetRGB32(vp, n, r, g, b); return; }
    set_pal((int)n, r >> 24, g >> 24, b >> 24);
}

void oc_LoadRGB4(struct ViewPort *vp, const UWORD *c, LONG count)
{
    LONG i;
    if (!is_ours_vp(vp)) { LoadRGB4(vp, (UWORD *)c, count); return; }
    for (i = 0; i < count; i++)
        set_pal((int)i, ((c[i] >> 8) & 15) * 17, ((c[i] >> 4) & 15) * 17, (c[i] & 15) * 17);
}

void oc_LoadRGB32(struct ViewPort *vp, const ULONG *t)
{
    if (!is_ours_vp(vp)) { LoadRGB32(vp, (ULONG *)t); return; }
    while (t && *t) {
        ULONG count = *t >> 16, first = *t & 0xFFFF, i;
        t++;
        for (i = 0; i < count; i++, t += 3) set_pal((int)(first + i), t[0] >> 24, t[1] >> 24, t[2] >> 24);
    }
}

ULONG oc_GetRGB4(struct ColorMap *cm, LONG n)
{
    ULONG c;
    if (!cur || cm != cur->s.ViewPort.ColorMap) return GetRGB4(cm, n);
    if (n < 0 || n > 255) return (ULONG)-1;
    c = pal[n];
    return ((c >> 20) & 15) << 8 | ((c >> 12) & 15) << 4 | ((c >> 4) & 15);
}

/* ---- the rest of the display calls ---- */

void oc_ScrollVPort(struct ViewPort *vp)
{
    if (!is_ours_vp(vp)) ScrollVPort(vp);     /* ours: RasInfo is read when shown */
}

void oc_ScreenToFront(struct Screen *s)
{
    if (!cur || s != &cur->s) { ScreenToFront(s); return; }
    if (win) WindowToFront(win);
}

void oc_ScreenToBack(struct Screen *s)
{
    if (!cur || s != &cur->s) ScreenToBack(s);
}

void oc_MakeScreen(struct Screen *s)
{
    if (!cur || s != &cur->s) MakeScreen(s);
}

void oc_InitRastPort(struct RastPort *rp)
{
    InitRastPort(rp);
    if (!topaz) topaz = OpenFont(&topaz_attr);
    if (topaz) SetFont(rp, topaz);
}

static void wait_tick(void)
{
    unsigned long long now, next;
    if (!timer_open()) return;
    now = now_us();
    if (last_wake) stat_work += now - last_wake;
    if (!t0) t0 = now;
    next = t0 + ((now - t0) / 20000 + 1) * 20000;
    sleep_us((ULONG)(next - now));
    waited = 1;
    last_wake = now_us();
    stat_sleep += last_wake - now;
    stat_waits++;
    if (last_wake > next) {
        stat_late += last_wake - next;
        if (last_wake - next > 20000) stat_missed++;
    }
}

/* the next tick of the 50 Hz clock: a PAL vertical blank */
void oc_WaitTOF(void)
{
    if (cur && !cur->dbuf) oc_present();
    poll_mouse();
    wait_tick();
}

void oc_WaitBOVP(struct ViewPort *vp)
{
    (void)vp;
    oc_WaitTOF();
}

/* ticks of the same 50 Hz clock (a game's Delay(1) loop runs at 50 Hz) */
void oc_Delay(LONG ticks)
{
    if (ticks <= 0) return;
    if (cur && !cur->dbuf) oc_present();
    poll_mouse();
    while (ticks-- > 0) wait_tick();
}

static void __attribute__((destructor)) screen_cleanup(void)
{
    if (cur) oc_CloseScreen(&cur->s);
    if (topaz) CloseFont(topaz);
    topaz = 0;
    free(line);
    line = 0;
    timer_close();
}
