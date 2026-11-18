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
 * Renderer — double-buffered draw over a 320x256 screen (4 bitplanes
 * on classic, 8 on OS4 through os4_compat). The baked tile sheet is drawn once into
 * an off-screen BitMap and each tile or sprite is one BltBitMap from it.
 * Each buffer remembers which sheet tile it holds in every cell and what
 * its HUD shows, so a frame only redraws the cells that changed or that
 * a sprite covered two frames ago, plus the HUD when a value changed.
 * Redrawing all 300 tiles every frame as per-row RectFill runs cost
 * ~12,800 RectFills a frame, about 0.3 frames/s on a 68020.
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
static struct RastPort       *rp;

/* Double-buffering. Two ScreenBuffers + two RastPorts; we draw into
 * the "back" one while the display shows the "front", then flip via
 * ChangeScreenBuffer at VBlank. Kills the single-buffer flicker where
 * mid-frame clears made the character (and everything else) strobe. */
static struct ScreenBuffer   *sbuf[2];
static struct RastPort        rp_buf[2];
static UWORD                  cur_buf = 1;

/* The tile sheet as a BitMap, one extra column for the dimmed collapse
 * tile (brown 3 -> dim 15) at SC_COLLAPSE_DIM. */
#define SHEET_COLS   (TSHEET_TILES_W + 1)
static struct BitMap          sheet_bmap;
static struct BitMap         *sheet_bm;

/* Chunky staging buffer for the dimmed collapse tile. */
static UBYTE                  scratch_tile[TILE_H][TILE_W];

/* What each buffer holds: the sheet tile (row << 8 | col) in each cell,
 * CELL_STALE when a sprite or a title / end screen drew over it. */
#define CELL_STALE   0xFFFF
static UWORD                  drawn[2][GRID_H][GRID_W];

struct HudShown {
    LONG valid, score, fill, pen, level, keys_got, keys_total, lives;
};
static struct HudShown        hud_shown[2];

#ifdef __PPC__
#define SCREEN_DEPTH 8
#else
#define SCREEN_DEPTH 4     /* the palette has 16 entries: half the blits of 8 */
#endif

static LONG build_sheet(void);
static void free_sheet(void);

static void invalidate_buffer(UWORD b)
{
    for (LONG row = 0; row < GRID_H; row++)
        for (LONG col = 0; col < GRID_W; col++)
            drawn[b][row][col] = CELL_STALE;
    hud_shown[b].valid = 0;
}

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
        SA_Depth,       SCREEN_DEPTH,
        SA_Type,        CUSTOMSCREEN,
        SA_DisplayID,   0x00000L,     /* PAL:LORES on classic; RTG uses default on OS4 */
        SA_Title,       (ULONG)"miner-meteor",
        SA_ShowTitle,   FALSE,
        SA_Quiet,       TRUE,
        TAG_END);
    if (!scr) return 1;
    install_palette();

    /* Double-buffered draw setup. sbuf[0] wraps the screen's live
     * BitMap; sbuf[1] is a freshly-allocated matching BitMap we can
     * scribble into off-screen. */
    sbuf[0] = AllocScreenBuffer(scr, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(scr, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) { CloseScreen(scr); scr = NULL; return 2; }

    InitRastPort(&rp_buf[0]); rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]); rp_buf[1].BitMap = sbuf[1]->sb_BitMap;
    /* JAM1: in the default JAM2, captions drawn in pen 0 on a coloured bar
     * came out as solid black boxes (bug in the original). */
    SetDrMd(&rp_buf[0], JAM1); SetDrMd(&rp_buf[1], JAM1);

    cur_buf = 1;
    rp = &rp_buf[cur_buf];

    /* Clear both buffers to black up front so the first flip doesn't
     * show garbage. */
    for (int i = 0; i < 2; i++) {
        SetAPen(&rp_buf[i], 0);
        RectFill(&rp_buf[i], 0, 0, SCREEN_W - 1, SCREEN_H - 1);
        invalidate_buffer((UWORD)i);
    }
    if (build_sheet() != 0) { render_close(); return 3; }
    return 0;
}

void render_close(void)
{
    if (scr && sbuf[0]) {
        /* Restore the original screen buffer so CloseScreen frees
         * the right bitmap. */
        int tries = 0;
        while (!ChangeScreenBuffer(scr, sbuf[0]) && ++tries < 5) WaitTOF();
        WaitTOF(); WaitTOF();
    }
    free_sheet();
    if (sbuf[1]) { FreeScreenBuffer(scr, sbuf[1]); sbuf[1] = NULL; }
    if (sbuf[0]) { FreeScreenBuffer(scr, sbuf[0]); sbuf[0] = NULL; }
    if (scr)     { CloseScreen(scr); scr = NULL; }
}

/* Called by main.cpp after each render_frame / render_title / render_
 * endscreen call to swap the buffer we just drew into onto the display,
 * and re-point our writes at the (now off-screen) other buffer. */
void render_flip(void)
{
    if (ChangeScreenBuffer(scr, sbuf[cur_buf])) {
        cur_buf ^= 1;
        rp = &rp_buf[cur_buf];
    }
}

struct Screen *render_get_screen(void)
{
    return scr;
}

/* Write one tile of chunky pens into the planar sheet BitMap. */
static void sheet_put_tile(struct BitMap *bm, LONG depth, LONG dest_x, LONG dest_y,
                           const UBYTE data[TILE_H][TILE_W])
{
    for (LONG py = 0; py < TILE_H; py++) {
        ULONG row_off = (ULONG)(dest_y + py) * bm->BytesPerRow;
        for (LONG px = 0; px < TILE_W; px++) {
            UBYTE pen = data[py][px];
            ULONG off = row_off + ((dest_x + px) >> 3);
            UBYTE bit = (UBYTE)(0x80 >> ((dest_x + px) & 7));
            for (LONG d = 0; d < depth; d++)
                if (pen & (1 << d)) bm->Planes[d][off] |= bit;
        }
    }
}

/* Build the sheet BitMap from tilesheet_data: a plain planar BitMap
 * (chip RAM on classic, for the blitter) the CPU writes directly. The
 * screen's bitmaps are planar on both systems (os4_compat keeps them in
 * RAM on OS4). Returns 0 on success. */
static LONG build_sheet(void)
{
    const LONG w = SHEET_COLS * TILE_W, h = TSHEET_TILES_H * TILE_H;
    InitBitMap(&sheet_bmap, SCREEN_DEPTH, w, h);
    for (LONG d = 0; d < SCREEN_DEPTH; d++) {
        sheet_bmap.Planes[d] = (PLANEPTR)AllocRaster(w, h);
        if (!sheet_bmap.Planes[d]) return 1;
        memset(sheet_bmap.Planes[d], 0, (size_t)sheet_bmap.BytesPerRow * h);
    }
    sheet_bm = &sheet_bmap;

    /* The dimmed collapse tile. */
    memcpy(scratch_tile, &tilesheet_data[0][3][0][0], sizeof(scratch_tile));
    for (LONG py = 0; py < TILE_H; py++)
        for (LONG px = 0; px < TILE_W; px++)
            if (scratch_tile[py][px] == 3) scratch_tile[py][px] = 15;

    for (LONG r = 0; r < TSHEET_TILES_H; r++)
        for (LONG c = 0; c < TSHEET_TILES_W; c++)
            sheet_put_tile(sheet_bm, SCREEN_DEPTH, c * TILE_W, r * TILE_H,
                           tilesheet_data[r][c]);
    sheet_put_tile(sheet_bm, SCREEN_DEPTH, TSHEET_TILES_W * TILE_W, 0, scratch_tile);
    return 0;
}

static void free_sheet(void)
{
    WaitBlit();
    for (LONG d = 0; d < SCREEN_DEPTH; d++) {
        if (sheet_bmap.Planes[d])
            FreeRaster(sheet_bmap.Planes[d], SHEET_COLS * TILE_W, TSHEET_TILES_H * TILE_H);
        sheet_bmap.Planes[d] = NULL;
    }
    sheet_bm = NULL;
}

/* Copy sheet tile (sheet_row, sheet_col) to (dest_x, dest_y), clipped to
 * the play area. */
static void blit_sheet_tile(LONG dest_x, LONG dest_y, LONG sheet_row, LONG sheet_col)
{
    LONG sx = sheet_col * TILE_W, sy = sheet_row * TILE_H;
    LONG w = TILE_W, h = TILE_H;
    if (dest_x < 0) { sx -= dest_x; w += dest_x; dest_x = 0; }
    if (dest_y < 0) { sy -= dest_y; h += dest_y; dest_y = 0; }
    if (dest_x + w > PLAY_W) w = PLAY_W - dest_x;
    if (dest_y + h > PLAY_H) h = PLAY_H - dest_y;
    if (w <= 0 || h <= 0) return;
    BltBitMap(sheet_bm, sx, sy, rp->BitMap, dest_x, dest_y, w, h, 0xC0, 0xFF, NULL);
}

/* A sprite was drawn at (x, y): the cells under it are stale in this
 * buffer. */
static void mark_sprite(LONG x, LONG y)
{
    LONG c0 = x >> 4, c1 = (x + TILE_W - 1) >> 4;
    LONG r0 = y >> 4, r1 = (y + TILE_H - 1) >> 4;
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= GRID_W) c1 = GRID_W - 1;
    if (r1 >= GRID_H) r1 = GRID_H - 1;
    for (LONG r = r0; r <= r1; r++)
        for (LONG c = c0; c <= c1; c++)
            drawn[cur_buf][r][c] = CELL_STALE;
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

/*
 * Sheet layout — must match tools/bake_tilesheet.py.
 * Row / column indexes into the 8x20 tile-sheet grid.
 */
enum {
    SR_TERRAIN     = 0,   /* row 0: terrain tiles */
    SR_DECOR       = 1,   /* row 1: exit + decoration */
    SR_PLAYER_R    = 2,   /* row 2: player facing right, 8 walk frames */
    SR_PLAYER_L    = 3,   /* row 3: player facing left */
    SR_PLAYER_POSE = 4,   /* row 4: jump / fall / death / climb */
    SR_GUARDIAN_A  = 5,
    SR_GUARDIAN_B  = 6,
    SR_GUARDIAN_C  = 7,
};
enum {
    /* Terrain-row columns. */
    SC_EMPTY       = 0,
    SC_SOLID_A     = 1,
    SC_SOLID_B     = 2,
    SC_COLLAPSE    = 3,
    SC_CONV_L      = 4,
    SC_CONV_R      = 5,
    SC_HAZARD      = 6,
    SC_KEY         = 7,
    SC_COLLAPSE_DIM = TSHEET_TILES_W,   /* built by build_sheet */
    /* Decor-row columns. */
    SC_EXIT_LOCKED = 0,
    SC_EXIT_OPEN   = 1,
    /* Player special-pose columns. */
    SC_POSE_JUMP   = 0,
    SC_POSE_FALL   = 1,
    SC_POSE_DEATH  = 2,
    SC_POSE_CLIMB  = 3,
};

/* The sheet tile (row << 8 | col) a cell shows this frame. */
static UWORD cell_tile(LONG col, LONG row, const TileState &t, ULONG tick,
                       LONG keys_remaining)
{
    switch (t.kind) {
        case T_SOLID:
            /* Chequer between the two solid variants for texture. */
            return (UWORD)((SR_TERRAIN << 8) |
                           (((col + row) & 1) ? SC_SOLID_A : SC_SOLID_B));
        case T_KEY:
            return (UWORD)((SR_TERRAIN << 8) | SC_KEY);
        case T_EXIT:
            /* Two sheet variants so the player has clear visual feedback
             * on objective progress; blink the unlocked variant with the
             * locked one every 6 frames for a soft glow effect. */
            if (keys_remaining == 0 && ((tick / 6) & 1))
                return (UWORD)((SR_DECOR << 8) | SC_EXIT_OPEN);
            return (UWORD)((SR_DECOR << 8) | SC_EXIT_LOCKED);
        case T_COLLAPSE:
            /* Darker on the second half of the collapse timer. */
            if (t.timer > 0 && t.timer <= COLLAPSE_FRAMES / 2)
                return (UWORD)((SR_TERRAIN << 8) | SC_COLLAPSE_DIM);
            return (UWORD)((SR_TERRAIN << 8) | SC_COLLAPSE);
        case T_CONV_L:
            return (UWORD)((SR_TERRAIN << 8) | SC_CONV_L);
        case T_CONV_R:
            return (UWORD)((SR_TERRAIN << 8) | SC_CONV_R);
        case T_HAZARD:
            return (UWORD)((SR_TERRAIN << 8) | SC_HAZARD);
        default:
            return (UWORD)((SR_TERRAIN << 8) | SC_EMPTY);
    }
}

static void draw_player(const Player &p)
{
    /* Pick the appropriate sheet frame:
     *   - airborne: jump if rising, fall if descending
     *   - grounded: walk cycle by direction, indexed by frame&7
     */
    LONG row, col;
    if (!p.on_ground) {
        row = SR_PLAYER_POSE;
        col = (p.vy < 0) ? SC_POSE_JUMP : SC_POSE_FALL;
    } else {
        row = p.facing ? SR_PLAYER_R : SR_PLAYER_L;
        col = p.frame & 7;
    }
    blit_sheet_tile(p.x, p.y, row, col);
    mark_sprite(p.x, p.y);
}

static void draw_guardian(const Guardian &G)
{
    if (!G.alive) return;
    /* Guardian colour selects sheet row; frame selects animation phase. */
    LONG row;
    switch (G.colour) {
        case 5: row = SR_GUARDIAN_B; break;
        case 6: row = SR_GUARDIAN_C; break;
        default: row = SR_GUARDIAN_A; break;
    }
    LONG col = (G.frame >> 2) & 7;   /* animation phase */
    blit_sheet_tile(G.x, G.y, row, col);
    mark_sprite(G.x, G.y);
}

static void draw_hud(const GameState &gs)
{
    LONG bar_w = 120;
    LONG bar_x = 100;
    LONG fill = (gs.air * bar_w) / AIR_MAX;
    if (fill < 0) fill = 0;
    if (fill > bar_w) fill = bar_w;
    UBYTE pen = (gs.air < AIR_MAX / 4) ? 9 : 7;

    /* Only when something it shows changed since this buffer's last HUD. */
    struct HudShown now = { 1, gs.score, fill, pen, gs.level_index,
                            gs.keys_total - gs.keys_remaining, gs.keys_total,
                            gs.lives };
    if (memcmp(&now, &hud_shown[cur_buf], sizeof(now)) == 0) return;
    hud_shown[cur_buf] = now;

    /* HUD strip along the bottom 16 px. */
    fill_rect(0, PLAY_H, SCREEN_W, HUD_H, 15);

    char buf[64];

    /* Score + air on the left, lives + keys + level on the right. */
    sprintf(buf, "SCORE %06ld", (long)gs.score);
    render_text(4, PLAY_H + 12, 1, buf);

    /* Air bar. */
    fill_rect(bar_x, PLAY_H + 4, bar_w, 8, 8);
    fill_rect(bar_x, PLAY_H + 4, fill, 8, pen);

    sprintf(buf, "LV%ld K%ld/%ld L%ld",
        (long)(gs.level_index + 1),
        (long)(gs.keys_total - gs.keys_remaining), (long)gs.keys_total,
        (long)gs.lives);
    render_text(230, PLAY_H + 12, 1, buf);
}

void render_frame(const GameState &gs)
{
    /* Play area — the tiles that differ from what this buffer holds. */
    for (LONG row = 0; row < GRID_H; row++) {
        for (LONG col = 0; col < GRID_W; col++) {
            UWORD t = cell_tile(col, row, gs.grid[row][col], gs.tick,
                                gs.keys_remaining);
            if (t != drawn[cur_buf][row][col]) {
                blit_sheet_tile(col * TILE_W, row * TILE_H, t >> 8, t & 0xFF);
                drawn[cur_buf][row][col] = t;
            }
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
    invalidate_buffer(cur_buf);
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
    invalidate_buffer(cur_buf);
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
