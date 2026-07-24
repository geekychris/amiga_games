/*
 * ui.c — shared drawing primitives + tab bar + list-row renderer.
 *
 * Kept intentionally small; the three tab subsystems handle their own
 * layouts and just call these helpers for atomic paints.
 */

#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/graphics.h>

#include "organizer.h"

void ui_draw_string(int x, int y, const char *s, UBYTE fg, UBYTE bg)
{
    SetAPen(g_rp, fg);
    SetBPen(g_rp, bg);
    SetDrMd(g_rp, JAM2);
    Move(g_rp, x, y);
    Text(g_rp, (STRPTR)s, (LONG)strlen(s));
}

void ui_fill_rect(int x0, int y0, int w, int h, UBYTE pen)
{
    if (w <= 0 || h <= 0) return;
    SetAPen(g_rp, pen);
    RectFill(g_rp, x0, y0, x0 + w - 1, y0 + h - 1);
}

void ui_draw_frame(int x0, int y0, int w, int h, UBYTE pen)
{
    if (w <= 0 || h <= 0) return;
    SetAPen(g_rp, pen);
    Move(g_rp, x0, y0);
    Draw(g_rp, x0 + w - 1, y0);
    Draw(g_rp, x0 + w - 1, y0 + h - 1);
    Draw(g_rp, x0, y0 + h - 1);
    Draw(g_rp, x0, y0);
}

/* Tab bar: three equal-width tabs across the top. Active tab painted
 * with the highlight pen so it looks pressed. Labels centred. */
static const char *tab_label(int t)
{
    switch (t) {
    case TAB_NOTES:    return "Notes  (F1)";
    case TAB_TASKS:    return "Tasks  (F2)";
    case TAB_CALENDAR: return "Calendar  (F3)";
    default:           return "?";
    }
}

/* All drawing coords are in window-outer space, so we bake g_ox/g_oy
 * (BorderLeft/BorderTop) into the layout — otherwise the tab bar is
 * drawn under Intuition's title bar and clicks land on the drag bar
 * instead of the tab. Hit-tests use the same origin so click ↔ paint
 * stay in sync. */

void ui_draw_tabs(int x0, int y0, int w)
{
    (void)x0; (void)y0; (void)w;
    int tw = g_win_w / TAB_COUNT;
    for (int i = 0; i < TAB_COUNT; i++) {
        int x = g_ox + i * tw;
        int y = g_oy;
        int is_active = (i == g_current_tab);
        UBYTE bg = is_active ? PEN_HL_BG : PEN_BG;
        UBYTE fg = is_active ? PEN_HL_FG : PEN_FG;
        ui_fill_rect(x, y, tw, TAB_H, bg);
        ui_draw_frame(x, y, tw, TAB_H, PEN_BORDER);
        const char *lbl = tab_label(i);
        int lx = x + (tw - (int)strlen(lbl) * g_char_w) / 2;
        int ly = y + (TAB_H + g_baseline) / 2 - 1;
        ui_draw_string(lx, ly, lbl, fg, bg);
    }
}

int ui_hit_tab(int mx, int my)
{
    if (my < g_oy || my >= g_oy + TAB_H) return -1;
    if (mx < g_ox || mx >= g_ox + g_win_w) return -1;
    int tw = g_win_w / TAB_COUNT;
    if (tw <= 0) return -1;
    int idx = (mx - g_ox) / tw;
    return (idx >= 0 && idx < TAB_COUNT) ? idx : -1;
}

/* Content area = between tab bar and status bar, inset by MARGIN. */
void ui_content_rect(int *x0, int *y0, int *w, int *h)
{
    *x0 = g_ox + MARGIN;
    *y0 = g_oy + TAB_H + MARGIN;
    *w  = g_win_w - 2 * MARGIN;
    *h  = g_win_h - TAB_H - STATUS_H - 2 * MARGIN;
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
}

void ui_draw_status(void)
{
    int y = g_oy + g_win_h - STATUS_H;
    ui_fill_rect(g_ox, y, g_win_w, STATUS_H, PEN_BG);
    ui_draw_frame(g_ox, y, g_win_w, STATUS_H, PEN_BORDER);
    ui_draw_string(g_ox + 4, y + 2 + g_baseline, g_status, PEN_FG, PEN_BG);
}

void ui_draw_row(int x0, int y0, int w, const char *left, const char *right, int selected)
{
    UBYTE bg = selected ? PEN_HL_BG : PEN_BG;
    UBYTE fg = selected ? PEN_HL_FG : PEN_FG;
    ui_fill_rect(x0, y0, w, g_row_h, bg);
    ui_draw_string(x0 + 4, y0 + g_baseline, left, fg, bg);
    if (right && *right) {
        int rw = (int)strlen(right) * g_char_w;
        int rx = x0 + w - rw - 4;
        if (rx > x0 + 4 + (int)strlen(left) * g_char_w + g_char_w)
            ui_draw_string(rx, y0 + g_baseline, right, fg, bg);
    }
}
