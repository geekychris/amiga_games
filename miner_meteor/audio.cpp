// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Sound effects. The samples are short 8-bit renders of the Atari Lynx
 * port's Mikey effects, made by tools/gen_sfx.py (sfx_data.cpp).
 *
 * Three voices, as the Lynx port uses three Mikey channels: jump and land
 * share one, the jingles (key, cavern cleared, game over, win) another,
 * and the death noise has its own, so a death under a jingle is heard. A
 * new effect on a voice cuts the one playing there.
 *
 *   Classic 68k: audio.device, one channel allocated per voice
 *                (samples copied to chip RAM), as rotoball does.
 *   AmigaOS 4:   ahi.device, one request per voice, as terra_strike's
 *                sound_ahi.c does (sam460ex has no Paula).
 *
 * Anything missing (no device, no free channel, no memory) leaves that
 * voice, or all of them, silent; the game runs the same. Every trigger
 * still goes to the bridge log ("sfx: <name>"), which tests read.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <proto/exec.h>
#include <string.h>

#include "audio.h"
#include "sfx_data.h"
extern "C" {
#include "bridge_client.h"
}

enum { V_MOVE = 0, V_JINGLE = 1, V_DEATH = 2, NVOICE = 3 };

#ifdef __PPC__
/* ------------------------------------------------------------ AmigaOS 4 */
#include <devices/ahi.h>

static struct MsgPort    *port;
static struct AHIRequest *req[NVOICE];
static int                busy[NVOICE], dev_open;
static BYTE              *pcm;

static void stop(int v)
{
    if (!busy[v]) return;
    if (!CheckIO((struct IORequest *)req[v])) AbortIO((struct IORequest *)req[v]);
    WaitIO((struct IORequest *)req[v]);
    busy[v] = 0;
}

static int backend_open(void)
{
    int v;
    pcm = (BYTE *)AllocVecTags(SFX_PCM_BYTES, AVT_Type, MEMF_SHARED, TAG_DONE);
    if (!pcm) return 0;
    memcpy(pcm, sfx_pcm, SFX_PCM_BYTES);      /* 58 KB: under the newlib memcpy bug's size */
    port = (struct MsgPort *)AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (!port) return 0;
    req[0] = (struct AHIRequest *)AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_ReplyPort, port,
                                                     ASOIOR_Size, sizeof(struct AHIRequest), TAG_DONE);
    if (!req[0]) return 0;
    req[0]->ahir_Version = 4;
    if (OpenDevice((CONST_STRPTR)AHINAME, AHI_DEFAULT_UNIT, (struct IORequest *)req[0], 0) != 0) {
        FreeSysObject(ASOT_IOREQUEST, req[0]);
        req[0] = 0;
        return 0;
    }
    dev_open = 1;
    for (v = 1; v < NVOICE; v++)
        if (!(req[v] = (struct AHIRequest *)AllocSysObjectTags(ASOT_IOREQUEST,
                                                               ASOIOR_Duplicate, req[0], TAG_DONE)))
            return 0;
    return 1;
}

static void backend_close(void)
{
    int v;
    for (v = 0; v < NVOICE; v++) if (req[v]) stop(v);
    if (dev_open) CloseDevice((struct IORequest *)req[0]);
    for (v = NVOICE - 1; v >= 0; v--) if (req[v]) FreeSysObject(ASOT_IOREQUEST, req[v]);
    if (port) FreeSysObject(ASOT_PORT, port);
    if (pcm) FreeVec(pcm);
    memset(req, 0, sizeof(req));
    port = 0;
    pcm = 0;
    dev_open = 0;
}

static void play(int v, int id)
{
    struct AHIRequest *r = req[v];
    if (!dev_open || !r || !pcm) return;
    stop(v);
    r->ahir_Std.io_Command = CMD_WRITE;
    r->ahir_Std.io_Data    = pcm + sfx_offset[id];
    r->ahir_Std.io_Length  = sfx_offset[id + 1] - sfx_offset[id];
    r->ahir_Std.io_Offset  = 0;
    r->ahir_Type      = AHIST_M8S;
    r->ahir_Frequency = SFX_RATE;
    r->ahir_Volume    = 0x10000;
    r->ahir_Position  = 0x8000;
    r->ahir_Link      = 0;
    SendIO((struct IORequest *)r);
    busy[v] = 1;
}

static int voices_ready(void) { return dev_open ? NVOICE : 0; }

#else
/* ---------------------------------------------------------- classic 68k */
#include <devices/audio.h>
extern "C" {
#include <clib/alib_protos.h>    /* BeginIO() (amiga.lib) */
}

#define SFX_VOLUME 64

static struct MsgPort *port;
static struct IOAudio *io[NVOICE];
static int             opened[NVOICE], busy[NVOICE];
static BYTE           *pcm;                 /* chip RAM */
static UBYTE           any_channel[] = { 1, 2, 4, 8 };

static void stop(int v)
{
    if (!busy[v]) return;
    if (!CheckIO((struct IORequest *)io[v])) AbortIO((struct IORequest *)io[v]);
    WaitIO((struct IORequest *)io[v]);
    busy[v] = 0;
}

static int backend_open(void)
{
    int v, n = 0;
    pcm = (BYTE *)AllocMem(SFX_PCM_BYTES, MEMF_CHIP);
    if (!pcm) return 0;
    CopyMem((APTR)sfx_pcm, pcm, SFX_PCM_BYTES);
    port = CreateMsgPort();
    if (!port) return 0;
    /* One open per voice, each allocating any free channel. */
    for (v = 0; v < NVOICE; v++) {
        io[v] = (struct IOAudio *)CreateIORequest(port, sizeof(struct IOAudio));
        if (!io[v]) break;
        io[v]->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
        io[v]->ioa_Request.io_Command = ADCMD_ALLOCATE;
        io[v]->ioa_Request.io_Flags   = ADIOF_NOWAIT;
        io[v]->ioa_AllocKey = 0;
        io[v]->ioa_Data     = any_channel;
        io[v]->ioa_Length   = sizeof(any_channel);
        if (OpenDevice((STRPTR)AUDIONAME, 0, (struct IORequest *)io[v], 0) != 0) break;
        opened[v] = 1;
        n++;
    }
    return n;
}

static void backend_close(void)
{
    int v;
    for (v = 0; v < NVOICE; v++) {
        if (opened[v]) { stop(v); CloseDevice((struct IORequest *)io[v]); opened[v] = 0; }
        if (io[v]) { DeleteIORequest((struct IORequest *)io[v]); io[v] = 0; }
    }
    if (port) { DeleteMsgPort(port); port = 0; }
    if (pcm) { FreeMem(pcm, SFX_PCM_BYTES); pcm = 0; }
}

static void play(int v, int id)
{
    struct IOAudio *a;
    /* fewer channels than voices: fall back to the first one we have */
    while (v > 0 && !opened[v]) v--;
    if (!opened[v] || !pcm) return;
    a = io[v];
    stop(v);
    a->ioa_Request.io_Command = CMD_WRITE;
    a->ioa_Request.io_Flags   = ADIOF_PERVOL;
    a->ioa_Data   = (UBYTE *)(pcm + sfx_offset[id]);
    a->ioa_Length = sfx_offset[id + 1] - sfx_offset[id];
    a->ioa_Period = SFX_PERIOD;
    a->ioa_Volume = SFX_VOLUME;
    a->ioa_Cycles = 1;
    BeginIO((struct IORequest *)a);
    busy[v] = 1;
}

static int voices_ready(void)
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++) n += opened[v];
    return pcm ? n : 0;
}

#endif

/* ---------------------------------------------------------------- common */

void audio_init(void)
{
    int n = backend_open();
    if (!n) {
        backend_close();
        AB_W("audio: no sound device, running silent");
        return;
    }
    AB_I("audio: init, %ld voices at %ld Hz", (long)voices_ready(), (long)SFX_RATE);
}

void audio_shutdown(void)
{
    backend_close();
    AB_I("audio: shutdown");
}

void sfx_jump(void)     { AB_I("sfx: jump");     play(V_MOVE,   SFX_JUMP); }
void sfx_land(void)     { AB_I("sfx: land");     play(V_MOVE,   SFX_LAND); }
void sfx_key(void)      { AB_I("sfx: key");      play(V_JINGLE, SFX_KEY); }
void sfx_death(void)    { AB_I("sfx: death");    play(V_DEATH,  SFX_DEATH); }
void sfx_levelwin(void) { AB_I("sfx: levelwin"); play(V_JINGLE, SFX_LEVELWIN); }
void sfx_gameover(void) { AB_I("sfx: gameover"); play(V_JINGLE, SFX_GAMEOVER); }
void sfx_win(void)      { AB_I("sfx: win");      play(V_JINGLE, SFX_WIN); }

/* "jump", "land", ... -> plays it (the bridge's "sfx" hook). 0 if unknown. */
int sfx_by_name(const char *name)
{
    static const struct { const char *name; void (*fn)(void); } t[] = {
        { "jump", sfx_jump }, { "land", sfx_land }, { "key", sfx_key },
        { "death", sfx_death }, { "levelwin", sfx_levelwin },
        { "gameover", sfx_gameover }, { "win", sfx_win },
    };
    unsigned i;
    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        if (name && !strcmp(name, t[i].name)) { t[i].fn(); return 1; }
    return 0;
}
