// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * sound_ahi.c - Terra Strike's sound on AmigaOS 4, through ahi.device.
 *
 * The music is terra_strike.mod rendered by modplay.c in 0.2 s chunks,
 * 16-bit stereo, with two requests always queued behind each other
 * (ahir_Link) so it plays without gaps. The effects that sound.c
 * synthesises go out on three more requests at the rate their Paula
 * period gives; like ptplayer, a new effect doesn't cut one of higher
 * priority. No ahi.device (or no working unit) just means silence.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <string.h>
#include "sound.h"
#include "modplay.h"

#define RATE       22050
#define CHUNK      (RATE / 5)           /* frames per music request */
#define MUSIC_VOL  0xA000               /* Fixed: a little under the effects */
#define PAULA_PAL  3546895UL

enum { R_MUSIC = 0, R_FX = 2, N_FX = 3, NREQ = R_FX + N_FX };

static struct MsgPort    *port;
static struct AHIRequest *req[NREQ];
static int                busy[NREQ], pri[NREQ], dev_open;
static int16_t           *mbuf[2];      /* one chunk per music request */
static int                music_on;

int ahi_open(void)
{
    int i;
    port = AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (!port) return 0;
    req[0] = AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_ReplyPort, port,
                                ASOIOR_Size, sizeof(struct AHIRequest), TAG_DONE);
    if (!req[0]) return 0;
    req[0]->ahir_Version = 4;
    if (OpenDevice((CONST_STRPTR)AHINAME, AHI_DEFAULT_UNIT, (struct IORequest *)req[0], 0) != 0) {
        FreeSysObject(ASOT_IOREQUEST, req[0]);
        req[0] = 0;
        return 0;
    }
    dev_open = 1;
    for (i = 1; i < NREQ; i++)
        if (!(req[i] = AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Duplicate, req[0], TAG_DONE))) return 0;
    return 1;
}

static void stop(int i)
{
    if (!busy[i]) return;
    if (!CheckIO((struct IORequest *)req[i])) AbortIO((struct IORequest *)req[i]);
    WaitIO((struct IORequest *)req[i]);
    busy[i] = 0;
}

void ahi_close(void)
{
    int i;
    for (i = 0; i < NREQ; i++) if (req[i]) stop(i);
    if (dev_open) CloseDevice((struct IORequest *)req[0]);
    for (i = NREQ - 1; i >= 0; i--) if (req[i]) FreeSysObject(ASOT_IOREQUEST, req[i]);
    if (port) FreeSysObject(ASOT_PORT, port);
    for (i = 0; i < 2; i++) if (mbuf[i]) FreeVec(mbuf[i]);
    memset(req, 0, sizeof(req));
    mbuf[0] = mbuf[1] = 0;
    port = 0;
    dev_open = music_on = 0;
}

static void send(int i, APTR data, ULONG len, ULONG type, ULONG freq, Fixed vol, struct AHIRequest *link)
{
    struct AHIRequest *r = req[i];
    r->ahir_Std.io_Command = CMD_WRITE;
    r->ahir_Std.io_Data = data;
    r->ahir_Std.io_Length = len;
    r->ahir_Std.io_Offset = 0;
    r->ahir_Type = type;
    r->ahir_Frequency = freq;
    r->ahir_Volume = vol;
    r->ahir_Position = 0x8000;
    r->ahir_Link = link;
    SendIO((struct IORequest *)r);
    busy[i] = 1;
}

/* collect the requests that have finished */
static void reap(void)
{
    int i;
    for (i = 0; i < NREQ; i++)
        if (busy[i] && CheckIO((struct IORequest *)req[i])) {
            WaitIO((struct IORequest *)req[i]);
            busy[i] = 0;
        }
}

void ahi_sfx(const SfxStructure *s)
{
    int i, pick = -1;
    if (!dev_open || !s->sfx_ptr || s->sfx_per <= 0) return;
    reap();
    for (i = R_FX; i < R_FX + N_FX && pick < 0; i++) if (!busy[i]) pick = i;
    if (pick < 0) {                              /* all busy: the lowest priority, if not above ours */
        for (i = R_FX; i < R_FX + N_FX; i++)
            if (pri[i] <= s->sfx_pri && (pick < 0 || pri[i] < pri[pick])) pick = i;
        if (pick < 0) return;
        stop(pick);
    }
    pri[pick] = s->sfx_pri;
    send(pick, s->sfx_ptr, (ULONG)s->sfx_len * 2, AHIST_M8S, PAULA_PAL / (ULONG)s->sfx_per,
         (Fixed)(0x10000L * s->sfx_vol / 64), 0);
}

int ahi_music(const void *mod, ULONG len)
{
    int i;
    if (!dev_open || !mod_load((const uint8_t *)mod, (long)len)) return 0;
    for (i = 0; i < 2; i++)
        if (!(mbuf[i] = AllocVecTags(CHUNK * 4, AVT_Type, MEMF_SHARED, TAG_DONE))) return 0;
    music_on = 1;
    ahi_update();
    return 1;
}

/* once a frame: keep two music chunks queued */
void ahi_update(void)
{
    int i;
    if (!dev_open) return;
    reap();
    if (!music_on) return;
    for (i = R_MUSIC; i < R_MUSIC + 2; i++)
        if (!busy[i]) {
            int other = i == R_MUSIC ? R_MUSIC + 1 : R_MUSIC;
            mod_render(mbuf[i - R_MUSIC], CHUNK, RATE);
            send(i, mbuf[i - R_MUSIC], CHUNK * 4, AHIST_S16S, RATE, MUSIC_VOL,
                 busy[other] ? req[other] : 0);
        }
}
