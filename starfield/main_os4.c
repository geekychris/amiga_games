// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Starfield for AmigaOS 4: the demo of starfield.asm + startup.c, in C,
 * on the shared layer in ../os4_compat (built with make ARCH=ppc).
 *
 * The 68k version takes over the machine: its own copper list, the
 * blitter clearing the planes, the keyboard read from the CIA.  Here the
 * same thing goes through the system, as the layer provides it:
 *   - a 320 x 256, 3 plane screen (the layer's stand-in, shown in a
 *     window), double buffered; the stars and the starship are set
 *     straight into its planes, as the assembly does
 *   - the background gradient as a user copper list (the layer applies
 *     its colour moves line by line), the colours as on the 68k
 *   - the keys from the window (IDCMP), Esc or the left mouse button quit
 *   - starfield.mod through the ptplayer interface (the layer's player)
 * The star motion, the steering and the starship are the assembly's,
 * step for step, with the same random number generator.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/copper.h>
#include <hardware/custom.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <string.h>
#include "bridge_client.h"
#include "ptplayer.h"

#define SCR_W        320
#define SCR_H        256
#define BPLROW       (SCR_W / 8)
#define NUM_BPL      3
#define MAX_STARS    200
#define ENT_W        96
#define ENT_H        48
#define ENT_WORDS    6
#define ENT_INTERVAL 500
#define ENT_SPEED    2
#define STEER_SPEED  0x10000L

#define KEY_ESC   0x45
#define KEY_W     0x11
#define KEY_A     0x20
#define KEY_S     0x21
#define KEY_D     0x22
#define KEY_UP    0x4C
#define KEY_DOWN  0x4D
#define KEY_LEFT  0x4F
#define KEY_RIGHT 0x4E

extern struct Custom custom;

struct star { LONG x, y; WORD speed, color; };

static struct star stars[MAX_STARS];
static ULONG random_seed;
static LONG vp_x, vp_y;
static LONG ent_active, ent_timer;
static WORD ent_x, ent_y, ent_dx;
static BYTE key_up, key_down, key_left, key_right, key_esc;
static long frame_count, star_count;

/* the starship silhouette, 96 x 48, one bit a pixel (from starfield.asm) */
static const UWORD starship_gfx[ENT_H * ENT_WORDS] = {
    0x0000,0x0780,0x0000,0x0000,0x0000,0x0000, 0x0000,0x1FE0,0x0000,0x0000,0x0000,0x0000,
    0x0000,0x3FFF,0x0000,0x0000,0x0000,0x0000, 0x0000,0x3FFF,0x0000,0xE000,0x0030,0x0000,
    0x0000,0x7FFF,0x0001,0xF000,0x00FC,0x0000, 0x0000,0x7FFF,0x0007,0xF800,0x07FE,0x0000,
    0x0000,0x7FFF,0x003F,0xFC00,0x07FF,0x0000, 0x0000,0x3FFF,0x007F,0xFF00,0x07FF,0x0000,
    0x0000,0x3FFC,0x00FF,0xFF80,0x0FFF,0x0000, 0x0000,0x1FF0,0x3FFF,0xFFC0,0x0FFF,0x0000,
    0x0200,0x07FF,0xFFFF,0xFFE0,0x07FF,0x0000, 0x3FFF,0xFFFF,0xFFFF,0xFFFF,0x03FE,0x0000,
    0x1FFF,0xFFFF,0xFFFF,0xFFFF,0xE1FE,0x0000, 0x1FFF,0xFFFF,0xFFFF,0xFFFF,0xFFF8,0x0000,
    0x0FFF,0xFFFF,0xFFFF,0xFFFF,0xFF80,0x0000, 0x07FF,0xFFFF,0xFFFF,0xFFFF,0xFFFE,0x0000,
    0x01FF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFE00, 0x0007,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFC,
    0x0000,0x07FF,0xFFFF,0xFFFF,0xFFFF,0xFFFE, 0x0000,0x0003,0xFFFF,0xFFFF,0xFFFF,0xFFFE,
    0x0000,0x0000,0x7FFF,0xFFFF,0xFFFF,0xFFFC, 0x0000,0x0000,0x3FFF,0xFFFF,0xFFFF,0xFFF8,
    0x0000,0x0000,0x1FFF,0xFFFE,0x0007,0xFF80, 0x0000,0x0000,0x1EFF,0xFFFC,0x0000,0x0000,
    0x0000,0x0000,0x0E3F,0xFFB8,0x0000,0x0000, 0x0000,0x0000,0x071F,0xFC70,0x0000,0x0000,
    0x0000,0x0000,0x0387,0xE0E0,0x0000,0x0000, 0x0000,0x0000,0x01C7,0xC1C0,0x0000,0x0000,
    0x0000,0x0000,0x01E7,0xC780,0x0000,0x0000, 0x0000,0x0000,0x00FF,0xEF00,0x0000,0x0000,
    0x0000,0x0000,0x007F,0xFE00,0x0000,0x0000, 0x0000,0x0000,0x00FF,0xFC00,0x0000,0x0000,
    0x0000,0x0000,0x01FF,0xFE00,0x0000,0x0000, 0x0000,0x0000,0x01FE,0xFE00,0x0000,0x0000,
    0x0000,0x0000,0x03FF,0xDF00,0x0000,0x0000, 0x0000,0x0000,0x03FF,0xFF00,0x0000,0x0000,
    0x0000,0x0000,0x03FF,0xFF00,0x0000,0x0000, 0x0000,0x0000,0x03FF,0xFF80,0x0000,0x0000,
    0x0000,0x0000,0x07FF,0xFF80,0x0000,0x0000, 0x0000,0x0000,0x07FF,0xFF80,0x0000,0x0000,
    0x0000,0x0000,0x03FF,0xFF00,0x0000,0x0000, 0x0000,0x0000,0x03FF,0xFF00,0x0000,0x0000,
    0x0000,0x0000,0x03FF,0xFF00,0x0000,0x0000, 0x0000,0x0000,0x01FF,0xFE00,0x0000,0x0000,
    0x0000,0x0000,0x00FF,0xFC00,0x0000,0x0000, 0x0000,0x0000,0x007F,0xF800,0x0000,0x0000,
};

/* the colours of the 68k copper list: dim to white for the stars */
static const UWORD colors[8] = { 0x002, 0x333, 0x666, 0x999, 0xBBB, 0xADF, 0xDEF, 0xFFF };

/* xorshift, as the assembly's `random` */
static ULONG random_next(void)
{
    ULONG x = random_seed;
    x ^= x << 7;
    x ^= x >> 5;
    x ^= x << 3;
    random_seed = x;
    return x;
}

static WORD speed_color(WORD speed)
{
    WORD c = speed + 2;
    return c > 7 ? 7 : c;
}

static void init_stars(void)
{
    int i;
    for (i = 0; i < MAX_STARS; i++) {
        WORD x = (WORD)(random_next() & 0x1FF), y;
        if (x >= SCR_W) x -= 192;
        stars[i].x = (LONG)x << 16;
        y = (WORD)(random_next() & 0xFF);
        stars[i].y = (LONG)y << 16;
        stars[i].speed = (WORD)((random_next() & 3) + 1);
        stars[i].color = speed_color(stars[i].speed);
    }
    star_count = MAX_STARS;
}

static void handle_steering(void)
{
    if (key_left)  { vp_x -= STEER_SPEED; if (vp_x < (16L << 16)) vp_x = 16L << 16; }
    if (key_right) { vp_x += STEER_SPEED; if (vp_x > ((LONG)(SCR_W - 16) << 16)) vp_x = (LONG)(SCR_W - 16) << 16; }
    if (key_up)    { vp_y -= STEER_SPEED; if (vp_y < (16L << 16)) vp_y = 16L << 16; }
    if (key_down)  { vp_y += STEER_SPEED; if (vp_y > ((LONG)(SCR_H - 16) << 16)) vp_y = (LONG)(SCR_H - 16) << 16; }
}

/* stars move away from the vanishing point; off screen, they start again there */
static void update_stars(void)
{
    int i;
    for (i = 0; i < MAX_STARS; i++) {
        struct star *s = &stars[i];
        int shift = 9 - s->speed;
        WORD sx, sy;
        s->x += (s->x - vp_x) >> shift;
        s->y += (s->y - vp_y) >> shift;
        sx = (WORD)(s->x >> 16);
        sy = (WORD)(s->y >> 16);
        if (sx < 0 || sx >= SCR_W || sy < 0 || sy >= SCR_H) {
            s->x = vp_x + ((LONG)((WORD)(random_next() & 0x0F) - 8) << 16);
            s->y = vp_y + ((LONG)((WORD)(random_next() & 0x0F) - 8) << 16);
            s->speed = (WORD)((random_next() & 3) + 1);
            s->color = speed_color(s->speed);
        }
    }
}

static void set_pixel(UBYTE **pl, int x, int y, int color)
{
    int off = y * BPLROW + (x >> 3), p;
    UBYTE bit = (UBYTE)(0x80 >> (x & 7));
    for (p = 0; p < NUM_BPL; p++)
        if (color & (1 << p)) pl[p][off] |= bit;
}

static void plot_stars(UBYTE **pl)
{
    int i;
    for (i = 0; i < MAX_STARS; i++) {
        int x = (int)(stars[i].x >> 16), y = (int)(stars[i].y >> 16);
        if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H) set_pixel(pl, x, y, stars[i].color);
    }
}

static void update_starship(void)
{
    if (!ent_active) {
        if (--ent_timer > 0) return;
        ent_active = 1;
        ent_y = (WORD)((random_next() & 0x7F) + 40);
        ent_x = SCR_W + ENT_W;
        ent_dx = -ENT_SPEED;
        return;
    }
    ent_x += ent_dx;
    if (ent_x <= -ENT_W - 16) {
        ent_active = 0;
        ent_timer = ENT_INTERVAL;
    }
}

static void draw_starship(UBYTE **pl)
{
    int row, w, b;
    for (row = 0; row < ENT_H; row++) {
        int y = ent_y + row;
        if (y < 0 || y >= SCR_H) continue;
        for (w = 0; w < ENT_WORDS; w++) {
            UWORD bits = starship_gfx[row * ENT_WORDS + w];
            for (b = 15; bits && b >= 0; b--)
                if (bits & (1 << b)) {
                    int x = ent_x + w * 16 + (15 - b);
                    if (x >= 0 && x < SCR_W) set_pixel(pl, x, y, 7);
                }
        }
    }
}

/* the 68k copper list's gradient: colour 0 per 16 lines, $003 down to black */
static struct UCopList *make_gradient(void)
{
    struct UCopList *ucl = AllocVecTags(sizeof(struct UCopList), AVT_ClearWithValue, 0, TAG_DONE);
    UWORD c = 3;
    int i;
    if (!ucl) return 0;
    CINIT(ucl, 32);
    for (i = 0; i < 14; i++) {
        CWAIT(ucl, i * 16, 0);
        CMOVE(ucl, custom.color[0], c);
        if (i >= 8 && c > 0) c--;
    }
    CEND(ucl);
    return ucl;
}

static UBYTE *load_file(const char *name, ULONG *size)
{
    BPTR fh = Open((STRPTR)name, MODE_OLDFILE);
    LONG n;
    UBYTE *buf;
    if (!fh) return 0;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    buf = n > 0 ? AllocVecTags((ULONG)n, TAG_DONE) : 0;
    if (buf && Read(fh, buf, n) != n) { FreeVec(buf); buf = 0; }
    Close(fh);
    *size = (ULONG)n;
    return buf;
}

static void key(UWORD code)
{
    BYTE down = (code & 0x80) ? 0 : 1;
    switch (code & 0x7F) {
    case KEY_ESC: key_esc = down; break;
    case KEY_W: case KEY_UP: key_up = down; break;
    case KEY_S: case KEY_DOWN: key_down = down; break;
    case KEY_A: case KEY_LEFT: key_left = down; break;
    case KEY_D: case KEY_RIGHT: key_right = down; break;
    }
}

int main(void)
{
    struct Screen *scr;
    struct Window *win = 0;
    struct ScreenBuffer *sb[2] = { 0, 0 };
    struct UCopList *ucl = 0;
    UBYTE *mod = 0;
    ULONG mod_size = 0;
    int cur = 1, i, bridge_ok, quit = 0;

    bridge_ok = ab_init("starfield") == 0;
    AB_I("Starfield starting (AmigaOS 4)");
    ab_register_var("frames", AB_TYPE_I32, &frame_count);
    ab_register_var("stars", AB_TYPE_I32, &star_count);

    scr = OpenScreenTags(NULL, SA_Width, SCR_W, SA_Height, SCR_H, SA_Depth, NUM_BPL,
                         SA_DisplayID, LORES_KEY, SA_Title, (ULONG)"Starfield",
                         SA_ShowTitle, FALSE, SA_Quiet, TRUE, SA_Type, CUSTOMSCREEN, TAG_DONE);
    if (!scr) { AB_E("no screen"); ab_cleanup(); return 20; }
    for (i = 0; i < 8; i++)
        SetRGB4(&scr->ViewPort, i, (colors[i] >> 8) & 15, (colors[i] >> 4) & 15, colors[i] & 15);
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Width, SCR_W, WA_Height, SCR_H,
                         WA_Borderless, TRUE, WA_Backdrop, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
                         WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS, TAG_DONE);
    sb[0] = AllocScreenBuffer(scr, NULL, SB_SCREEN_BITMAP);
    sb[1] = AllocScreenBuffer(scr, NULL, 0);
    ucl = make_gradient();
    if (!win || !sb[0] || !sb[1]) { AB_E("no window or buffers"); quit = 1; }
    if (ucl) scr->ViewPort.UCopIns = ucl;

    mod = load_file("PROGDIR:starfield.mod", &mod_size);
    if (mod) {
        AB_I("Loaded starfield.mod (%ld bytes)", (long)mod_size);
        mt_install_cia(0, 0, 1);
        mt_init(0, mod, 0, 0);
        mt_Enable = 1;
    } else AB_W("Failed to load starfield.mod");

    vp_x = (LONG)(SCR_W / 2) << 16;
    vp_y = (LONG)(SCR_H / 2) << 16;
    random_seed = (ULONG)custom.vhposr << 16 | custom.vhposr;
    random_seed |= 0xA3B5C7D9UL;
    init_stars();
    ent_active = 0;
    ent_timer = ENT_INTERVAL;

    while (!quit) {
        struct IntuiMessage *m;
        struct BitMap *bm = sb[cur]->sb_BitMap;
        UBYTE *pl[NUM_BPL];

        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) {
            if (m->Class == IDCMP_RAWKEY) key(m->Code);
            else if (m->Class == IDCMP_MOUSEBUTTONS && m->Code == SELECTDOWN) quit = 1;
            ReplyMsg((struct Message *)m);
        }
        if (key_esc) break;
        handle_steering();

        for (i = 0; i < NUM_BPL; i++) {                 /* the blitter's clear */
            pl[i] = (UBYTE *)bm->Planes[i];
            memset(pl[i], 0, (size_t)bm->BytesPerRow * bm->Rows);
        }
        update_stars();
        plot_stars(pl);
        update_starship();
        if (ent_active) draw_starship(pl);

        WaitTOF();
        ChangeScreenBuffer(scr, sb[cur]);
        cur ^= 1;
        frame_count++;
        if (bridge_ok) ab_poll();
    }

    if (mod) {
        mt_end(0);
        mt_remove_cia(0);
        FreeVec(mod);
    }
    AB_I("Starfield done, %ld frames", frame_count);
    if (ucl) {
        scr->ViewPort.UCopIns = 0;
        FreeCopList(ucl->FirstCopList);
        FreeVec(ucl);
    }
    if (win) CloseWindow(win);
    if (sb[1]) FreeScreenBuffer(scr, sb[1]);
    if (sb[0]) FreeScreenBuffer(scr, sb[0]);
    CloseScreen(scr);
    ab_cleanup();
    return 0;
}
