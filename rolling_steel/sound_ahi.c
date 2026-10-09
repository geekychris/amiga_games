/*
 * Music and sounds (Music.cs, Sfx.cs) through ahi.device; on the 3DO they
 * went to the Amiga layer's Paula. The themes and effects were rendered
 * offline by the 3DO version's tools/sounds.py: signed 8-bit mono at
 * 11050 Hz, in data/.
 *
 *   music   a whole theme per request, two requests queued behind each
 *           other (ahir_Link) so it loops without a gap
 *   rumble  the rolling loop, the same way; each pass takes the pitch
 *           (0.6 .. 1.5) and volume that the marble's speed asks for then
 *   effects three requests in turn
 * No ahi.device (or no working AHI unit) just means silence.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include "rs.h"

#define RATE       11050L
#define MUSIC_VOL  30               /* the 3DO's Paula volumes (0..64) */
#define SFX_VOL    58
#define ROLL_VOL   28
#define NCLIPS     (SFX_COUNT + 1)  /* + the roll loop */

enum { R_MUSIC = 0, R_ROLL = 2, R_FX = 4, NREQ = 7 };

void *rs_load(const char *name, long *size);

static BYTE  *sfx_data, *music;
static BYTE  *clip[NCLIPS];
static ULONG  clip_len[NCLIPS];
static long   music_len;
static int    theme = -1, enabled = 1, fx_next;
static fix    roll_target;
static long   roll_vol;             /* eased, 0..ROLL_VOL << 8 */

static struct MsgPort    *port;
static struct AHIRequest *req[NREQ];
static int                busy[NREQ], dev_open;

static int ahi_open(void)
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

static void send(int i, BYTE *data, ULONG len, long rate, long vol, struct AHIRequest *link)
{
    struct AHIRequest *r = req[i];
    long v = 0x10000L * vol / SFX_VOL;        /* effects at full scale */
    if (!dev_open || !r || !data || !len) return;
    r->ahir_Std.io_Command = CMD_WRITE;
    r->ahir_Std.io_Data = data;
    r->ahir_Std.io_Length = len;
    r->ahir_Std.io_Offset = 0;
    r->ahir_Type = AHIST_M8S;
    r->ahir_Frequency = rate;
    r->ahir_Volume = (Fixed)(v > 0x10000L ? 0x10000L : v < 0 ? 0 : v);
    r->ahir_Position = 0x8000;
    r->ahir_Link = link;
    SendIO((struct IORequest *)r);
    busy[i] = 1;
}

int snd_init(void)
{
    long len = 0;
    ULONG off;
    int i, n;
    sfx_data = (BYTE *)rs_load("sfx.raw", &len);
    if (sfx_data && len >= 4) {
        n = (int)((ULONG *)sfx_data)[0];
        if (n > NCLIPS) n = NCLIPS;
        off = 4 + 4 * ((ULONG *)sfx_data)[0];
        for (i = 0; i < n && off < (ULONG)len; i++) {
            clip_len[i] = ((ULONG *)sfx_data)[1 + i];
            clip[i] = sfx_data + off;
            off += clip_len[i];
        }
    }
    return ahi_open() && sfx_data;
}

void snd_exit(void)
{
    int i;
    for (i = 0; i < NREQ; i++) if (req[i]) stop(i);
    if (dev_open) CloseDevice((struct IORequest *)req[0]);
    for (i = NREQ - 1; i >= 0; i--) if (req[i]) FreeSysObject(ASOT_IOREQUEST, req[i]);
    if (port) FreeSysObject(ASOT_PORT, port);
    if (music) FreeVec(music);
    if (sfx_data) FreeVec(sfx_data);
    dev_open = 0;
}

int snd_available(void) { return dev_open; }

static void music_stop(void)
{
    stop(R_MUSIC);
    stop(R_MUSIC + 1);
    if (music) FreeVec(music);
    music = 0;
    theme = -1;
}

void snd_music(int t)
{
    char name[24];
    if (t == theme && music) return;
    music_stop();
    if (t < 0) return;
    sprintf(name, "theme%d.raw", t);
    music = (BYTE *)rs_load(name, &music_len);
    if (!music || music_len < 8) {
        if (music) FreeVec(music);
        music = 0;
        return;
    }
    music_len &= ~1L;
    theme = t;
}

void snd_music_enable(int on)
{
    enabled = on;
    if (!on) { stop(R_MUSIC); stop(R_MUSIC + 1); }
}

void snd_play(int id)
{
    int i;
    if (id < 0 || id >= SFX_COUNT || !clip[id]) return;
    i = R_FX + fx_next;
    fx_next = (fx_next + 1) % 3;
    stop(i);
    send(i, clip[id], clip_len[id], RATE, SFX_VOL, 0);
}

void snd_roll(fix speed01) { roll_target = speed01; }

/* a looping pair: keep both requests queued, each behind the other */
static void loop_pair(int base, BYTE *data, ULONG len, long rate, long vol)
{
    int k;
    for (k = 0; k < 2; k++)
        if (!busy[base + k]) {
            int other = base + 1 - k;
            send(base + k, data, len, rate, vol, busy[other] ? req[other] : 0);
        }
}

/* once a frame */
void snd_update(void)
{
    int i;
    long v;
    if (!dev_open) return;
    for (i = 0; i < NREQ; i++)
        if (busy[i] && CheckIO((struct IORequest *)req[i])) {
            WaitIO((struct IORequest *)req[i]);
            busy[i] = 0;
        }
    if (music && enabled) loop_pair(R_MUSIC, music, (ULONG)music_len, RATE, MUSIC_VOL);
    /* the rumble eases toward the speed, like the Unity Lerp */
    v = (roll_target * ROLL_VOL) >> 4;                       /* << 8 */
    roll_vol += (v - roll_vol) / 4;
    if (clip[SFX_COUNT] && roll_vol > 64)
        loop_pair(R_ROLL, clip[SFX_COUNT], clip_len[SFX_COUNT],
                  RATE * (2458 + ((roll_target * 3686) >> 12)) / 4096, roll_vol >> 8);
}
