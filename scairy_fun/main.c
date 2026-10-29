// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * SCAIRY FUN for the classic Amiga (68020+, OS 3.0+)
 *
 * You ride in a self-driving car. Points come from distance driven on
 * autopilot; take the wheel just before it gets something wrong, then
 * hand it back.
 *
 *   Left / right, down   steer, brake (takes over)    up   accelerate
 *   Space / fire         autopilot on / off           V    chase / cockpit
 *   P                    pause                        Esc  title / quit
 *
 * This file is the classic Amiga's side: the double-buffered PAL screen,
 * the copper sky gradient, the palette (washed out by the low sun), the
 * joystick, ptplayer and the timers. The game's side of the frame loop,
 * and everything exposed over the AmigaBridge, is in game.c.
 */
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <graphics/copper.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <devices/timer.h>
#include <proto/timer.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "sc.h"
#include "models.h"
#include "render.h"
#include "sound.h"
#include "ptplayer.h"
#include "game.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
struct Device *TimerBase = NULL;
static struct timerequest timer_io;
static ULONG eclock_khz = 709;

extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

static struct Screen *screen;
static struct Window *window;
static struct ScreenBuffer *sbuf[2];
static struct RastPort rp_buf[2];
static struct MsgPort *db_port[2];
static WORD cur_buf;

static UBYTE *mod_data;
static ULONG mod_size;
static int music_on;

/* ---------- input ---------- */
static unsigned keys, pressed;          /* held now; pressed since the last frame */
static int32_t v_in_keys, v_in_joy;     /* bridge: what each input source says */

static unsigned read_joystick(void)
{
    UWORD joy = custom.joy1dat;
    unsigned r = 0;
    UWORD right = (joy >> 1) & 1, left = (joy >> 9) & 1;
    if (right) r |= IN_RIGHT;
    if (left) r |= IN_LEFT;
    if (((joy >> 1) ^ joy) & 1) r |= IN_DOWN;
    if (((joy >> 9) ^ (joy >> 8)) & 1) r |= IN_UP;
    if (!(ciaa.ciapra & 0x80)) r |= IN_FIRE;
    return r;
}

/* ---------- display ---------- */
static int setup_display(void)
{
    int i;
    screen = OpenScreenTags(NULL,
        SA_Width, SCREEN_W, SA_Height, SCREEN_H, SA_Depth, 5,
        SA_DisplayID, LORES_KEY, SA_Title, (ULONG)"ScAIry Fun",
        SA_ShowTitle, FALSE, SA_Quiet, TRUE, SA_Type, CUSTOMSCREEN, TAG_DONE);
    if (!screen) { AB_E("OpenScreen failed"); return 0; }
    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    db_port[0] = CreateMsgPort();
    db_port[1] = CreateMsgPort();
    if (!sbuf[0] || !sbuf[1] || !db_port[0] || !db_port[1]) { AB_E("screen buffers failed"); return 0; }
    for (i = 0; i < 2; i++) {
        sbuf[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = db_port[i];
        InitRastPort(&rp_buf[i]);
        rp_buf[i].BitMap = sbuf[i]->sb_BitMap;
        SetRast(&rp_buf[i], 0);
    }
    window = OpenWindowTags(NULL,
        WA_Left, 0, WA_Top, 0, WA_Width, SCREEN_W, WA_Height, SCREEN_H,
        WA_CustomScreen, (ULONG)screen, WA_Borderless, TRUE, WA_Backdrop, TRUE,
        WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_IDCMP, IDCMP_RAWKEY, TAG_DONE);
    if (!window) { AB_E("OpenWindow failed"); return 0; }
    cur_buf = 1;
    return 1;
}

/* Show the buffer we just drew, then wait for ITS SafeMessage: the
 * previously displayed bitmap, the one we draw into next, is then really
 * off screen. */
static void swap_buffers(void)
{
    WaitBlit();
    if (ChangeScreenBuffer(screen, sbuf[cur_buf])) {
        while (!GetMsg(db_port[cur_buf])) WaitPort(db_port[cur_buf]);
    }
    cur_buf ^= 1;
}

static void cleanup_display(void)
{
    int i;
    if (window) CloseWindow(window);
    for (i = 0; i < 2; i++) {
        if (sbuf[i]) FreeScreenBuffer(screen, sbuf[i]);
        if (db_port[i]) DeleteMsgPort(db_port[i]);
    }
    if (screen) CloseScreen(screen);      /* frees the user copper list too */
}

/* ---------- colours ---------- */
static UWORD mix(UWORD c, UWORD to, int num, int den)
{
    int r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
    int tr = (to >> 8) & 15, tg = (to >> 4) & 15, tb = to & 15;
    r += ((tr - r) * num) / den;
    g += ((tg - g) * num) / den;
    b += ((tb - b) * num) / den;
    return (UWORD)((r << 8) | (g << 4) | b);
}

/* the route's colours; the low sun washes everything towards white */
void plat_palette(int route, int glare)
{
    UWORD pal[32];
    int i;
    for (i = 0; i < 32; i++) {
        pal[i] = route_palette[route][i];
        if (glare > 0 && i >= 2) pal[i] = mix(pal[i], 0xFFE, glare * 10, 16 * 16);
    }
    LoadRGB4(&screen->ViewPort, pal, 32);
}

/* The sky: the copper changes PEN_SKY every few lines from the route's top
 * colour to its horizon colour. */
void plat_sky(int route, int view)
{
    struct ViewPort *vp = &screen->ViewPort;
    struct UCopList *ucl, *old;
    int hz = render_horizon(view), steps = 16, k;

    ucl = (struct UCopList *)AllocMem(sizeof(struct UCopList), MEMF_PUBLIC | MEMF_CLEAR);
    if (!ucl) return;
    CINIT(ucl, steps * 2 + 4);
    for (k = 0; k < steps; k++) {
        CWAIT(ucl, (k * hz) / steps, 0);
        CMOVE(ucl, custom.color[PEN_SKY], mix(route_sky[route][0], route_sky[route][1], k, steps - 1));
    }
    CEND(ucl);
    Forbid();
    old = vp->UCopIns;
    vp->UCopIns = ucl;
    Permit();
    RethinkDisplay();
    if (old) {
        FreeCopList(old->FirstCopList);
        FreeMem(old, sizeof(struct UCopList));
    }
}

static UBYTE *load_chip(const char *path, ULONG *size)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    UBYTE *buf;
    LONG len;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    buf = len > 0 ? (UBYTE *)AllocMem(len, MEMF_CHIP) : NULL;
    if (buf && Read(fh, buf, len) != len) { FreeMem(buf, len); buf = NULL; }
    Close(fh);
    *size = (ULONG)len;
    return buf;
}

static ULONG eclock_lo(void)
{
    struct EClockVal ev;
    ReadEClock(&ev);
    return ev.ev_lo;
}

/* ---------- what game.c needs from the platform ---------- */
uint32_t plat_clock(void) { return eclock_lo(); }

int32_t plat_us_since(uint32_t t0)
{
    return (int32_t)(((eclock_lo() - t0) * 1000UL) / eclock_khz);
}

int32_t plat_ticks(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Minute * 3000L + ds.ds_Tick;
}

int main(void)
{
    int running = 1, n;

    /* A previous instance may still be unregistering (devbench's amiga_run
     * stops it and relaunches 0.3 s later), so retry for up to 3 s. */
    {
        int tries;
        for (tries = 0; tries < 30 && ab_init("scairy_fun") != 0; tries++) Delay(5);
        AB_I("ScAIry Fun starting (bridge after %ld retries)", (long)tries);
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!IntuitionBase || !GfxBase) { AB_E("needs OS 3.0"); goto out; }

    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&timer_io, 0) == 0) {
        struct EClockVal ev;
        TimerBase = timer_io.tr_node.io_Device;
        eclock_khz = ReadEClock(&ev) / 1000;
    }
    game_init();
    ab_register_var("in_keys", AB_TYPE_I32, &v_in_keys);
    ab_register_var("in_joy",  AB_TYPE_I32, &v_in_joy);
    if (!render_init() || !setup_display()) { AB_E("display setup failed"); goto out; }

    sound_init();
    n = voice_load("PROGDIR:scairy.voc");
    AB_I(n ? "voice: %ld clips" : "scairy.voc not found - the AI is silent", (long)n);
    mod_data = load_chip("PROGDIR:scairy.mod", &mod_size);
    if (mod_data) {
        mt_install_cia(CUSTOM_BASE, NULL, 1);
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;
        mt_musicmask(CUSTOM_BASE, 0x03);    /* effects and the voice never take the melody or bass */
        mt_Enable = 1;
        music_on = 1;
        AB_I("music loaded (%ld bytes)", (long)mod_size);
    } else {
        /* ptplayer still plays the effects and the voice without a song
         * (mt_install_cia resets its channels) */
        mt_install_cia(CUSTOM_BASE, NULL, 1);
        mt_MusicChannels = 0;
        mt_Enable = 0;
        music_on = 2;
        AB_W("scairy.mod not found - no music");
    }
    AB_I("chip free %ld, fast free %ld", (long)AvailMem(MEMF_CHIP), (long)AvailMem(MEMF_FAST));
    SetSignal(0L, SIGBREAKF_CTRL_C);

    while (running) {
        struct IntuiMessage *msg;
        unsigned in;

        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            if (msg->Class == IDCMP_RAWKEY) {
                unsigned b = game_key_bit(msg->Code & 0x7F);
                if (msg->Code & 0x80) keys &= ~b;
                else { keys |= b; pressed |= b; }
            }
            ReplyMsg((struct Message *)msg);
        }
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) running = 0;

        /* a key pressed and released within one frame still counts
         * (devbench's key injection does exactly that) */
        {
            /* in_joy / in_keys: every bit seen over the last 10 s, so a
             * glitch shows up when read over the bridge */
            static int window;
            unsigned joy = read_joystick();
            if (++window > 250) { window = 0; v_in_joy = v_in_keys = 0; }
            v_in_joy |= (int32_t)joy;
            v_in_keys |= (int32_t)(keys | pressed);
            in = keys | pressed | joy;
        }
        pressed = 0;
        if (!game_input(in)) running = 0;
        game_update(in);
        game_draw(&rp_buf[cur_buf], cur_buf);
        game_after_frame();
        swap_buffers();
    }

    AB_I("ScAIry Fun shutting down, score %ld", (long)world.score);
out:
    if (music_on) { mt_end(CUSTOM_BASE); mt_remove_cia(CUSTOM_BASE); }
    if (mod_data) FreeMem(mod_data, mod_size);
    sound_cleanup();
    cleanup_display();
    render_cleanup();
    ab_cleanup();
    if (TimerBase) CloseDevice((struct IORequest *)&timer_io);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
