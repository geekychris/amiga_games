// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * TERRA STRIKE for the classic Amiga (68020+)
 *
 * A Zarch / Carrier Command style landscape shooter: lift off from the
 * landing pad, fly over a wrapping fractal island and destroy the tanks,
 * buildings and ships. Land on the pad again to refuel and repair.
 *
 *   Arrows / joystick   turn and climb / dive
 *   A / Z               throttle up / down
 *   Space / fire        cannon
 *   P                   pause        Esc   title / quit
 *
 * This file is the classic Amiga's side: the double-buffered PAL screen,
 * the joystick, ptplayer and the timers. The game's side of the frame
 * loop, and everything exposed over the AmigaBridge, is in game.c; the
 * AmigaOS 4 version of this file is main_os4.c.
 */
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <devices/timer.h>
#include <proto/timer.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "ts.h"
#include "models.h"
#include "render.h"
#include "title_image.h"
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

/* ---------- input ---------- */
static unsigned keys;

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
        SA_DisplayID, LORES_KEY, SA_Title, (ULONG)"Terra Strike",
        SA_ShowTitle, FALSE, SA_Quiet, TRUE, SA_Type, CUSTOMSCREEN, TAG_DONE);
    if (!screen) return 0;
    for (i = 0; i < 32; i++)
        SetRGB4(&screen->ViewPort, i, (palette[i] >> 8) & 15, (palette[i] >> 4) & 15, palette[i] & 15);

    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    db_port[0] = CreateMsgPort();
    db_port[1] = CreateMsgPort();
    if (!sbuf[0] || !sbuf[1] || !db_port[0] || !db_port[1]) return 0;
    for (i = 0; i < 2; i++) {
        sbuf[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = db_port[i];
        InitRastPort(&rp_buf[i]);
        rp_buf[i].BitMap = sbuf[i]->sb_BitMap;
        render_attach(&rp_buf[i]);
        SetRast(&rp_buf[i], 0);
    }
    window = OpenWindowTags(NULL,
        WA_Left, 0, WA_Top, 0, WA_Width, SCREEN_W, WA_Height, SCREEN_H,
        WA_CustomScreen, (ULONG)screen, WA_Borderless, TRUE, WA_Backdrop, TRUE,
        WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_IDCMP, IDCMP_RAWKEY, TAG_DONE);
    if (!window) return 0;
    cur_buf = 1;
    return 1;
}

/* Show the buffer we just drew, then wait for ITS SafeMessage: that is
 * the signal that the previously displayed bitmap - the one we draw into
 * next - is really off screen. (Waiting on the other buffer's message, as
 * the code this started from did, lets the next frame's blitter clear
 * run on the visible bitmap: planes vanish one by one on screen.) */
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
    if (screen) CloseScreen(screen);
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

void plat_palette(int title)
{
    LoadRGB4(&screen->ViewPort, (UWORD *)(title ? title_palette : palette), 32);
}

int main(void)
{
    int running = 1;

    /* A previous instance may still be unregistering (devbench's amiga_run
     * stops it and relaunches 0.3 s later), so retry for up to 3 s. */
    {
        int tries;
        for (tries = 0; tries < 30 && ab_init("terra_strike") != 0; tries++) Delay(5);
        AB_I("Terra Strike starting (bridge after %ld retries)", (long)tries);
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
    if (!render_init() || !setup_display()) { AB_E("display setup failed"); goto out; }

    sound_init();
    mod_data = load_chip("PROGDIR:terra_strike.mod", &mod_size);
    if (mod_data) {
        mt_install_cia(CUSTOM_BASE, NULL, 1);
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;
        mt_Enable = 1;
        music_on = 1;
        AB_I("music loaded (%ld bytes)", (long)mod_size);
    } else {
        AB_W("terra_strike.mod not found - no music");
    }

    AB_I("island seed %ld, pad at tile %ld,%ld height %ld, %ld targets",
         (long)world.seed, (long)world.pad_tx, (long)world.pad_tz, (long)world.pad_h,
         (long)world.targets_left);
    SetSignal(0L, SIGBREAKF_CTRL_C);

    while (running) {
        struct IntuiMessage *msg;
        unsigned in;

        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            if (msg->Class == IDCMP_RAWKEY) {
                unsigned b = game_key_bit(msg->Code & 0x7F);
                if (msg->Code & 0x80) keys &= ~b; else keys |= b;
            }
            ReplyMsg((struct Message *)msg);
        }
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) running = 0;

        in = keys | read_joystick();
        if (!game_input(in)) running = 0;
        game_update(in);
        game_draw(&rp_buf[cur_buf], cur_buf);
        game_after_frame();
        swap_buffers();
    }

    AB_I("Terra Strike shutting down, score %ld", (long)world.score);
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
