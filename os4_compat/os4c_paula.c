// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * os4c_paula.c - Amiga Paula on AmigaOS 4 (see paula.h).
 *
 * A task of its own mixes the four channels from the `custom` registers
 * into 16-bit stereo and plays it through ahi.device, two requests queued
 * behind each other (ahir_Link) so there are no gaps.  It also calls the
 * music player's tick at its rate, counted in output frames.
 *
 * The games set the audio registers directly and start and stop DMA by
 * writing dmacon.  os4_compat.h turns each `custom.dmacon = v` into a
 * store into the next slot of custom.dmacon_q; taking the slot
 * (oc_dmacon_slot) also notes ac_ptr/ac_len of each channel at that
 * moment, which is what Paula would latch.  The mixer works through the
 * queue in order before each block, so a stop-set-start sequence or a
 * start followed by new loop registers behaves as on the real chip,
 * just up to one block (23 ms) later.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stddef.h>
#include <string.h>
#include "paula.h"
#include "bridge_client.h"

struct Custom custom;

/* the layout the games were compiled for */
typedef char oc_check_dmacon[offsetof(struct Custom, dmacon) == 0x96 ? 1 : -1];
typedef char oc_check_aud[offsetof(struct Custom, aud) == 0xa0 ? 1 : -1];
typedef char oc_check_color[offsetof(struct Custom, color) == 0x180 ? 1 : -1];
typedef char oc_check_fmode[offsetof(struct Custom, fmode) == 0x1fc ? 1 : -1];

#define RATE      22050
#define FRAMES    512                   /* per AHI request: 23 ms */
#define PAULA_HZ  3546895UL
#define QMASK     (OC_DMACON_QUEUE - 1)
#define EMPTY     0xFFFFu               /* a slot not yet written */

struct voice {
    const BYTE *ptr;
    ULONG len;                          /* bytes */
    ULONG pos;                          /* 16.16 */
    int on;
    UWORD per;                          /* the period inc is for */
    ULONG inc;                          /* 16.16 per output frame */
};

static struct voice v[4];
static UWORD enabled;
static struct SignalSemaphore sem;
static int sem_ready;

static struct { UWORD *ptr; UWORD len; } snap[OC_DMACON_QUEUE][4];
static volatile ULONG q_w, q_r;

static volatile int mixer_alive;      /* set before the task starts, cleared as it ends */
static struct Task *volatile mixer_task;
static struct Task *parent;
static volatile int state;              /* 0 off, 1 running, 2 stopping */
static int failed;                      /* no AHI: don't try again */
static BYTE ready_sig = -1;

static void (*volatile tick_fn)(void);
static ULONG tick_num = 50, tick_den = 1, tick_acc;

static void init_sem(void)
{
    if (!sem_ready) {
        int i;
        InitSemaphore(&sem);
        for (i = 0; i < OC_DMACON_QUEUE; i++) custom.dmacon_q[i] = EMPTY;
        custom.dmaconr = 0x0200;
        sem_ready = 1;
    }
}

UWORD paula_lock(void)
{
    init_sem();
    ObtainSemaphore(&sem);
    return 0;
}

void paula_unlock(UWORD sr)
{
    (void)sr;
    ReleaseSemaphore(&sem);
}

static void latch(int ch, UWORD *ptr, UWORD len)
{
    v[ch].ptr = (const BYTE *)ptr;
    v[ch].len = (ULONG)len * 2;
    v[ch].pos = 0;
    v[ch].on = ptr && len;
}

/* a dmacon write with the channels' latched registers */
static void apply(UWORD val, UWORD *const ptrs[4], const UWORD lens[4])
{
    UWORD bits = val & 0x0f;
    int ch;
    if (val & 0x8000) {
        UWORD start = bits & ~enabled;
        enabled |= bits;
        for (ch = 0; ch < 4; ch++)
            if (start & (1 << ch)) latch(ch, ptrs[ch], lens[ch]);
    } else {
        enabled &= ~bits;
        for (ch = 0; ch < 4; ch++)
            if (bits & (1 << ch)) v[ch].on = 0;
    }
    custom.dmaconr = (UWORD)(0x0200 | enabled);
}

void paula_dmacon(UWORD val)
{
    UWORD *ptrs[4];
    UWORD lens[4];
    int ch;
    paula_lock();
    for (ch = 0; ch < 4; ch++) {
        ptrs[ch] = custom.aud[ch].ac_ptr;
        lens[ch] = custom.aud[ch].ac_len;
    }
    apply(val, ptrs, lens);
    paula_unlock(0);
}

/* the queued dmacon writes, oldest first (the lock held) */
static void drain(void)
{
    volatile UWORD *q = custom.dmacon_q;
    while (q_r != q_w) {
        ULONG s = q_r & QMASK;
        UWORD val = q[s], lens[4];
        UWORD *ptrs[4];
        int ch;
        if (val == EMPTY) break;        /* taken, not yet written */
        for (ch = 0; ch < 4; ch++) {
            ptrs[ch] = snap[s][ch].ptr;
            lens[ch] = snap[s][ch].len;
        }
        apply(val, ptrs, lens);
        q[s] = EMPTY;
        q_r++;
    }
}

static void start_mixer(void);

int oc_dmacon_slot(void)
{
    ULONG s;
    int ch;
    init_sem();
    if (!state && !failed) start_mixer();
    if (q_w - q_r >= OC_DMACON_QUEUE - 1) {
        /* full (no mixer, or it fell behind): use up what's written */
        paula_lock();
        drain();
        if (q_w - q_r >= OC_DMACON_QUEUE - 1) {     /* a slot never written */
            custom.dmacon_q[q_r & QMASK] = EMPTY;
            q_r = q_w;
        }
        paula_unlock(0);
    }
    s = q_w & QMASK;
    for (ch = 0; ch < 4; ch++) {
        snap[s][ch].ptr = custom.aud[ch].ac_ptr;
        snap[s][ch].len = custom.aud[ch].ac_len;
    }
    custom.dmacon_q[s] = EMPTY;
    q_w++;
    return (int)s;
}

/* ---- mixing ---- */

static LONG acc_l[FRAMES], acc_r[FRAMES];

/* for the frame rate log (os4c_screen.c): the loudest sample and the
 * blocks played since it last looked */
volatile ULONG oc_audio_peak, oc_audio_blocks;

static void mix_voice(int ch, LONG *acc, int n)
{
    struct voice *c = &v[ch];
    volatile struct AudChannel *a = &custom.aud[ch];
    UWORD per = a->ac_per;
    int vol = a->ac_vol, i;
    ULONG pos, end, inc;
    if (!c->on) return;
    if (vol > 64) vol = 64;
    if (per != c->per) {
        c->per = per;
        c->inc = per >= 64 ? (ULONG)(((unsigned long long)PAULA_HZ << 16) / ((ULONG)per * RATE)) : 0;
    }
    inc = c->inc;
    if (!inc) return;
    pos = c->pos;
    end = c->len << 16;
    for (i = 0; i < n; i++) {
        if (vol) acc[i] += c->ptr[pos >> 16] * vol;
        pos += inc;
        while (pos >= end) {            /* block done: reload, as Paula does */
            pos -= end;
            c->ptr = (const BYTE *)a->ac_ptr;
            c->len = (ULONG)a->ac_len * 2;
            end = c->len << 16;
            if (!c->ptr || !end) { c->on = 0; c->pos = 0; return; }
        }
    }
    c->pos = pos;
}

static void render(WORD *out, int n)
{
    int i;
    memset(acc_l, 0, sizeof(LONG) * n);
    memset(acc_r, 0, sizeof(LONG) * n);
    mix_voice(0, acc_l, n);
    mix_voice(3, acc_l, n);
    mix_voice(1, acc_r, n);
    mix_voice(2, acc_r, n);
    for (i = 0; i < n; i++) {
        /* two channels a side at +-8192 each; a little of each side in
         * the other, as the hard Amiga stereo is tiring on headphones */
        LONG l = acc_l[i] * 3 / 2 + acc_r[i] / 2;
        LONG r = acc_r[i] * 3 / 2 + acc_l[i] / 2;
        if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
        out[i * 2] = (WORD)l;
        out[i * 2 + 1] = (WORD)r;
        if (l < 0) l = -l;
        if (r < 0) r = -r;
        if ((ULONG)l > oc_audio_peak) oc_audio_peak = (ULONG)l;
        if ((ULONG)r > oc_audio_peak) oc_audio_peak = (ULONG)r;
    }
}

/* one block: the queued writes, then the frames, the tick at its times */
static void block(WORD *out)
{
    int done = 0;
    paula_lock();
    drain();
    while (done < FRAMES) {
        int k = FRAMES - done;
        if (tick_fn) {
            ULONG period = RATE * tick_den;               /* in frames * tick_num */
            ULONG left = (period - tick_acc + tick_num - 1) / tick_num;
            if (left < (ULONG)k) k = (int)left;
            if (k < 1) k = 1;
        }
        render(out + done * 2, k);
        done += k;
        if (tick_fn) {
            tick_acc += (ULONG)k * tick_num;
            if (tick_acc >= RATE * tick_den) {
                tick_acc -= RATE * tick_den;
                tick_fn();
                drain();
            }
        }
    }
    paula_unlock(0);
}

static int32 mixer_main(void)
{
    struct MsgPort *port = AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    struct AHIRequest *req[2] = { 0, 0 };
    WORD *buf[2] = { 0, 0 };
    int open = 0, busy[2] = { 0, 0 }, cur = 0;

    if (port)
        req[0] = AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_ReplyPort, port,
                                    ASOIOR_Size, sizeof(struct AHIRequest), TAG_DONE);
    if (req[0]) {
        req[0]->ahir_Version = 4;
        open = OpenDevice((CONST_STRPTR)AHINAME, AHI_DEFAULT_UNIT, (struct IORequest *)req[0], 0) == 0;
    }
    if (open) req[1] = AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Duplicate, req[0], TAG_DONE);
    buf[0] = AllocVecTags(FRAMES * 4, AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    buf[1] = AllocVecTags(FRAMES * 4, AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!req[1] || !buf[0] || !buf[1]) state = 0;
    Signal(parent, 1UL << ready_sig);

    while (state == 1) {
        struct AHIRequest *r = req[cur], *other = req[cur ^ 1];
        block(buf[cur]);
        r->ahir_Std.io_Command = CMD_WRITE;
        r->ahir_Std.io_Data = buf[cur];
        r->ahir_Std.io_Length = FRAMES * 4;
        r->ahir_Std.io_Offset = 0;
        r->ahir_Type = AHIST_S16S;
        r->ahir_Frequency = RATE;
        r->ahir_Volume = 0x10000;
        r->ahir_Position = 0x8000;
        r->ahir_Link = busy[cur ^ 1] ? other : 0;
        SendIO((struct IORequest *)r);
        busy[cur] = 1;
        oc_audio_blocks++;
        if (busy[cur ^ 1]) {            /* the one before: done when ours starts */
            /* waits that Ctrl-C ends (paula's stop), not WaitIO: if
             * ahi.device stops returning requests, the program can still end */
            while (!CheckIO((struct IORequest *)other) && state == 1)
                Wait(1UL << port->mp_SigBit | SIGBREAKF_CTRL_C);
            if (!CheckIO((struct IORequest *)other)) break;
            WaitIO((struct IORequest *)other);
            busy[cur ^ 1] = 0;
        }
        cur ^= 1;
    }

    for (cur = 0; cur < 2; cur++)
        if (busy[cur]) {
            if (!CheckIO((struct IORequest *)req[cur])) AbortIO((struct IORequest *)req[cur]);
            WaitIO((struct IORequest *)req[cur]);
        }
    if (req[1]) FreeSysObject(ASOT_IOREQUEST, req[1]);
    if (open) CloseDevice((struct IORequest *)req[0]);
    if (req[0]) FreeSysObject(ASOT_IOREQUEST, req[0]);
    if (port) FreeSysObject(ASOT_PORT, port);
    if (buf[0]) FreeVec(buf[0]);
    if (buf[1]) FreeVec(buf[1]);
    Forbid();                           /* the parent frees our code only after this */
    mixer_alive = 0;
    mixer_task = 0;
    Signal(parent, 1UL << ready_sig);
    return 0;
}

static void start_mixer(void)
{
    init_sem();
    if (state || failed) return;
    parent = FindTask(NULL);
    ready_sig = AllocSignal(-1);
    if (ready_sig < 0) { failed = 1; return; }
    SetSignal(0, 1UL << ready_sig);
    state = 1;
    mixer_alive = 1;
    /* (if the task ends at once, mixer_alive is clear before it is used) */
    mixer_task = (struct Task *)CreateNewProcTags(NP_Entry, (ULONG)mixer_main, NP_Name, (ULONG)"os4c Paula",
                                                  NP_Priority, 10, NP_Child, TRUE, TAG_DONE);
    if (!mixer_task) {
        state = 0;
        mixer_alive = 0;
    } else {
        Wait(1UL << ready_sig);         /* AHI open, or not */
        if (!state)
            while (mixer_alive) Wait(1UL << ready_sig);     /* it is ending */
    }
    AB_I("os4c: sound %s", state ? "through ahi.device" : "off (no ahi.device)");
    if (!state) {
        failed = 1;
        FreeSignal(ready_sig);
        ready_sig = -1;
    }
}

static void stop_mixer(void)
{
    if (state != 1) return;
    state = 2;
    Forbid();
    if (mixer_alive && mixer_task) Signal(mixer_task, SIGBREAKF_CTRL_C);
    Permit();
    while (mixer_alive) Wait(1UL << ready_sig);
    state = 0;
    FreeSignal(ready_sig);
    ready_sig = -1;
}

int paula_init(void (*tick)(void), int hz)
{
    paula_lock();
    tick_num = (ULONG)hz;
    tick_den = 1;
    tick_acc = 0;
    tick_fn = tick;
    paula_unlock(0);
    start_mixer();
    return state == 1 ? 0 : 1;
}

void paula_exit(void)
{
    int ch;
    paula_lock();
    tick_fn = 0;
    for (ch = 0; ch < 4; ch++) v[ch].on = 0;
    enabled = 0;
    paula_unlock(0);
}

void paula_set_rate(int hz)
{
    (void)hz;
}

void paula_set_tick_rate(int num, int den)
{
    paula_lock();
    tick_num = (ULONG)num;
    tick_den = (ULONG)den;
    if (tick_acc >= RATE * tick_den) tick_acc = 0;
    paula_unlock(0);
}

/* the program is ending: the mixer must be gone before its code is */
static void __attribute__((destructor)) paula_cleanup(void)
{
    stop_mixer();
}
