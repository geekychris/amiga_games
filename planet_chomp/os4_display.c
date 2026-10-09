/*
 * The game's picture on AmigaOS 4: a window on the Workbench, or full
 * screen on a screen of its own (the smallest RTG mode that holds the
 * frame at least once, 32-bit if there is one, else 16-bit), the frame
 * scaled up by a whole number and centred. WritePixelArray converts the
 * A R G B frame to whatever the screen's pixel format is.
 *
 * The same file is in planet_chomp, rolling_steel and spectral_keep.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/pointerclass.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <stdlib.h>
#include <string.h>
#include "os4_display.h"

static struct Screen *scr;
static struct Window *win;
static ULONG *big;                    /* the scaled frame */
static const char *title_;
static int fw, fh, wscale, full;
static int sc, ox, oy, bw, bh;        /* current scale, offset, scaled size */
static int clear_borders;

/* the mode for a full screen: the smallest that shows the frame at least
 * twice over (a 320 x 256 frame wants 800 x 600, not 640 x 480), else the
 * smallest that holds it at all; 32-bit if there is one, else 16-bit */
static ULONG find_mode(int need_w, int need_h, int *w, int *h, int *depth)
{
    static const int depths[2] = { 32, 16 };
    static const int sizes[5][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 }, { 0, 0 } };
    int d, s;
    for (d = 0; d < 2; d++)
        for (s = 0; s < 5; s++) {
            int ww = sizes[s][0] ? sizes[s][0] : need_w, hh = sizes[s][1] ? sizes[s][1] : need_h;
            struct DimensionInfo di;
            ULONG id;
            if (ww < need_w || hh < need_h) continue;
            id = BestModeID(BIDTAG_NominalWidth, ww, BIDTAG_NominalHeight, hh,
                            BIDTAG_DesiredWidth, ww, BIDTAG_DesiredHeight, hh,
                            BIDTAG_Depth, depths[d], TAG_DONE);
            if (id == INVALID_ID) continue;
            if (GetDisplayInfoData(NULL, (UBYTE *)&di, sizeof(di), DTAG_DIMS, id) > 0) {
                ww = di.Nominal.MaxX - di.Nominal.MinX + 1;
                hh = di.Nominal.MaxY - di.Nominal.MinY + 1;
                if (ww < need_w || hh < need_h) continue;
            }
            *w = ww; *h = hh; *depth = depths[d];
            return id;
        }
    return INVALID_ID;
}

static ULONG pick_mode(int *w, int *h, int *depth)
{
    ULONG id = INVALID_ID;
    if (fw < 640) id = find_mode(fw * 2, fh * 2, w, h, depth);
    if (id == INVALID_ID) id = find_mode(fw, fh, w, h, depth);
    return id;
}

static void layout(int avail_w, int avail_h, int want)
{
    sc = want;
    if (sc < 1 || fw * sc > avail_w || fh * sc > avail_h) {
        sc = avail_w / fw < avail_h / fh ? avail_w / fw : avail_h / fh;
        if (sc < 1) sc = 1;
    }
    bw = fw * sc;
    bh = fh * sc;
    ox = (avail_w - bw) / 2;
    oy = (avail_h - bh) / 2;
    free(big);
    big = sc > 1 ? (ULONG *)malloc((size_t)bw * bh * 4) : 0;
    clear_borders = 1;
}

static int open_window(void)
{
    win = OpenWindowTags(NULL,
                         WA_Title, (ULONG)title_,
                         WA_InnerWidth, fw * wscale, WA_InnerHeight, fh * wscale,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                         WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_GimmeZeroZero, TRUE,
                         WA_IDCMP, DISP_IDCMP, TAG_DONE);
    if (!win) return 0;
    layout(fw * wscale, fh * wscale, wscale);
    full = 0;
    return 1;
}

static int open_full(void)
{
    int w = 0, h = 0, depth = 32;
    ULONG id = pick_mode(&w, &h, &depth);
    if (id == INVALID_ID) return 0;
    scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, w, SA_Height, h, SA_Depth, depth,
                         SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN,
                         SA_Title, (ULONG)title_, TAG_DONE);
    if (!scr) return 0;
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 0, WA_Top, 0,
                         WA_Width, w, WA_Height, h, WA_Backdrop, TRUE, WA_Borderless, TRUE,
                         WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_IDCMP, DISP_IDCMP,
                         WA_PointerType, POINTERTYPE_NONE, TAG_DONE);           /* no mouse pointer */
    if (!win) { CloseScreen(scr); scr = 0; return 0; }
    layout(w, h, 0);                    /* the biggest whole scale that fits */
    full = 1;
    return 1;
}

static void close_all(void)
{
    if (win) {
        struct IntuiMessage *m;
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) ReplyMsg((struct Message *)m);
        CloseWindow(win);
    }
    win = 0;
    if (scr) CloseScreen(scr);
    scr = 0;
}

int disp_open(const char *title, int w, int h, int scale, int fullscreen)
{
    title_ = title;
    fw = w;
    fh = h;
    wscale = scale < 1 ? 1 : scale;
    if (fullscreen && open_full()) return 1;
    return open_window();
}

void disp_close(void)
{
    close_all();
    free(big);
    big = 0;
}

int disp_toggle(void)
{
    int was = full;
    close_all();
    if (was ? open_window() : open_full()) return 1;
    return was ? open_full() : open_window();   /* back to what worked */
}

int disp_fullscreen(void) { return full; }
struct Window *disp_window(void) { return win; }

void disp_present(const void *argb)
{
    struct RastPort *rp;
    int x0, y0;
    if (!win) return;
    rp = win->RPort;
    x0 = ox;
    y0 = oy;
    if (clear_borders && full) {
        SetRPAttrs(rp, RPTAG_APenColor, 0xFF000000UL, TAG_DONE);
        RectFill(rp, 0, 0, win->Width - 1, win->Height - 1);
        clear_borders = 0;
    }
    if (sc == 1) {
        WritePixelArray((uint8 *)argb, 0, 0, fw * 4, PIXF_A8R8G8B8, rp, x0, y0, fw, fh);
        return;
    }
    {
        const ULONG *src = (const ULONG *)argb;
        int y, x, k;
        for (y = 0; y < fh; y++, src += fw) {
            ULONG *dst = big + (size_t)y * sc * bw, *row = dst;
            if (sc == 2)
                for (x = 0; x < fw; x++) { dst[0] = dst[1] = src[x]; dst += 2; }
            else
                for (x = 0; x < fw; x++)
                    for (k = 0; k < sc; k++) *dst++ = src[x];
            /* duplicate the row: a loop (newlib's memcpy is unreliable for big copies here) */
            for (k = 1; k < sc; k++) {
                ULONG *d = row + (size_t)k * bw;
                for (x = 0; x < bw; x++) d[x] = row[x];
            }
        }
        WritePixelArray((uint8 *)big, 0, 0, bw * 4, PIXF_A8R8G8B8, rp, x0, y0, bw, bh);
    }
}
