/*
 * void_trader — Elite-alike space trading + combat demo for Amiga.
 * Phase 1: multi-object 3D engine, 6DoF flight, cockpit + HUD,
 * starfield, one enemy ship visible (static for now). No combat.
 *
 * Controls:
 *   W / S    pitch nose down / up
 *   A / D    yaw left / right
 *   Q / E    roll left / right
 *   R / F    thrust forward / reverse
 *   SPACE    fire (Phase 2)
 *   ESC      quit
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <graphics/displayinfo.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <dos/dosextens.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"
#include "engine3d.h"
#include "models.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;

/* Bigger stack — engine3d.c has a couple of medium-sized arrays
 * and view_matrix does ~50 stack LONGs. Give ourselves headroom. */
ULONG __stack = 65536;

/* Screen geometry. LORES AGA 320x256 8bpp. */
#define SCREEN_W    320
#define SCREEN_H    256
#define VIEW_H      180                 /* HUD chrome eats the rest */
#define VIEW_H_HALF (VIEW_H / 2)

/* Raw-key codes. */
#define RK_ESC   0x45
#define RK_W     0x11
#define RK_A     0x20
#define RK_S     0x21
#define RK_D     0x22
#define RK_Q     0x10
#define RK_E     0x12
#define RK_R     0x13
#define RK_F     0x23
#define RK_SPACE 0x40
#define RK_UP_MASK 0x80

#define IN_PITCH_DOWN 0x0001
#define IN_PITCH_UP   0x0002
#define IN_YAW_L      0x0004
#define IN_YAW_R      0x0008
#define IN_ROLL_L     0x0010
#define IN_ROLL_R     0x0020
#define IN_THRUST_FWD 0x0040
#define IN_THRUST_REV 0x0080
#define IN_FIRE       0x0100

static UWORD input_flags;

static void apply_key(UWORD code)
{
    UWORD released = code & RK_UP_MASK;
    UWORD raw = code & 0x7F;
    UWORD bit = 0;
    switch (raw) {
    case RK_W: bit = IN_PITCH_DOWN; break;
    case RK_S: bit = IN_PITCH_UP;   break;
    case RK_A: bit = IN_YAW_L;      break;
    case RK_D: bit = IN_YAW_R;      break;
    case RK_Q: bit = IN_ROLL_L;     break;
    case RK_E: bit = IN_ROLL_R;     break;
    case RK_R: bit = IN_THRUST_FWD; break;
    case RK_F: bit = IN_THRUST_REV; break;
    case RK_SPACE: bit = IN_FIRE;   break;
    default: break;
    }
    if (!bit) return;
    if (released) input_flags &= ~bit;
    else          input_flags |=  bit;
}

/* ------------------------------------------------------------------ */
/* Screen setup                                                        */
/* ------------------------------------------------------------------ */

static struct Screen        *screen;
static struct Window        *window;
static struct ScreenBuffer  *sbuf[2];
static struct RastPort       rp_buf[2];
static struct RastPort       mrp;                /* master RP for AreaFill */
static struct AreaInfo       areaInfo;
static struct TmpRas         tmpRas;
static UBYTE                *areaBuf;
static PLANEPTR              tmpPlane;
static UWORD                 cur_buf = 1;

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
static void put_rgb(struct ViewPort *vp, UWORD pen, int r, int g, int b)
{
    r = clampi(r, 0, 255);
    g = clampi(g, 0, 255);
    b = clampi(b, 0, 255);
    SetRGB32(vp, pen, (ULONG)r << 24, (ULONG)g << 24, (ULONG)b << 24);
}

static void install_palette(struct ViewPort *vp)
{
    int i;
    /* 0 = deep space black */
    put_rgb(vp, 0, 0, 0, 8);
    /* 1..4 = starfield white/yellow tints */
    for (i = 0; i < 4; i++) {
        int v = 130 + i * 32;
        if (v > 255) v = 255;
        put_rgb(vp, 1 + i, v, v, v);
    }
    /* 8..39 = ship shading ramp: dark grey -> mid grey -> highlight.
     * Pen base per ship gets bumped so we can colour-code teams. */
    for (i = 0; i < 32; i++) {
        int shade = 20 + i * 6;
        if (shade > 250) shade = 250;
        put_rgb(vp, 8 + i, shade, shade, shade + 10);
    }
    /* 40..71 = second ramp for enemy — reddish */
    for (i = 0; i < 32; i++) {
        int shade = 20 + i * 6;
        if (shade > 250) shade = 250;
        put_rgb(vp, 40 + i, shade + 20, shade / 2, shade / 3);
    }
    /* 72..103 = station ramp — greenish */
    for (i = 0; i < 32; i++) {
        int shade = 20 + i * 6;
        if (shade > 250) shade = 250;
        put_rgb(vp, 72 + i, shade / 3, shade + 20, shade / 2);
    }
    /* 120 = HUD phosphor green */
    put_rgb(vp, 120, 60, 240, 120);
    /* 121 = HUD dim */
    put_rgb(vp, 121, 30, 120, 60);
    /* 122 = cockpit chrome (dark grey) */
    put_rgb(vp, 122, 40, 40, 55);
    /* 123 = cockpit highlight */
    put_rgb(vp, 123, 90, 90, 105);
    /* 124 = warning red */
    put_rgb(vp, 124, 240, 60, 60);
    /* 125 = laser green */
    put_rgb(vp, 125, 60, 255, 90);
}

static int open_display(void)
{
    memset(&rp_buf[0], 0, sizeof(rp_buf[0]));
    memset(&rp_buf[1], 0, sizeof(rp_buf[1]));
    memset(&mrp, 0, sizeof(mrp));

    screen = OpenScreenTags(NULL,
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     8,
        SA_DisplayID, LORES_KEY,
        SA_Title,     (ULONG)"Void Trader",
        SA_ShowTitle, FALSE,
        SA_Quiet,     TRUE,
        SA_Type,      CUSTOMSCREEN,
        TAG_DONE);
    if (!screen) return 1;

    window = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)screen,
        WA_Left, 0, WA_Top, 0,
        WA_Width, SCREEN_W, WA_Height, SCREEN_H,
        WA_Borderless, TRUE, WA_Backdrop, TRUE,
        WA_Activate, TRUE,
        WA_IDCMP, IDCMP_RAWKEY,
        TAG_DONE);
    if (!window) return 2;

    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) return 3;

    /* AreaInfo + TmpRas for AreaFill. Shared master RP that we
     * point at whichever bitmap we're drawing to this frame. */
    InitRastPort(&mrp);
    areaBuf  = AllocMem(8L * 5L, MEMF_CLEAR);
    tmpPlane = (PLANEPTR)AllocRaster(SCREEN_W, SCREEN_H);
    if (!areaBuf || !tmpPlane) return 4;
    InitArea(&areaInfo, areaBuf, 7);
    InitTmpRas(&tmpRas, tmpPlane, RASSIZE(SCREEN_W, SCREEN_H));
    mrp.AreaInfo = &areaInfo;
    mrp.TmpRas   = &tmpRas;

    InitRastPort(&rp_buf[0]); rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]); rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    install_palette(&screen->ViewPort);
    cur_buf = 1;
    return 0;
}

static void close_display(void)
{
    if (sbuf[0]) {
        int t = 0;
        while (!ChangeScreenBuffer(screen, sbuf[0]) && ++t < 5) WaitTOF();
        WaitTOF(); WaitTOF();
    }
    if (tmpPlane) { FreeRaster(tmpPlane, SCREEN_W, SCREEN_H); tmpPlane = NULL; }
    if (areaBuf)  { FreeMem(areaBuf, 8L * 5L); areaBuf = NULL; }
    if (sbuf[1])  { FreeScreenBuffer(screen, sbuf[1]); sbuf[1] = NULL; }
    if (sbuf[0])  { FreeScreenBuffer(screen, sbuf[0]); sbuf[0] = NULL; }
    if (window)   { CloseWindow(window); window = NULL; }
    if (screen)   { CloseScreen(screen); screen = NULL; }
}

/* ------------------------------------------------------------------ */
/* Cockpit HUD                                                         */
/* ------------------------------------------------------------------ */

static void draw_cockpit(struct RastPort *rp, int fps, LONG speed,
                         LONG energy)
{
    /* Bottom console — hides the lower portion of the screen. */
    SetAPen(rp, 122);
    RectFill(rp, 0, VIEW_H, SCREEN_W - 1, SCREEN_H - 1);
    /* Console highlight line */
    SetAPen(rp, 123);
    Move(rp, 0, VIEW_H); Draw(rp, SCREEN_W - 1, VIEW_H);

    /* Central crosshair reticle */
    SetAPen(rp, 120);
    int cx = SCREEN_W / 2, cy = VIEW_H_HALF;
    Move(rp, cx - 8, cy); Draw(rp, cx - 3, cy);
    Move(rp, cx + 3, cy); Draw(rp, cx + 8, cy);
    Move(rp, cx, cy - 8); Draw(rp, cx, cy - 3);
    Move(rp, cx, cy + 3); Draw(rp, cx, cy + 8);

    /* Console text */
    SetAPen(rp, 120);
    SetDrMd(rp, JAM1);
    char buf[32];
    sprintf(buf, "SPD %04ld", (long)speed);
    Move(rp, 8, VIEW_H + 12); Text(rp, (STRPTR)buf, 8);
    sprintf(buf, "ENGY %04ld", (long)energy);
    Move(rp, 8, VIEW_H + 24); Text(rp, (STRPTR)buf, 9);
    sprintf(buf, "FPS %ld", (long)fps);
    Move(rp, SCREEN_W - 56, VIEW_H + 12); Text(rp, (STRPTR)buf, strlen(buf));

    /* Left/right cockpit pillars, top only */
    SetAPen(rp, 122);
    RectFill(rp,  0, 0,  8, VIEW_H - 1);
    RectFill(rp, SCREEN_W - 9, 0, SCREEN_W - 1, VIEW_H - 1);
    SetAPen(rp, 123);
    Move(rp,  9, 0);            Draw(rp,  9, VIEW_H - 1);
    Move(rp, SCREEN_W - 10, 0); Draw(rp, SCREEN_W - 10, VIEW_H - 1);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static Camera cam;
static Entity world[8];

static void setup_world(void)
{
    memset(world, 0, sizeof(world));
    /* Player camera at origin looking down +Z. */
    cam.x = 0; cam.y = 0; cam.z = 0;
    cam.pitch = 0; cam.yaw = 0; cam.roll = 0;

    /* One friendly Cobra floating a bit away — for the demo. */
    world[0].active = 1;
    world[0].model = &model_cobra;
    world[0].x = -4000; world[0].y = 200; world[0].z = 8000;
    world[0].yaw = 30;
    world[0].team = 0;
    world[0].pen_base = 8;

    /* One enemy Krait. */
    world[1].active = 1;
    world[1].model = &model_krait;
    world[1].x = 3500; world[1].y = -300; world[1].z = 6500;
    world[1].yaw = -20;
    world[1].team = 1;
    world[1].pen_base = 40;

    /* A station in the distance. */
    world[2].active = 1;
    world[2].model = &model_station;
    world[2].x = 0; world[2].y = 500; world[2].z = 14000;
    world[2].team = 2;
    world[2].pen_base = 72;
}

static LONG ship_speed = 0;             /* signed: negative = reverse */
#define SHIP_MAX_SPEED   80
#define SHIP_ACCEL        2
#define ROTATE_RATE       3             /* degrees per tick */

static void update_camera(UWORD in)
{
    /* Rotation. Held keys rotate at ROTATE_RATE°/frame. */
    if (in & IN_PITCH_DOWN) cam.pitch = (cam.pitch + ROTATE_RATE) % 360;
    if (in & IN_PITCH_UP)   cam.pitch = (cam.pitch - ROTATE_RATE + 360) % 360;
    if (in & IN_YAW_R)      cam.yaw   = (cam.yaw   + ROTATE_RATE) % 360;
    if (in & IN_YAW_L)      cam.yaw   = (cam.yaw   - ROTATE_RATE + 360) % 360;
    if (in & IN_ROLL_R)     cam.roll  = (cam.roll  + ROTATE_RATE) % 360;
    if (in & IN_ROLL_L)     cam.roll  = (cam.roll  - ROTATE_RATE + 360) % 360;

    /* Thrust — hold R to accelerate, F to decelerate. Speed
     * persists (cruise-control) once you let go. */
    if (in & IN_THRUST_FWD) {
        ship_speed += SHIP_ACCEL;
        if (ship_speed > SHIP_MAX_SPEED) ship_speed = SHIP_MAX_SPEED;
    }
    if (in & IN_THRUST_REV) {
        ship_speed -= SHIP_ACCEL;
        if (ship_speed < 0) ship_speed = 0;
    }

    /* Translate along camera's local +Z (forward). Forward vector
     * in world space = the third row of the camera's rotation
     * matrix's transpose, i.e. rotate (0,0,1) by camera orientation. */
    LONG sp = e3d_sin(cam.pitch), cp = e3d_cos(cam.pitch);
    LONG sy = e3d_sin(cam.yaw),   cy = e3d_cos(cam.yaw);
    /* Forward = (Ry Rx) * (0,0,1). */
    LONG fx = ( sy * cp) >> FP;
    LONG fy = (-sp)     ;                  /* already ONE-scaled */
    LONG fz = ( cy * cp) >> FP;
    cam.x += (fx * ship_speed) >> FP;
    cam.y += (fy * ship_speed) >> FP;
    cam.z += (fz * ship_speed) >> FP;
}

int main(void)
{
    ULONG frame = 0, prev_frame = 0;
    int bridge_ok, running = 1, fps_shown = 0;
    struct DateStamp prev_ds = { 0, 0, 0 };

    IntuitionBase = (struct IntuitionBase *)OpenLibrary(
        (CONST_STRPTR)"intuition.library", 39);
    if (!IntuitionBase) return 20;
    GfxBase = (struct GfxBase *)OpenLibrary(
        (CONST_STRPTR)"graphics.library", 39);
    if (!GfxBase) { CloseLibrary((struct Library *)IntuitionBase); return 20; }

    bridge_ok = (ab_init("void_trader") == 0);
    if (bridge_ok) {
        AB_I("void_trader Phase 1 starting");
        ab_register_var("cam_x", AB_TYPE_I32, &cam.x);
        ab_register_var("cam_y", AB_TYPE_I32, &cam.y);
        ab_register_var("cam_z", AB_TYPE_I32, &cam.z);
        ab_register_var("cam_yaw", AB_TYPE_I32, &cam.yaw);
        ab_register_var("cam_pitch", AB_TYPE_I32, &cam.pitch);
        ab_register_var("cam_roll", AB_TYPE_I32, &cam.roll);
        ab_register_var("speed", AB_TYPE_I32, &ship_speed);
        ab_register_var("running", AB_TYPE_I32, (LONG *)&running);
    }

    e3d_init(SCREEN_W, VIEW_H);
    vt_build_models();
    setup_world();

    if (open_display()) {
        printf("open_display failed\n");
        if (bridge_ok) ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 20;
    }

    while (running) {
        struct IntuiMessage *msg;
        ULONG sig = SetSignal(0L, 0L);
        if (sig & SIGBREAKF_CTRL_C) break;

        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            UWORD code = msg->Code;
            ULONG cls  = msg->Class;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_RAWKEY) {
                if ((code & 0x7F) == RK_ESC) { running = 0; break; }
                apply_key(code);
            }
        }
        if (!running) break;

        update_camera(input_flags);

        /* Render into off-screen buffer, then flip. */
        struct RastPort *rp = &rp_buf[cur_buf];
        mrp.BitMap = sbuf[cur_buf]->sb_BitMap;
        rp->BitMap = sbuf[cur_buf]->sb_BitMap;
        e3d_render_frame(&mrp, &cam, world, 8);
        draw_cockpit(&mrp, fps_shown, ship_speed, 1000);

        WaitBlit();
        if (ChangeScreenBuffer(screen, sbuf[cur_buf])) {
            WaitTOF();
            cur_buf ^= 1;
        } else {
            WaitTOF();
        }

        frame++;
        if (bridge_ok) ab_poll();

        /* FPS via DateStamp every 30 frames. */
        if ((frame % 30) == 0) {
            struct DateStamp now;
            DateStamp(&now);
            LONG dt = (now.ds_Days   - prev_ds.ds_Days)   * 24 * 60 * 3000
                    + (now.ds_Minute - prev_ds.ds_Minute) * 3000
                    + (now.ds_Tick   - prev_ds.ds_Tick);
            LONG df = (LONG)(frame - prev_frame);
            LONG fps10 = (dt > 0) ? (500L * df) / dt : 0;
            fps_shown = (int)(fps10 / 10);
            if (bridge_ok) {
                AB_I("hb frame=%ld fps=%ld.%ld cam=(%ld,%ld,%ld) yaw=%ld pitch=%ld spd=%ld",
                     (long)frame, (long)(fps10 / 10), (long)(fps10 % 10),
                     (long)cam.x, (long)cam.y, (long)cam.z,
                     (long)cam.yaw, (long)cam.pitch, (long)ship_speed);
            }
            prev_ds = now;
            prev_frame = frame;
        }
    }

    close_display();
    if (bridge_ok) { AB_I("void_trader shutting down (%lu frames)", frame); ab_cleanup(); }
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
