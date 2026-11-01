// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * sound_ahi.c - Boing Twist's sounds through ahi.device, after the shared
 * engine's (../amiga3d/sound_ahi.c) without the music: four requests for
 * effects, the oldest cut when all are busy. The sounds are synthesised
 * here: the Boing ball's thud (a falling low tone and a knock), a click
 * for a twist, a whoosh for a flip and a little chord for a solved ball.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "sound_ahi.h"

#define RATE 22050
#define NREQ 4

int snd_volume = 56;

static struct MsgPort    *port;
static struct AHIRequest *req[NREQ];
static int                busy[NREQ], dev_open, next;
static int8_t            *sample[SND_COUNT];
static int                slen[SND_COUNT];

static uint32_t noise_state = 0x1234567u;
static float noise(void)
{
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    return (float)(noise_state & 0xFFFF) / 32768.0f - 1.0f;
}

static int8_t clamp8(float v)
{
    if (v > 127) v = 127;
    if (v < -128) v = -128;
    return (int8_t)v;
}

static int make(int id, float secs)
{
    int n = (int)(secs * RATE), i;
    int8_t *d = AllocVecTags(n, AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    float ph = 0, ph2 = 0, ph3 = 0;
    if (!d) return 0;
    for (i = 0; i < n; i++) {
        float t = (float)i / RATE, v = 0, env;
        switch (id) {
        case SND_THUD:          /* the Boing ball landing */
            env = expf(-t * 11.0f);
            ph += 2 * 3.14159f * (95.0f - 45.0f * t / secs) / RATE;
            v = sinf(ph) * env * 120.0f + noise() * expf(-t * 90.0f) * 70.0f;
            break;
        case SND_CLICK:         /* a band clicks round */
            env = expf(-t * 60.0f);
            ph += 2 * 3.14159f * 1400.0f / RATE;
            v = (sinf(ph) * 0.6f + noise() * 0.4f) * env * 100.0f;
            break;
        case SND_FLIP:          /* a half swings over */
            env = sinf(3.14159f * t / secs);
            ph += 2 * 3.14159f * (180.0f + 500.0f * t / secs) / RATE;
            v = (noise() * 0.7f + sinf(ph) * 0.3f) * env * env * 70.0f;
            break;
        case SND_SOLVED:        /* a major chord, rising */
            env = (t < 0.02f ? t / 0.02f : 1.0f) * expf(-t * 2.2f);
            ph += 2 * 3.14159f * 523.25f / RATE;
            if (t > 0.08f) ph2 += 2 * 3.14159f * 659.25f / RATE;
            if (t > 0.16f) ph3 += 2 * 3.14159f * 783.99f / RATE;
            v = (sinf(ph) + sinf(ph2) + sinf(ph3)) * env * 38.0f;
            break;
        }
        d[i] = clamp8(v);
    }
    sample[id] = d;
    slen[id] = n;
    return 1;
}

int snd_open(void)
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
    make(SND_THUD, 0.45f);
    make(SND_CLICK, 0.08f);
    make(SND_FLIP, 0.35f);
    make(SND_SOLVED, 1.4f);
    return 1;
}

static void stop(int i)
{
    if (!busy[i]) return;
    if (!CheckIO((struct IORequest *)req[i])) AbortIO((struct IORequest *)req[i]);
    WaitIO((struct IORequest *)req[i]);
    busy[i] = 0;
}

void snd_close(void)
{
    int i;
    for (i = 0; i < NREQ; i++) if (req[i]) stop(i);
    if (dev_open) CloseDevice((struct IORequest *)req[0]);
    for (i = NREQ - 1; i >= 0; i--) if (req[i]) FreeSysObject(ASOT_IOREQUEST, req[i]);
    if (port) FreeSysObject(ASOT_PORT, port);
    for (i = 0; i < SND_COUNT; i++) if (sample[i]) FreeVec(sample[i]);
    memset(req, 0, sizeof(req));
    memset(sample, 0, sizeof(sample));
    port = 0;
    dev_open = 0;
}

void snd_update(void)
{
    int i;
    if (!dev_open) return;
    for (i = 0; i < NREQ; i++)
        if (busy[i] && CheckIO((struct IORequest *)req[i])) {
            WaitIO((struct IORequest *)req[i]);
            busy[i] = 0;
        }
}

void snd_play(int id)
{
    int i, pick = -1;
    struct AHIRequest *r;
    if (!dev_open || id < 0 || id >= SND_COUNT || !sample[id] || snd_volume <= 0) return;
    snd_update();
    for (i = 0; i < NREQ && pick < 0; i++) if (!busy[i]) pick = i;
    if (pick < 0) { pick = next; next = (next + 1) % NREQ; stop(pick); }
    r = req[pick];
    r->ahir_Std.io_Command = CMD_WRITE;
    r->ahir_Std.io_Data = sample[id];
    r->ahir_Std.io_Length = slen[id];
    r->ahir_Std.io_Offset = 0;
    r->ahir_Type = AHIST_M8S;
    r->ahir_Frequency = RATE;
    r->ahir_Volume = (Fixed)(0x10000L * snd_volume / 64);
    r->ahir_Position = 0x8000;
    r->ahir_Link = 0;
    SendIO((struct IORequest *)r);
    busy[pick] = 1;
}
