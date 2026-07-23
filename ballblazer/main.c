/*
 * Ballblazer — main loop & display setup.
 *
 * Phase 1 scaffolding: opens a 320x256 5-plane double-buffered
 * screen, splits it into two 128-line panes, and renders each pane
 * from a fixed camera position at opposite ends of the pitch. Ball
 * sits at world origin. ESC quits. No gameplay yet; this exists to
 * validate the projection, split, and palette before any movement,
 * physics, or AI is layered on top.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <string.h>
#include <stdio.h>

#include "bridge_client.h"
#include "ballblazer.h"

#define VERSION "0.1"

/* ---- display -------------------------------------------------------- */
typedef struct {
    struct Screen        *scr;
    struct Window        *win;
    struct ScreenBuffer  *sb[2];
    struct RastPort       mrp;    /* off-screen RP that swaps bitmaps */
    struct ViewPort      *vp;
    int cur;
} Display;
static Display D;

static LONG frame_count = 0;
static LONG running = 1;
static LONG bridge_ok = 0;

/* ---- palette -------------------------------------------------------- */
static void put_rgb(struct ViewPort *vp, int pen, int r, int g, int b)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    SetRGB32(vp, pen, (ULONG)r << 24, (ULONG)g << 24, (ULONG)b << 24);
}

static void set_palette(struct ViewPort *vp)
{
    put_rgb(vp, PEN_BLACK,      0,   0,   0);
    put_rgb(vp, PEN_SKY_TOP,   30,  20,  90);   /* P2 pane sky: deep blue */
    put_rgb(vp, PEN_SKY_BOT,  110,  60, 130);   /* P1 pane sky: magenta */
    put_rgb(vp, PEN_GRID_A,    30,  90,  40);   /* ground green */
    put_rgb(vp, PEN_GRID_B,    20,  70,  30);   /* ground green darker */
    put_rgb(vp, PEN_GRID_LINE,180, 200, 220);   /* grid line pale cyan */
    put_rgb(vp, PEN_GOAL_P1,   40, 200, 255);   /* P1 goal cyan */
    put_rgb(vp, PEN_GOAL_P2,  255, 180,  40);   /* P2 goal amber */
    put_rgb(vp, PEN_BALL,     240,  40,  40);   /* classic red ball */
    put_rgb(vp, PEN_HUD_BG,    10,  10,  25);
    put_rgb(vp, PEN_HUD_FG,   220, 220, 240);
    put_rgb(vp, PEN_WHITE,    255, 255, 255);
}

/* ---- display open/close -------------------------------------------- */
static void close_display(void);
static int open_display(void)
{
    ULONG modeID = LORES_KEY;
    D.scr = OpenScreenTags(NULL,
                           SA_Width,     SCR_W,
                           SA_Height,    SCR_H,
                           SA_Depth,     SCR_DEPTH,
                           SA_DisplayID, modeID,
                           SA_Type,      CUSTOMSCREEN,
                           SA_Quiet,     TRUE,
                           SA_ShowTitle, FALSE,
                           TAG_END);
    if (!D.scr) { AB_E("OpenScreen failed"); return 1; }
    D.vp = &D.scr->ViewPort;

    D.win = OpenWindowTags(NULL,
                           WA_CustomScreen, (ULONG)D.scr,
                           WA_Width,        SCR_W,
                           WA_Height,       SCR_H,
                           WA_Left,         0,
                           WA_Top,          0,
                           WA_Backdrop,     TRUE,
                           WA_Borderless,   TRUE,
                           WA_Activate,     TRUE,
                           WA_RMBTrap,      TRUE,
                           WA_IDCMP,        IDCMP_RAWKEY | IDCMP_MOUSEMOVE,
                           TAG_END);
    if (!D.win) { AB_E("OpenWindow failed"); close_display(); return 1; }

    D.sb[0] = AllocScreenBuffer(D.scr, NULL, SB_SCREEN_BITMAP);
    D.sb[1] = AllocScreenBuffer(D.scr, NULL, 0);
    if (!D.sb[0] || !D.sb[1]) { AB_E("AllocScreenBuffer failed"); close_display(); return 1; }
    D.cur = 0;

    InitRastPort(&D.mrp);
    D.mrp.Layer = NULL;

    set_palette(D.vp);
    return 0;
}
static void close_display(void)
{
    if (D.sb[1]) { FreeScreenBuffer(D.scr, D.sb[1]); D.sb[1] = NULL; }
    if (D.sb[0]) { FreeScreenBuffer(D.scr, D.sb[0]); D.sb[0] = NULL; }
    if (D.win)   { CloseWindow(D.win); D.win = NULL; }
    if (D.scr)   { CloseScreen(D.scr); D.scr = NULL; }
}

/* ---- gameplay state (Phase 1: static, no controls) ---------------- */
static Rotofoil p1, p2;
static Ball     ball;

static void init_game(void)
{
    p1.x = -PITCH_LENGTH * 3 / 4;  p1.z = 0;
    p1.vx = 0; p1.vz = 0;  p1.angle = 0;   p1.has_ball = 0;
    p2.x =  PITCH_LENGTH * 3 / 4;  p2.z = 0;
    p2.vx = 0; p2.vz = 0;  p2.angle = 180; p2.has_ball = 0;
    ball.x = 0;  ball.z = 0;  ball.vx = 0; ball.vz = 0; ball.carrier = -1;
}

/* ---- frame loop --------------------------------------------------- */
static void render_frame(void)
{
    struct BitMap *bm = D.sb[D.cur]->sb_BitMap;
    struct RastPort *rp = &D.mrp;
    rp->BitMap = bm;

    /* P1 (human) — bottom pane. */
    pitch_render(rp, PANE_P1_Y0, p1.x, p1.z, p1.angle, &ball);
    /* P2 (CPU) — top pane. */
    pitch_render(rp, PANE_P2_Y0, p2.x, p2.z, p2.angle, &ball);

    /* Mid-screen HUD divider (2 px black). */
    SetAPen(rp, PEN_BLACK);
    RectFill(rp, 0, PANE_H - 1, SCR_W - 1, PANE_H);

    WaitBlit();
    if (ChangeScreenBuffer(D.scr, D.sb[D.cur])) {
        WaitTOF();
        D.cur ^= 1;
    } else {
        WaitTOF();
    }
}

int main(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    if (!IntuitionBase) return 1;
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!GfxBase) { CloseLibrary((struct Library *)IntuitionBase); return 1; }

    printf("ballblazer v%s\n", VERSION);
    if (ab_init("ballblazer") != 0) { printf("  Bridge: NOT FOUND\n"); bridge_ok = 0; }
    else { printf("  Bridge: CONNECTED\n"); bridge_ok = 1; }
    AB_I("ballblazer v%s starting", VERSION);
    ab_register_var("frame_count", AB_TYPE_I32, &frame_count);
    ab_register_var("running",     AB_TYPE_I32, &running);

    math_init();
    init_game();

    if (open_display()) {
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
            ULONG cls  = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_RAWKEY) {
                if (code == 0x45) running = 0;   /* ESC */
            }
        }

        render_frame();
        frame_count++;
        if (bridge_ok) ab_poll();
    }

    AB_I("ballblazer shutting down (%ld frames)", (long)frame_count);
    close_display();
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    printf("ballblazer finished.\n");
    return 0;
}
