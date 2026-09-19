// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "render.h"
#include "game.h"
#include "tilesheet_data.h"

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
 * emberkeep renderer — 320x256 8bpp AGA screen, ScreenBuffer double-
 * buffered, per-scanline RectFill runs against a chunky tile sheet.
 * Same architecture that worked reliably for miner_meteor.
 *
 * The signature effect: a rectangular "lantern cone" masks the field
 * outside a small radius around the player with pen 0 (black). Every
 * frame we redraw the whole level normally, then paint four black
 * rectangles around the lantern box. Fast, works on any display, no
 * blitting tricks needed. The visual is a hard-edge square light —
 * softer circle mask is a follow-up.
 *
 * Palette (16 slots at the top of the 256-entry AGA colormap):
 *   0  black
 *   1  white
 *   2  brick red      (walls)
 *   3  brown          (wall variant / trim)
 *   4  cyan           (enemy A)
 *   5  magenta        (enemy B)
 *   6  yellow         (rune / oil highlight)
 *   7  green          (portal)
 *   8  dark grey      (hazard)
 *   9  bright red     (player body)
 *   10 orange         (lantern flame)
 *   11 blue           (title chrome)
 *   12..15            greys / dim
 */

#ifndef __PPC__
extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase       *GfxBase;
#endif

static struct Screen         *scr;
static struct RastPort       *rp;
static struct ScreenBuffer   *sbuf[2];
static struct RastPort        rp_buf[2];
static UWORD                  cur_buf = 1;

static UBYTE                  scratch_tile[TILE_H][TILE_W];

static const UBYTE PALETTE[16][3] = {
    /* 0  bg           */ {   0,   0,   0 },
    /* 1  white        */ { 240, 240, 240 },
    /* 2  brick        */ { 168,  48,  48 },
    /* 3  brown        */ { 128,  88,  40 },
    /* 4  cyan         */ {  64, 208, 224 },
    /* 5  magenta      */ { 224,  80, 192 },
    /* 6  yellow       */ { 240, 224,  64 },
    /* 7  green        */ {  64, 200,  64 },
    /* 8  dark grey    */ {  72,  72,  80 },
    /* 9  bright red   */ { 240,  64,  64 },
    /* 10 orange       */ { 240, 160,  64 },
    /* 11 blue         */ {  80, 128, 240 },
    /* 12 grey         */ { 128, 128, 128 },
    /* 13 light green  */ { 168, 224, 128 },
    /* 14 pink         */ { 240, 168, 200 },
    /* 15 dim          */ {  40,  40,  40 },
};

static void install_palette(void)
{
    ULONG buf[2 + 16 * 3];
    buf[0] = (16UL << 16) | 0;
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
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     8,
        SA_Type,      CUSTOMSCREEN,
        SA_DisplayID, 0x00000L,
        SA_Title,     (ULONG)"emberkeep",
        SA_ShowTitle, FALSE,
        SA_Quiet,     TRUE,
        TAG_END);
    if (!scr) return 1;
    install_palette();

    sbuf[0] = AllocScreenBuffer(scr, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(scr, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) { CloseScreen(scr); scr = NULL; return 2; }

    InitRastPort(&rp_buf[0]); rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]); rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    cur_buf = 1;
    rp = &rp_buf[cur_buf];

    for (int i = 0; i < 2; i++) {
        SetAPen(&rp_buf[i], 0);
        RectFill(&rp_buf[i], 0, 0, SCREEN_W - 1, SCREEN_H - 1);
    }
    return 0;
}

void render_close(void)
{
    if (scr && sbuf[0]) {
        int tries = 0;
        while (!ChangeScreenBuffer(scr, sbuf[0]) && ++tries < 5) WaitTOF();
        WaitTOF(); WaitTOF();
    }
    if (sbuf[1]) { FreeScreenBuffer(scr, sbuf[1]); sbuf[1] = NULL; }
    if (sbuf[0]) { FreeScreenBuffer(scr, sbuf[0]); sbuf[0] = NULL; }
    if (scr)     { CloseScreen(scr); scr = NULL; }
}

struct Screen *render_get_screen(void) { return scr; }

void render_flip(void)
{
    if (ChangeScreenBuffer(scr, sbuf[cur_buf])) {
        cur_buf ^= 1;
        rp = &rp_buf[cur_buf];
    }
}

static inline void fill_rect(LONG x, LONG y, LONG w, LONG h, UBYTE pen)
{
    if (w <= 0 || h <= 0) return;
    SetAPen(rp, pen);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
}

void render_text(LONG x, LONG y, UBYTE pen, const char *text)
{
    SetAPen(rp, pen);
    Move(rp, x, y);
    Text(rp, (STRPTR)text, (LONG)strlen(text));
}

/* Per-scanline same-pen RectFill runs from a 16x16 chunky tile — the
 * blit path that Just Worked in miner_meteor. */
static void blit_tile_data(LONG dest_x, LONG dest_y,
                           const UBYTE data[TILE_H][TILE_W])
{
    for (LONG py = 0; py < TILE_H; py++) {
        UBYTE run_pen = data[py][0];
        LONG  run_start = 0;
        for (LONG px = 1; px <= TILE_W; px++) {
            UBYTE p = (px < TILE_W) ? data[py][px] : (UBYTE)(run_pen ^ 1);
            if (p != run_pen) {
                SetAPen(rp, run_pen);
                RectFill(rp,
                    dest_x + run_start, dest_y + py,
                    dest_x + px - 1,    dest_y + py);
                run_pen   = p;
                run_start = px;
            }
        }
    }
}

static inline void blit_sheet_tile(LONG dest_x, LONG dest_y,
                                   LONG sheet_row, LONG sheet_col)
{
    blit_tile_data(dest_x, dest_y, tilesheet_data[sheet_row][sheet_col]);
}

/*
 * Sheet layout — mirror of tools/bake_tilesheet.py's grid.
 */
enum {
    SR_TERRAIN     = 0,
    SR_DECOR       = 1,
    SR_PLAYER_R    = 2,
    SR_PLAYER_L    = 3,
    SR_PLAYER_POSE = 4,
    SR_ENEMY_A     = 5,
    SR_ENEMY_B     = 6,
    SR_ENEMY_C     = 7,
};
enum {
    /* Terrain row */
    SC_FLOOR       = 0,
    SC_WALL_A      = 1,
    SC_WALL_B      = 2,
    SC_RUNE        = 3,
    SC_OIL         = 4,
    SC_HAZARD      = 5,
    /* SC 6-7 spare — leaves room to add e.g. torch sconce, chest */
    /* Decor row */
    SC_PORTAL_LOCKED = 0,
    SC_PORTAL_OPEN   = 1,
    /* Player-pose row */
    SC_POSE_UP     = 0,
    SC_POSE_RIGHT  = 1,
    SC_POSE_DOWN   = 2,
    SC_POSE_LEFT   = 3,
};

static void draw_tile(LONG col, LONG row, UBYTE kind,
                      LONG runes_remaining, ULONG tick)
{
    LONG x = col * TILE_W, y = row * TILE_H;
    switch (kind) {
        case T_FLOOR:  blit_sheet_tile(x, y, SR_TERRAIN, SC_FLOOR); break;
        case T_WALL:
            blit_sheet_tile(x, y, SR_TERRAIN,
                            ((col + row) & 1) ? SC_WALL_A : SC_WALL_B);
            break;
        case T_RUNE:   blit_sheet_tile(x, y, SR_TERRAIN, SC_RUNE); break;
        case T_OIL:    blit_sheet_tile(x, y, SR_TERRAIN, SC_OIL);  break;
        case T_HAZARD: blit_sheet_tile(x, y, SR_TERRAIN, SC_HAZARD); break;
        case T_PORTAL:
            if (runes_remaining == 0) {
                blit_sheet_tile(x, y, SR_DECOR,
                                ((tick / 6) & 1) ? SC_PORTAL_OPEN : SC_PORTAL_LOCKED);
            } else {
                blit_sheet_tile(x, y, SR_DECOR, SC_PORTAL_LOCKED);
            }
            break;
        default:       blit_sheet_tile(x, y, SR_TERRAIN, SC_FLOOR); break;
    }
}

static void draw_player(const Player &p)
{
    LONG col;
    switch (p.facing) {
        case 0:  col = SC_POSE_UP;    break;
        case 1:  col = SC_POSE_RIGHT; break;
        case 3:  col = SC_POSE_LEFT;  break;
        default: col = SC_POSE_DOWN;  break;
    }
    blit_sheet_tile(p.x, p.y, SR_PLAYER_POSE, col);
}

static void draw_enemy(const Enemy &E)
{
    if (!E.alive) return;
    LONG row = SR_ENEMY_A;
    if (E.colour == 5) row = SR_ENEMY_B;
    else if (E.colour == 6) row = SR_ENEMY_C;
    LONG col = (E.frame >> 2) & 7;
    blit_sheet_tile(E.x, E.y, row, col);
}

/* Paint pen-0 rectangles around the lantern cone so the field beyond
 * the player's light stays black. The cone follows the player's centre
 * and only covers the play area (rows 0..PLAY_H-1) — HUD strip below
 * always renders. */
static void apply_lantern_mask(const GameState &gs)
{
    LONG cx = gs.player.x + 8;
    LONG cy = gs.player.y + 8;

    /* Radius shrinks as the lantern runs low so the endgame gets
     * cramped and tense. Full = LIGHT_RADIUS_PX, empty = 8 px. */
    LONG r = 8 + (LIGHT_RADIUS_PX - 8) * gs.lantern / LANTERN_MAX;
    if (r < 8) r = 8;

    LONG lx = cx - r; if (lx < 0)      lx = 0;
    LONG rx = cx + r; if (rx > PLAY_W) rx = PLAY_W;
    LONG ty = cy - r; if (ty < 0)      ty = 0;
    LONG by = cy + r; if (by > PLAY_H) by = PLAY_H;

    /* Four bands around the light square. */
    SetAPen(rp, 0);
    if (ty > 0)          RectFill(rp, 0,        0,  PLAY_W - 1, ty - 1);
    if (by < PLAY_H)     RectFill(rp, 0,        by, PLAY_W - 1, PLAY_H - 1);
    if (lx > 0)          RectFill(rp, 0,        ty, lx - 1,     by - 1);
    if (rx < PLAY_W)     RectFill(rp, rx,       ty, PLAY_W - 1, by - 1);

    /* Cheap "circle" — chop the 4 corners of the light square with
     * small triangular blocks so it feels round-ish, not blockish. */
    LONG chop = r / 3;
    if (chop > 0) {
        if (ty > 0 && lx > 0)               RectFill(rp, lx, ty, lx + chop - 1, ty + chop - 1);
        if (ty > 0 && rx < PLAY_W)          RectFill(rp, rx - chop, ty, rx - 1, ty + chop - 1);
        if (by < PLAY_H && lx > 0)          RectFill(rp, lx, by - chop, lx + chop - 1, by - 1);
        if (by < PLAY_H && rx < PLAY_W)     RectFill(rp, rx - chop, by - chop, rx - 1, by - 1);
    }
}

static void draw_hud(const GameState &gs)
{
    fill_rect(0, PLAY_H, SCREEN_W, HUD_H, 15);

    char buf[64];
    sprintf(buf, "SCORE %06ld", (long)gs.score);
    render_text(4, PLAY_H + 12, 1, buf);

    /* Lantern fuel bar (orange -> red as it runs low). */
    LONG bar_w = 120, bar_x = 100;
    fill_rect(bar_x, PLAY_H + 4, bar_w, 8, 8);
    LONG fill = (gs.lantern * bar_w) / LANTERN_MAX;
    if (fill < 0) fill = 0;
    if (fill > bar_w) fill = bar_w;
    UBYTE pen = (gs.lantern < LANTERN_MAX / 4) ? 9 : 10;
    fill_rect(bar_x, PLAY_H + 4, fill, 8, pen);

    sprintf(buf, "LV%ld R%ld/%ld L%ld",
        (long)(gs.level_index + 1),
        (long)(gs.runes_total - gs.runes_remaining),
        (long)gs.runes_total,
        (long)gs.lives);
    render_text(230, PLAY_H + 12, 1, buf);
}

void render_frame(const GameState &gs)
{
    /* All tiles first — the lantern mask then blacks out any that
     * fall outside the light cone. */
    for (LONG row = 0; row < GRID_H; row++) {
        for (LONG col = 0; col < GRID_W; col++) {
            draw_tile(col, row, gs.grid[row][col], gs.runes_remaining, gs.tick);
        }
    }
    for (LONG i = 0; i < gs.num_enemies; i++) draw_enemy(gs.enemies[i]);
    draw_player(gs.player);
    apply_lantern_mask(gs);
    draw_hud(gs);
}

void render_title(const GameState &gs)
{
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    fill_rect(0, 40, SCREEN_W, 40, 11);
    render_text(96, 66, 1, "EMBERKEEP");

    render_text(40, 120, 6, "A LANTERN IN THE DEEP DARK");
    render_text(56, 138, 1, "INSPIRED BY 8-BIT CLASSICS");

    render_text(56, 176, 7, "CURSOR KEYS :  MOVE");
    render_text(56, 190, 7, "SPACE       :  BEGIN / RESTART");
    render_text(56, 204, 7, "ESC         :  QUIT");

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
        render_text(80, 100, 0, "THE DARK IS BEATEN");
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
