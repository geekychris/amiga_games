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
 * warden_keep renderer — isometric painter's-algorithm scene draw.
 *
 * Every drawable is projected from world (wx,wy,wz) to screen
 * coordinates and rendered with graphics.library primitives (RectFill
 * + Draw lines). Cubes are drawn as three quads: top diamond, left
 * face parallelogram, right face parallelogram. Sort key for
 * painter's algorithm is (wx+wy) — draw back (low) to front (high).
 *
 * Palette follows the Fairlight "monochrome yellow keep" aesthetic:
 *   0 black background
 *   1 dark yellow      (floor / left face shading)
 *   2 mid yellow       (top face)
 *   3 bright yellow    (right face highlight, jewels, outlines)
 *   4 dim floor stroke
 *   5 door
 *   6 white (HUD text)
 *   7 red   (player accent)
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

static const UBYTE PALETTE[8][3] = {
    /* 0 */ {   0,   0,   0 },
    /* 1 */ { 128, 112,  32 },
    /* 2 */ { 208, 176,  40 },
    /* 3 */ { 248, 232,  80 },
    /* 4 */ {  56,  48,  16 },
    /* 5 */ {  64,  56, 168 },
    /* 6 */ { 240, 240, 240 },
    /* 7 */ { 240,  72,  56 },
};

static void install_palette(void)
{
    ULONG buf[2 + 8 * 3];
    buf[0] = (8UL << 16) | 0;
    for (LONG i = 0; i < 8; i++) {
        buf[1 + i * 3 + 0] = ((ULONG)PALETTE[i][0]) * 0x01010101ul;
        buf[1 + i * 3 + 1] = ((ULONG)PALETTE[i][1]) * 0x01010101ul;
        buf[1 + i * 3 + 2] = ((ULONG)PALETTE[i][2]) * 0x01010101ul;
    }
    buf[1 + 8 * 3] = 0;
    LoadRGB32(&scr->ViewPort, (ULONG *)buf);
}

LONG render_open(void)
{
    scr = OpenScreenTags(NULL,
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     3,
        SA_Type,      CUSTOMSCREEN,
        SA_DisplayID, 0x00000L,
        SA_Title,     (ULONG)"warden_keep",
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

void render_text(LONG x, LONG y, UBYTE pen, const char *text)
{
    SetAPen(rp, pen);
    Move(rp, x, y);
    Text(rp, (STRPTR)text, (LONG)strlen(text));
}

/* World -> screen isometric projection. wz is world height (0 = floor). */
static inline void iso_project(LONG wx, LONG wy, LONG wz, LONG *sx, LONG *sy)
{
    *sx = (wx - wy) * (ISO_TILE_W / 2) + ORG_X;
    *sy = (wx + wy) * (ISO_TILE_H / 2) - wz * ISO_CUBE_H + ORG_Y;
}

/* Fill a horizontal-scanline "quad" — no arbitrary polygon fill in
 * plain graphics.library, so we approximate the diamond / parallelogram
 * shapes with a series of RectFill scanlines. Fast enough for a
 * pixel-count budget of a few hundred quads. */
static void fill_top_diamond(LONG cx, LONG cy, UBYTE pen)
{
    /* A tile top-face diamond of full width ISO_TILE_W and height
     * ISO_TILE_H, centred on (cx, cy). Draw as pairs of scanlines
     * expanding from centre outward. */
    SetAPen(rp, pen);
    LONG half_w = ISO_TILE_W / 2;
    LONG half_h = ISO_TILE_H / 2;
    for (LONG i = 0; i < half_h; i++) {
        LONG w = ((i + 1) * half_w) / half_h;
        RectFill(rp, cx - w, cy - i, cx + w - 1, cy - i);
        RectFill(rp, cx - w, cy + i, cx + w - 1, cy + i);
    }
}

static void fill_left_face(LONG cx, LONG cy_top, UBYTE pen)
{
    /* Left face: parallelogram from (cx - half_w, cy_top) going down-
     * right. Width = half_w, height = ISO_CUBE_H. Skew: as we descend
     * one pixel, the top edge drops by ((ISO_TILE_H/2) / half_w) — but
     * for 32-wide iso that's ~0.5. We draw scanline by scanline. */
    SetAPen(rp, pen);
    LONG half_w = ISO_TILE_W / 2;
    LONG half_h = ISO_TILE_H / 2;
    for (LONG y = 0; y < ISO_CUBE_H; y++) {
        /* Bottom-left corner at cy_top + half_h + y, x=cx-half_w
         * Top-left corner at cy_top - half_h... but the top face
         * already covers the top. Draw only the "wall" portion. */
        LONG top_y = cy_top + y;
        for (LONG px = 0; px < half_w; px++) {
            LONG line_top_y = cy_top - half_h + ((px * half_h) / half_w);
            if (top_y >= line_top_y)
                WritePixel(rp, cx - half_w + px, top_y);
        }
    }
}

static void fill_right_face(LONG cx, LONG cy_top, UBYTE pen)
{
    SetAPen(rp, pen);
    LONG half_w = ISO_TILE_W / 2;
    LONG half_h = ISO_TILE_H / 2;
    for (LONG y = 0; y < ISO_CUBE_H; y++) {
        LONG top_y = cy_top + y;
        for (LONG px = 0; px < half_w; px++) {
            LONG line_top_y = cy_top + ((px * half_h) / half_w);
            if (top_y >= line_top_y)
                WritePixel(rp, cx + px, top_y);
        }
    }
}

/* Draw a full unit cube standing at world (wx, wy, wz=0). Top diamond
 * uses pen_top; left face pen_left; right face pen_right. */
static void draw_cube(LONG wx, LONG wy, UBYTE pen_top, UBYTE pen_left, UBYTE pen_right)
{
    LONG sx, sy;
    iso_project(wx, wy, 1, &sx, &sy);   /* top of the cube */
    LONG half_h = ISO_TILE_H / 2;
    /* Draw the faces first (behind the top). */
    fill_left_face(sx, sy + half_h, pen_left);
    fill_right_face(sx, sy + half_h, pen_right);
    /* Then the top diamond. */
    fill_top_diamond(sx, sy, pen_top);
}

/* Draw a taller cube — stack N unit-cubes vertically. Used for walls
 * so they look room-height, not floor-tile-height. */
static void draw_stacked_cube(LONG wx, LONG wy, LONG stack,
                              UBYTE pen_top, UBYTE pen_left, UBYTE pen_right)
{
    /* Draw faces once at full height, then top diamond at the top. */
    LONG sx, sy;
    iso_project(wx, wy, stack, &sx, &sy);   /* top face position */
    LONG half_w = ISO_TILE_W / 2;
    LONG half_h = ISO_TILE_H / 2;

    /* Extended left face: draw scanline by scanline down to the base. */
    SetAPen(rp, pen_left);
    LONG total_h = stack * ISO_CUBE_H;
    for (LONG y = 0; y < total_h; y++) {
        LONG top_y = sy + half_h + y;
        for (LONG px = 0; px < half_w; px++) {
            LONG line_top_y = sy - half_h + ((px * half_h) / half_w) + half_h;
            if (top_y >= line_top_y)
                WritePixel(rp, sx - half_w + px, top_y);
        }
    }
    /* Extended right face. */
    SetAPen(rp, pen_right);
    for (LONG y = 0; y < total_h; y++) {
        LONG top_y = sy + half_h + y;
        for (LONG px = 0; px < half_w; px++) {
            LONG line_top_y = sy + ((px * half_h) / half_w) + half_h;
            if (top_y >= line_top_y)
                WritePixel(rp, sx + px, top_y);
        }
    }
    /* Top diamond. */
    fill_top_diamond(sx, sy, pen_top);
}

/* Draw a floor tile at wz=0 (flat diamond only). */
static void draw_floor_tile(LONG wx, LONG wy, UBYTE kind)
{
    LONG sx, sy;
    iso_project(wx, wy, 0, &sx, &sy);
    UBYTE pen = (kind == F_DOOR) ? 5 : 1;
    fill_top_diamond(sx, sy, pen);
    /* Faint darker outline for grout. */
    SetAPen(rp, 4);
    LONG half_w = ISO_TILE_W / 2;
    LONG half_h = ISO_TILE_H / 2;
    /* Top-left, top-right, bottom-left, bottom-right edges of the diamond. */
    Move(rp, sx - half_w, sy);       Draw(rp, sx, sy - half_h);
    Move(rp, sx, sy - half_h);       Draw(rp, sx + half_w, sy);
    Move(rp, sx + half_w, sy);       Draw(rp, sx, sy + half_h);
    Move(rp, sx, sy + half_h);       Draw(rp, sx - half_w, sy);
}

/* Draw a small "player" — a red pillar so they're immediately visible
 * against the yellow walls / floor. Facing indicator = a bright white
 * pixel on top diamond in the facing direction. */
static void draw_player(const Player &p)
{
    /* Two-cube tall pillar in the player accent colour (red). */
    draw_stacked_cube(p.wx, p.wy, 2, 7, 7, 6);
    LONG sx, sy;
    iso_project(p.wx, p.wy, 2, &sx, &sy);
    SetAPen(rp, 6);
    switch (p.facing) {
        case 0: RectFill(rp, sx - 3, sy - 6, sx + 2, sy - 4); break;
        case 1: RectFill(rp, sx + 3, sy - 2, sx + 7, sy + 1); break;
        case 2: RectFill(rp, sx - 3, sy + 3, sx + 2, sy + 5); break;
        case 3: RectFill(rp, sx - 7, sy - 2, sx - 3, sy + 1); break;
    }
}

static void draw_jewel(const Prop &o)
{
    LONG sx, sy;
    iso_project(o.wx, o.wy, 0, &sx, &sy);
    /* Small bright rhombus above the floor. */
    SetAPen(rp, 3);
    RectFill(rp, sx - 2, sy - 6, sx + 1, sy - 3);
    RectFill(rp, sx - 3, sy - 5, sx + 2, sy - 4);
    SetAPen(rp, 6);
    WritePixel(rp, sx, sy - 6);
}

/* Sort a small array of object indices by (wx+wy) ascending — plain
 * insertion sort, enough for <=MAX_OBJECTS entries. */
static void sort_objects(const GameState &gs, LONG *idx, LONG n)
{
    for (LONG i = 1; i < n; i++) {
        LONG cur = idx[i];
        LONG ck  = (LONG)gs.objects[cur].wx + (LONG)gs.objects[cur].wy;
        LONG j = i;
        while (j > 0) {
            LONG pk = (LONG)gs.objects[idx[j-1]].wx + (LONG)gs.objects[idx[j-1]].wy;
            if (pk <= ck) break;
            idx[j] = idx[j-1];
            j--;
        }
        idx[j] = cur;
    }
}

static void draw_hud(const GameState &gs)
{
    SetAPen(rp, 4);
    RectFill(rp, 0, PLAY_H, SCREEN_W - 1, SCREEN_H - 1);
    char buf[64];
    sprintf(buf, "SCORE %06ld", (long)gs.score);
    render_text(4, PLAY_H + 12, 6, buf);
    sprintf(buf, "JEWELS %ld", (long)gs.jewels_left);
    render_text(230, PLAY_H + 12, 6, buf);
}

void render_frame(const GameState &gs)
{
    /* Clear play area to bg. */
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, PLAY_H - 1);

    /* 1. Floor tiles — always at wz=0, drawn back-to-front by row+col
     *    so overlaps are visually consistent. */
    for (LONG wy = 0; wy < WORLD_H; wy++)
        for (LONG wx = 0; wx < WORLD_W; wx++)
            draw_floor_tile(wx, wy, gs.floor[wy][wx]);

    /* 2. Objects + player — combined painter-sorted pass. Player slot
     *    is index gs.num_objects (sentinel). */
    LONG idx[MAX_OBJECTS + 1];
    LONG n = 0;
    for (LONG i = 0; i < gs.num_objects; i++) {
        if (gs.objects[i].kind == O_NONE) continue;
        idx[n++] = i;
    }
    /* Sort objects by (wx+wy). */
    sort_objects(gs, idx, n);

    /* Merge the player into the draw pass by its own (wx+wy) key. */
    LONG pk = gs.player.wx + gs.player.wy;
    LONG drew_player = 0;
    for (LONG i = 0; i < n; i++) {
        LONG ok = (LONG)gs.objects[idx[i]].wx + (LONG)gs.objects[idx[i]].wy;
        if (!drew_player && pk < ok) {
            draw_player(gs.player);
            drew_player = 1;
        }
        const Prop &o = gs.objects[idx[i]];
        switch (o.kind) {
            case O_WALL:   draw_stacked_cube(o.wx, o.wy, 3, 2, 1, 3); break;
            case O_BARREL: draw_cube(o.wx, o.wy, 3, 2, 1); break;
            case O_JEWEL:  draw_jewel(o); break;
            default: break;
        }
    }
    if (!drew_player) draw_player(gs.player);

    draw_hud(gs);
}

void render_title(const GameState &gs)
{
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    SetAPen(rp, 2);
    RectFill(rp, 0, 40, SCREEN_W - 1, 79);
    render_text(96, 66, 0, "WARDEN  KEEP");

    render_text(48, 120, 3, "AN ISOMETRIC KEEP TO PLUNDER");
    render_text(56, 138, 6, "PUSH BARRELS, GRAB JEWELS");

    render_text(56, 176, 3, "CURSOR KEYS :  MOVE");
    render_text(56, 190, 3, "SPACE       :  BEGIN");
    render_text(56, 204, 3, "ESC         :  QUIT");

    if ((gs.tick / 15) & 1)
        render_text(80, 232, 7, "PRESS  SPACE  TO  BEGIN");
}

void render_endscreen(const GameState &gs)
{
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
    SetAPen(rp, 3);
    RectFill(rp, 0, 80, SCREEN_W - 1, 109);
    render_text(64, 100, 0, "EVERY JEWEL CLAIMED");

    char buf[64];
    sprintf(buf, "FINAL SCORE  %06ld", (long)gs.score);
    render_text(88, 156, 6, buf);

    if (gs.state_timer == 0 && ((gs.tick / 15) & 1))
        render_text(72, 208, 2, "PRESS SPACE TO RETURN");
}
