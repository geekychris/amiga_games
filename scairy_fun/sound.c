// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * sound.c - ScAIry Fun's sound effects, synthesised into chip RAM at
 * start-up, and the autopilot's voice.
 *
 * The voice clips stay in fast RAM; one at a time is copied into a chip
 * buffer just before it plays, so they cost one clip's worth of chip RAM.
 *
 * scairy.voc: "SCVO", UWORD clip count, UWORD sample rate, then for each
 * clip (indexed by MSG_*) a ULONG offset and a ULONG length into the
 * file, then the signed 8-bit samples.
 */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#include "sc.h"
#include "sound.h"

#define FX_PERIOD 300                           /* the effects play at 11.8 kHz */
#define FX_RATE   (3546895L / FX_PERIOD)

static const UWORD fx_len[SFX_COUNT] = { 2048, 4096, 512, 2400, 2400, 320, 3600, 3000, 6000, 600 };
static const BYTE fx_pri[SFX_COUNT]  = {   60,   60,  40,   30,   30,  30,   50,   50,   20,  10 };
static const WORD fx_vol[SFX_COUNT]  = {   64,   64,  44,   40,   40,  48,   44,   48,   36,  30 };
static BYTE *fx_data[SFX_COUNT];
static SfxStructure fx[SFX_COUNT];

static ULONG rng = 4242;
static int noise8(void)
{
    rng = rng * 1103515245UL + 12345UL;
    return (int)((rng >> 16) & 255) - 128;
}

/* a triangle wave sweeping f0 -> f1 Hz over n samples, fading out over the
 * last `fade` of them */
static void tone(BYTE *d, int n, int f0, int f1, int amp, int fade)
{
    ULONG ph = 0;
    int i;
    for (i = 0; i < n; i++) {
        int f = f0 + ((f1 - f0) * i) / n, t, a = amp;
        ph += (ULONG)((f * 65536L) / FX_RATE);
        t = (int)((ph >> 8) & 255);
        t = t < 128 ? t * 2 - 128 : 383 - t * 2;           /* -128..127 */
        if (i > n - fade) a = (amp * (n - i)) / fade;
        d[i] = (BYTE)((t * a) / 128);
    }
}

/* noise, decaying, smoothed over `lp` samples (more = duller) */
static void noise(BYTE *d, int n, int amp, int lp, int attack)
{
    int i, acc = 0;
    for (i = 0; i < n; i++) {
        int env = i < attack ? (amp * i) / attack : (amp * (n - i)) / (n - attack);
        acc += noise8() - acc / lp;
        d[i] = (BYTE)(((acc / lp) * env) / 128);
    }
}

void sound_init(void)
{
    int k, i;
    for (k = 0; k < SFX_COUNT; k++) {
        BYTE *d = fx_data[k] = (BYTE *)AllocMem(fx_len[k], MEMF_CHIP | MEMF_CLEAR);
        int n = fx_len[k];
        if (!d) continue;
        switch (k) {
        case SFX_CRASH:  noise(d, n, 127, 2, 8); break;
        case SFX_SPLASH: noise(d, n, 127, 6, 600); break;
        case SFX_SCRAPE:
            noise(d, n, 90, 1, 4);
            for (i = 0; i < n; i++) if ((i / 6) & 1) d[i] = (BYTE)(d[i] / 2);
            break;
        case SFX_CHIME_ON:  tone(d, n / 2, 660, 660, 90, 200); tone(d + n / 2, n / 2, 990, 990, 90, 900); break;
        case SFX_CHIME_OFF: tone(d, n / 2, 990, 990, 90, 200); tone(d + n / 2, n / 2, 660, 660, 90, 900); break;
        case SFX_CONE:   tone(d, n, 220, 90, 110, n); break;
        case SFX_SAVE:
            tone(d, 1200, 523, 523, 90, 150);
            tone(d + 1200, 1200, 659, 659, 90, 150);
            tone(d + 2400, 1200, 784, 784, 100, 900);
            break;
        case SFX_ALERT:  tone(d, 900, 1200, 1200, 110, 60); tone(d + 1400, 900, 1200, 1200, 110, 60); break;
        case SFX_SIREN:  tone(d, n / 2, 620, 930, 100, 40); tone(d + n / 2, n / 2, 930, 620, 100, 300); break;
        case SFX_BLIP:   tone(d, n, 1500, 1800, 80, n); break;
        }
        fx[k].sfx_ptr = d;
        fx[k].sfx_len = (WORD)(n / 2);
        fx[k].sfx_per = FX_PERIOD;
        fx[k].sfx_vol = fx_vol[k];
        fx[k].sfx_cha = -1;
        fx[k].sfx_pri = fx_pri[k];
    }
}

/* ---------- the voice ---------- */
static UBYTE *voc;                       /* the whole file, in fast RAM */
static LONG voc_size;
static BYTE *voc_chip;                   /* the clip playing */
static ULONG voc_chip_size;
static int voc_count, voc_period;

static ULONG be32(const UBYTE *p) { return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3]; }

int voice_load(const char *path)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    ULONG biggest = 0;
    int i;
    if (!fh) return 0;
    Seek(fh, 0, OFFSET_END);
    voc_size = Seek(fh, 0, OFFSET_BEGINNING);
    if (voc_size > 8) voc = (UBYTE *)AllocMem(voc_size, MEMF_ANY);
    if (voc && Read(fh, voc, voc_size) != voc_size) { FreeMem(voc, voc_size); voc = NULL; }
    Close(fh);
    if (!voc) return 0;
    if (memcmp(voc, "SCVO", 4)) { FreeMem(voc, voc_size); voc = NULL; return 0; }
    voc_count = (voc[4] << 8) | voc[5];
    voc_period = (int)(3546895L / ((voc[6] << 8) | voc[7]));
    for (i = 0; i < voc_count; i++) {
        ULONG off = be32(voc + 8 + i * 8), len = be32(voc + 12 + i * 8);
        if (off + len > (ULONG)voc_size || len > 131070) { voc_count = i; break; }
        if (len > biggest) biggest = len;
    }
    if (biggest) voc_chip = (BYTE *)AllocMem(biggest, MEMF_CHIP | MEMF_CLEAR);
    if (!voc_chip) { voc_count = 0; return 0; }
    voc_chip_size = biggest;
    return voc_count;
}

int voice_clips(void) { return voc_count; }

void voice_say(int msg)
{
    static SfxStructure v;
    ULONG off, len;
    if (!voc_chip || msg <= 0 || msg >= voc_count) return;
    off = be32(voc + 8 + msg * 8);
    len = be32(voc + 12 + msg * 8) & ~1UL;
    if (len < 2) return;
    CopyMem(voc + off, voc_chip, len);
    v.sfx_ptr = voc_chip;
    v.sfx_len = (WORD)(len / 2);
    v.sfx_per = (WORD)voc_period;
    v.sfx_vol = 64;
    v.sfx_cha = -1;
    v.sfx_pri = 100;
    mt_playfx(CUSTOM_BASE, &v);
}

void sfx(int which)
{
    if (which >= 0 && which < SFX_COUNT && fx_data[which]) mt_playfx(CUSTOM_BASE, &fx[which]);
}

void sound_cleanup(void)
{
    int k;
    for (k = 0; k < SFX_COUNT; k++)
        if (fx_data[k]) { FreeMem(fx_data[k], fx_len[k]); fx_data[k] = NULL; }
    if (voc_chip) { FreeMem(voc_chip, voc_chip_size); voc_chip = NULL; }
    if (voc) { FreeMem(voc, voc_size); voc = NULL; }
    voc_count = 0;
}
