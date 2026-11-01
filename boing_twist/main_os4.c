// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * BOING TWIST - a twisty puzzle on the Boing ball, for AmigaOS 4.1.
 *
 * A picture is wrapped round the ball, the ball is cut into latitude bands
 * and longitude segments, and the tiles are jumbled by twisting bands round
 * and flipping halves of the ball over. Put the picture back together.
 * The puzzle is bt_core.c, the 3D render.c (software Mesa), the text
 * hud.c; this file is the screens, the input, the files and the bridge.
 *
 * Usage: boing_twist [RES=640|800|1024] [FULLSCREEN] [FLAT] [LINEAR]
 *   RES         the frame: 640x480, 800x600 (default) or 1024x768
 *   FULLSCREEN  a screen of its own (F or F10 switches while playing)
 *   FLAT        no shading on the ball (OSMesa's fastest textured path)
 *   LINEAR      smooth (bilinear) texture filtering
 *
 * Pictures: PROGDIR:data/<name>.btx (tools/make_texture.py makes them from
 * any equirectangular PNG or JPEG); themes: PROGDIR:themes/<name>.theme.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#include "bridge_client.h"
#include "bt_core.h"
#include "render.h"
#include "hud.h"
#include "sound_ahi.h"
#include "os4_display.h"
#include "tests/vectors.h"

/* Mesa's software rasteriser keeps whole spans on the stack */
static const char __attribute__((used)) stack_cookie[] = "$STACK:2097152";
static const char __attribute__((used)) version_tag[] = "$VER: boing_twist 1.0 (31.10.2026)";

#define PI_F 3.14159265f
#define FRAME_US 33333           /* 30 frames a second at most */

/* ---------------- colours ---------------- */
#define C_WHITE  0xF4F4FF
#define C_DIM    0xB8B8D0
#define C_YELLOW 0xFFD838
#define C_RED    0xFF5040
#define C_CYAN   0x40E0FF
#define C_PURPLE 0xD060FF

/* ---------------- screens ---------------- */
enum { SC_TITLE, SC_SELECT, SC_PLAY, SC_SOLVED, SC_HELP };
static int32_t screen = SC_TITLE, help_back = SC_TITLE;

/* ---------------- levels ---------------- */
static const int grids[][2] = { { 4, 8 }, { 6, 12 }, { 8, 16 } };
static const char *grid_name[] = { "4 X 8  (32 TILES)", "6 X 12  (72 TILES)", "8 X 16  (128 TILES)" };
static const int scrambles[] = { 6, 14, 30, 60 };
static const char *scramble_name[] = { "GENTLE", "MIXED", "TANGLED", "CHAOS" };
#define NGRIDS 3
#define NSCRAMBLES 4
static int32_t sel_grid = 0, sel_scr = 1, sel_row = 0;

/* ---------------- pictures ---------------- */
#define MAX_PICS 64
typedef struct { char file[48]; char title[32]; } Pic;
static Pic pics[MAX_PICS];
static int32_t npics, sel_pic;
static uint8_t *pic_rgb;                 /* the loaded picture */
static int pic_w, pic_h, loaded_pic = -1, loaded_b, loaded_s;
static uint32_t sigs[BT_MAX_T][2];

/* ---------------- themes ---------------- */
#define MAX_THEMES 16
static char theme_file[MAX_THEMES][48], theme_title[MAX_THEMES][32];
static int32_t nthemes, cur_theme;
static uint8_t *face_tex[NFACES];

/* ---------------- the game ---------------- */
static BtPuzzle P, show;                 /* the puzzle; a solved ball for the title and select screens */
static RdView view = { -180.0f, 12.0f, 0.0f, 0.0f, 0.0f, 3.6f };
static float target_yaw = -180.0f, target_pitch = 12.0f;
static int32_t sel_band = 1, sel_half = 2;
static int32_t moves32, secs, solved32, hash32, peeks, gave_up, paused, user_paused, peeking;
static int32_t anim_busy;
static uint64_t play_start_us, play_paused_us, pause_t0;
static int timer_running;
static uint64_t solve_t0;
static int best_new;

/* moves waiting to be shown */
enum { K_DO, K_UNDO, K_SOLVE, K_SCRAMBLE };
typedef struct { int kind, move; } QMove;
static QMove queue[64];
static int nq;
static int anim_move = -1, anim_kind;
static int auto_solve;                   /* giving up: unwind the whole history, move by move */
static uint64_t anim_t0;
static uint32_t anim_len;

/* measurements */
static int32_t fps10, ms_shade, ms_bg, ms_ball, ms_hud, ms_blit, tris32, bench_left, bench_fps10, sleep_late_ms;
static int32_t frame_w, frame_h;

static int quit;
static const char *quit_why = "Esc";
static int dirty = 1;
static void unpause(void);

/* ---------------- timing ---------------- */
static struct MsgPort *timer_port;
static struct TimeRequest *timer_req;
struct Device *TimerBase;
struct TimerIFace *ITimer;
static int timer_pending;

static int timer_open(void)
{
    timer_port = AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    if (!timer_port) return 0;
    timer_req = AllocSysObjectTags(ASOT_IOREQUEST, ASOIOR_ReplyPort, timer_port,
                                   ASOIOR_Size, sizeof(struct TimeRequest), TAG_DONE);
    if (!timer_req) return 0;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)timer_req, 0) != 0) {
        FreeSysObject(ASOT_IOREQUEST, timer_req);
        timer_req = 0;
        return 0;
    }
    TimerBase = timer_req->Request.io_Device;
    ITimer = (struct TimerIFace *)GetInterface((struct Library *)TimerBase, "main", 1, NULL);
    return ITimer != 0;
}

static void timer_stop(void)
{
    if (!timer_pending) return;
    if (!CheckIO((struct IORequest *)timer_req)) AbortIO((struct IORequest *)timer_req);
    WaitIO((struct IORequest *)timer_req);
    timer_pending = 0;
}

static void timer_close(void)
{
    if (timer_req && TimerBase) timer_stop();
    if (ITimer) DropInterface((struct Interface *)ITimer);
    if (TimerBase) CloseDevice((struct IORequest *)timer_req);
    if (timer_req) FreeSysObject(ASOT_IOREQUEST, timer_req);
    if (timer_port) FreeSysObject(ASOT_PORT, timer_port);
}

static uint64_t now_us(void)
{
    struct TimeVal tv;
    GetUpTime(&tv);
    return (uint64_t)tv.Seconds * 1000000ULL + tv.Microseconds;
}

static unsigned long clock_us(void) { return (unsigned long)now_us(); }

static void timer_start(uint32 us)
{
    timer_req->Request.io_Command = TR_ADDREQUEST;
    timer_req->Time.Seconds = us / 1000000;
    timer_req->Time.Microseconds = us % 1000000;
    SendIO((struct IORequest *)timer_req);
    timer_pending = 1;
}

/* ---------------- files ---------------- */

static void *load_file(const char *path, int32 *size)
{
    BPTR f;
    int64 len;
    void *p = 0;
    *size = 0;
    if (!(f = Open((CONST_STRPTR)path, MODE_OLDFILE))) return 0;
    len = GetFileSize(f);
    if (len > 0 && len < 0x1000000 && (p = malloc((size_t)len)) != 0) {
        if (Read(f, p, (int32)len) != (int32)len) { free(p); p = 0; }
        else *size = (int32)len;
    }
    Close(f);
    return p;
}

static int pot(int v) { return v >= 8 && v <= 4096 && (v & (v - 1)) == 0; }

/* a .btx: "BTX1", width and height (16-bit big-endian), a 32-byte title,
 * then R G B rows, top (north) first. *title may be 0. */
static uint8_t *load_btx(const char *path, int *w, int *h, char *title)
{
    int32 n;
    uint8_t *d = (uint8_t *)load_file(path, &n), *rgb;
    int ww, hh, i;
    if (!d) return 0;
    if (n < 40 || memcmp(d, "BTX1", 4) != 0) { free(d); return 0; }
    ww = d[4] << 8 | d[5];
    hh = d[6] << 8 | d[7];
    if (!pot(ww) || !pot(hh) || n < 40 + ww * hh * 3) { free(d); return 0; }
    if (title) { for (i = 0; i < 31; i++) title[i] = (char)d[8 + i]; title[31] = 0; }
    rgb = (uint8_t *)malloc((size_t)ww * hh * 3);
    if (rgb) {
        const uint8_t *s = d + 40;
        for (i = 0; i < ww * hh * 3; i++) rgb[i] = s[i];   /* a loop: newlib's memcpy is unsafe for big copies */
    }
    free(d);
    *w = ww;
    *h = hh;
    return rgb;
}

static int btx_title(const char *path, char *title)
{
    BPTR f = Open((CONST_STRPTR)path, MODE_OLDFILE);
    uint8_t hd[40];
    int i, ok = 0;
    if (!f) return 0;
    if (Read(f, hd, 40) == 40 && memcmp(hd, "BTX1", 4) == 0) {
        for (i = 0; i < 31; i++) title[i] = (char)hd[8 + i];
        title[31] = 0;
        ok = 1;
    }
    Close(f);
    return ok;
}

/* the names in a drawer that end in ext, sorted */
static int list_dir(const char *dir, const char *ext, char names[][48], int max)
{
    APTR ctx = ObtainDirContextTags(EX_StringNameInput, (ULONG)dir, EX_DataFields, EXF_NAME | EXF_TYPE, TAG_DONE);
    struct ExamineData *dat;
    int n = 0, i, j, el = (int)strlen(ext);
    if (!ctx) return 0;
    while ((dat = ExamineDir(ctx)) != 0 && n < max) {
        int l;
        if (!EXD_IS_FILE(dat)) continue;
        l = (int)strlen((const char *)dat->Name);
        if (l <= el || l >= 47 || strcasecmp((const char *)dat->Name + l - el, ext) != 0) continue;
        strcpy(names[n++], (const char *)dat->Name);
    }
    ReleaseDirContext(ctx);
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--) {
            char t[48];
            strcpy(t, names[j]); strcpy(names[j], names[j - 1]); strcpy(names[j - 1], t);
        }
    return n;
}

static void scan_pictures(void)
{
    static char names[MAX_PICS][48];
    int n, i;
    strcpy(pics[0].file, "");
    strcpy(pics[0].title, "BOING BALL");
    npics = 1;
    n = list_dir("PROGDIR:data", ".btx", names, MAX_PICS - 1);
    for (i = 0; i < n; i++) {
        char path[96];
        Pic *p = &pics[npics];
        snprintf(path, sizeof(path), "PROGDIR:data/%.40s", names[i]);
        strcpy(p->file, names[i]);
        if (!btx_title(path, p->title)) continue;
        if (!p->title[0]) { strncpy(p->title, names[i], 31); p->title[31] = 0; }
        npics++;
    }
}

/* the Boing ball: red and white squares, one per tile of this grid */
static uint8_t *boing_picture(int bands, int segs, int *w, int *h)
{
    int x, y;
    uint8_t *rgb = (uint8_t *)malloc(512 * 256 * 3), *d;
    if (!rgb) return 0;
    d = rgb;
    for (y = 0; y < 256; y++)
        for (x = 0; x < 512; x++) {
            int red = ((x * segs / 512) + (y * bands / 256)) & 1;
            *d++ = red ? 0xE0 : 0xF4;
            *d++ = red ? 0x18 : 0xF4;
            *d++ = red ? 0x18 : 0xF4;
        }
    *w = 512;
    *h = 256;
    return rgb;
}

/* the picture on the ball, cut for this grid */
static int ensure_picture(int pic, int bands, int segs)
{
    if (pic == loaded_pic && bands == loaded_b && segs == loaded_s) return 1;
    if (pic != loaded_pic || pic == 0) {
        uint8_t *rgb = 0;
        int w = 0, h = 0;
        if (pic > 0 && pic < npics) {
            char path[96];
            snprintf(path, sizeof(path), "PROGDIR:data/%s", pics[pic].file);
            rgb = load_btx(path, &w, &h, 0);
            if (!rgb) AB_W("can't load %.40s", path);
        }
        if (!rgb) { rgb = boing_picture(bands, segs, &w, &h); pic = 0; }
        if (!rgb) return 0;
        free(pic_rgb);
        pic_rgb = rgb;
        pic_w = w;
        pic_h = h;
    }
    rd_set_picture(pic_rgb, pic_w, pic_h, bands, segs, sigs);
    loaded_pic = pic;
    loaded_b = bands;
    loaded_s = segs;
    dirty = 1;
    return 1;
}

/* ---------------- themes ---------------- */

static uint32_t hexcol(const char *s, uint32_t dflt)
{
    char *e;
    unsigned long v;
    while (*s == ' ' || *s == '#') s++;
    if (strncasecmp(s, "none", 4) == 0) return NO_GRID;
    v = strtoul(s, &e, 16);
    return e == s ? dflt : (uint32_t)(v & 0xFFFFFF);
}

static void classic_theme(RdTheme *t)
{
    int f;
    memset(t, 0, sizeof(*t));
    t->bg = 0xAAAAAA;
    for (f = 0; f < 2; f++) {
        t->face[f].on = 1;
        t->face[f].col = 0xAAAAAA;
        t->face[f].grid = 0xA000A0;
        t->face[f].cell = 0.5f;
    }
    t->shadow_pct = 45;
}

/* a theme file: lines "key = value", # comments. See themes/boing.theme. */
static int load_theme(int idx)
{
    static const char *face_key[NFACES] = { "back", "floor", "left", "right", "ceiling" };
    RdTheme t;
    char path[96], *text, *line, *next;
    int32 n;
    int f;
    classic_theme(&t);
    for (f = 0; f < NFACES; f++) { free(face_tex[f]); face_tex[f] = 0; }
    if (idx < 0 || idx >= nthemes) { rd_set_theme(&t); return 0; }
    snprintf(path, sizeof(path), "PROGDIR:themes/%s", theme_file[idx]);
    text = (char *)load_file(path, &n);
    if (!text) { rd_set_theme(&t); return 0; }
    text = (char *)realloc(text, (size_t)n + 1);
    text[n] = 0;
    for (line = text; line && *line; line = next) {
        char *eq, *key, *val, *e;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if ((e = strchr(line, '#')) != 0) *e = 0;
        if (!(eq = strchr(line, '='))) continue;
        *eq = 0;
        key = line;
        val = eq + 1;
        while (*key == ' ' || *key == '\t') key++;
        for (e = eq - 1; e >= key && (*e == ' ' || *e == '\t' || *e == '\r'); e--) *e = 0;
        while (*val == ' ' || *val == '\t') val++;
        for (e = val + strlen(val) - 1; e >= val && (*e == ' ' || *e == '\t' || *e == '\r'); e--) *e = 0;
        if (strcasecmp(key, "background") == 0) t.bg = hexcol(val, t.bg);
        else if (strcasecmp(key, "shadow") == 0) {
            t.shadow_floor = strncasecmp(val, "floor", 5) == 0;
            e = strchr(val, ' ');
            if (e) t.shadow_pct = atoi(e + 1);
            if (strncasecmp(val, "off", 3) == 0) t.shadow_pct = 0;
        } else
            for (f = 0; f < NFACES; f++)
                if (strcasecmp(key, face_key[f]) == 0) {
                    RdFace *fc = &t.face[f];
                    char w[4][48];
                    int nw = 0;
                    const char *s = val;
                    memset(w, 0, sizeof(w));
                    while (*s && nw < 4) {
                        int k = 0;
                        while (*s == ' ' || *s == '\t') s++;
                        while (*s && *s != ' ' && *s != '\t' && k < 47) w[nw][k++] = *s++;
                        while (*s && *s != ' ' && *s != '\t') s++;
                        if (k) nw++;
                    }
                    memset(fc, 0, sizeof(*fc));
                    if (nw == 0 || strcasecmp(w[0], "off") == 0) break;
                    fc->on = 1;
                    fc->col = hexcol(w[0], 0x808080);
                    fc->grid = nw > 1 ? hexcol(w[1], NO_GRID) : NO_GRID;
                    fc->cell = nw > 2 ? (float)atof(w[2]) : 0.5f;
                    if (nw > 3) {
                        char tp[112];
                        snprintf(tp, sizeof(tp), "PROGDIR:themes/%s", w[3]);
                        face_tex[f] = load_btx(tp, &fc->tw, &fc->th, 0);
                        fc->tex = face_tex[f];
                        if (!fc->tex) AB_W("theme texture %.40s missing", w[3]);
                    }
                    break;
                }
    }
    free(text);
    rd_set_theme(&t);
    dirty = 1;
    return 1;
}

static void scan_themes(void)
{
    int n, i;
    n = list_dir("PROGDIR:themes", ".theme", theme_file, MAX_THEMES);
    nthemes = 0;
    for (i = 0; i < n; i++) {
        char path[96], *text;
        int32 len;
        snprintf(path, sizeof(path), "PROGDIR:themes/%.40s", theme_file[i]);
        strcpy(theme_file[nthemes], theme_file[i]);
        strncpy(theme_title[nthemes], theme_file[i], 31);
        theme_title[nthemes][31] = 0;
        if ((text = (char *)load_file(path, &len)) != 0) {
            char *nm = 0;
            int k;
            text = (char *)realloc(text, (size_t)len + 1);
            text[len] = 0;
            for (k = 0; k < len; k++)
                if ((k == 0 || text[k - 1] == '\n') && strncasecmp(text + k, "name", 4) == 0) { nm = strchr(text + k, '='); break; }
            if (nm) {
                int j = 0;
                nm++;
                while (*nm == ' ') nm++;
                while (*nm && *nm != '\n' && *nm != '\r' && *nm != '#' && j < 31) theme_title[nthemes][j++] = *nm++;
                while (j > 0 && theme_title[nthemes][j - 1] == ' ') j--;
                theme_title[nthemes][j] = 0;
            }
            free(text);
        }
        nthemes++;
    }
    /* the classic Boing room first */
    for (i = 0; i < nthemes; i++)
        if (strcasecmp(theme_file[i], "boing.theme") == 0) { cur_theme = i; break; }
}

/* ---------------- best scores ---------------- */
#define BEST_FILE "PROGDIR:boing_twist.best"
#define MAX_BEST 256
typedef struct { char key[64]; int32_t moves, secs; } Best;
static Best best[MAX_BEST];
static int nbest;

static void best_key(char *k, int n)
{
    snprintf(k, n, "%s|%ldx%ld|%ld", loaded_pic > 0 ? pics[loaded_pic].file : "boing",
             (long)P.bands, (long)P.segs, (long)scrambles[sel_scr]);
}

static Best *find_best(const char *k)
{
    int i;
    for (i = 0; i < nbest; i++)
        if (strcmp(best[i].key, k) == 0) return &best[i];
    return 0;
}

static void best_load(void)
{
    int32 n;
    char *text = (char *)load_file(BEST_FILE, &n), *line, *next;
    nbest = 0;
    if (!text) return;
    text = (char *)realloc(text, (size_t)n + 1);
    text[n] = 0;
    for (line = text; line && *line && nbest < MAX_BEST; line = next) {
        char key[64];
        long m, s;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (sscanf(line, "%63s %ld %ld", key, &m, &s) == 3) {
            strcpy(best[nbest].key, key);
            best[nbest].moves = (int32_t)m;
            best[nbest].secs = (int32_t)s;
            nbest++;
        }
    }
    free(text);
}

static void best_save(void)
{
    BPTR f = Open((CONST_STRPTR)BEST_FILE, MODE_NEWFILE);
    int i;
    char line[96];
    if (!f) return;
    for (i = 0; i < nbest; i++) {
        snprintf(line, sizeof(line), "%.63s %ld %ld\n", best[i].key, (long)best[i].moves, (long)best[i].secs);
        Write(f, line, (int32)strlen(line));
    }
    Close(f);
}

/* a solve: a new best on moves or time? (file names have no spaces in them) */
static int best_record(int32_t mv, int32_t s)
{
    char k[64];
    Best *b;
    best_key(k, sizeof(k));
    b = find_best(k);
    if (!b) {
        if (nbest >= MAX_BEST) return 0;
        b = &best[nbest++];
        strcpy(b->key, k);
        b->moves = mv;
        b->secs = s;
        best_save();
        return 1;
    }
    if (mv < b->moves || s < b->secs) {
        if (mv < b->moves) b->moves = mv;
        if (s < b->secs) b->secs = s;
        best_save();
        return 1;
    }
    return 0;
}

/* ---------------- the game's moves ---------------- */

static uint32_t anim_ms(int kind, int m)
{
    int flip = BT_IS_FLIP(m);
    switch (kind) {
    case K_SCRAMBLE: return flip ? 160 : 90;
    case K_SOLVE:    return flip ? 260 : 150;
    default:         return flip ? 420 : 240;
    }
}

static int enqueue(int kind, int m)
{
    if (nq >= 64) return 0;
    if ((kind == K_DO) && !bt_valid(&P, m)) return 0;
    queue[nq].kind = kind;
    queue[nq].move = m;
    nq++;
    return 1;
}

static void start_timer_if_needed(void)
{
    if (!timer_running) {
        timer_running = 1;
        play_start_us = now_us();
        play_paused_us = 0;
    }
}

static void enter_solved(void)
{
    screen = SC_SOLVED;
    solve_t0 = now_us();
    timer_running = 0;
    best_new = gave_up ? 0 : best_record(P.moves, secs);
    snd_play(SND_SOLVED);
    AB_I("solved: %ld moves, %ld s, peeks %ld%s%s", (long)P.moves, (long)secs, (long)peeks,
         gave_up ? " (solved for you)" : "", best_new ? " NEW BEST" : "");
    dirty = 1;
}

/* the next move off the queue, if there's none running */
static void anim_next(uint64_t t)
{
    if (auto_solve && nq == 0 && anim_move < 0) {
        if (P.nhist > 0) enqueue(K_SOLVE, 0);
        else auto_solve = 0;
    }
    while (anim_move < 0 && nq > 0) {
        QMove q = queue[0];
        int m = q.move, i;
        for (i = 1; i < nq; i++) queue[i - 1] = queue[i];
        nq--;
        if (q.kind == K_UNDO) m = bt_last_move(&P) >= 0 ? bt_inverse(bt_last_move(&P)) : -1;
        else if (q.kind == K_SOLVE) m = P.nhist > 0 ? bt_inverse(P.hist[P.nhist - 1]) : -1;
        if (m < 0) continue;
        anim_move = m;
        anim_kind = q.kind;
        anim_t0 = t;
        anim_len = anim_ms(q.kind, m) * 1000;
        if (q.kind == K_DO) start_timer_if_needed();
        if (q.kind != K_SCRAMBLE || BT_IS_FLIP(m)) snd_play(BT_IS_FLIP(m) ? SND_FLIP : SND_CLICK);
    }
}

static void anim_finish(void)
{
    int m = anim_move;
    switch (anim_kind) {
    case K_DO: bt_do(&P, m); break;
    case K_UNDO: bt_undo(&P); break;
    case K_SOLVE:
        P.nhist--;
        if (P.nscramble > P.nhist) P.nscramble = P.nhist;
        bt_apply(&P, m);
        break;
    case K_SCRAMBLE:
        bt_apply(&P, m);
        if (P.nhist < BT_HIST) P.hist[P.nhist++] = (int16_t)m;
        P.nscramble = P.nhist;
        break;
    }
    anim_move = -1;
    dirty = 1;
    if (screen == SC_PLAY && nq == 0 && anim_kind != K_SCRAMBLE && bt_solved(&P)) {
        auto_solve = 0;
        if (anim_kind == K_SOLVE) gave_up = 1;
        enter_solved();
    }
}

static float ease(float t) { return t < 0 ? 0 : t > 1 ? 1 : t * t * (3 - 2 * t); }

static void start_game(uint32_t seed)
{
    int b = grids[sel_grid][0], s = grids[sel_grid][1], i, tries;
    BtPuzzle tmp;
    ensure_picture(sel_pic, b, s);
    bt_init(&P, b, s);
    for (i = 0; i < b * s; i++) bt_set_sig(&P, i, sigs[i][0], sigs[i][1]);
    /* a scramble that leaves the ball solved (a checkerboard can) is no puzzle */
    tmp = P;
    for (tries = 0; tries < 40; tries++) {
        bt_scramble(&tmp, seed + (uint32_t)tries * 7919u, scrambles[sel_scr]);
        if (!bt_solved(&tmp)) break;
    }
    nq = 0;
    anim_move = -1;
    auto_solve = 0;
    for (i = 0; i < tmp.nscramble; i++) enqueue(K_SCRAMBLE, tmp.hist[i]);
    screen = SC_PLAY;
    timer_running = 0;
    secs = 0;
    peeks = 0;
    gave_up = 0;
    paused = user_paused = 0;
    sel_band = b / 2 - 1;
    sel_half = (s / 2 - s / 4 + s) % s;                 /* the half facing the eye at the start */
    view.x = 0; view.y = 0; view.roll = 0; view.dist = 3.6f;
    target_yaw = -180.0f;
    target_pitch = 12.0f;
    AB_I("play: %.31s %ldx%ld scramble %ld seed %lu", pics[sel_pic].title, (long)b, (long)s,
         (long)scrambles[sel_scr], (unsigned long)seed);
    dirty = 1;
}

/* the half centred on the eye's longitude */
static int half_facing(void)
{
    int S = P.segs ? P.segs : 8;
    float lon = -view.yaw, w = 360.0f / S;
    int c;
    while (lon < 0) lon += 360.0f;
    c = (int)floorf(lon / w + 0.5f);
    return ((c - S / 4) % S + S) % S;
}

static void face_half(int k)
{
    int S = P.segs;
    float a = (k + S / 4.0f) * 360.0f / S, ty = -a;
    while (ty - view.yaw > 180.0f) ty -= 360.0f;
    while (ty - view.yaw < -180.0f) ty += 360.0f;
    target_yaw = ty;
}

/* ---------------- input ---------------- */

static int busy_input(void) { return nq > 0 && queue[0].kind == K_SCRAMBLE; }

static void play_key(UWORD code)
{
    int S = P.segs, B = P.bands;
    if (paused && code != 0x19) return;
    switch (code) {
    case 0x4C: if (sel_band > 0) sel_band--; break;                                /* up */
    case 0x4D: if (sel_band < B - 1) sel_band++; break;                            /* down */
    case 0x4F: if (!busy_input()) enqueue(K_DO, BT_TWIST(sel_band, 1)); break;     /* left: west */
    case 0x4E: if (!busy_input()) enqueue(K_DO, BT_TWIST(sel_band, 0)); break;     /* right: east */
    case 0x20: sel_half = (sel_half + S - 1) % S; face_half(sel_half); break;      /* A */
    case 0x22: sel_half = (sel_half + 1) % S; face_half(sel_half); break;          /* D */
    case 0x40: case 0x44: case 0x43: if (!busy_input()) enqueue(K_DO, BT_FLIP(sel_half)); break;
    case 0x16: case 0x41: if (!busy_input()) enqueue(K_UNDO, 0); break;            /* U, backspace */
    case 0x10: target_yaw += 22.5f; break;                                         /* Q */
    case 0x12: target_yaw -= 22.5f; break;                                         /* E */
    case 0x11: target_pitch += 12.0f; if (target_pitch > 60) target_pitch = 60; break;   /* W */
    case 0x21: target_pitch -= 12.0f; if (target_pitch < -60) target_pitch = -60; break; /* S */
    case 0x33: face_half(sel_half); target_pitch = 12.0f; break;                   /* C */
    case 0x0B: case 0x4A: if (view.dist < 5.0f) view.dist += 0.2f; break;         /* - : further */
    case 0x0C: case 0x5E: if (view.dist > 2.8f) view.dist -= 0.2f; break;         /* = + : nearer */
    case 0x25: if (!peeking) peeks++; peeking = 1; break;                          /* H: hold to peek */
    case 0x19: user_paused = paused = !paused;                                     /* P */
        if (paused) pause_t0 = now_us(); else unpause();
        break;
    case 0x13: if (!busy_input()) { nq = 0; auto_solve = 1; } break;                /* R: solve it for me */
    case 0x36: start_game((uint32_t)now_us()); break;                              /* N */
    }
    dirty = 1;
}

static void change_row(int d)
{
    switch (sel_row) {
    case 0: sel_pic = (sel_pic + d + npics) % npics; break;
    case 1: sel_grid = (sel_grid + d + NGRIDS) % NGRIDS; break;
    case 2: sel_scr = (sel_scr + d + NSCRAMBLES) % NSCRAMBLES; break;
    case 3: if (nthemes > 0) { cur_theme = (cur_theme + d + nthemes) % nthemes; load_theme(cur_theme); } break;
    }
}

static void key_down(UWORD code)
{
    if (code == 0x5F || code == 0x50) {                 /* Help, F1 */
        if (screen != SC_HELP) { help_back = screen; screen = SC_HELP; }
        else screen = help_back;
        dirty = 1;
        return;
    }
    switch (screen) {
    case SC_TITLE:
        if (code == 0x45) { quit = 1; quit_why = "Esc"; }
        else if (code == 0x40 || code == 0x44 || code == 0x43) screen = SC_SELECT;
        else if (code == 0x25) { help_back = SC_TITLE; screen = SC_HELP; }
        else if (code == 0x14 && nthemes > 0) { cur_theme = (cur_theme + 1) % nthemes; load_theme(cur_theme); }
        break;
    case SC_SELECT:
        if (code == 0x45) screen = SC_TITLE;
        else if (code == 0x4C) sel_row = (sel_row + 3) % 4;
        else if (code == 0x4D) sel_row = (sel_row + 1) % 4;
        else if (code == 0x4F) change_row(-1);
        else if (code == 0x4E) change_row(1);
        else if (code == 0x40 || code == 0x44 || code == 0x43) start_game((uint32_t)now_us());
        break;
    case SC_PLAY:
        if (code == 0x45) { screen = SC_TITLE; nq = 0; anim_move = -1; paused = 0; auto_solve = 0; }
        else if (code == 0x14 && nthemes > 0) { cur_theme = (cur_theme + 1) % nthemes; load_theme(cur_theme); }
        else play_key(code);
        break;
    case SC_SOLVED:
        if (code == 0x45) screen = SC_TITLE;
        else if (code == 0x40 || code == 0x44 || code == 0x43) screen = SC_SELECT;
        break;
    case SC_HELP:
        screen = help_back;
        break;
    }
    dirty = 1;
}

/* the mouse: left drag on the ball turns (sideways: twist that band; up or
 * down: flip the half centred there), left click selects, right drag
 * turns the whole ball round to look at it */
static int ldown, rdown, lacted, mx0, my0, grab_band, grab_lon_ok;
static float grab_lon;

static void pick_at(int x, int y, int *band, float *lon)
{
    float la, lo;
    *band = -1;
    if (!rd_pick(&view, x, y, &la, &lo)) return;
    *band = (int)((90.0f - la) * P.bands / 180.0f);
    if (*band >= P.bands) *band = P.bands - 1;
    if (*band < 0) *band = 0;
    *lon = lo;
}

static int half_at(float lon)
{
    int S = P.segs, c = (int)floorf(lon / (360.0f / S) + 0.5f);
    return ((c - S / 4) % S + S) % S;
}

static void mouse_button(UWORD code, int x, int y)
{
    switch (code) {
    case SELECTDOWN:
        ldown = 1; lacted = 0; mx0 = x; my0 = y;
        grab_band = -1;
        if (screen == SC_PLAY) { pick_at(x, y, &grab_band, &grab_lon); grab_lon_ok = grab_band >= 0; }
        break;
    case SELECTUP:
        if (ldown && !lacted) {
            if (screen == SC_PLAY && grab_band >= 0) {
                sel_band = grab_band;
                sel_half = half_at(grab_lon);
            } else if (screen == SC_TITLE) screen = SC_SELECT;
            else if (screen == SC_SELECT) {
                int cx, cy, r;
                rd_ball_screen(&view, &cx, &cy, &r);
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) < r * r) start_game((uint32_t)now_us());
            } else if (screen == SC_SOLVED) screen = SC_SELECT;
            else if (screen == SC_HELP) screen = help_back;
        }
        ldown = 0;
        break;
    case MENUDOWN: rdown = 1; mx0 = x; my0 = y; break;
    case MENUUP: rdown = 0; break;
    }
    dirty = 1;
}

static void mouse_move(int x, int y)
{
    int dx = x - mx0, dy = y - my0;
    if (rdown) {
        view.yaw += dx * 0.45f;
        view.pitch += dy * 0.45f;
        if (view.pitch > 70) view.pitch = 70;
        if (view.pitch < -70) view.pitch = -70;
        target_yaw = view.yaw;
        target_pitch = view.pitch;
        mx0 = x; my0 = y;
        dirty = 1;
        return;
    }
    if (!ldown || screen != SC_PLAY || grab_band < 0 || paused || busy_input()) return;
    if (abs(dx) > 18 && abs(dx) > abs(dy)) {                       /* a twist each 18 px sideways */
        enqueue(K_DO, BT_TWIST(grab_band, dx < 0));
        sel_band = grab_band;
        mx0 = x; my0 = y;
        lacted = 1;
    } else if (!lacted && abs(dy) > 24 && abs(dy) > abs(dx)) {     /* one flip per drag */
        sel_half = half_at(grab_lon);
        sel_band = grab_band;
        enqueue(K_DO, BT_FLIP(sel_half));
        lacted = 1;
    }
}

/* ---------------- bridge hooks ---------------- */

static int parse_move(const char *a)
{
    /* T<band>+ / T<band>- or F<half> */
    if (!a) return -1;
    while (*a == ' ') a++;
    if (*a == 'T' || *a == 't') {
        int b = atoi(a + 1);
        const char *e = a + 1;
        while (*e >= '0' && *e <= '9') e++;
        return BT_TWIST(b, *e == '-');
    }
    if (*a == 'F' || *a == 'f') return BT_FLIP(atoi(a + 1));
    return -1;
}

static int hk_selftest(const char *args, char *res, int len)
{
    uint32_t h = bt_vector_hash();
    (void)args;
    snprintf(res, len, "hash=%08lx expect=%08lx %s", (unsigned long)h, (unsigned long)BT_VECTOR_HASH,
             h == BT_VECTOR_HASH ? "PASS" : "FAIL");
    AB_I("selftest %s", res);
    return h == BT_VECTOR_HASH ? 0 : -1;
}

static int hk_scramble(const char *args, char *res, int len)
{
    long seed = 1, n = scrambles[sel_scr], g = -1;
    int i;
    if (args) sscanf(args, "%ld %ld %ld", &seed, &n, &g);
    if (g >= 0 && g < NGRIDS) sel_grid = (int32_t)g;
    for (i = 0; i < NSCRAMBLES; i++) if (scrambles[i] == n) sel_scr = i;
    start_game((uint32_t)seed);
    if (n != scrambles[sel_scr]) {           /* any length: straight to the state, no animation */
        BtPuzzle tmp = P;
        bt_scramble(&tmp, (uint32_t)seed, (int)n);
        nq = 0;
        P = tmp;
    }
    snprintf(res, len, "scramble seed %ld len %ld grid %ldx%ld", seed, n, (long)P.bands, (long)P.segs);
    return 0;
}

static int hk_move(const char *args, char *res, int len)
{
    int m = parse_move(args), now = args && strchr(args, '!') != 0;
    if (m < 0 || !bt_valid(&P, m) || screen != SC_PLAY) { snprintf(res, len, "bad move or not playing"); return -1; }
    if (now) {
        bt_do(&P, m);
        start_timer_if_needed();
        dirty = 1;
        if (bt_solved(&P)) enter_solved();
    } else enqueue(K_DO, m);
    snprintf(res, len, "move %x%s", m, now ? " now" : " queued");
    return 0;
}

static int hk_hash(const char *args, char *res, int len)
{
    (void)args;
    snprintf(res, len, "hash=%08lx solved=%ld moves=%ld queued=%ld anim=%ld screen=%ld", (unsigned long)bt_hash(&P),
             (long)bt_solved(&P), (long)P.moves, (long)nq, (long)(anim_move >= 0), (long)screen);
    return 0;
}

static int hk_solve(const char *args, char *res, int len)
{
    if (screen != SC_PLAY) { snprintf(res, len, "not playing"); return -1; }
    if (args && strncmp(args, "now", 3) == 0) {
        nq = 0;
        anim_move = -1;
        auto_solve = 0;
        while (P.nhist > 0) { bt_apply(&P, bt_inverse(P.hist[--P.nhist])); }
        P.nscramble = 0;
        gave_up = 1;
        if (bt_solved(&P)) enter_solved();
    } else {
        nq = 0;
        auto_solve = 1;
    }
    snprintf(res, len, "solving (%ld moves back)", (long)P.nhist);
    return 0;
}

static int hk_picture(const char *args, char *res, int len)
{
    int i = args ? atoi(args) : 0;
    if (i < 0 || i >= npics) { snprintf(res, len, "0..%ld", (long)npics - 1); return -1; }
    sel_pic = i;
    snprintf(res, len, "picture %ld %.31s", (long)i, pics[i].title);
    dirty = 1;
    return 0;
}

static int hk_theme(const char *args, char *res, int len)
{
    int i = args ? atoi(args) : 0;
    if (i < 0 || i >= nthemes) { snprintf(res, len, "0..%ld", (long)nthemes - 1); return -1; }
    cur_theme = i;
    load_theme(i);
    snprintf(res, len, "theme %ld %.31s", (long)i, theme_title[i]);
    return 0;
}

static int hk_screen(const char *args, char *res, int len)
{
    static const char *names[] = { "title", "select", "play", "solved", "help" };
    int i;
    for (i = 0; i < 5; i++)
        if (args && strncasecmp(args, names[i], strlen(names[i])) == 0) {
            if (i == SC_PLAY && screen != SC_PLAY) start_game(1);
            else if (i == SC_HELP) { help_back = screen; screen = SC_HELP; }
            else screen = i;
            dirty = 1;
            snprintf(res, len, "screen %s", names[i]);
            return 0;
        }
    snprintf(res, len, "title select play solved help");
    return -1;
}

static int hk_undo(const char *args, char *res, int len)
{
    (void)args;
    enqueue(K_UNDO, 0);
    snprintf(res, len, "undo queued");
    return 0;
}

static int hk_view(const char *args, char *res, int len)
{
    float y = view.yaw, p = view.pitch, d = view.dist;
    long sb = sel_band, sh = sel_half;
    if (args) sscanf(args, "%f %f %f %ld %ld", &y, &p, &d, &sb, &sh);
    view.yaw = target_yaw = y;
    view.pitch = target_pitch = p;
    if (d >= 2.4f && d <= 6.0f) view.dist = d;
    sel_band = (int32_t)sb;
    sel_half = (int32_t)sh;
    dirty = 1;
    snprintf(res, len, "view yaw %ld pitch %ld dist %ld/10", (long)y, (long)p, (long)(view.dist * 10));
    return 0;
}

static void unpause(void)
{
    uint64_t d = now_us() - pause_t0;
    play_paused_us += d;
    anim_t0 += d;                         /* a move half done carries on from there */
}

static int hk_pause(const char *args, char *res, int len)
{
    int on = args && strncasecmp(args, "on", 2) == 0;
    if (on && !paused) pause_t0 = now_us();
    if (!on && paused) unpause();
    paused = on;
    user_paused = 0;
    snprintf(res, len, "pause %s", on ? "on" : "off");
    return 0;
}

static int hk_quit(const char *args, char *res, int len)
{
    (void)args;
    quit = 1;
    quit_why = "bridge";
    snprintf(res, len, "quitting");
    return 0;
}

static int hk_activate(const char *args, char *res, int len)
{
    struct Window *w = disp_window();
    (void)args;
    if (w) { WindowToFront(w); ActivateWindow(w); }
    snprintf(res, len, "activated");
    return 0;
}

static int hk_bench(const char *args, char *res, int len)
{
    int n = args ? atoi(args) : 60;
    bench_left = n > 0 ? n : 60;
    bench_fps10 = 0;
    snprintf(res, len, "bench %ld frames", (long)bench_left);
    return 0;
}

static int hk_peek(const char *args, char *res, int len)
{
    peeking = args && strncasecmp(args, "on", 2) == 0;
    dirty = 1;
    snprintf(res, len, "peek %s", peeking ? "on" : "off");
    return 0;
}

/* ---------------- drawing ---------------- */

static void fmt_time(char *b, int n, long s)
{
    snprintf(b, n, "%ld:%02ld", s / 60, s % 60);
}

static void draw_hud(int W, int H)
{
    int U = W >= 800 ? 2 : 1, L = 10 * U, x;
    char buf[96], t[16];
    switch (screen) {
    case SC_TITLE:
        hud_ctext(H / 12, "BOING TWIST", W >= 800 ? 6 : 4, C_RED);
        hud_ctext(H / 12 + 64 * U / 2 + 14 * U, "PUT THE PICTURE BACK TOGETHER", U, C_WHITE);
        if ((now_us() / 500000) % 3 != 2) hud_ctext(H - 7 * L, "PRESS SPACE OR CLICK TO PLAY", U, C_YELLOW);
        hud_ctext(H - 5 * L, "H HELP    T THEME    F FULL SCREEN    ESC QUIT", U, C_DIM);
        snprintf(buf, sizeof(buf), "ROOM: %s", nthemes ? theme_title[cur_theme] : "BOING (BUILT IN)");
        hud_ctext(H - 3 * L, buf, U, C_DIM);
        break;
    case SC_SELECT: {
        static const char *rows[] = { "PICTURE", "GRID", "SCRAMBLE", "ROOM" };
        char k[64];
        Best *b;
        int i, y0 = H / 10;
        hud_ctext(y0 - 3 * L / 2, "CHOOSE A PUZZLE", 2 * U, C_YELLOW);
        hud_shade(16 * U, y0 + L, W * 9 / 20, y0 + 11 * L, 45);
        for (i = 0; i < 4; i++) {
            const char *v = i == 0 ? pics[sel_pic].title : i == 1 ? grid_name[sel_grid]
                          : i == 2 ? scramble_name[sel_scr] : (nthemes ? theme_title[cur_theme] : "BOING");
            int y = y0 + 2 * L + i * 2 * L;
            hud_text(24 * U, y, i == sel_row ? ">" : " ", U, C_YELLOW);
            if (i == 0) snprintf(buf, sizeof(buf), "PICTURE  %ld OF %ld", (long)sel_pic + 1, (long)npics);
            else snprintf(buf, sizeof(buf), "%s", rows[i]);
            hud_text(36 * U, y, buf, U, i == sel_row ? C_YELLOW : C_DIM);
            if (i == 2) snprintf(buf, sizeof(buf), "%s (%ld MOVES)", v, (long)scrambles[sel_scr]);
            else snprintf(buf, sizeof(buf), "%s", v);
            hud_text(36 * U, y + L, buf, U, i == sel_row ? C_WHITE : C_DIM);
        }
        snprintf(k, sizeof(k), "%s|%ldx%ld|%ld", sel_pic > 0 ? pics[sel_pic].file : "boing",
                 (long)grids[sel_grid][0], (long)grids[sel_grid][1], (long)scrambles[sel_scr]);
        b = find_best(k);
        if (b) { fmt_time(t, sizeof(t), b->secs); snprintf(buf, sizeof(buf), "BEST %ld MOVES  %s", (long)b->moves, t); }
        else snprintf(buf, sizeof(buf), "NOT SOLVED YET");
        hud_text(24 * U, y0 + 12 * L, buf, U, C_CYAN);
        if (pic_rgb && loaded_pic == sel_pic) {
            int pw = W * 9 / 20 - 16 * U, ph = pw / 2, px = 16 * U, py = H - ph - 4 * L;
            if (py > y0 + 14 * L) {
                hud_frame(px - 2, py - 2, px + pw + 1, py + ph + 1, 2, C_WHITE);
                hud_picture(pic_rgb, pic_w, pic_h, px, py, pw, ph);
            }
        }
        hud_ctext(H - 2 * L, "ARROWS CHOOSE  RETURN PLAY  ESC BACK", U, C_DIM);
        break;
    }
    case SC_PLAY:
    case SC_SOLVED:
        hud_shade(0, 0, W - 1, 2 * L - 1, 50);
        hud_text(8 * U, L / 2, pics[loaded_pic >= 0 ? loaded_pic : 0].title, U, C_WHITE);
        fmt_time(t, sizeof(t), secs);
        snprintf(buf, sizeof(buf), "MOVES %ld  TIME %s", (long)P.moves, t);
        hud_text(W - hud_width(buf, U) - 8 * U, L / 2, buf, U, C_YELLOW);
        if (screen == SC_PLAY) {
            if (busy_input()) hud_ctext(H - 4 * L, "SCRAMBLING...", 2 * U, C_YELLOW);
            else if (peeking) hud_ctext(3 * L, "PEEK: THE PICTURE AS IT SHOULD BE", U, C_CYAN);
            hud_shade(0, H - 2 * L, W - 1, H - 1, 50);
            hud_ctext(H - 3 * L / 2, "ARROWS TWIST  A/D SPACE FLIP  U UNDO  H PEEK", U, C_DIM);
            if (paused && user_paused) {
                hud_shade(0, 2 * L, W - 1, H - 2 * L - 1, 55);
                hud_ctext(H / 2 - 8 * U, "PAUSED", 4 * U / 2 + 2, C_WHITE);
                hud_ctext(H / 2 + 24 * U, "P TO CARRY ON", U, C_DIM);
            }
        } else {
            hud_ctext(3 * L, gave_up ? "SOLVED FOR YOU" : "SOLVED!", gave_up ? 3 * U / 2 + 1 : 3 * U, gave_up ? C_DIM : C_YELLOW);
            if (!gave_up) {
                fmt_time(t, sizeof(t), secs);
                snprintf(buf, sizeof(buf), "%ld MOVES IN %s%s", (long)P.moves, t, best_new ? " - A NEW BEST!" : "");
                hud_ctext(3 * L + 30 * U, buf, U, best_new ? C_CYAN : C_WHITE);
            }
            hud_ctext(H - 2 * L, "SPACE: ANOTHER PUZZLE    ESC: TITLE", U, C_DIM);
        }
        break;
    case SC_HELP: {
        static const char *lines[] = {
            "THE PICTURE ON THE BALL IS CUT INTO TILES",
            "AND JUMBLED. PUT IT BACK TOGETHER.",
            "",
            "TWIST: A BAND OF TILES TURNS ROUND THE BALL",
            "  UP/DOWN PICKS THE BAND (YELLOW RINGS)",
            "  LEFT/RIGHT TWISTS IT ONE TILE",
            "FLIP: HALF THE BALL TURNS OVER, TOP TO BOTTOM",
            "  A/D PICKS THE HALF (BLUE LINES)",
            "  SPACE FLIPS IT. ITS TILES END UP UPSIDE DOWN",
            "",
            "MOUSE: DRAG A BAND SIDEWAYS TO TWIST IT,",
            "  DRAG UP OR DOWN TO FLIP THAT HALF,",
            "  RIGHT BUTTON DRAG TO TURN THE BALL ROUND",
            "",
            "Q/E W/S TURN THE BALL  +/- ZOOM  C FACE",
            "U UNDO   H PEEK (HOLD)   P PAUSE   R GIVE UP",
            "N NEW SCRAMBLE   T ROOM   F FULL SCREEN",
            "",
            "PRESS ANY KEY",
        };
        int i, n = (int)(sizeof(lines) / sizeof(lines[0])), y0 = (H - n * L) / 2;
        hud_shade(0, 0, W - 1, H - 1, 35);
        hud_ctext(y0 - 3 * L, "HOW TO PLAY", 2 * U, C_YELLOW);
        x = (W - hud_width(lines[3], U)) / 2;
        for (i = 0; i < n; i++) hud_text(x, y0 + i * L, lines[i], U, i == n - 1 ? C_YELLOW : C_WHITE);
        break;
    }
    }
}

/* where the ball is and what it shows on this screen */
static void scene(uint64_t t, const BtPuzzle **pz, int *flags)
{
    float secs_f = (float)(t % 1000000000ULL) / 1e6f;
    *flags = 0;
    *pz = &P;
    switch (screen) {
    case SC_TITLE: {
        /* the 1984 demo: bouncing across the room, spinning on a leaning axis */
        float ph = secs_f * 2.4f, b = fabsf(sinf(ph));
        static float last_b = 1;
        ensure_picture(0, 8, 16);
        bt_init(&show, 8, 16);
        *pz = &show;
        view.x = 1.6f * sinf(secs_f * 0.55f);
        view.y = -0.5f + 1.7f * b;
        view.roll = -17.0f;
        view.pitch = 0;
        view.yaw = fmodf(secs_f * 70.0f, 360.0f);
        view.dist = 4.4f;
        if (b < 0.08f && last_b >= 0.08f) snd_play(SND_THUD);
        last_b = b;
        break;
    }
    case SC_SELECT:
        ensure_picture(sel_pic, grids[sel_grid][0], grids[sel_grid][1]);
        bt_init(&show, grids[sel_grid][0], grids[sel_grid][1]);
        *pz = &show;
        view.x = 1.05f; view.y = 0.05f; view.roll = 0; view.pitch = 12.0f;
        view.yaw = fmodf(-secs_f * 25.0f, 360.0f);
        view.dist = 3.9f;
        *flags = 0;
        break;
    case SC_SOLVED: {
        float s = (float)(t - solve_t0) / 1e6f, ph = s * 2.6f, b = fabsf(sinf(ph));
        static float last_b2 = 1;
        view.x = 1.0f * sinf(s * 0.8f);
        view.y = -0.5f + 1.5f * b;
        view.dist = 4.3f;
        view.roll = -17.0f * (s < 1 ? s : 1);
        view.yaw = target_yaw + s * 90.0f;
        view.pitch = target_pitch * (s < 1 ? 1 - s : 0);
        if (b < 0.08f && last_b2 >= 0.08f && s > 0.2f) snd_play(SND_THUD);
        last_b2 = b;
        break;
    }
    case SC_PLAY:
    case SC_HELP:
        view.x = 0; view.y = 0; view.roll = 0;
        if (screen == SC_HELP && help_back != SC_PLAY) { *pz = &show; break; }   /* from the title or select */
        *flags = RD_SELECT;
        if (peeking) *flags |= RD_PEEK;
        break;
    }
}

static int moving_screen(void)
{
    return screen == SC_TITLE || screen == SC_SELECT || screen == SC_SOLVED ||
           (screen == SC_HELP && help_back != SC_PLAY);
}

/* ---------------- main ---------------- */

int main(int argc, char **argv)
{
    int W = 800, H = 600, full = 0, bridge = 0, i, rc = 0;
    uint64_t next_frame, t_fps;
    long fps_frames = 0;
    uint64_t bench_sum[6];
    int bench_n = 0, last_lit, last_lin, last_detail;

    for (i = 1; i < argc; i++) {
        if (strncasecmp(argv[i], "RES=", 4) == 0) {
            int r = atoi(argv[i] + 4);
            if (r >= 1000) { W = 1024; H = 768; }
            else if (r >= 800) { W = 800; H = 600; }
            else { W = 640; H = 480; }
        } else if (strcasecmp(argv[i], "FULLSCREEN") == 0) full = 1;
        else if (strcasecmp(argv[i], "FLAT") == 0) rd_lit = 0;
        else if (strcasecmp(argv[i], "LINEAR") == 0) rd_linear = 1;
        else if (strcmp(argv[i], "?") == 0) {
            printf("Usage: boing_twist [RES=640|800|1024] [FULLSCREEN] [FLAT] [LINEAR]\n");
            return 0;
        }
    }
    frame_w = W;
    frame_h = H;
    if (!timer_open()) { printf("boing_twist: no timer.device\n"); rc = 20; goto done; }
    bridge = ab_init("boing_twist") == 0;
    rd_clock = clock_us;
    if (!rd_open(W, H)) { printf("boing_twist: no OpenGL (OSMesa) context\n"); AB_E("rd_open failed"); rc = 20; goto done; }
    if (!hud_init()) AB_W("topaz 8 not available: no text");
    hud_target(rd_pixels(), W, H);
    if (!disp_open("Boing Twist", W, H, 1, full)) {
        printf("boing_twist: can't open a window or screen\n");
        rc = 20;
        goto done;
    }
    if (!snd_open()) AB_W("no sound (ahi.device unavailable)");
    scan_pictures();
    scan_themes();
    load_theme(cur_theme);
    best_load();
    bt_init(&P, 4, 8);
    bt_init(&show, 8, 16);
    ensure_picture(0, 8, 16);

    if (bridge) {
        ab_register_var("screen", AB_TYPE_I32, &screen);
        ab_register_var("moves", AB_TYPE_I32, &moves32);
        ab_register_var("secs", AB_TYPE_I32, &secs);
        ab_register_var("solved", AB_TYPE_I32, &solved32);
        ab_register_var("hash", AB_TYPE_I32, &hash32);
        ab_register_var("anim", AB_TYPE_I32, &anim_busy);
        ab_register_var("pic", AB_TYPE_I32, &sel_pic);
        ab_register_var("npics", AB_TYPE_I32, &npics);
        ab_register_var("grid", AB_TYPE_I32, &sel_grid);
        ab_register_var("scr", AB_TYPE_I32, &sel_scr);
        ab_register_var("theme", AB_TYPE_I32, &cur_theme);
        ab_register_var("nthemes", AB_TYPE_I32, &nthemes);
        ab_register_var("sel_band", AB_TYPE_I32, &sel_band);
        ab_register_var("sel_half", AB_TYPE_I32, &sel_half);
        ab_register_var("peeks", AB_TYPE_I32, &peeks);
        ab_register_var("paused", AB_TYPE_I32, &paused);
        ab_register_var("fps10", AB_TYPE_I32, &fps10);
        ab_register_var("bench_fps10", AB_TYPE_I32, &bench_fps10);
        ab_register_var("bench_left", AB_TYPE_I32, &bench_left);
        ab_register_var("ms_bg", AB_TYPE_I32, &ms_bg);
        ab_register_var("ms_ball", AB_TYPE_I32, &ms_ball);
        ab_register_var("ms_hud", AB_TYPE_I32, &ms_hud);
        ab_register_var("ms_shade", AB_TYPE_I32, &ms_shade);
        ab_register_var("ms_blit", AB_TYPE_I32, &ms_blit);
        ab_register_var("tris", AB_TYPE_I32, &tris32);
        ab_register_var("lit", AB_TYPE_I32, &rd_lit);
        ab_register_var("linear", AB_TYPE_I32, &rd_linear);
        ab_register_var("detail", AB_TYPE_I32, &rd_detail);
        ab_register_var("width", AB_TYPE_I32, &frame_w);
        ab_register_var("height", AB_TYPE_I32, &frame_h);
        ab_register_var("zbuf", AB_TYPE_I32, &rd_zbuf);
        ab_register_var("bands", AB_TYPE_I32, &P.bands);
        ab_register_hook("selftest", "puzzle core vector hash vs the host's", hk_selftest);
        ab_register_hook("scramble", "seed len [grid]: start a puzzle", hk_scramble);
        ab_register_hook("move", "T<band>+ T<band>- F<half> (! = now)", hk_move);
        ab_register_hook("hash", "the puzzle's state hash", hk_hash);
        ab_register_hook("solve", "anim | now", hk_solve);
        ab_register_hook("picture", "index", hk_picture);
        ab_register_hook("theme", "index", hk_theme);
        ab_register_hook("screen", "title select play solved help", hk_screen);
        ab_register_hook("undo", "undo the last move", hk_undo);
        ab_register_hook("view", "yaw pitch dist band half", hk_view);
        ab_register_hook("pause", "on | off (no overlay)", hk_pause);
        ab_register_hook("quit", "quit the game", hk_quit);
        ab_register_hook("activate", "window to front and active", hk_activate);
        ab_register_hook("bench", "frames: render flat out and time it", hk_bench);
        ab_register_hook("peek", "on | off", hk_peek);
    }
    AB_I("ready %s %ldx%ld pictures=%ld themes=%ld sound=%ld", rd_renderer(), (long)W, (long)H,
         (long)npics, (long)nthemes, (long)1);

    last_lit = rd_lit; last_lin = rd_linear; last_detail = rd_detail;
    memset(bench_sum, 0, sizeof(bench_sum));
    SetSignal(0L, SIGBREAKF_CTRL_C);
    next_frame = t_fps = now_us();
    while (!quit) {
        struct IntuiMessage *m;
        uint64_t t, t0, t1, t2, t3;
        const BtPuzzle *pz;
        int flags, rendered = 0;

        while (disp_window() && (m = (struct IntuiMessage *)GetMsg(disp_window()->UserPort)) != 0) {
            ULONG cls = m->Class;
            UWORD code = m->Code, qual = m->Qualifier;
            int mx, my;
            ReplyMsg((struct Message *)m);
            disp_mouse(&mx, &my);
            if (cls == IDCMP_CLOSEWINDOW) { quit = 1; quit_why = "close gadget"; }
            else if (cls == IDCMP_INACTIVEWINDOW) { peeking = 0; ldown = rdown = 0; }
            else if (cls == IDCMP_MOUSEBUTTONS) mouse_button(code, mx, my);
            else if (cls == IDCMP_MOUSEMOVE) mouse_move(mx, my);
            else if (cls == IDCMP_RAWKEY) {
                if (code == DISP_KEY_F || code == DISP_KEY_F10) {
                    if (!disp_toggle()) { quit = 1; quit_why = "no display"; }
                    AB_I("display: %s", disp_fullscreen() ? "full screen" : "window");
                    dirty = 1;
                    break;                                          /* the old window's port is gone */
                }
                if (code & IECODE_UP_PREFIX) {
                    if ((code & 0x7F) == 0x25) { peeking = 0; dirty = 1; }
                } else if (!(qual & IEQUALIFIER_REPEAT) || code == 0x4C || code == 0x4D)
                    key_down(code);
            }
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) { quit = 1; quit_why = "Ctrl-C"; }
        if (bridge) ab_poll();
        if (quit) break;

        /* up to 30 frames a second, on a timer (WaitTOF spins under QEMU); a
         * frame that took longer still sleeps 3 ms, so the bridge and the
         * rest of the system get the CPU too */
        t = now_us();
        if (t < next_frame && bench_left <= 0) {
            uint64_t ts;
            if (!timer_pending) timer_start((uint32)(next_frame - t));
            ts = now_us();
            Wait((1UL << timer_port->mp_SigBit) | (disp_window() ? 1UL << disp_window()->UserPort->mp_SigBit : 0) | SIGBREAKF_CTRL_C);
            if (timer_pending && CheckIO((struct IORequest *)timer_req)) {
                uint64_t late;
                WaitIO((struct IORequest *)timer_req);
                timer_pending = 0;
                late = now_us() - ts;
                late = late > next_frame - ts ? late - (next_frame - ts) : 0;
                if ((int32_t)(late / 1000) > sleep_late_ms) sleep_late_ms = (int32_t)(late / 1000);
            }
            snd_update();
            continue;
        }
        next_frame += FRAME_US;
        if (next_frame < t + 3000 && bench_left <= 0) next_frame = t + 3000;

        /* the game moves on */
        if (paused) {
            /* nothing moves */
        } else {
            anim_next(t);
            while (anim_move >= 0 && t - anim_t0 >= anim_len) { anim_finish(); anim_next(t); }
            if (timer_running && screen == SC_PLAY) {
                int32_t s = (int32_t)((t - play_start_us - play_paused_us) / 1000000ULL);
                if (s != secs) { secs = s; dirty = 1; }
            }
            if (fabsf(target_yaw - view.yaw) > 0.05f || fabsf(target_pitch - view.pitch) > 0.05f) {
                if (screen == SC_PLAY) {
                    view.yaw += (target_yaw - view.yaw) * 0.25f;
                    view.pitch += (target_pitch - view.pitch) * 0.25f;
                    dirty = 1;
                }
            }
        }
        anim_busy = anim_move >= 0 || nq > 0;
        moves32 = P.moves;
        solved32 = bt_solved(&P);
        hash32 = (int32_t)bt_hash(&P);
        tris32 = rd_tris;
        if (rd_lit != last_lit || rd_linear != last_lin || rd_detail != last_detail) {
            if (rd_detail < 8) rd_detail = 8;
            if (rd_detail > 64) rd_detail = 64;
            last_lit = rd_lit; last_lin = rd_linear; last_detail = rd_detail;
            dirty = 1;
        }

        if (bench_left > 0) { view.yaw += 3.0f; target_yaw = view.yaw; dirty = 1; }
        if ((dirty || anim_move >= 0 || (moving_screen() && !paused)) && disp_window()) {
            t0 = now_us();
            scene(t, &pz, &flags);
            rd_draw(pz, &view, pz == &P ? anim_move : -1,
                    anim_move >= 0 ? ease((float)((paused ? pause_t0 : t) - anim_t0) / (float)anim_len) : 0,
                    sel_band, sel_half, flags);
            t1 = now_us();
            draw_hud(W, H);
            t2 = now_us();
            disp_present(rd_pixels());
            t3 = now_us();
            ms_bg = (int32_t)(rd_us_bg / 1000);
            ms_ball = (int32_t)(rd_us_ball / 1000);
            ms_shade = (int32_t)(rd_us_shade / 1000);
            ms_hud = (int32_t)((t2 - t1) / 1000);
            ms_blit = (int32_t)((t3 - t2) / 1000);
            dirty = 0;
            rendered = 1;
            fps_frames++;
            if (bench_left > 0) {
                bench_sum[0] += t3 - t0;
                bench_sum[1] += rd_us_bg;
                bench_sum[2] += rd_us_ball;
                bench_sum[3] += t2 - t1;
                bench_sum[4] += t3 - t2;
                bench_sum[5] += rd_us_shade;
                bench_n++;
                if (--bench_left == 0) {
                    long f = (long)(bench_sum[0] / bench_n);
                    bench_fps10 = f > 0 ? (int32_t)(10000000L / f) : 0;
                    AB_I("bench %ldx%ld lit=%ld linear=%ld detail=%ld tris=%ld: %ld.%ld ms/frame = %ld.%ld fps (bg %ld ball %ld of which shading %ld, hud %ld blit %ld ms)",
                         (long)W, (long)H, (long)rd_lit, (long)rd_linear, (long)rd_detail, (long)rd_tris,
                         f / 1000, (f % 1000) / 100, (long)bench_fps10 / 10, (long)bench_fps10 % 10,
                         (long)(bench_sum[1] / bench_n / 1000), (long)(bench_sum[2] / bench_n / 1000),
                         (long)(bench_sum[5] / bench_n / 1000), (long)(bench_sum[3] / bench_n / 1000), (long)(bench_sum[4] / bench_n / 1000));
                    memset(bench_sum, 0, sizeof(bench_sum));
                    bench_n = 0;
                }
            }
        }
        (void)rendered;
        snd_update();
        if (t - t_fps >= 2000000ULL) {
            fps10 = (int32_t)(fps_frames * 10000000ULL / (t - t_fps));
            fps_frames = 0;
            t_fps = t;
        }
    }
    AB_I("quit (%s)", quit_why);
done:
    timer_stop();
    snd_close();
    disp_close();
    rd_close();
    free(pic_rgb);
    for (i = 0; i < NFACES; i++) free(face_tex[i]);
    timer_close();
    if (bridge) ab_cleanup();
    return rc;
}
