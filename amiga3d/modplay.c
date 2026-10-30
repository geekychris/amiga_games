// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * modplay.c - a small 4-channel Protracker player that mixes in software.
 *
 * AmigaOS 4 has no Paula, so ptplayer can't run there; this plays the same
 * terra_strike.mod into a stereo buffer that sound_ahi.c streams to
 * ahi.device. Channels 1 and 4 go left, 2 and 3 right, as on the Amiga,
 * with a little of each side mixed into the other for headphones.
 *
 * The usual effects are handled: arpeggio, slides, tone portamento,
 * vibrato, sample offset, volume, position jump, pattern break, speed and
 * tempo, and the E commands for fine slides, retrigger, note cut, note
 * delay and pattern delay. Samples aren't interpolated, much like Paula.
 */
#include <string.h>
#include "modplay.h"

#define PAULA_CLOCK 3546895.0       /* PAL */

typedef struct {
    const int8_t *data;
    uint32_t len, loop_start, loop_len;
    int vol, finetune;              /* finetune -8..7 */
} Sample;

typedef struct {
    const Sample *smp;
    uint32_t pos, frac, step;       /* step: 16.16 sample positions per output frame */
    int active, smpnum, finetune;
    int period, base, target;       /* period played, before arpeggio / vibrato, porta target */
    int volume;
    int eff, prm, porta, vib_prm, vib_pos, offset;
    int delay_period;               /* EDx: the note waiting for its tick */
} Chan;

static Sample smp[31];
static Chan ch[4];
static const uint8_t *song, *patterns;
static int song_len, speed, bpm, tick, row, pos, pat_delay, delay_rows;
static int jump_pos, break_row;     /* -1 = none */
static int tick_left;               /* output frames until the next tick */
static int out_rate;

static const uint8_t vib_sine[32] = {
    0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
    255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24
};

/* 2^(-n/12) and 2^(-ft/96), 16.16 */
static const int32_t semitone_mul[16] = {
    65536, 61858, 58386, 55109, 52016, 49097, 46341, 43740,
    41285, 38968, 36781, 34716, 32768, 30929, 29193, 27554
};
static const int32_t finetune_mul[16] = {
    65536, 65065, 64596, 64132, 63670, 63212, 62757, 62306,   /* 0..7 */
    69433, 68933, 68438, 67945, 67456, 66971, 66489, 66010    /* -8..-1 */
};

static void set_step(Chan *c, int period)
{
    int32_t p;
    if (period <= 0) { c->step = 0; return; }
    p = (int32_t)(((int64_t)period * finetune_mul[c->finetune & 15]) >> 16);
    if (p < 1) p = 1;
    c->step = (uint32_t)(PAULA_CLOCK / p * 65536.0 / out_rate);
}

int mod_load(const uint8_t *d, long len)
{
    int i, npat = 0;
    long off;
    if (len < 1084 || (memcmp(d + 1080, "M.K.", 4) && memcmp(d + 1080, "M!K!", 4) &&
                       memcmp(d + 1080, "4CHN", 4) && memcmp(d + 1080, "FLT4", 4)))
        return 0;
    song_len = d[950];
    if (song_len < 1 || song_len > 128) return 0;
    song = d + 952;
    for (i = 0; i < 128; i++) if (song[i] + 1 > npat) npat = song[i] + 1;
    patterns = d + 1084;
    off = 1084 + (long)npat * 1024;
    if (off > len) return 0;
    for (i = 0; i < 31; i++) {
        const uint8_t *h = d + 20 + i * 30;
        Sample *s = &smp[i];
        s->len = (uint32_t)((h[22] << 8) | h[23]) * 2;
        s->finetune = h[24] & 15;
        s->vol = h[25] > 64 ? 64 : h[25];
        s->loop_start = (uint32_t)((h[26] << 8) | h[27]) * 2;
        s->loop_len = (uint32_t)((h[28] << 8) | h[29]) * 2;
        if (off + (long)s->len > len) s->len = off < len ? (uint32_t)(len - off) : 0;
        s->data = (const int8_t *)(d + off);
        off += (long)((h[22] << 8) | h[23]) * 2;
        if (s->loop_start >= s->len) s->loop_len = 0;
        else if (s->loop_start + s->loop_len > s->len) s->loop_len = s->len - s->loop_start;
    }
    memset(ch, 0, sizeof(ch));
    speed = 6;
    bpm = 125;
    tick = row = pos = pat_delay = delay_rows = 0;
    jump_pos = break_row = -1;
    tick_left = 0;
    return 1;
}

int mod_position(void) { return pos; }

static void trigger(Chan *c, int period)
{
    if (!c->smpnum) return;
    c->smp = &smp[c->smpnum - 1];
    c->finetune = c->smp->finetune;
    c->period = c->base = period;
    c->pos = c->frac = 0;
    c->vib_pos = 0;
    c->active = c->smp->len > 2;
    set_step(c, period);
}

static void vol_slide(Chan *c)
{
    if (c->prm >> 4) c->volume += c->prm >> 4;
    else c->volume -= c->prm & 15;
    if (c->volume > 64) c->volume = 64;
    if (c->volume < 0) c->volume = 0;
}

static void tone_porta(Chan *c)
{
    if (!c->target) return;
    if (c->base < c->target) { c->base += c->porta; if (c->base > c->target) c->base = c->target; }
    else if (c->base > c->target) { c->base -= c->porta; if (c->base < c->target) c->base = c->target; }
    c->period = c->base;
    set_step(c, c->period);
}

static void vibrato(Chan *c)
{
    int d = (vib_sine[c->vib_pos & 31] * (c->vib_prm & 15)) >> 7;
    if (c->vib_pos & 32) d = -d;
    set_step(c, c->base + d);
    c->vib_pos = (c->vib_pos + (c->vib_prm >> 4)) & 63;
}

static void new_row(void)
{
    const uint8_t *r = patterns + (long)song[pos] * 1024 + row * 16;
    int i;
    for (i = 0; i < 4; i++, r += 4) {
        Chan *c = &ch[i];
        int s = (r[0] & 0xF0) | (r[2] >> 4);
        int period = ((r[0] & 0x0F) << 8) | r[1];
        int eff = r[2] & 15, prm = r[3], ex = prm >> 4;

        c->eff = eff;
        c->prm = prm;
        if (s >= 1 && s <= 31) {
            c->smpnum = s;
            c->volume = smp[s - 1].vol;
        }
        c->delay_period = 0;
        if (period) {
            if (eff == 3 || eff == 5) c->target = period;
            else if (eff == 14 && ex == 13 && (prm & 15)) c->delay_period = period;
            else {
                trigger(c, period);
                if (eff == 9 && c->active) {
                    if (prm) c->offset = prm << 8;
                    c->pos = (uint32_t)c->offset;
                    if (c->pos >= c->smp->len) c->active = 0;
                }
            }
        }
        switch (eff) {
        case 0: if (prm) c->base = c->period; break;
        case 3: if (prm) c->porta = prm; break;
        case 4:
            if (prm & 15) c->vib_prm = (c->vib_prm & 0xF0) | (prm & 15);
            if (prm >> 4) c->vib_prm = (c->vib_prm & 0x0F) | (prm & 0xF0);
            break;
        case 11: jump_pos = prm; break;
        case 12: c->volume = prm > 64 ? 64 : prm; break;
        case 13: break_row = (prm >> 4) * 10 + (prm & 15); if (break_row > 63) break_row = 0; break;
        case 14:
            switch (ex) {
            case 1: c->base -= prm & 15; if (c->base < 113) c->base = 113; c->period = c->base; set_step(c, c->period); break;
            case 2: c->base += prm & 15; if (c->base > 856) c->base = 856; c->period = c->base; set_step(c, c->period); break;
            case 10: c->volume += prm & 15; if (c->volume > 64) c->volume = 64; break;
            case 11: c->volume -= prm & 15; if (c->volume < 0) c->volume = 0; break;
            case 12: if (!(prm & 15)) c->volume = 0; break;
            case 14: pat_delay = prm & 15; break;
            }
            break;
        case 15:
            if (prm == 0) break;
            if (prm < 32) speed = prm; else bpm = prm;
            break;
        }
    }
}

static void effects(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        Chan *c = &ch[i];
        int ex = c->prm >> 4, x = c->prm & 15;
        switch (c->eff) {
        case 0:
            if (c->prm) {
                int n = tick % 3 == 1 ? ex : tick % 3 == 2 ? x : 0;
                set_step(c, (int)(((int64_t)c->base * semitone_mul[n]) >> 16));
            }
            break;
        case 1: c->base -= c->prm; if (c->base < 113) c->base = 113; c->period = c->base; set_step(c, c->period); break;
        case 2: c->base += c->prm; if (c->base > 856) c->base = 856; c->period = c->base; set_step(c, c->period); break;
        case 3: tone_porta(c); break;
        case 4: vibrato(c); break;
        case 5: tone_porta(c); vol_slide(c); break;
        case 6: vibrato(c); vol_slide(c); break;
        case 10: vol_slide(c); break;
        case 14:
            if (ex == 9 && x && tick % x == 0 && c->smp) { c->pos = c->frac = 0; c->active = c->smp->len > 2; }
            else if (ex == 12 && tick == x) c->volume = 0;
            else if (ex == 13 && tick == x && c->delay_period) { trigger(c, c->delay_period); c->delay_period = 0; }
            break;
        }
    }
}

static void next_row(void)
{
    if (delay_rows > 0) { delay_rows--; return; }
    if (pat_delay) { delay_rows = pat_delay; pat_delay = 0; if (delay_rows) { delay_rows--; return; } }
    if (jump_pos >= 0 || break_row >= 0) {
        pos = jump_pos >= 0 ? jump_pos : pos + 1;
        row = break_row >= 0 ? break_row : 0;
        jump_pos = break_row = -1;
    } else if (++row >= 64) {
        row = 0;
        pos++;
    }
    if (pos >= song_len) pos = 0;
}

static void do_tick(void)
{
    if (tick == 0 && delay_rows == 0) new_row();
    else effects();
    if (++tick >= speed) {
        tick = 0;
        next_row();
    }
}

void mod_render(int16_t *out, int frames, int rate)
{
    out_rate = rate;
    while (frames > 0) {
        int n, i, f;
        if (tick_left <= 0) {
            do_tick();
            tick_left = rate * 5 / (bpm * 2);   /* 2.5 / bpm seconds */
        }
        n = frames < tick_left ? frames : tick_left;
        for (f = 0; f < n; f++) {
            int32_t side[2] = { 0, 0 }, l, r;
            for (i = 0; i < 4; i++) {
                Chan *c = &ch[i];
                const Sample *s = c->smp;
                uint32_t end;
                if (!c->active) continue;
                side[(i == 1 || i == 2)] += c->smp->data[c->pos] * c->volume;
                c->frac += c->step;
                c->pos += c->frac >> 16;
                c->frac &= 0xFFFF;
                end = s->loop_len > 2 ? s->loop_start + s->loop_len : s->len;
                if (c->pos >= end) {
                    if (s->loop_len > 2) c->pos = s->loop_start + (c->pos - end) % s->loop_len;
                    else c->active = 0;
                }
            }
            l = (side[0] * 3 + side[1]) * 3 / 8;   /* x1.5, a quarter of the other side */
            r = (side[1] * 3 + side[0]) * 3 / 8;
            *out++ = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            *out++ = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        }
        frames -= n;
        tick_left -= n;
    }
}
