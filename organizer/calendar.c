/*
 * calendar.c — Calendar tab: month grid + selected-day drilldown.
 *
 * Top half: Sun..Sat header row + 6 week rows. Each day cell shows
 * the day number and a small count of items on that day (tasks
 * whose due matches, and events on that date). Cursor cell inverted;
 * today's cell has a coloured border.
 * Bottom half: list of items on `g_cal_selected` — tasks due there
 * plus events that fire there. Keys: <>/,./page = change month;
 * arrows = move cursor; Enter = drill into day; N = new event.
 */

#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "organizer.h"
#include "bridge_client.h"

static int g_next_event_id = 1;

int events_find_by_id(int id)
{
    for (int i = 0; i < g_events_count; i++)
        if (g_events[i].id == id) return i;
    return -1;
}

static int next_event_id(void)
{
    int max = 0;
    for (int i = 0; i < g_events_count; i++)
        if (g_events[i].id > max) max = g_events[i].id;
    if (g_next_event_id <= max) g_next_event_id = max + 1;
    return g_next_event_id++;
}

int events_add(const char *title, long date)
{
    if (g_events_count >= MAX_EVENTS) return -1;
    Event *e = &g_events[g_events_count++];
    memset(e, 0, sizeof(*e));
    e->id = next_event_id();
    e->date = date ? date : g_today;
    e->time = -1;
    e->recur = RECUR_NONE;
    strncpy(e->title, title ? title : "New event", sizeof(e->title) - 1);
    e->title[sizeof(e->title) - 1] = 0;
    AB_I("event added id=%ld date=%ld title=%s", (long)e->id, e->date, e->title);
    return e->id;
}

int events_delete(int id)
{
    int idx = events_find_by_id(id);
    if (idx < 0) return -1;
    for (int i = idx; i < g_events_count - 1; i++)
        g_events[i] = g_events[i + 1];
    g_events_count--;
    return 0;
}

/* --- helpers ------------------------------------------------------ */

static int items_on(long ymd)
{
    int n = 0;
    for (int i = 0; i < g_tasks_count; i++) {
        Task *t = &g_tasks[i];
        if (t->state == ST_DONE || t->due == 0) continue;
        if (recur_fires_on(t->recur, t->due, ymd)) n++;
    }
    for (int i = 0; i < g_events_count; i++) {
        Event *e = &g_events[i];
        if (recur_fires_on(e->recur, e->date, ymd)) n++;
    }
    return n;
}

static void step_month(int delta)
{
    int y = (int)(g_cal_month / 100L);
    int m = (int)(g_cal_month % 100L);
    m += delta;
    while (m < 1)  { m += 12; y--; }
    while (m > 12) { m -= 12; y++; }
    g_cal_month = (long)y * 100L + m;
    /* Keep selected day inside the new month if possible. */
    int sd = (int)(g_cal_selected % 100L);
    int dim = ymd_days_in_month(g_cal_month);
    if (sd > dim) sd = dim;
    if (sd < 1)   sd = 1;
    g_cal_selected = g_cal_month * 100L + sd;
}

/* --- rendering ---------------------------------------------------- */

static const char *dow_labels[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };

void calendar_draw(int x0, int y0, int w, int h)
{
    /* Header line. */
    int y = (int)(g_cal_month / 100L);
    int m = (int)(g_cal_month % 100L);
    char hdr[128];
    snprintf(hdr, sizeof(hdr),
             "%s %04d   <>=month  arrows=day  Enter=drill  N)ew event   selected: %ld",
             month_name(m), y, g_cal_selected);
    ui_draw_string(x0 + 4, y0 + g_baseline, hdr, PEN_FG, PEN_BG);

    /* Grid dimensions: 7 columns, 6 rows. */
    int grid_top = y0 + g_row_h + 6;
    int drill_h = g_row_h * 8;                 /* bottom drilldown gets 8 rows */
    int grid_h  = h - g_row_h - 6 - drill_h - 4;
    if (grid_h < g_row_h * 4) { grid_h = h - g_row_h - 6 - g_row_h * 2; drill_h = g_row_h * 2; }
    int cell_w  = w / 7;
    int cell_h  = grid_h / 7;   /* 1 header row + 6 week rows */

    /* Header row: day-of-week labels. */
    for (int i = 0; i < 7; i++) {
        int cx = x0 + i * cell_w;
        ui_fill_rect(cx, grid_top, cell_w, cell_h, PEN_HL_BG);
        ui_draw_frame(cx, grid_top, cell_w, cell_h, PEN_BORDER);
        int lx = cx + (cell_w - (int)strlen(dow_labels[i]) * g_char_w) / 2;
        int ly = grid_top + (cell_h + g_baseline) / 2 - 1;
        ui_draw_string(lx, ly, dow_labels[i], PEN_HL_FG, PEN_HL_BG);
    }

    /* Compute first-day-of-month weekday and total days. */
    long first_ymd = g_cal_month * 100L + 1L;
    int first_dow = ymd_weekday(first_ymd);
    int dim = ymd_days_in_month(g_cal_month);
    int sel_d = (int)(g_cal_selected % 100L);

    for (int week = 0; week < 6; week++) {
        for (int col = 0; col < 7; col++) {
            int day = week * 7 + col - first_dow + 1;
            int cx = x0 + col * cell_w;
            int cy = grid_top + (week + 1) * cell_h;
            ui_draw_frame(cx, cy, cell_w, cell_h, PEN_BORDER);
            if (day < 1 || day > dim) {
                ui_fill_rect(cx + 1, cy + 1, cell_w - 2, cell_h - 2, PEN_BG);
                continue;
            }
            long ymd = g_cal_month * 100L + day;
            int is_sel   = (day == sel_d);
            int is_today = (ymd == g_today);
            UBYTE bg = is_sel ? PEN_HL_BG : PEN_BG;
            UBYTE fg = is_sel ? PEN_HL_FG : PEN_FG;
            ui_fill_rect(cx + 1, cy + 1, cell_w - 2, cell_h - 2, bg);
            if (is_today) {
                SetAPen(g_rp, PEN_ACTIVE);
                Move(g_rp, cx + 1, cy + 1);
                Draw(g_rp, cx + cell_w - 2, cy + 1);
                Draw(g_rp, cx + cell_w - 2, cy + cell_h - 2);
                Draw(g_rp, cx + 1, cy + cell_h - 2);
                Draw(g_rp, cx + 1, cy + 1);
            }
            char n[16];
            snprintf(n, sizeof(n), "%2d", day);
            ui_draw_string(cx + 3, cy + g_baseline + 1, n, fg, bg);
            int items = items_on(ymd);
            if (items > 0) {
                char c[16];
                snprintf(c, sizeof(c), "%d", items);
                int cw = (int)strlen(c) * g_char_w;
                ui_draw_string(cx + cell_w - cw - 3, cy + cell_h - 3, c, PEN_ACTIVE, bg);
            }
        }
    }

    /* Drilldown pane at the bottom for g_cal_selected. */
    int drill_top = grid_top + 7 * cell_h + 4;
    SetAPen(g_rp, PEN_BORDER);
    Move(g_rp, x0, drill_top - 2);
    Draw(g_rp, x0 + w - 1, drill_top - 2);

    int sy, sm, sd; ymd_split(g_cal_selected, &sy, &sm, &sd);
    char lbl[80];
    snprintf(lbl, sizeof(lbl), "Items on %s %s %d, %04d:",
             weekday_short(ymd_weekday(g_cal_selected)),
             month_name(sm), sd, sy);
    ui_draw_string(x0 + 4, drill_top + g_baseline, lbl, PEN_MUTED, PEN_BG);

    int line_y = drill_top + g_row_h + 2;
    int shown = 0;
    int max_lines = (h - (line_y - y0)) / g_row_h;
    for (int i = 0; i < g_tasks_count && shown < max_lines; i++) {
        Task *t = &g_tasks[i];
        if (t->due == 0) continue;
        if (!recur_fires_on(t->recur, t->due, g_cal_selected)) continue;
        char row[MAX_TITLE_LEN + 32];
        snprintf(row, sizeof(row), "  task [%d] P%d %s%s",
                 t->id, t->priority, t->title,
                 t->state == ST_DONE ? "  (done)" : "");
        ui_draw_string(x0 + 4, line_y + shown * g_row_h + g_baseline, row, PEN_FG, PEN_BG);
        shown++;
    }
    for (int i = 0; i < g_events_count && shown < max_lines; i++) {
        Event *e = &g_events[i];
        if (!recur_fires_on(e->recur, e->date, g_cal_selected)) continue;
        char row[MAX_TITLE_LEN + 32];
        snprintf(row, sizeof(row), "  event [%d] %s", e->id, e->title);
        ui_draw_string(x0 + 4, line_y + shown * g_row_h + g_baseline, row, PEN_FG, PEN_BG);
        shown++;
    }
    if (shown == 0) {
        ui_draw_string(x0 + 4, line_y + g_baseline,
                       "  (nothing scheduled)", PEN_MUTED, PEN_BG);
    }
}

/* --- input -------------------------------------------------------- */

int calendar_handle_key(UWORD raw)
{
    if (raw & 0x80) return 0;
    int c = raw & 0x7F;
    switch (c) {
    case 0x4C: /* Up   */ g_cal_selected = ymd_add_days(g_cal_selected, -7); break;
    case 0x4D: /* Down */ g_cal_selected = ymd_add_days(g_cal_selected,  7); break;
    case 0x4E: /* Right*/ g_cal_selected = ymd_add_days(g_cal_selected,  1); break;
    case 0x4F: /* Left */ g_cal_selected = ymd_add_days(g_cal_selected, -1); break;
    case 0x38: /* < , */
    case 0x33: /* also comma */ step_month(-1); break;
    case 0x39: /* > . */
    case 0x34: /* also period */ step_month(+1); break;
    case 0x36: /* N */ {
        char s[MAX_TITLE_LEN];
        if (prompt_string("Organizer - New event title", "", s, sizeof(s)) == 0 && *s) {
            events_add(s, g_cal_selected);
            state_touched();
            return 1;
        }
        break;
    }
    default: return 0;
    }
    /* Keep g_cal_month in sync with the selected day. */
    long ym = g_cal_selected / 100L;
    if (ym != g_cal_month) g_cal_month = ym;
    redraw_all();
    return 1;
}

int calendar_handle_click(int mx, int my)
{
    int cx, cy, cw, ch;
    ui_content_rect(&cx, &cy, &cw, &ch);
    if (mx < cx || my < cy || mx >= cx + cw || my >= cy + ch) return 0;

    int grid_top = cy + g_row_h + 6;
    int drill_h = g_row_h * 8;
    int grid_h  = ch - g_row_h - 6 - drill_h - 4;
    if (grid_h < g_row_h * 4) grid_h = ch - g_row_h - 6 - g_row_h * 2;
    int cell_w = cw / 7;
    int cell_h = grid_h / 7;
    if (my < grid_top + cell_h) return 0;      /* clicked header */

    int col = (mx - cx) / cell_w;
    int week = (my - grid_top - cell_h) / cell_h;
    if (col < 0 || col > 6 || week < 0 || week > 5) return 0;
    int first_dow = ymd_weekday(g_cal_month * 100L + 1L);
    int day = week * 7 + col - first_dow + 1;
    int dim = ymd_days_in_month(g_cal_month);
    if (day < 1 || day > dim) return 0;
    g_cal_selected = g_cal_month * 100L + day;
    redraw_all();
    return 1;
}
