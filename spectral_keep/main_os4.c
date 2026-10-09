/*
 * SPECTRAL KEEP - AmigaOS 4 version of geekychris/spectral-keep, by way of
 * the 3DO version (3do-dev/projects/spectral_keep).
 *
 * An isometric flip-screen adventure in the style of Knight Lore: three
 * keeps of rooms, relics to carry to the throne, keys for the gates,
 * guards, hounds, ghosts and bouncers.
 *
 * The rules (game.c), rooms and actors (room.c), physics (world.c), level
 * data and the software voxel renderer that draws every model into sprites
 * (vox.c) are the 3DO code unchanged. scene.c composes the room from those
 * sprites in software now, where the 3DO used cels. This file is the
 * window, the keyboard as the pad, the 50 Hz loop and the HUD, in the
 * game's own 8x8 font (font.c).
 *
 * Keys: arrows / WASD / keypad walk (screen-relative; C on the title
 * switches to grid directions), Space or Return jumps (A), P pauses,
 * Esc quits. Title: up/down choose a keep, Space starts, M music on/off,
 * Q+E a tour of every room of every keep.
 *
 * Usage: spectral_keep [SCALE=n]     (window n x 320x240, default 2)
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "bridge_client.h"
#include "keep.h"

/* room for vox.c's software rasteriser and scene.c's depth-first walk */
static const char __attribute__((used)) stack_cookie[] = "$STACK:524288";

void *scene_frame(int show_player, int panel, int flash_ink);
void  scene_init(void);
void  scene_room(void);
extern int scene_ncels;
extern unsigned long *scene_fb;
extern const unsigned char keep_font[96][8];
void  snd_update(void);             /* sound_ahi.c */
void  snd_exit(void);
int   snd_available(void);

/* the 3DO pad bits game.c reads */
#define B_UP    0x0001
#define B_DOWN  0x0002
#define B_LEFT  0x0004
#define B_RIGHT 0x0008
#define B_A     0x0010
#define B_B     0x0020
#define B_C     0x0040
#define B_P     0x0080
#define B_L     0x0200
#define B_R     0x0400

#define SW 320
#define SH 240
#define PEN_BLACK  8                /* pens 1..7: the Spectrum inks */
#define PEN_SHADOW 9
static const ULONG pen_rgb[10] = { 0x000000, 0x2040FF, 0xFF3030, 0xFF40FF, 0x30E030, 0x30F0F0, 0xFFF040, 0xFFFFFF,
                                   0x080808, 0x101018 };

static unsigned long frame[SW * SH];

void keep_log(const char *fmt, ...)
{
    char buf[200];
    int n;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    n = (int)strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = 0;
    AB_I("%s", buf);
}

/* data/<name> next to the program, in one AllocVec block */
void *keep_load(const char *name, long *size)
{
    char path[96];
    BPTR f;
    int64 len;
    void *p = 0;
    *size = 0;
    snprintf(path, sizeof(path), "PROGDIR:data/%s", name);
    f = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!f) return 0;
    len = GetFileSize(f);
    if (len > 0 && len < 0x1000000 && (p = AllocVecTags((uint32)len, AVT_Type, MEMF_SHARED, TAG_DONE)) != 0) {
        if (Read(f, p, (int32)len) != (int32)len) { FreeVec(p); p = 0; }
        else *size = (long)len;
    }
    Close(f);
    return p;
}

void keep_free(void *p) { FreeVec(p); }

/* ---- the HUD, drawn straight into the frame ---- */

static void fill(int x0, int y0, int x1, int y1, int pen)
{
    unsigned long v = 0xFF000000UL | pen_rgb[pen % 10];
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SW - 1) x1 = SW - 1;
    if (y1 > SH - 1) y1 = SH - 1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) frame[y * SW + x] = v;
}

static void glyphs(int x, int y, const char *s, int pen, int bg, int scale)
{
    unsigned long v = 0xFF000000UL | pen_rgb[pen % 10], b = bg >= 0 ? 0xFF000000UL | pen_rgb[bg % 10] : 0;
    for (; *s; s++, x += 8 * scale) {
        int c = (unsigned char)*s, r, k, i, j;
        const unsigned char *g = keep_font[(c >= 32 && c < 128) ? c - 32 : 0];
        for (r = 0; r < 8; r++)
            for (k = 0; k < 8; k++) {
                int on = (g[r] >> (7 - k)) & 1;
                if (!on && bg < 0) continue;
                for (j = 0; j < scale; j++)
                    for (i = 0; i < scale; i++) {
                        int X = x + k * scale + i, Y = y + r * scale + j;
                        if (X >= 0 && X < SW && Y >= 0 && Y < SH) frame[Y * SW + X] = on ? v : b;
                    }
            }
    }
}

static void text_bg(int x, int y, const char *s, int pen, int bg) { glyphs(x, y, s, pen, bg, 1); }
static void text(int x, int y, const char *s, int pen) { text_bg(x, y, s, pen, -1); }
static void ctext(int y, const char *s, int pen) { text(160 - 4 * (int)strlen(s), y, s, pen); }
static void big(int y, const char *s, int pen)       /* double size, with a drop shadow */
{
    int x = 160 - 8 * (int)strlen(s);
    glyphs(x + 2, y + 2, s, PEN_SHADOW, -1, 2);
    glyphs(x, y, s, pen, -1, 2);
}
static void bar(int x0, int y0, int x1, int y1, int pen) { fill(x0, y0, x1, y1, pen); }

/* ---- screens (Hud.cs) ---- */

static int blink(int per_s) { return ((G.clock * per_s * 2) / 50) % 2 == 0; }

/* word wrap like Hud.Wrap; returns the number of lines */
static int wrap(const char *t, int width, char lines[][44], int max)
{
    int n = 0, len = 0;
    lines[0][0] = 0;
    while (*t && n < max) {
        const char *w = t;
        int wl = 0;
        while (w[wl] && w[wl] != ' ') wl++;
        if (len > 0 && len + 1 + wl > width) {
            if (++n >= max) break;
            lines[n][0] = 0;
            len = 0;
        }
        if (len > 0) lines[n][len++] = ' ';
        if (wl > width) wl = width;
        memcpy(lines[n] + len, w, wl);
        len += wl;
        lines[n][len] = 0;
        t = w + wl;
        while (*t == ' ') t++;
    }
    return len > 0 ? n + 1 : n;
}

static void hud_panel(void)
{
    char buf[44], lines[2][44];
    int n, i;
    if (!R.room) return;
    ctext(202, R.room->name, R.room->ink ? R.room->ink : INK_WHITE);
    sprintf(buf, "{%ld", (long)G.lives);
    text(10, 212, buf, INK_RED);
    sprintf(buf, "|%ld/%ld", (long)G.relics, (long)G.level->relics);
    text(60, 212, buf, INK_YELLOW);
    sprintf(buf, "}%ld", (long)G.keys);
    text(130, 212, buf, INK_CYAN);
    sprintf(buf, "ROOM %ld/%ld", (long)G.nvisited, (long)G.level->nrooms);
    text(180, 212, buf, INK_WHITE);
    if (game_message_visible()) {
        n = wrap(G.message, 38, lines, 2);
        for (i = 0; i < n; i++) ctext(222 + 9 * i, lines[i], G.msg_ink);
    } else
        ctext(231, G.level->name, INK_WHITE);
}

static void hud_title(void)
{
    char buf[44];
    int i;
    bar(0, 157, SW - 1, SH - 1, PEN_BLACK);
    big(10, "SPECTRAL KEEP", INK_YELLOW);
    ctext(34, "AN ISOMETRIC ADVENTURE", INK_CYAN);
    for (i = 0; i < keep_nlevels && i < 4; i++) {
        int sel = i == G.selected, y = 165 + 10 * i;
        sprintf(buf, "%ld %s", (long)i + 1, keep_levels[i].name);
        text(56, y, sel ? ">" : " ", INK_WHITE);
        text_bg(72, y, buf, sel ? PEN_BLACK : INK_WHITE, sel ? ((G.clock / 33) % 2 ? INK_CYAN : INK_YELLOW) : PEN_BLACK);
    }
    ctext(200, "SPACE PLAY   UP/DOWN CHOOSE A KEEP", INK_WHITE);
    sprintf(buf, "C CONTROLS:%s   M MUSIC:%s", G.grid_controls ? "GRID" : "SCREEN", G.music_on ? "ON" : "OFF");
    ctext(212, buf, INK_GREEN);
    ctext(228, "(C) 2026 HITORRO", INK_MAGENTA);
}

static void hud_play(void)
{
    hud_panel();
    if (G.state == GS_LEVELDONE && blink(3)) big(60, "WELL DONE!", INK_YELLOW);
    if (G.paused) {
        bar(124, 80, 195, 89, INK_RED);
        ctext(81, "PAUSED", INK_WHITE);
    }
}

static void hud_end_screen(void)
{
    char buf[44];
    bar(0, 0, SW - 1, SH - 1, PEN_BLACK);
    if (G.state == GS_GAMEOVER) {
        big(86, "GAME OVER", INK_RED);
        sprintf(buf, "RELICS FOUND: %ld", (long)G.relics);
        ctext(130, buf, INK_YELLOW);
        sprintf(buf, "ROOMS EXPLORED: %ld", (long)G.nvisited);
        ctext(140, buf, INK_CYAN);
    } else {
        big(56, "VICTORY!", INK_YELLOW);
        ctext(100, "EVERY KEEP HAS FALLEN", INK_CYAN);
        ctext(110, "AND THE CROWN IS YOURS.", INK_CYAN);
        ctext(140, "THANKS FOR PLAYING", INK_MAGENTA);
    }
    if (blink(2)) ctext(180, "PRESS SPACE", INK_WHITE);
}

/* ---- the keyboard as the pad ---- */

static ULONG held, pressed, inj_held, inj_pressed;
static int inj_frames, quit;
static const char *quit_why = "";

static ULONG key_bit(UWORD code)
{
    switch (code) {
    case 0x4C: case 0x11: case 0x3E: return B_UP;        /* cursor, W, keypad 8 */
    case 0x4D: case 0x21: case 0x1E: return B_DOWN;      /* cursor, S, keypad 2 */
    case 0x4F: case 0x20: case 0x2D: return B_LEFT;      /* cursor, A, keypad 4 */
    case 0x4E: case 0x22: case 0x2F: return B_RIGHT;     /* cursor, D, keypad 6 */
    case 0x40: case 0x44: case 0x43: return B_A;         /* space, return, enter: jump / start */
    case 0x37: return B_B;                               /* M: music (B) */
    case 0x33: return B_C;                               /* C */
    case 0x19: return B_P;                               /* P */
    case 0x10: return B_L;                               /* Q */
    case 0x12: return B_R;                               /* E */
    }
    return 0;
}

/* bridge hook: "press UP" holds a pad button for a few frames; "press L+R" both */
static int hk_press(const char *args, char *res, int len)
{
    static const struct { const char *n; ULONG b; } names[] = {
        { "UP", B_UP }, { "DOWN", B_DOWN }, { "LEFT", B_LEFT }, { "RIGHT", B_RIGHT }, { "A", B_A },
        { "B", B_B }, { "C", B_C }, { "P", B_P }, { "L", B_L }, { "R", B_R }, { "L+R", B_L | B_R }
    };
    unsigned i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (args && strcmp(args, names[i].n) == 0) {
            inj_held = names[i].b;
            inj_pressed |= names[i].b;
            inj_frames = 8;
            snprintf(res, len, "pressed %s", names[i].n);
            return 0;
        }
    snprintf(res, len, "unknown button");
    return -1;
}
/* bridge hook: "hold RIGHT 50" holds a button for that many frames (walking) */
static int hk_hold(const char *args, char *res, int len)
{
    char name[16];
    int n = 0;
    if (!args || sscanf(args, "%15s %d", name, &n) != 2 || n <= 0) { snprintf(res, len, "usage: BUTTON FRAMES"); return -1; }
    if (hk_press(name, res, len) != 0) return -1;
    inj_frames = n;
    return 0;
}
static int hk_quit(const char *a, char *r, int n) { (void)a; snprintf(r, n, "quitting"); quit = 1; quit_why = "bridge"; return 0; }

/* ---- timing ---- */

static struct MsgPort *timer_port;
static struct TimeRequest *timer_req;
struct Device *TimerBase;
struct TimerIFace *ITimer;

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

static void timer_close(void)
{
    if (ITimer) DropInterface((struct Interface *)ITimer);
    if (TimerBase) CloseDevice((struct IORequest *)timer_req);
    if (timer_req) FreeSysObject(ASOT_IOREQUEST, timer_req);
    if (timer_port) FreeSysObject(ASOT_PORT, timer_port);
}

static unsigned long long now_us(void)
{
    struct TimeVal tv;
    GetUpTime(&tv);
    return (unsigned long long)tv.Seconds * 1000000ULL + tv.Microseconds;
}

/* ---- the frame to the window, scaled up O times ---- */

static unsigned long *out;
static int O, outw, outh;

static void present(struct Window *win)
{
    int y, x, k;
    if (O == 1) {
        WritePixelArray((uint8 *)frame, 0, 0, SW * 4, PIXF_A8R8G8B8, win->RPort, 0, 0, SW, SH);
        return;
    }
    for (y = 0; y < SH; y++) {
        const unsigned long *src = frame + y * SW;
        unsigned long *dst = out + (size_t)y * O * outw;
        if (O == 2)
            for (x = 0; x < SW; x++) { dst[0] = dst[1] = src[x]; dst += 2; }
        else
            for (x = 0; x < SW; x++)
                for (k = 0; k < O; k++) *dst++ = src[x];
        for (k = 1; k < O; k++)
            memcpy(out + ((size_t)y * O + k) * outw, out + (size_t)y * O * outw, (size_t)outw * 4);
    }
    WritePixelArray((uint8 *)out, 0, 0, outw * 4, PIXF_A8R8G8B8, win->RPort, 0, 0, outw, outh);
}

/* ---- main ---- */

int main(int argc, char **argv)
{
    struct Window *win = 0;
    int scale = 2, bridge, rc = 0, i;
    long frames = 0, fps_frames = 0, fps10 = 0, steps_done = 0, cels = 0;
    long cur_state = 0, cur_lives = 0, cur_relics = 0, cur_keys = 0, px = 0, py = 0, pz = 0;
    char room_id[16] = "-";
    unsigned long long t_last, t_fps, acc = 0, prof[3] = { 0, 0, 0 };

    for (i = 1; i < argc; i++) {
        if (strncasecmp(argv[i], "SCALE=", 6) == 0) scale = atoi(argv[i] + 6);
        else if (strcmp(argv[i], "?") == 0) { printf("Usage: spectral_keep [SCALE=n]\n"); return 0; }
    }
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    O = scale;
    outw = SW * O;
    outh = SH * O;
    bridge = ab_init("KEEP") == 0;
    scene_fb = frame;
    if (O > 1 && !(out = (unsigned long *)malloc((size_t)outw * outh * 4))) { rc = 20; goto done; }
    if (!timer_open()) { printf("spectral_keep: no timer.device\n"); rc = 20; goto done; }

    win = OpenWindowTags(NULL,
                         WA_Title, (ULONG)"Spectral Keep",
                         WA_InnerWidth, outw, WA_InnerHeight, outh,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                         WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_GimmeZeroZero, TRUE,
                         WA_IDCMP, IDCMP_RAWKEY | IDCMP_CLOSEWINDOW | IDCMP_INACTIVEWINDOW,
                         TAG_DONE);
    if (!win) { printf("spectral_keep: can't open a %ldx%ld window\n", (long)outw, (long)outh); rc = 20; goto done; }

    /* something to look at while the sprites are drawn */
    fill(0, 0, SW - 1, SH - 1, PEN_BLACK);
    big(100, "SPECTRAL KEEP", INK_YELLOW);
    glyphs(160 - 4 * 10, 140, "LOADING...", INK_WHITE, -1, 1);
    present(win);

    scene_init();
    if (!vox_init()) { AB_E("no memory for sprites"); rc = 20; goto done; }
    if (!snd_init()) AB_W("no sound (ahi.device or data/sfx.raw unavailable)");
    game_init();

    if (bridge) {
        ab_register_var("state", AB_TYPE_I32, &cur_state);
        ab_register_var("lives", AB_TYPE_I32, &cur_lives);
        ab_register_var("relics", AB_TYPE_I32, &cur_relics);
        ab_register_var("keys", AB_TYPE_I32, &cur_keys);
        ab_register_var("room", AB_TYPE_STR, room_id);
        ab_register_var("px", AB_TYPE_I32, &px);
        ab_register_var("py", AB_TYPE_I32, &py);
        ab_register_var("pz", AB_TYPE_I32, &pz);
        ab_register_var("fps10", AB_TYPE_I32, &fps10);
        ab_register_var("cels", AB_TYPE_I32, &cels);
        ab_register_hook("press", "tap a pad button: UP DOWN LEFT RIGHT A B C P L R L+R", hk_press);
        ab_register_hook("hold", "hold a pad button for N frames: e.g. RIGHT 50", hk_hold);
        ab_register_hook("quit", "quit the game", hk_quit);
    }
    AB_I("ready %ldx%ld sound=%ld keeps=%ld", (long)outw, (long)outh, (long)snd_available(), (long)keep_nlevels);

    t_last = t_fps = now_us();
    while (!quit) {
        struct IntuiMessage *m;
        unsigned long long t, t0, t1, t2, t3;
        ULONG h, p;
        int steps;

        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) {
            ULONG cls = m->Class;
            UWORD code = m->Code, qual = m->Qualifier;
            ReplyMsg((struct Message *)m);
            if (cls == IDCMP_CLOSEWINDOW) { quit = 1; quit_why = "close gadget"; }
            else if (cls == IDCMP_INACTIVEWINDOW) held = 0;
            else if (cls == IDCMP_RAWKEY) {
                if (code == 0x45) { quit = 1; quit_why = "Esc"; }
                else if (code & IECODE_UP_PREFIX) held &= ~key_bit(code & 0x7F);
                else if (!(qual & IEQUALIFIER_REPEAT)) {
                    held |= key_bit(code);
                    pressed |= key_bit(code);
                }
            }
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) { quit = 1; quit_why = "Ctrl-C"; }
        if (bridge) ab_poll();
        if (quit) break;

        /* 50 logic steps a second whatever the frame rate */
        t = now_us();
        acc += t - t_last;
        t_last = t;
        steps = (int)(acc / 20000);
        if (steps == 0) { WaitTOF(); continue; }
        acc -= (unsigned long long)steps * 20000;
        if (steps > 8) { steps = 8; acc = 0; }

        h = held | pressed | inj_held;
        p = pressed | inj_pressed;
        pressed = inj_pressed = 0;
        if (inj_frames > 0 && --inj_frames == 0) inj_held = 0;
        t0 = now_us();
        while (steps-- > 0) {
            game_step(h, p);
            p = 0;                                      /* an edge counts once */
            steps_done++;
            if (G.room_changed) {
                scene_room();
                G.room_changed = 0;
            }
        }
        t1 = now_us();
        if (G.state != GS_GAMEOVER && G.state != GS_VICTORY)
            scene_frame(G.state != GS_DYING, G.state != GS_TITLE || G.tour,
                        G.clock < G.flash_until ? G.flash_ink : -1);
        if (G.state == GS_TITLE && G.tour) {
            ctext(202, R.room->name, R.room->ink ? R.room->ink : INK_WHITE);
            ctext(216, G.level->name, INK_WHITE);
            ctext(230, "Q+E OR SPACE: BACK TO THE TITLE", INK_GREEN);
        } else if (G.state == GS_TITLE) hud_title();
        else if (G.state == GS_GAMEOVER || G.state == GS_VICTORY) hud_end_screen();
        else hud_play();
        t2 = now_us();
        present(win);
        t3 = now_us();
        snd_update();
        prof[0] += t1 - t0; prof[1] += t2 - t1; prof[2] += t3 - t2;

        cur_state = G.state;
        cur_lives = G.lives;
        cur_relics = G.relics;
        cur_keys = G.keys;
        cels = scene_ncels;
        if (R.room) { strncpy(room_id, R.room->id, sizeof(room_id) - 1); room_id[sizeof(room_id) - 1] = 0; }
        if (R.player >= 0) {
            const Body *b = R.act[R.player].body;
            px = b->px * 100 >> 12; py = b->py * 100 >> 12; pz = b->pz * 100 >> 12;
        }
        frames++;
        fps_frames++;
        if (t - t_fps >= 5000000ULL) {
            fps10 = (long)(fps_frames * 10000000ULL / (t - t_fps));
            AB_I("fps=%ld.%ld cels=%ld state=%ld room=%s steps=%ld ms/frame logic=%ld draw=%ld blit=%ld",
                 fps10 / 10, fps10 % 10, cels, cur_state, room_id, steps_done,
                 (long)(prof[0] / 1000 / fps_frames), (long)(prof[1] / 1000 / fps_frames),
                 (long)(prof[2] / 1000 / fps_frames));
            memset(prof, 0, sizeof(prof));
            fps_frames = 0;
            t_fps = t;
        }
    }
    AB_I("quit (%s) after %ld frames", quit_why, frames);
done:
    snd_exit();
    if (win) CloseWindow(win);
    timer_close();
    free(out);
    if (bridge) ab_cleanup();
    return rc;
}
