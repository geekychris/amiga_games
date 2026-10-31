// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * main_os4.c - the shared engine's AmigaOS 4.1 (PowerPC) main, from Terra
 * Strike's: the game draws (in its 320 x 256 coordinates) into a 640 x 512
 * frame in full colour, the engine looking its pens up in the palette
 * and the sky gradient as it goes, shown by os4_display.c in a window or
 * on a screen of its own. The music is played by modplay.c and the
 * effects through ahi.device (sound_ahi.c).
 *
 *   F or F10   window <-> full screen
 *
 * Usage: <game> [SCALE=n] [FULLSCREEN]   (SCALE: window n x 320x256, even, default 2)
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
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "sound_ahi.h"
#include "os4_display.h"

static const char __attribute__((used)) stack_cookie[] = "$STACK:262144";

static uint32_t argb[PHYS_W * PHYS_H];
static int32_t us_convert, us_present;
int32_t plat_in_keys, plat_in_joy;

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
 * under QEMU, so waiting on it keeps the CPU busy (and Delay isn't safe) */
static uint32_t sleep_over_max;        /* worst oversleep in the last report, us */
static void sleep_us(uint32 us)
{
    uint64_t t0 = now_us(), late;
    timer_req->Request.io_Command = TR_ADDREQUEST;
    timer_req->Time.Seconds = 0;
    timer_req->Time.Microseconds = us;
    DoIO((struct IORequest *)timer_req);
    late = now_us() - t0;
    late = late > us ? late - us : 0;
    if (late > sleep_over_max) sleep_over_max = (uint32_t)late;
}

/* the frame timer, asynchronous: the loop Waits on it and the window together,
 * so keys are handled as they come without waking every few ms */
static int timer_pending;
static void timer_start(uint32 us)
{
    timer_req->Request.io_Command = TR_ADDREQUEST;
    timer_req->Time.Seconds = us / 1000000;
    timer_req->Time.Microseconds = us % 1000000;
    SendIO((struct IORequest *)timer_req);
    timer_pending = 1;
}
static void timer_stop(void)
{
    if (!timer_pending) return;
    if (!CheckIO((struct IORequest *)timer_req)) AbortIO((struct IORequest *)timer_req);
    WaitIO((struct IORequest *)timer_req);
    timer_pending = 0;
}

/* ---------- what the game gets ---------- */
void plat_activate(void)
{
    struct Window *w = disp_window();
    if (w) { WindowToFront(w); ActivateWindow(w); }
}

uint32_t plat_clock(void) { return (uint32_t)now_us(); }
int32_t plat_us_since(uint32_t t0) { return (int32_t)((uint32_t)now_us() - t0); }
int32_t plat_ticks(void) { return (int32_t)(now_us() / 20000); }

static ULONG rgb12(uint16_t c)
{
    ULONG r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
    return 0xFF000000UL | (r * 17) << 16 | (g * 17) << 8 | b * 17;
}

void plat_palette(const uint16_t pal[32])
{
    int i;
    for (i = 0; i < 32; i++) a3_lut[i] = rgb12(pal[i]);
}

/* the gradient pen: a smooth blend down every row (the classic Amiga has
 * the copper's 16 steps) */
void plat_sky(int pen, uint16_t top, uint16_t bottom, int rows)
{
    int y, n = rows * A3_SUB;
    a3_sky_pen = rows > 0 ? pen : -1;
    a3_sky_rows = n < PHYS_H ? n : PHYS_H;
    for (y = 0; y < a3_sky_rows; y++) {
        int t = n > 1 ? (y * 256) / (n - 1) : 0, c, out = 0;
        for (c = 0; c < 3; c++) {
            int sh = 8 - c * 4, a = ((top >> sh) & 15) * 17, b = ((bottom >> sh) & 15) * 17;
            out = (out << 8) | (a + ((b - a) * t) / 256);
        }
        a3_sky_col[y] = 0xFF000000UL | (uint32_t)out;
    }
}

void *a3_sound_alloc(int len)
{
    return AllocVecTags(len, AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
}

void a3_sound_free(void *p, int len) { (void)len; FreeVec(p); }

void a3_sound_play(int8_t *data, int len, int period, int vol, int pri)
{
    SfxStructure s;
    s.sfx_ptr = data;
    s.sfx_len = (WORD)(len / 2);
    s.sfx_per = (WORD)period;
    s.sfx_vol = (WORD)vol;
    s.sfx_cha = -1;
    s.sfx_pri = (BYTE)pri;
    ahi_sfx(&s);
}

static void *load_file(const char *path, ULONG *size)
{
    BPTR f;
    int64 len;
    void *p = 0;
    *size = 0;
    if (!path[0] || !(f = Open((CONST_STRPTR)path, MODE_OLDFILE))) return 0;
    len = GetFileSize(f);
    if (len > 0 && len < 0x1000000 && (p = AllocVecTags((uint32)len, AVT_Type, MEMF_SHARED, TAG_DONE)) != 0) {
        if (Read(f, p, (int32)len) != (int32)len) { FreeVec(p); p = 0; }
        else *size = (ULONG)len;
    }
    Close(f);
    return p;
}



int main(int argc, char **argv)
{
    int scale = 2, full = 0, running = 1, bridge = 0, i, n;
    unsigned keys = 0, pressed = 0;
    uint64_t next_frame, tm_last = 0;
    uint32_t tm_gap_sum = 0, tm_gap_max = 0, tm_gap_min = 0xFFFFFFFFu, tm_draw_sum = 0, tm_draw_max = 0;
    int tm_frames = 0, tm_late = 0, tm_early = 0, tm_steps = 0;
    void *mod = 0;
    ULONG mod_size = 0;
    Canvas cv;
    const char *why = "Esc";

    for (i = 1; i < argc; i++) {
        if (strncasecmp(argv[i], "SCALE=", 6) == 0) scale = atoi(argv[i] + 6);
        else if (strcasecmp(argv[i], "FULLSCREEN") == 0) full = 1;
        else if (strcmp(argv[i], "?") == 0) { printf("Usage: %s [SCALE=n] [FULLSCREEN]\n", game_client); return 0; }
    }
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;

    if (!timer_open()) { printf("%s: no timer.device\n", game_client); goto out; }
    {
        int tries;
        for (tries = 0; tries < 10 && ab_init(game_client) != 0; tries++) sleep_us(300000);
        bridge = tries < 10;
        AB_I("%s (AmigaOS 4) starting (bridge after %ld retries)", game_title, (long)tries);
    }
    ab_register_var("in_keys", AB_TYPE_I32, &plat_in_keys);
    ab_register_var("in_joy",  AB_TYPE_I32, &plat_in_joy);
    a3_draw_init();
    scale = scale / A3_SUB < 1 ? 1 : scale / A3_SUB;          /* the frame is twice 320 x 256 already */
    if (!disp_open(game_title, PHYS_W, PHYS_H, scale, full)) {
        printf("%s: can't open a window or screen\n", game_client);
        AB_E("display setup failed");
        goto out;
    }
    cv.pix = argb; cv.w = PHYS_W; cv.h = PHYS_H; cv.bpp = 4;
    {
        /* the screen's byte order, before the game draws anything it keeps */
        uint32_t *p;
        int stride, order;
        if (disp_begin(&p, &stride, &order)) { a3_order = order; disp_end(); }
        {
            extern int disp_fb_why;
            AB_I("display: %s (%ld)", disp_begin(&p, &stride, &order) ? (disp_end(), "drawn in the screen's format, blitted") : "A R G B, converted", (long)disp_fb_why);
        }
    }
    if (!ahi_open()) AB_W("no ahi.device - no sound");
    game_init();
    game_sound_init();
    mod = load_file(game_music, &mod_size);
    if (mod && ahi_music(mod, mod_size)) AB_I("music loaded (%ld bytes)", (long)mod_size);
    else if (game_music[0]) AB_W("%s not found or not playable - no music", game_music);
    n = a3_voice_load(game_voice);
    if (game_voice[0]) AB_I(n ? "voice: %ld clips" : "no voice clips", (long)n);
    ab_register_var("us_convert", AB_TYPE_I32, &us_convert);
    ab_register_var("us_present", AB_TYPE_I32, &us_present);
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
                if (code == DISP_KEY_F10 || (code == DISP_KEY_F && !game_key_bit(DISP_KEY_F))) {
                    if (!disp_toggle()) { running = 0; why = "no display"; }
                    AB_I("display: %s", disp_fullscreen() ? "full screen" : "window");
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

        /* 25 game steps a second on a fixed 40 ms schedule. Sleep until the
         * next one is due (one timer request, woken early by input); if we
         * woke late, run the missed steps and draw once, so every frame
         * shown is where the game is now. Drawing each missed step back to
         * back instead showed as judder whenever the timer woke late. */
        {
            uint64_t t = now_us();
            int steps;
            if (t < next_frame) {
                if (!timer_pending) timer_start((uint32)(next_frame - t));
                Wait((1UL << timer_port->mp_SigBit) | (1UL << disp_window()->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
                if (timer_pending && CheckIO((struct IORequest *)timer_req)) {
                    WaitIO((struct IORequest *)timer_req);
                    timer_pending = 0;
                }
                ahi_update();
                continue;
            }
            if (t - next_frame > sleep_over_max) sleep_over_max = (uint32_t)(t - next_frame);
            steps = 1 + (int)((t - next_frame) / 40000);
            if (steps > 5) { steps = 1; next_frame = t; }   /* fell far behind: don't fast-forward */
            next_frame += (uint64_t)steps * 40000;
            tm_steps += steps;
            {
                static int window10;
                if (++window10 > 250) { window10 = 0; plat_in_keys = plat_in_joy = 0; }
                plat_in_keys |= (int32_t)(keys | pressed);
            }
            while (steps-- > 0) {
                game_update(keys | pressed);
                pressed = 0;
            }
        }
        {
            /* straight into the screen's format if possible (a blit shows it) */
            uint32_t *p;
            int stride, order, direct = disp_begin(&p, &stride, &order);
            uint32_t t0;
            if (direct) { cv.pix = p; cv.w = stride; a3_order = order; cv.bpp = A3_BPP(order); }
            else { cv.pix = argb; cv.w = PHYS_W; a3_order = 0; cv.bpp = 4; }
            {
                uint32_t td = plat_clock();
                game_draw(&cv, 0);
                td = (uint32_t)plat_us_since(td);
                tm_draw_sum += td;
                if (td > tm_draw_max) tm_draw_max = td;
            }
            t0 = plat_clock();
            us_convert = direct;                /* 1: drawn in the screen's format */
            if (direct) disp_end(); else disp_present(argb);
            us_present = plat_us_since(t0);
        }
        ahi_update();
        game_after_frame();
        {
            /* frame pacing report: uneven gaps between shown frames are
             * what makes a moving camera judder */
            uint64_t now = now_us();
            if (tm_last) {
                uint32_t gap = (uint32_t)(now - tm_last);
                tm_gap_sum += gap;
                if (gap > tm_gap_max) tm_gap_max = gap;
                if (gap < tm_gap_min) tm_gap_min = gap;
                if (gap > 50000) tm_late++;
                if (gap < 30000) tm_early++;
            }
            tm_last = now;
            if (++tm_frames == 250) {
                /* only when frames come unevenly: a guest whose timer wakes
                 * late (seen on a busy QEMU) shows here */
                if (tm_late + tm_early > 12)
                AB_I("timing: gap avg %ld min %ld max %ld ms, %ld late >50 ms, %ld early <30 ms; draw avg %ld max %ld ms; woke late max %ld ms; %ld steps in 250 frames",
                     (long)(tm_gap_sum / 249000), (long)(tm_gap_min / 1000), (long)(tm_gap_max / 1000),
                     (long)tm_late, (long)tm_early, (long)(tm_draw_sum / 250000), (long)(tm_draw_max / 1000),
                     (long)(sleep_over_max / 1000), (long)tm_steps);
                tm_frames = 0; tm_steps = 0; tm_gap_sum = tm_draw_sum = 0; tm_gap_max = tm_draw_max = 0;
                tm_gap_min = 0xFFFFFFFFu; tm_late = tm_early = 0; tm_last = 0; sleep_over_max = 0;
            }
        }
    }
    timer_stop();
    AB_I("%s shutting down (%s)", game_title, why);

out:
    ahi_close();
    a3_sfx_cleanup();
    if (mod) FreeVec(mod);
    disp_close();
    timer_close();
    if (bridge) ab_cleanup();
    return 0;
}
