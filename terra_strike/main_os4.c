// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * TERRA STRIKE for AmigaOS 4.1 (PowerPC)
 *
 * The same game as the classic Amiga version: the simulation (ts_math.c,
 * terrain.c, world.c), the renderer (render.c), the models and the bridge
 * interface (game.c) are shared. This file stands in for main.c: the
 * renderer draws into a 320x256 buffer of pens instead of five bitplanes,
 * which is turned into RGB through the current palette and shown by
 * os4_display.c in a window or on a screen of its own, scaled up. The
 * music is played by modplay.c and the effects through ahi.device.
 *
 *   Arrows   turn and climb / dive      A / Z   throttle up / down
 *   Space    cannon                     P       pause
 *   Return   start                      Esc     title / quit
 *   F or F10 window <-> full screen
 *
 * Usage: terra_strike [SCALE=n] [FULLSCREEN]
 *   SCALE=n: window n x 320x256 (default 2)
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_client.h"
#include "ts.h"
#include "render.h"
#include "title_image.h"
#include "sound.h"
#include "game.h"
#include "os4_display.h"

/* the title artwork's planes are decoded into a buffer, and the renderer's
 * tables are static, so the default stack is plenty; a little extra anyway */
static const char __attribute__((used)) stack_cookie[] = "$STACK:262144";

/* the game palette, as in main.c (12-bit OCS colours) */
static const UWORD palette[32] = {
    0x000, 0xFFF, 0x333, 0x999,
    0x027, 0x04A,            /* sea */
    0xB95, 0xDB7,            /* sand */
    0x392, 0x5B3,            /* grass */
    0x161, 0x283,            /* forest */
    0x654, 0x987,            /* rock */
    0xAAB, 0xEEF,            /* snow */
    0xF30, 0x900, 0xFE0, 0xF80,
    0x9EF, 0x59B, 0x357,     /* craft */
    0xC64, 0x732,            /* tank */
    0xDDC, 0x998, 0xB43,     /* building, roof */
    0xABC, 0x567,            /* ship */
    0x2C3, 0x111             /* tree, shadow */
};

static uint8_t pens[SCREEN_W * SCREEN_H];
static ULONG argb[SCREEN_W * SCREEN_H];
static ULONG lut[256];               /* pen -> A R G B for the palette showing */
/* bridge vars: microseconds turning pens into RGB and showing the frame */
static int32_t us_convert, us_present;

/* ---------- timing ---------- */
static struct MsgPort *timer_port;
static struct TimeRequest *timer_req;
struct Device *TimerBase;
struct TimerIFace *ITimer;

static int timer_open(void)
{
    timer_port = AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (!timer_port) return 0;
    timer_req = AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_ReplyPort, timer_port,
                                   ASOIOR_Size, sizeof(struct TimeRequest), TAG_DONE);
    if (!timer_req) return 0;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)timer_req, 0) != 0) {
        FreeSysObject(ASOT_IOREQUEST, timer_req);
        timer_req = 0;
        return 0;
    }
    TimerBase = timer_req->Request.io_Device;
    ITimer = (struct TimerIFace *)GetInterface((struct Library *)TimerBase, "main", 1, NULL);
    return ITimer != 0;
}

static void timer_close(void)
{
    if (ITimer) DropInterface((struct Interface *)ITimer);
    if (TimerBase) CloseDevice((struct IORequest *)timer_req);
    if (timer_req) FreeSysObject(ASOT_IOREQUEST, timer_req);
    if (timer_port) FreeSysObject(ASOT_PORT, timer_port);
}

static uint64_t now_us(void)
{
    struct TimeVal tv;
    GetUpTime(&tv);
    return (uint64_t)tv.Seconds * 1000000ULL + tv.Microseconds;
}

/* sleep on timer.device: WaitTOF() returns hundreds of times a second
 * under QEMU, so waiting on it would keep the CPU busy */
static void sleep_us(uint32 us)
{
    timer_req->Request.io_Command = TR_ADDREQUEST;
    timer_req->Time.Seconds = 0;
    timer_req->Time.Microseconds = us;
    DoIO((struct IORequest *)timer_req);
}

/* ---------- what game.c needs from the platform ---------- */
uint32_t plat_clock(void) { return (uint32_t)now_us(); }
int32_t plat_us_since(uint32_t t0) { return (int32_t)((uint32_t)now_us() - t0); }
int32_t plat_ticks(void) { return (int32_t)(now_us() / 20000); }

void plat_palette(int title)
{
    const UWORD *p = title ? title_palette : palette;
    int i;
    for (i = 0; i < 32; i++) {
        ULONG r = (p[i] >> 8) & 15, g = (p[i] >> 4) & 15, b = p[i] & 15;
        lut[i] = 0xFF000000UL | (r * 17) << 16 | (g * 17) << 8 | b * 17;
    }
}

/* ---------- music ---------- */
static void *load_file(const char *path, ULONG *size)
{
    BPTR f = Open((CONST_STRPTR)path, MODE_OLDFILE);
    int64 len;
    void *p = 0;
    *size = 0;
    if (!f) return 0;
    len = GetFileSize(f);
    if (len > 0 && len < 0x1000000 && (p = AllocVecTags((uint32)len, AVT_Type, MEMF_SHARED, TAG_DONE)) != 0) {
        if (Read(f, p, (int32)len) != (int32)len) { FreeVec(p); p = 0; }
        else *size = (ULONG)len;
    }
    Close(f);
    return p;
}

/* ---------- main ---------- */
int main(int argc, char **argv)
{
    int scale = 2, full = 0, running = 1, bridge = 0, i;
    unsigned keys = 0, pressed = 0;
    uint64_t next_frame;
    void *mod = 0;
    ULONG mod_size = 0;
    Canvas cv;
    const char *why = "Esc";

    for (i = 1; i < argc; i++) {
        if (strncasecmp(argv[i], "SCALE=", 6) == 0) scale = atoi(argv[i] + 6);
        else if (strcasecmp(argv[i], "FULLSCREEN") == 0) full = 1;
        else if (strcmp(argv[i], "?") == 0) { printf("Usage: terra_strike [SCALE=n] [FULLSCREEN]\n"); return 0; }
    }
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;

    if (!timer_open()) { printf("terra_strike: no timer.device\n"); goto out; }

    /* A previous instance may still be unregistering (devbench's amiga_run
     * stops it and relaunches 0.3 s later), so retry for up to 3 s.
     * timer.device rather than Delay, which isn't safe here (see CLAUDE.md). */
    {
        int tries;
        for (tries = 0; tries < 10 && ab_init("terra_strike") != 0; tries++) sleep_us(300000);
        bridge = tries < 10;
        AB_I("Terra Strike (AmigaOS 4) starting (bridge after %ld retries)", (long)tries);
    }

    game_init();
    ab_register_var("us_convert", AB_TYPE_I32, &us_convert);
    ab_register_var("us_present", AB_TYPE_I32, &us_present);
    if (!render_init() || !disp_open("Terra Strike", SCREEN_W, SCREEN_H, scale, full)) {
        printf("terra_strike: can't open a window or screen\n");
        AB_E("display setup failed");
        goto out;
    }
    cv.pix = pens;

    if (!ahi_open()) AB_W("no ahi.device - no sound");
    sound_init();
    mod = load_file("PROGDIR:terra_strike.mod", &mod_size);
    if (mod && ahi_music(mod, mod_size)) AB_I("music loaded (%ld bytes)", (long)mod_size);
    else AB_W("terra_strike.mod not found or not playable - no music");

    AB_I("island seed %ld, pad at tile %ld,%ld height %ld, %ld targets",
         (long)world.seed, (long)world.pad_tx, (long)world.pad_tz, (long)world.pad_h,
         (long)world.targets_left);
    SetSignal(0L, SIGBREAKF_CTRL_C);
    next_frame = now_us();

    while (running) {
        struct IntuiMessage *msg;

        while ((msg = (struct IntuiMessage *)GetMsg(disp_window()->UserPort)) != 0) {
            ULONG cls = msg->Class;
            UWORD code = msg->Code, qual = msg->Qualifier;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) { running = 0; why = "close gadget"; }
            else if (cls == IDCMP_INACTIVEWINDOW) keys = 0;
            else if (cls == IDCMP_RAWKEY) {
                if (code == DISP_KEY_F || code == DISP_KEY_F10) {
                    if (!disp_toggle()) { running = 0; why = "no display"; }
                    AB_I("display: %s", disp_fullscreen() ? "full screen" : "window");
                    render_hud_reset();
                    break;                          /* the old window's port is gone */
                }
                if (code & IECODE_UP_PREFIX) keys &= ~game_key_bit(code & 0x7F);
                else if (!(qual & IEQUALIFIER_REPEAT)) {
                    keys |= game_key_bit(code);
                    pressed |= game_key_bit(code);   /* seen even if released in the same batch */
                }
            }
        }
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) { running = 0; why = "Ctrl-C"; }
        if (!game_input(keys | pressed)) running = 0;
        if (!running) break;

        /* 25 frames a second, as the classic version (locked to every other
         * PAL frame) runs: on a fixed 40 ms schedule, so the time spent
         * waiting doesn't push each frame later */
        {
            uint64_t t = now_us();
            if (t < next_frame) {
                uint64_t left = next_frame - t;
                sleep_us(left > 10000 ? 10000 : (uint32)left);    /* still read keys every 10 ms */
                ahi_update();
                continue;
            }
        }
        next_frame += 40000;
        if (now_us() > next_frame + 200000) next_frame = now_us();   /* fell far behind */

        game_update(keys | pressed);
        pressed = 0;
        game_draw(&cv, 0);
        {
            uint32_t t0 = plat_clock();
            for (i = 0; i < SCREEN_W * SCREEN_H; i++) argb[i] = lut[pens[i]];
            us_convert = plat_us_since(t0);
            t0 = plat_clock();
            disp_present(argb);
            us_present = plat_us_since(t0);
        }
        ahi_update();
        game_after_frame();
    }
    AB_I("Terra Strike shutting down (%s), score %ld", why, (long)world.score);

out:
    ahi_close();
    sound_cleanup();
    if (mod) FreeVec(mod);
    disp_close();
    render_cleanup();
    timer_close();
    if (bridge) ab_cleanup();
    return 0;
}
