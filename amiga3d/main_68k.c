// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * main_68k.c - the shared engine's classic Amiga (68020+, OS 3.0+) main:
 * a double-buffered 320x256 PAL screen of 32 colours, the copper gradient
 * for plat_sky, the keyboard and the joystick, ptplayer for the music and
 * the effects, and the AmigaBridge client. The game is in its own files
 * (a3d_plat.h).
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

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "ptplayer.h"

#define CUSTOM_BASE ((void *)0xdff000)

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
int32_t plat_in_keys, plat_in_joy;

/* ---------- input ---------- */
static unsigned read_joystick(void)
{
    UWORD joy = custom.joy1dat;
    unsigned r = 0;
    if ((joy >> 1) & 1) r |= A3_JOY_RIGHT;
    if ((joy >> 9) & 1) r |= A3_JOY_LEFT;
    if (((joy >> 1) ^ joy) & 1) r |= A3_JOY_DOWN;
    if (((joy >> 9) ^ (joy >> 8)) & 1) r |= A3_JOY_UP;
    if (!(ciaa.ciapra & 0x80)) r |= A3_JOY_FIRE;
    return r;
}

/* ---------- display ---------- */
static int setup_display(void)
{
    int i;
    screen = OpenScreenTags(NULL,
        SA_Width, SCREEN_W, SA_Height, SCREEN_H, SA_Depth, 5,
        SA_DisplayID, LORES_KEY, SA_Title, (ULONG)game_title,
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

/* Show the buffer just drawn, then wait for ITS SafeMessage: the other
 * bitmap, the one drawn into next, is then really off screen. */
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

/* ---------- what the game gets ---------- */
void plat_activate(void)
{
    if (screen) ScreenToFront(screen);
    if (window) ActivateWindow(window);
}

void plat_palette(const uint16_t pal[32])
{
    if (screen) LoadRGB4(&screen->ViewPort, (UWORD *)pal, 32);
}

static UWORD mix(UWORD c, UWORD to, int num, int den)
{
    int r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
    r += ((((to >> 8) & 15) - r) * num) / den;
    g += ((((to >> 4) & 15) - g) * num) / den;
    b += (((to & 15) - b) * num) / den;
    return (UWORD)((r << 8) | (g << 4) | b);
}

void plat_sky(int pen, uint16_t top, uint16_t bottom, int rows)
{
    struct ViewPort *vp;
    struct UCopList *ucl = NULL, *old;
    int steps = 16, k;
    if (!screen) return;
    vp = &screen->ViewPort;
    if (rows > 0) {
        ucl = (struct UCopList *)AllocMem(sizeof(struct UCopList), MEMF_PUBLIC | MEMF_CLEAR);
        if (!ucl) return;
        CINIT(ucl, steps * 2 + 4);
        for (k = 0; k < steps; k++) {
            CWAIT(ucl, (k * rows) / steps, 0);
            CMOVE(ucl, custom.color[pen], mix(top, bottom, k, steps - 1));
        }
        CEND(ucl);
    }
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

void *a3_sound_alloc(int len) { return AllocMem(len, MEMF_CHIP | MEMF_CLEAR); }
void a3_sound_free(void *p, int len) { FreeMem(p, len); }

void a3_sound_play(int8_t *data, int len, int period, int vol, int pri)
{
    static SfxStructure s[4];
    static int k;
    SfxStructure *x = &s[k++ & 3];      /* ptplayer reads it when the interrupt comes */
    x->sfx_ptr = data;
    x->sfx_len = (WORD)(len / 2);
    x->sfx_per = (WORD)period;
    x->sfx_vol = (WORD)vol;
    x->sfx_cha = -1;
    x->sfx_pri = (BYTE)pri;
    mt_playfx(CUSTOM_BASE, x);
}

static UBYTE *load_chip(const char *path, ULONG *size)
{
    BPTR fh;
    UBYTE *buf;
    LONG len;
    if (!path[0] || !(fh = Open((CONST_STRPTR)path, MODE_OLDFILE))) return NULL;
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
    unsigned keys = 0, pressed = 0;

    /* A previous instance may still be unregistering (devbench's amiga_run
     * stops it and relaunches 0.3 s later), so retry for up to 3 s. */
    {
        int tries;
        for (tries = 0; tries < 30 && ab_init(game_client) != 0; tries++) Delay(5);
        AB_I("%s starting (bridge after %ld retries)", game_title, (long)tries);
    }
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!IntuitionBase || !GfxBase) { AB_E("needs OS 3.0"); goto out; }
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&timer_io, 0) == 0) {
        struct EClockVal ev;
        TimerBase = timer_io.tr_node.io_Device;
        eclock_khz = ReadEClock(&ev) / 1000;
    }
    ab_register_var("in_keys", AB_TYPE_I32, &plat_in_keys);
    ab_register_var("in_joy",  AB_TYPE_I32, &plat_in_joy);
    a3_draw_init();
    if (!setup_display()) { AB_E("display setup failed"); goto out; }
    game_init();

    mt_install_cia(CUSTOM_BASE, NULL, 1);     /* resets ptplayer's channels too */
    mt_musicmask(CUSTOM_BASE, 0x03);          /* effects never take the melody or bass */
    mod_data = load_chip(game_music, &mod_size);
    if (mod_data) {
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;
        mt_Enable = 1;
        AB_I("music loaded (%ld bytes)", (long)mod_size);
    } else if (game_music[0]) {
        AB_W("%s not found - no music", game_music);
    }
    music_on = 1;
    game_sound_init();
    n = a3_voice_load(game_voice);
    if (game_voice[0]) AB_I(n ? "voice: %ld clips" : "no voice clips", (long)n);
    AB_I("chip free %ld, fast free %ld", (long)AvailMem(MEMF_CHIP), (long)AvailMem(MEMF_FAST));
    SetSignal(0L, SIGBREAKF_CTRL_C);

    while (running) {
        struct IntuiMessage *msg;
        unsigned in, joy;
        static int window10;

        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            if (msg->Class == IDCMP_RAWKEY) {
                unsigned b = game_key_bit(msg->Code & 0x7F);
                if (msg->Code & 0x80) keys &= ~b;
                else { keys |= b; pressed |= b; }   /* seen even if released within the frame */
            }
            ReplyMsg((struct Message *)msg);
        }
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) running = 0;

        joy = read_joystick();
        /* every input bit seen over the last 10 s, readable over the bridge */
        if (++window10 > 250) { window10 = 0; plat_in_keys = plat_in_joy = 0; }
        plat_in_joy |= (int32_t)joy;
        plat_in_keys |= (int32_t)(keys | pressed);
        in = keys | pressed | game_joy_bits(joy);
        pressed = 0;
        if (!game_input(in)) running = 0;
        game_update(in);
        game_draw(&rp_buf[cur_buf], cur_buf);
        game_after_frame();
        swap_buffers();
    }
    AB_I("%s shutting down", game_title);
out:
    if (music_on) { mt_end(CUSTOM_BASE); mt_remove_cia(CUSTOM_BASE); }
    if (mod_data) FreeMem(mod_data, mod_size);
    a3_sfx_cleanup();
    cleanup_display();
    ab_cleanup();
    if (TimerBase) CloseDevice((struct IORequest *)&timer_io);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
