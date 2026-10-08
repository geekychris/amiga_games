/*
 * Every sound is synthesised at startup, as in Sfx.cs (and the 3DO
 * version): a phase-integrated oscillator (sweeping pitch never clicks),
 * square / triangle / sine, short attack and release. Signed 8-bit mono
 * at 11050 Hz, played through ahi.device instead of Paula:
 *   channel 0: waka   1, 3: other effects (alternating)   2: ambience loop
 * No AHI (or no sound card) just means silence.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <stdlib.h>
#include "sfx.h"

#define RATE   11050L               /* Paula period 321 */
#define PERIOD 321

enum { W_SQUARE, W_TRI, W_SINE };

static BYTE  *clip[SFX_COUNT];
static ULONG  clip_len[SFX_COUNT];
static int    waka_flip, fx_chan = 1, amb_now = -1;

static const short sine256[64] = {     /* quarter wave, Q14 */
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379
};

static long wave(int w, ULONG ph)      /* ph: 16-bit phase -> Q14 */
{
    ULONG p = ph & 0xFFFF;
    switch (w) {
    case W_SQUARE: return p < 32768 ? 9830 : -9830;                  /* +-0.6 */
    case W_TRI: {
        long d = (long)p - 32768;                                      /* 1 - 4|p - 0.5| */
        if (d < 0) d = -d;
        return 16384 - (d >> 1);
    }
    default: {
        int q = (int)(p >> 8), i = q & 63;
        long v = (q & 64) ? sine256[63 - i] : sine256[i];
        return (q & 128) ? -v : v;
    }
    }
}

/* frequency (Hz) at sample i of clip id */
static long freq(int id, long i)
{
    static const short start[16] = { 494, 988, 740, 622, 988, 740, 622, 0, 523, 1047, 784, 659, 1047, 784, 659, 0 };
    static const short clear[12] = { 523, 659, 784, 1047, 784, 1047, 1319, 1568, 1319, 1568, 2093, 2093 };
    static const short extra[6] = { 1319, 1568, 2637, 2093, 2349, 3136 };
    static const short key[5] = { 523, 659, 784, 1047, 1319 };
    long n;
    switch (id) {
    case SFX_WAKA_A: return 260 + 260 * i / (RATE / 10);
    case SFX_WAKA_B: return 520 - 260 * i / (RATE / 10);
    case SFX_KEY:    n = i / (RATE / 10); return key[n > 4 ? 4 : n];
    case SFX_EAT: {
        long x = i * 1024 / (RATE * 35 / 100);                         /* 0..1024 */
        long px = x * (1331 - 307 * x / 1024) / 1024;                  /* ~ x^0.7 */
        return 180 + 1220 * px / 1024;
    }
    case SFX_DEATH: {
        long base = 900 - 810 * i / (RATE * 3 / 2);
        long ph = i * 5664 / 100;          /* sin(60 t): 60/(2 pi) turns/s = 56.64 phase units/sample */
        return base + base * 12 / 100 * wave(W_SINE, (ULONG)ph) / 16384;
    }
    case SFX_START:  n = i / (RATE * 12 / 100); return start[n > 15 ? 15 : n];
    case SFX_CLEAR:  n = i / (RATE * 9 / 100);  return clear[n > 11 ? 11 : n];
    case SFX_EXTRA:  n = i / (RATE * 8 / 100);  return extra[n > 5 ? 5 : n];
    case SFX_SIREN:  return 420 + 200 * wave(W_SINE, (ULONG)(i * 65536L / (RATE * 6 / 10))) / 16384;
    case SFX_FRIGHT: {
        long s = wave(W_SINE, (ULONG)(i * 65536L / (RATE / 4)));
        if (s < 0) s = -s;
        return 180 + 120 * s / 16384;
    }
    }
    return 0;
}

static short wtab[3][256];             /* one cycle of each wave, Q14 */

static int build(int id, long ms, int w, int amp100, int looped)
{
    /* the ARM60 has no divide instruction: pitch is worked out every 16
     * samples, everything per sample is multiply-and-shift */
    long n = RATE * ms / 1000, i, att = RATE / 200, rel = RATE * 3 / 100;
    long amp = (long)amp100 * 256 / 100, f = 0, inc = 0;
    ULONG phase = 0;
    BYTE *b;
    n &= ~1L;
    b = (BYTE *)AllocVecTags((ULONG)n, AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!b) return 0;
    for (i = 0; i < n; i++) {
        long v, env = 256;
        if ((i & 15) == 0) {
            f = freq(id, i);
            inc = (f * 380) >> 6;                   /* f * 65536 / 11050 */
        }
        phase += (ULONG)inc;
        if (!looped) {
            if (i < att) env = i * 256 / att;
            else if (n - i < rel) env = (n - i) * 256 / rel;
        }
        v = f <= 0 ? 0 : (((wtab[w][(phase >> 8) & 255] * amp) >> 8) * env) >> 8;   /* Q14 */
        b[i] = (BYTE)((v * 127) >> 14);
    }
    clip[id] = b;
    clip_len[id] = (ULONG)n;
    return 1;
}

/* ---- ahi.device: one IO request per channel; the ambience has two, so
 * the next loop is always queued behind the one playing (ahir_Link) ---- */

#define NREQ 5                         /* 0 waka, 1 + 3 effects, 2 + 4 ambience */
static struct MsgPort    *port;
static struct AHIRequest *req[NREQ];
static int                busy[NREQ], dev_open, amb_id = -1, amb_vol;

static int ahi_open(void)
{
    int ch;
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
    for (ch = 1; ch < NREQ; ch++) {
        req[ch] = AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_Duplicate, req[0], TAG_DONE);
        if (!req[ch]) return 0;
    }
    return 1;
}

static void stop(int ch)
{
    if (busy[ch]) {
        if (!CheckIO((struct IORequest *)req[ch])) AbortIO((struct IORequest *)req[ch]);
        WaitIO((struct IORequest *)req[ch]);
        busy[ch] = 0;
    }
}

/* vol: Paula's 0..64 as on the 3DO, but 40 is full scale here (AHI mixes
 * at a lower level than the 3DO's Paula emulation) */
static void send(int ch, int id, int vol, struct AHIRequest *link)
{
    struct AHIRequest *r = req[ch];
    long v = 0x10000L * vol / 40;
    r->ahir_Std.io_Command = CMD_WRITE;
    r->ahir_Std.io_Data = clip[id];
    r->ahir_Std.io_Length = clip_len[id];
    r->ahir_Std.io_Offset = 0;
    r->ahir_Type = AHIST_M8S;
    r->ahir_Frequency = RATE;
    r->ahir_Volume = (Fixed)(v > 0x10000L ? 0x10000L : v);
    r->ahir_Position = 0x8000;
    r->ahir_Link = link;
    SendIO((struct IORequest *)r);
    busy[ch] = 1;
}

static void start(int ch, int id, int vol)
{
    if (!dev_open || !req[ch] || !clip[id]) return;
    stop(ch);
    send(ch, id, vol, 0);
}

int sfx_init(void)
{
    int w, i, ok;
    for (w = 0; w < 3; w++)
        for (i = 0; i < 256; i++)
            wtab[w][i] = (short)wave(w, (ULONG)i << 8);
    ok = build(SFX_WAKA_A, 100, W_SQUARE, 50, 0) & build(SFX_WAKA_B, 100, W_SQUARE, 50, 0) &
         build(SFX_KEY, 600, W_TRI, 80, 0) & build(SFX_EAT, 350, W_SQUARE, 45, 0) &
         build(SFX_DEATH, 1500, W_TRI, 90, 0) & build(SFX_START, 1900, W_SQUARE, 35, 0) &
         build(SFX_CLEAR, 1100, W_TRI, 70, 0) & build(SFX_EXTRA, 500, W_TRI, 60, 0) &
         build(SFX_SIREN, 1200, W_SINE, 100, 1) & build(SFX_FRIGHT, 1000, W_TRI, 100, 1);
    if (!ok) return 0;
    return ahi_open();
}

void sfx_exit(void)
{
    int ch;
    for (ch = 0; ch < NREQ; ch++) if (req[ch]) stop(ch);
    if (dev_open) CloseDevice((struct IORequest *)req[0]);
    for (ch = NREQ - 1; ch >= 0; ch--) if (req[ch]) FreeSysObject(ASOT_IOREQUEST, req[ch]);
    if (port) FreeSysObject(ASOT_PORT, port);
    for (ch = 0; ch < SFX_COUNT; ch++) if (clip[ch]) FreeVec(clip[ch]);
    dev_open = 0;
}

/* once a frame: finished requests are collected, and the ambience
 * channel pair re-queued behind each other */
void sfx_update(void)
{
    int ch;
    if (!dev_open) return;
    for (ch = 0; ch < NREQ; ch++)
        if (busy[ch] && CheckIO((struct IORequest *)req[ch])) {
            WaitIO((struct IORequest *)req[ch]);
            busy[ch] = 0;
        }
    if (amb_id < 0) return;
    for (ch = 2; ch <= 4; ch += 2)
        if (!busy[ch]) {
            int other = ch == 2 ? 4 : 2;
            send(ch, amb_id, amb_vol, busy[other] ? req[other] : 0);
        }
}

int sfx_available(void) { return dev_open; }

void sfx_play(int id)
{
    start(fx_chan, id, 40);
    fx_chan = fx_chan == 1 ? 3 : 1;
}

void sfx_waka(void)
{
    waka_flip = !waka_flip;
    start(0, waka_flip ? SFX_WAKA_A : SFX_WAKA_B, 30);
}

void sfx_ambience(int which)
{
    if (which == amb_now) return;
    amb_now = which;
    if (dev_open) { stop(2); stop(4); }
    amb_id = which == 0 ? -1 : which == 2 ? SFX_FRIGHT : SFX_SIREN;
    amb_vol = which == 2 ? 14 : 8;
    sfx_update();
}
