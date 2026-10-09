/*
 * Music and sound effects (Music.cs, Beeper.cs) through ahi.device; on the
 * 3DO they went to the Amiga layer's Paula. The four music loops and the
 * effects were rendered offline by the 3DO version's tools/assets.py:
 * signed 8-bit mono at 11050 Hz, in data/.
 *
 * The music streams in one-second chunks, two requests always queued
 * behind each other (ahir_Link), so it loops without a gap and each chunk
 * takes the volume asked for when it is sent: that is how the music ducks
 * while someone talks. Effects alternate between two requests.
 * No ahi.device (or no working AHI unit) just means silence.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include "keep.h"

#define RATE       11050L
#define CHUNK      11050L           /* bytes per music request: one second */
#define MUSIC_VOL  36               /* the 3DO's Paula volumes (0..64) */
#define SFX_VOL    56

enum { R_MUSIC = 0, R_FX = 2, NREQ = 4 };

void *keep_load(const char *name, long *size);   /* main_os4.c */
void  keep_free(void *p);

static const char *track_file[4] = { "title.raw", "gatehouse.raw", "crypt.raw", "tower.raw" };

static BYTE  *sfx_data, *music;
static BYTE  *clip[SFX_COUNT];
static ULONG  clip_len[SFX_COUNT];
static long   music_len, music_pos;
static int    track = T_NONE, enabled = 1, fx_next;
static long   frames, duck_until;   /* ~50 Hz: snd_update() is called once a frame */

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

static void send(int i, BYTE *data, ULONG len, long vol, struct AHIRequest *link)
{
    struct AHIRequest *r = req[i];
    long v = 0x10000L * vol / SFX_VOL;        /* effects at full scale */
    if (!dev_open || !r || !data || !len) return;
    r->ahir_Std.io_Command = CMD_WRITE;
    r->ahir_Std.io_Data = data;
    r->ahir_Std.io_Length = len;
    r->ahir_Std.io_Offset = 0;
    r->ahir_Type = AHIST_M8S;
    r->ahir_Frequency = RATE;
    r->ahir_Volume = (Fixed)(v > 0x10000L ? 0x10000L : v);
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
    sfx_data = (BYTE *)keep_load("sfx.raw", &len);
    if (sfx_data && len >= 4) {
        n = (int)((ULONG *)sfx_data)[0];
        if (n > SFX_COUNT) n = SFX_COUNT;
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
    if (music) keep_free(music);
    if (sfx_data) keep_free(sfx_data);
    dev_open = 0;
}

int snd_available(void) { return dev_open; }

static void music_stop(void)
{
    stop(R_MUSIC);
    stop(R_MUSIC + 1);
    if (music) keep_free(music);
    music = 0;
    track = T_NONE;
}

void snd_music(int t)
{
    if (t == track && music) return;
    music_stop();
    if (t < 0 || t >= T_NONE) return;
    music = (BYTE *)keep_load(track_file[t], &music_len);
    if (!music || music_len < 8) {
        keep_log("no music %s\n", track_file[t]);
        if (music) keep_free(music);
        music = 0;
        return;
    }
    music_len &= ~1L;
    music_pos = 0;
    track = t;
}

void snd_music_enable(int on)
{
    enabled = on;
    if (!on) { stop(R_MUSIC); stop(R_MUSIC + 1); }
}

void snd_duck(long t) { duck_until = frames + t; }

void snd_play(int id)
{
    int i;
    if (id < 0 || id >= SFX_COUNT || !clip[id]) return;
    i = R_FX + fx_next;
    fx_next ^= 1;
    stop(i);
    send(i, clip[id], clip_len[id], SFX_VOL, 0);
}

/* once a frame: collect finished requests, keep two music chunks queued */
void snd_update(void)
{
    int i;
    frames++;
    if (!dev_open) return;
    for (i = 0; i < NREQ; i++)
        if (busy[i] && CheckIO((struct IORequest *)req[i])) {
            WaitIO((struct IORequest *)req[i]);
            busy[i] = 0;
        }
    if (!music || !enabled) return;
    for (i = R_MUSIC; i < R_MUSIC + 2; i++)
        if (!busy[i]) {
            int other = i == R_MUSIC ? R_MUSIC + 1 : R_MUSIC;
            long n = music_len - music_pos < CHUNK ? music_len - music_pos : CHUNK;
            send(i, music + music_pos, (ULONG)n, frames < duck_until ? MUSIC_VOL / 5 : MUSIC_VOL,
                 busy[other] ? req[other] : 0);
            music_pos += n;
            if (music_pos >= music_len) music_pos = 0;
        }
}
