/*
 * buttons.c - clickable button bar along the bottom of the window.
 *
 * Uses plain RastPort Text rendering with a bordered rectangle per
 * button (no gadgets, no BOOPSI). Two-row layout so we get 10-12
 * short-labelled buttons without cramping.
 *
 * Hit-test uses cached rects computed at layout time - click coords
 * always match the drawn button positions.
 */

#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "masterwork.h"

typedef struct {
    const char *label;
    void (*action)(void);
    /* Cached hit rect (filled at draw-time). */
    int x0, y0, x1, y1;
} Button;

/* Action wrappers so buttons.c can hold plain function pointers. */
static void act_copy   (void) { op_copy(0);   }
static void act_move   (void) { op_move(0);   }
static void act_delete (void) { op_delete(1); }
static void act_rename (void) { op_rename();  }
static void act_mkdir  (void) { op_mkdir();   }
static void act_view   (void) { op_view();    }
static void act_refresh(void) {
    refresh_pane(&panes[0]);
    refresh_pane(&panes[1]);
    snprintf(status_msg, sizeof(status_msg), "both panes refreshed");
}
static void act_swap   (void) {
    /* Swap the two pane paths so cursors stay put but data flips. */
    char tmp[MAX_PATH];
    strncpy(tmp, panes[0].path, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
    strncpy(panes[0].path, panes[1].path, MAX_PATH - 1); panes[0].path[MAX_PATH - 1] = 0;
    strncpy(panes[1].path, tmp, MAX_PATH - 1); panes[1].path[MAX_PATH - 1] = 0;
    refresh_pane(&panes[0]);
    refresh_pane(&panes[1]);
}
static void act_devices(void) {
    /* Drop the active pane straight to the volume/assigns picker. */
    refresh_volumes(&panes[active_pane]);
}
static void act_quit   (void) { running = 0; }

static Button g_buttons[] = {
    { "Copy",    act_copy,    0, 0, 0, 0 },
    { "Move",    act_move,    0, 0, 0, 0 },
    { "Delete",  act_delete,  0, 0, 0, 0 },
    { "Rename",  act_rename,  0, 0, 0, 0 },
    { "MakeDir", act_mkdir,   0, 0, 0, 0 },
    { "View",    act_view,    0, 0, 0, 0 },
    { "Refresh", act_refresh, 0, 0, 0, 0 },
    { "Swap",    act_swap,    0, 0, 0, 0 },
    { "Devices", act_devices, 0, 0, 0, 0 },
    { "Quit",    act_quit,    0, 0, 0, 0 },
};
#define BUTTON_COUNT (int)(sizeof(g_buttons) / sizeof(g_buttons[0]))

/* Layout: two rows of 5. Recompute each redraw against current
 * g_row_h so different font sizes stay aligned. */
#define BUTTON_ROWS  2
#define BUTTON_COLS  5

int buttons_height(void)
{
    /* Each button = row_h + padding; margins top/bottom. */
    return (g_row_h + 6) * BUTTON_ROWS + 4;
}

static void draw_button(Button *b, int is_hover)
{
    struct RastPort *r = rp;
    /* Filled buttons pop against the pane backgrounds. We use the
     * Workbench highlight pen (usually orange) for the fill and the
     * regular text pen for labels — those two are guaranteed to be
     * defined on any Workbench palette. */
    UBYTE fill = is_hover ? PEN_FG : PEN_HL_BG;
    UBYTE fg   = is_hover ? PEN_HL_BG : PEN_HL_FG;

    /* Fill + border */
    SetAPen(r, fill);
    RectFill(r, b->x0, b->y0, b->x1, b->y1);
    SetAPen(r, PEN_BORDER);
    Move(r, b->x0, b->y0);
    Draw(r, b->x1, b->y0);
    Draw(r, b->x1, b->y1);
    Draw(r, b->x0, b->y1);
    Draw(r, b->x0, b->y0);

    /* Centred label */
    int label_w = (int)strlen(b->label) * g_char_w;
    int lx = b->x0 + ((b->x1 - b->x0) - label_w) / 2;
    int ly = b->y0 + ((b->y1 - b->y0) + g_baseline) / 2;
    SetAPen(r, fg);
    SetBPen(r, fill);
    SetDrMd(r, JAM2);
    Move(r, lx, ly);
    Text(r, (STRPTR)b->label, (LONG)strlen(b->label));
}

void draw_buttons(void)
{
    int top = WIN_H - buttons_height();
    int bar_h = buttons_height();
    int cell_w = (WIN_W - PANE_MARGIN * (BUTTON_COLS + 1)) / BUTTON_COLS;
    int cell_h = (bar_h - 4) / BUTTON_ROWS - 2;

    /* Clear the bar background so we don't leave old text ghosts. */
    SetAPen(rp, PEN_BG);
    RectFill(rp, 0, top, WIN_W - 1, WIN_H - 1);

    for (int i = 0; i < BUTTON_COUNT; i++) {
        int col = i % BUTTON_COLS;
        int row = i / BUTTON_COLS;
        g_buttons[i].x0 = PANE_MARGIN + col * (cell_w + PANE_MARGIN);
        g_buttons[i].y0 = top + 2 + row * (cell_h + 2);
        g_buttons[i].x1 = g_buttons[i].x0 + cell_w;
        g_buttons[i].y1 = g_buttons[i].y0 + cell_h;
        draw_button(&g_buttons[i], 0);
    }
}

int hit_test_button(int mx, int my)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        Button *b = &g_buttons[i];
        if (mx >= b->x0 && mx <= b->x1 && my >= b->y0 && my <= b->y1) {
            return i;
        }
    }
    return -1;
}

void run_button(int idx)
{
    if (idx < 0 || idx >= BUTTON_COUNT) return;
    if (g_buttons[idx].action) g_buttons[idx].action();
}
