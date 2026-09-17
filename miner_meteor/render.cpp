// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "render.h"
#include "game.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <graphics/text.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

/*
 * Renderer — RectFill-per-tile double-buffered draw over a 320x256
 * 8bpp screen. Same primitive path works on classic bitplanes and OS4
 * RTG. The player + guardian sprites are drawn as coloured rectangles
 * with a small facing-strip pixel indicator; sprite art is a follow-up.
 *
 * Palette layout (16 entries — first 16 of the 256-slot AGA screen):
 *   0  black background
 *   1  white
 *   2  brick red   (walls)
 *   3  brown       (walls variant)
 *   4  cyan        (guardian A)
 *   5  magenta     (guardian B)
 *   6  yellow      (keys / conveyors / guardian C)
 *   7  green       (exit)
 *   8  dark grey   (hazard)
 *   9  bright red  (player)
 *   10 orange      (player highlight)
 *   11 blue        (title decoration)
 *   12..15         reserved
 */

#ifndef __PPC__
extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase       *GfxBase;
#endif

static struct Screen         *scr;
static struct DrawInfo       *drawinfo;
static struct RastPort       *rp;

/* 16-entry palette in (r,g,b) 8-bit-per-channel; scaled to 32-bit on
 * classic via LoadRGB32 and passed directly to OS4's SetRGB32. */
static const UBYTE PALETTE[16][3] = {
    /* 0  bg          */ {   0,   0,   0 },
    /* 1  white       */ { 240, 240, 240 },
    /* 2  brick       */ { 168,  48,  48 },
    /* 3  brown       */ { 128,  88,  40 },
    /* 4  cyan        */ {  64, 208, 224 },
    /* 5  magenta     */ { 224,  80, 192 },
    /* 6  yellow      */ { 240, 224,  64 },
    /* 7  green       */ {  64, 200,  64 },
    /* 8  dark grey   */ {  72,  72,  80 },
    /* 9  bright red  */ { 240,  64,  64 },
    /* 10 orange      */ { 240, 160,  64 },
    /* 11 blue        */ {  80, 128, 240 },
    /* 12 grey        */ { 128, 128, 128 },
    /* 13 light green */ { 168, 224, 128 },
    /* 14 pink        */ { 240, 168, 200 },
    /* 15 dim         */ {  40,  40,  40 },
};

static void install_palette(void)
{
    /* LoadRGB32 expects a (count << 16) | first-index header, then
     * 3 * count 32-bit RGB values, then a terminator zero. */
    ULONG buf[2 + 16 * 3];
    buf[0] = (16UL << 16) | 0;
    /* LoadRGB32 wants 32-bit RGB per channel; replicate the 8-bit
     * value across all four bytes so AGA reads any byte and gets the
     * intended intensity. */
    for (LONG i = 0; i < 16; i++) {
        buf[1 + i * 3 + 0] = ((ULONG)PALETTE[i][0]) * 0x01010101ul;
        buf[1 + i * 3 + 1] = ((ULONG)PALETTE[i][1]) * 0x01010101ul;
        buf[1 + i * 3 + 2] = ((ULONG)PALETTE[i][2]) * 0x01010101ul;
    }
    buf[1 + 16 * 3] = 0;
    LoadRGB32(&scr->ViewPort, (ULONG *)buf);
}

LONG render_open(void)
{
    scr = OpenScreenTags(NULL,
        SA_Width,       SCREEN_W,
        SA_Height,      SCREEN_H,
        SA_Depth,       8,
        SA_Type,        CUSTOMSCREEN,
        SA_DisplayID,   0x00000L,     /* PAL:LORES on classic; RTG uses default on OS4 */
        SA_Title,       (ULONG)"miner-meteor",
        SA_ShowTitle,   FALSE,
        SA_Quiet,       TRUE,
        TAG_END);
    if (!scr) return 1;
    rp = &scr->RastPort;
    install_palette();

    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
    return 0;
}

void render_close(void)
{
    if (scr) { CloseScreen(scr); scr = NULL; }
}

static inline void fill_tile(LONG col, LONG row, UBYTE pen)
{
    LONG x = col * TILE_W;
    LONG y = row * TILE_H;
    SetAPen(rp, pen);
    RectFill(rp, x, y, x + TILE_W - 1, y + TILE_H - 1);
}

static inline void fill_rect(LONG x, LONG y, LONG w, LONG h, UBYTE pen)
{
    SetAPen(rp, pen);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
}

void render_text(LONG x, LONG y, UBYTE pen, const char *text)
{
    SetAPen(rp, pen);
    Move(rp, x, y);
    Text(rp, (STRPTR)text, (LONG)strlen(text));
}

static void draw_tile(LONG col, LONG row, const TileState &t, ULONG tick,
                      LONG keys_remaining)
{
    switch (t.kind) {
        case T_EMPTY:
            fill_tile(col, row, 0);
            break;
        case T_SOLID:
            /* Alternating brick pattern — brick / brown chequer per column. */
            fill_tile(col, row, ((col + row) & 1) ? 2 : 3);
            break;
        case T_KEY: {
            fill_tile(col, row, 0);
            LONG x = col * TILE_W, y = row * TILE_H;
            fill_rect(x + 5,  y + 2, 6, 6, 6);        /* head */
            fill_rect(x + 7,  y + 8, 2, 6, 6);        /* shaft */
            fill_rect(x + 9,  y + 10, 3, 2, 6);       /* tooth */
            break;
        }
        case T_EXIT: {
            fill_tile(col, row, 0);
            LONG x = col * TILE_W, y = row * TILE_H;
            /* Bright green + blinking outline when all keys collected;
             * dim + dark when still locked so the player has a visual
             * cue for objective progress. */
            UBYTE pen = (keys_remaining == 0)
                ? (UBYTE)(((tick / 6) & 1) ? 7 : 13)
                : 15;
            fill_rect(x + 2, y + 2, 12, 12, pen);
            fill_rect(x + 5, y + 6,  6,  8, 0);
            break;
        }
        case T_COLLAPSE: {
            /* Brown crumbling look; darker as timer counts down. */
            UBYTE pen = (t.timer == 0 || t.timer > COLLAPSE_FRAMES / 2) ? 3 : 15;
            fill_tile(col, row, pen);
            LONG x = col * TILE_W, y = row * TILE_H;
            /* Faint cracks. */
            SetAPen(rp, 15);
            Move(rp, x + 2,  y + 4); Draw(rp, x + 13, y + 6);
            Move(rp, x + 3,  y + 11); Draw(rp, x + 12, y + 13);
            break;
        }
        case T_CONV_L:
        case T_CONV_R: {
            LONG x = col * TILE_W, y = row * TILE_H;
            /* Yellow band with dark cleats that shift each frame in
             * the push direction — the classic conveyor "movement". */
            fill_rect(x, y + 4, TILE_W, 8, 6);
            SetAPen(rp, 8);
            LONG shift = (t.kind == T_CONV_R)
                ? (LONG)((tick / 2) % 4)
                : (LONG)(3 - ((tick / 2) % 4));
            for (LONG i = 0; i < 4; i++) {
                LONG cx = x + shift + i * 4;
                RectFill(rp, cx, y + 6, cx + 1, y + 9);
            }
            break;
        }
        case T_HAZARD: {
            LONG x = col * TILE_W, y = row * TILE_H;
            fill_tile(col, row, 0);
            SetAPen(rp, 8);
            /* Zig-zag spikes. */
            for (LONG i = 0; i < 4; i++) {
                LONG sx = x + i * 4;
                RectFill(rp, sx, y + 12, sx + 3, y + 15);
                RectFill(rp, sx + 1, y + 8,  sx + 2, y + 11);
                RectFill(rp, sx + 1, y + 5,  sx + 2, y + 7);
            }
            break;
        }
        default:
            fill_tile(col, row, 0);
            break;
    }
}

static void draw_player(const Player &p)
{
    /* Body: bright-red 12x14 with a smaller orange "helmet" strip.
     * Facing direction shown by a small dark eye offset either way. */
    fill_rect(p.x + 2, p.y + 2, 12, 12, 9);       /* body */
    fill_rect(p.x + 2, p.y + 2, 12, 4,  10);      /* helmet stripe */
    LONG eye_x = (p.facing) ? p.x + 10 : p.x + 4;
    fill_rect(eye_x, p.y + 4, 2, 2, 0);           /* eye */
    /* Feet indicator flickers per animation frame. */
    if (p.frame & 1) {
        fill_rect(p.x + 3, p.y + 14, 3, 2, 8);
    } else {
        fill_rect(p.x + 10, p.y + 14, 3, 2, 8);
    }
}

static void draw_guardian(const Guardian &G)
{
    if (!G.alive) return;
    /* 14x14 body of the given colour with a "face" strip. */
    fill_rect(G.x + 1, G.y + 1, 14, 14, G.colour ? G.colour : 4);
    fill_rect(G.x + 3, G.y + 4, 3, 2, 0);
    fill_rect(G.x + 10, G.y + 4, 3, 2, 0);
    fill_rect(G.x + 4, G.y + 10, 8, 1, 0);
    /* Twitchy antenna. */
    LONG a = (G.frame >> 2) & 1;
    fill_rect(G.x + 7, G.y - 1 + a, 2, 2, G.colour ? G.colour : 4);
}

static void draw_hud(const GameState &gs)
{
    /* HUD strip along the bottom 16 px. */
    fill_rect(0, PLAY_H, SCREEN_W, HUD_H, 15);

    char buf[64];

    /* Score + air on the left, lives + keys + level on the right. */
    sprintf(buf, "SCORE %06ld", (long)gs.score);
    render_text(4, PLAY_H + 12, 1, buf);

    /* Air bar. */
    LONG bar_w = 120;
    LONG bar_x = 100;
    fill_rect(bar_x, PLAY_H + 4, bar_w, 8, 8);
    LONG fill = (gs.air * bar_w) / AIR_MAX;
    if (fill < 0) fill = 0;
    if (fill > bar_w) fill = bar_w;
    UBYTE pen = (gs.air < AIR_MAX / 4) ? 9 : 7;
    fill_rect(bar_x, PLAY_H + 4, fill, 8, pen);

    sprintf(buf, "LV%ld K%ld/%ld L%ld",
        (long)(gs.level_index + 1),
        (long)(gs.keys_total - gs.keys_remaining), (long)gs.keys_total,
        (long)gs.lives);
    render_text(230, PLAY_H + 12, 1, buf);
}

void render_frame(const GameState &gs)
{
    /* Play area — tiles. */
    for (LONG row = 0; row < GRID_H; row++) {
        for (LONG col = 0; col < GRID_W; col++) {
            draw_tile(col, row, gs.grid[row][col], gs.tick, gs.keys_remaining);
        }
    }

    /* Guardians. */
    for (LONG i = 0; i < gs.num_guards; i++) {
        draw_guardian(gs.guards[i]);
    }

    /* Player last so it sits on top. */
    draw_player(gs.player);

    /* HUD strip. */
    draw_hud(gs);
}

void render_title(const GameState &gs)
{
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    /* Big title bar. */
    fill_rect(0, 40, SCREEN_W, 40, 11);
    render_text(60, 66, 1, "MINER  METEOR");

    render_text(48, 120, 6, "A CLEAN-ROOM PLATFORMER");
    render_text(56, 138, 1, "INSPIRED BY 8-BIT CLASSICS");

    render_text(48, 176, 7, "LEFT / RIGHT :  MOVE");
    render_text(48, 190, 7, "SPACE        :  JUMP");
    render_text(48, 204, 7, "ESC          :  QUIT");

    /* Blinking start prompt. */
    if ((gs.tick / 15) & 1) {
        render_text(80, 232, 9, "PRESS  SPACE  TO  BEGIN");
    }
}

void render_endscreen(const GameState &gs)
{
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    if (gs.mode == GM_WIN) {
        fill_rect(0, 80, SCREEN_W, 30, 7);
        render_text(96, 100, 0, "ALL CAVERNS CLEARED!");
    } else {
        fill_rect(0, 80, SCREEN_W, 30, 9);
        render_text(112, 100, 0, "GAME OVER");
    }

    char buf[64];
    sprintf(buf, "FINAL SCORE  %06ld", (long)gs.score);
    render_text(88, 156, 1, buf);

    if (gs.state_timer == 0 && ((gs.tick / 15) & 1)) {
        render_text(72, 208, 6, "PRESS SPACE TO RETURN");
    }
}
