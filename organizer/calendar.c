/*
 * calendar.c — Calendar tab: month grid, hourly day view, rich event editor.
 *
 * Two view modes toggled with V:
 *   Month view: 7x6 grid, per-cell item count, click/arrow to select day.
 *   Day view:   hourly grid (DAY_HOUR_START..DAY_HOUR_END) with events
 *               rendered as time-block rectangles. Click an empty hour to
 *               create; click an event to edit.
 *
 * Event editor (events_edit_interactive) walks the user through a
 * sequence of CON: prompts — date, start/end times, recur, tags,
 * attendees, url, notes. Any prompt cancelled (empty on a required
 * field) aborts the whole edit.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

int events_add_full(const char *title, long date, int start, int end,
                    int recur, const char *tags, const char *attendees,
                    const char *url, const char *notes)
{
    if (g_events_count >= MAX_EVENTS) return -1;
    Event *e = &g_events[g_events_count++];
    memset(e, 0, sizeof(*e));
    e->id = next_event_id();
    e->date       = date ? date : g_today;
    e->start_time = start;
    e->end_time   = end;
    e->recur      = recur;
    if (title)     { strncpy(e->title,     title,     sizeof(e->title)-1);     }
    if (tags)      { strncpy(e->tags,      tags,      sizeof(e->tags)-1);      }
    if (attendees) { strncpy(e->attendees, attendees, sizeof(e->attendees)-1); }
    if (url)       { strncpy(e->url,       url,       sizeof(e->url)-1);       }
    if (notes)     { strncpy(e->notes,     notes,     sizeof(e->notes)-1);     }
    AB_I("event added id=%ld date=%ld start=%ld title=%s",
         (long)e->id, e->date, (long)e->start_time, e->title);
    return e->id;
}

int events_add(const char *title, long date)
{
    return events_add_full(title, date, -1, -1, RECUR_NONE, "", "", "", "");
}

int events_delete(int id)
{
    int idx = events_find_by_id(id);
    if (idx < 0) return -1;
    for (int i = idx; i < g_events_count - 1; i++)
        g_events[i] = g_events[i + 1];
    g_events_count--;
    if (g_cal_event_cursor >= g_events_count && g_cal_event_cursor > 0)
        g_cal_event_cursor--;
    return 0;
}

/* --- helpers ------------------------------------------------------ */

/* Counts anything landing on `ymd`: due tasks, scheduled tasks, and
 * events. Skips done tasks. Scheduled + due may both fire for the same
 * task — we still count once (the by-due branch does the work). */
static int items_on(long ymd)
{
    int n = 0;
    for (int i = 0; i < g_tasks_count; i++) {
        Task *t = &g_tasks[i];
        if (t->state == ST_DONE) continue;
        int matched = 0;
        if (t->due && recur_fires_on(t->recur, t->due, ymd))              matched = 1;
        if (!matched && t->scheduled_date == ymd)                          matched = 1;
        if (matched) n++;
    }
    for (int i = 0; i < g_events_count; i++) {
        Event *e = &g_events[i];
        if (recur_fires_on(e->recur, e->date, ymd)) n++;
    }
    return n;
}

int events_on_day(long ymd, int *out)
{
    int n = 0;
    for (int i = 0; i < g_events_count && n < MAX_EVENTS; i++) {
        if (recur_fires_on(g_events[i].recur, g_events[i].date, ymd))
            out[n++] = i;
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
    int sd = (int)(g_cal_selected % 100L);
    int dim = ymd_days_in_month(g_cal_month);
    if (sd > dim) sd = dim;
    if (sd < 1)   sd = 1;
    g_cal_selected = g_cal_month * 100L + sd;
}

static void step_day(int delta)
{
    g_cal_selected = ymd_add_days(g_cal_selected, delta);
    long ym = g_cal_selected / 100L;
    if (ym != g_cal_month) g_cal_month = ym;
    g_cal_event_cursor = 0;
}

static void fmt_hhmm(int hhmm, char *out, size_t osz)
{
    if (hhmm < 0) { snprintf(out, osz, "--:--"); return; }
    snprintf(out, osz, "%02d:%02d", hhmm / 100, hhmm % 100);
}

static int hhmm_to_minutes(int hhmm)
{
    if (hhmm < 0) return -1;
    return (hhmm / 100) * 60 + (hhmm % 100);
}

/* --- rendering: month view --------------------------------------- */

static const char *dow_labels[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };

static void calendar_draw_month(int x0, int y0, int w, int h)
{
    int y = (int)(g_cal_month / 100L);
    int m = (int)(g_cal_month % 100L);
    char hdr[128];
    snprintf(hdr, sizeof(hdr),
             "%s %04d  V=day view  <>=month  arrows=day  N=event  Enter=drill",
             month_name(m), y);
    ui_draw_string(x0 + 4, y0 + g_baseline, hdr, PEN_FG, PEN_BG);

    int grid_top = y0 + g_row_h + 6;
    int drill_h  = g_row_h * 8;
    int grid_h   = h - g_row_h - 6 - drill_h - 4;
    if (grid_h < g_row_h * 4) { grid_h = h - g_row_h - 6 - g_row_h * 2; drill_h = g_row_h * 2; }
    int cell_w = w / 7;
    int cell_h = grid_h / 7;

    for (int i = 0; i < 7; i++) {
        int cx = x0 + i * cell_w;
        ui_fill_rect(cx, grid_top, cell_w, cell_h, PEN_HL_BG);
        ui_draw_frame(cx, grid_top, cell_w, cell_h, PEN_BORDER);
        int lx = cx + (cell_w - (int)strlen(dow_labels[i]) * g_char_w) / 2;
        int ly = grid_top + (cell_h + g_baseline) / 2 - 1;
        ui_draw_string(lx, ly, dow_labels[i], PEN_HL_FG, PEN_HL_BG);
    }

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
                int ww = (int)strlen(c) * g_char_w;
                ui_draw_string(cx + cell_w - ww - 3, cy + cell_h - 3, c, PEN_ACTIVE, bg);
            }
        }
    }

    /* Drilldown at the bottom for the selected day. */
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
        int by_due   = (t->due && recur_fires_on(t->recur, t->due, g_cal_selected));
        int by_sched = (t->scheduled_date == g_cal_selected);
        if (!by_due && !by_sched) continue;
        char row[MAX_TITLE_LEN + 48];
        const char *why = by_sched ? "sched" : "due";
        snprintf(row, sizeof(row), "  task [%d] P%d %s  (%s%s)",
                 t->id, t->priority, t->title, why,
                 t->state == ST_DONE ? " done" : "");
        ui_draw_string(x0 + 4, line_y + shown * g_row_h + g_baseline, row, PEN_FG, PEN_BG);
        shown++;
    }
    for (int i = 0; i < g_events_count && shown < max_lines; i++) {
        Event *e = &g_events[i];
        if (!recur_fires_on(e->recur, e->date, g_cal_selected)) continue;
        char st[8], et[8], row[MAX_TITLE_LEN + 48];
        fmt_hhmm(e->start_time, st, sizeof(st));
        fmt_hhmm(e->end_time,   et, sizeof(et));
        snprintf(row, sizeof(row), "  event [%d] %s-%s %s", e->id, st, et, e->title);
        ui_draw_string(x0 + 4, line_y + shown * g_row_h + g_baseline, row, PEN_FG, PEN_BG);
        shown++;
    }
    if (shown == 0) {
        ui_draw_string(x0 + 4, line_y + g_baseline,
                       "  (nothing scheduled)", PEN_MUTED, PEN_BG);
    }
}

/* --- rendering: day view ----------------------------------------- */

/* Cached hit-test rects for day-view rows, computed at draw-time and
 * consumed by the click handler. */
#define DAY_MAX_HOURS  (DAY_HOUR_END - DAY_HOUR_START)
static struct {
    int y, h;
    int hour;
} g_day_rows[DAY_MAX_HOURS];
static int g_day_grid_x0, g_day_grid_w, g_day_labels_w;

/* Cached hit-test rects for event blocks (up to MAX_EVENTS per day). */
static struct {
    int x, y, w, h;
    int ev_idx;
} g_day_events[MAX_EVENTS];
static int g_day_events_count;

static void calendar_draw_day(int x0, int y0, int w, int h)
{
    int sy, sm, sd; ymd_split(g_cal_selected, &sy, &sm, &sd);
    char hdr[128];
    snprintf(hdr, sizeof(hdr),
             "%s %s %d, %04d  V=month  <>=day  N=event  E=edit  D=delete",
             weekday_short(ymd_weekday(g_cal_selected)),
             month_name(sm), sd, sy);
    ui_draw_string(x0 + 4, y0 + g_baseline, hdr, PEN_FG, PEN_BG);

    /* Top strip: all-day events (start_time < 0). Grows vertically so
     * multiple all-day events are visible; also lists timed events'
     * ids for a quick summary if the grid is too cramped. */
    int strip_y = y0 + g_row_h + 4;
    int allday_count = 0;
    for (int i = 0; i < g_events_count; i++) {
        Event *e = &g_events[i];
        if (e->start_time >= 0) continue;
        if (!recur_fires_on(e->recur, e->date, g_cal_selected)) continue;
        allday_count++;
    }
    int strip_h = g_row_h * (1 + (allday_count > 0 ? allday_count : 1)) + 4;
    ui_fill_rect(x0, strip_y, w, strip_h, PEN_BG);
    ui_draw_frame(x0, strip_y, w, strip_h, PEN_BORDER);
    ui_draw_string(x0 + 4, strip_y + g_baseline,
                   allday_count ? "All day:" : "All day: (none)",
                   PEN_MUTED, PEN_BG);
    int allday_line = 0;
    for (int i = 0; i < g_events_count; i++) {
        Event *e = &g_events[i];
        if (e->start_time >= 0) continue;
        if (!recur_fires_on(e->recur, e->date, g_cal_selected)) continue;
        char row[MAX_TITLE_LEN + 16];
        snprintf(row, sizeof(row), "  [%d] %s", e->id, e->title);
        ui_draw_string(x0 + 4,
                       strip_y + g_row_h + allday_line * g_row_h + g_baseline,
                       row, PEN_FG, PEN_BG);
        allday_line++;
    }

    /* Grid below strip: DAY_MAX_HOURS rows sharing the remaining height. */
    int grid_top = strip_y + strip_h + 4;
    int grid_h   = h - (grid_top - y0) - g_row_h - 4;
    if (grid_h < DAY_MAX_HOURS * 8) grid_h = DAY_MAX_HOURS * 8;
    int row_h    = grid_h / DAY_MAX_HOURS;
    if (row_h < 8) row_h = 8;

    int labels_w = 6 * g_char_w;
    int gx0 = x0 + labels_w;
    int gw  = w - labels_w - 2;

    g_day_grid_x0  = gx0;
    g_day_grid_w   = gw;
    g_day_labels_w = labels_w;
    g_day_events_count = 0;

    /* Hour rows. */
    for (int i = 0; i < DAY_MAX_HOURS; i++) {
        int hour = DAY_HOUR_START + i;
        int ry = grid_top + i * row_h;
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%02d:00", hour);
        ui_draw_string(x0, ry + g_baseline, lbl, PEN_MUTED, PEN_BG);
        ui_draw_frame(gx0, ry, gw, row_h, PEN_BORDER);
        g_day_rows[i].y    = ry;
        g_day_rows[i].h    = row_h;
        g_day_rows[i].hour = hour;
    }

    /* Scheduled tasks render as time-blocks alongside events (drawn
     * first so events overlay them if there's overlap). Different
     * pen so users can tell them apart at a glance. */
    for (int i = 0; i < g_tasks_count; i++) {
        Task *t = &g_tasks[i];
        if (t->state == ST_DONE) continue;
        if (t->scheduled_date != g_cal_selected) continue;
        if (t->scheduled_start < 0) continue;   /* all-day scheduled - skip grid, still shows in header */
        int sm2 = hhmm_to_minutes(t->scheduled_start);
        int em2 = sm2 + (t->effort_min > 0 ? t->effort_min : 30);
        int base_m = DAY_HOUR_START * 60;
        int span_m = DAY_MAX_HOURS * 60;
        int sm3 = sm2 - base_m;
        int em3 = em2 - base_m;
        if (em3 <= 0 || sm3 >= span_m) continue;
        if (sm3 < 0) sm3 = 0;
        if (em3 > span_m) em3 = span_m;
        int by = grid_top + (sm3 * row_h) / 60;
        int bh = ((em3 - sm3) * row_h) / 60;
        if (bh < 6) bh = 6;
        ui_fill_rect(gx0 + 2, by + 1, gw - 4, bh - 2, PEN_MUTED);
        ui_draw_frame(gx0 + 2, by + 1, gw - 4, bh - 2, PEN_BORDER);
        char lbl[MAX_TITLE_LEN + 32];
        snprintf(lbl, sizeof(lbl), "TASK [%d] P%d  %s (%dm)",
                 t->id, t->priority, t->title, t->effort_min);
        ui_draw_string(gx0 + 6, by + g_baseline + 2, lbl, PEN_FG, PEN_MUTED);
    }

    /* Event blocks. Overlay on the grid; last-drawn wins (fine for
     * visual — hit-test iterates blocks in reverse so top-most gets
     * the click). */
    for (int i = 0; i < g_events_count; i++) {
        Event *e = &g_events[i];
        if (e->start_time < 0) continue;
        if (!recur_fires_on(e->recur, e->date, g_cal_selected)) continue;
        int sm2 = hhmm_to_minutes(e->start_time);
        int em2 = hhmm_to_minutes(e->end_time);
        if (em2 <= sm2) em2 = sm2 + 60;   /* default 1h */
        int base_m = DAY_HOUR_START * 60;
        int span_m = DAY_MAX_HOURS * 60;
        int sm3 = sm2 - base_m;
        int em3 = em2 - base_m;
        if (em3 <= 0 || sm3 >= span_m) continue;   /* out of view */
        if (sm3 < 0) sm3 = 0;
        if (em3 > span_m) em3 = span_m;
        int by = grid_top + (sm3 * row_h) / 60;
        int bh = ((em3 - sm3) * row_h) / 60;
        if (bh < 6) bh = 6;
        int selected = (g_day_events_count == g_cal_event_cursor);
        UBYTE bg = selected ? PEN_ACTIVE : PEN_HL_BG;
        UBYTE fg = selected ? PEN_HL_FG  : PEN_HL_FG;
        ui_fill_rect(gx0 + 2, by + 1, gw - 4, bh - 2, bg);
        ui_draw_frame(gx0 + 2, by + 1, gw - 4, bh - 2, PEN_BORDER);
        char st[8], et[8], lbl[MAX_TITLE_LEN + 24];
        fmt_hhmm(e->start_time, st, sizeof(st));
        fmt_hhmm(e->end_time,   et, sizeof(et));
        snprintf(lbl, sizeof(lbl), "%s-%s [%d] %s", st, et, e->id, e->title);
        ui_draw_string(gx0 + 6, by + g_baseline + 2, lbl, fg, bg);
        /* Second line if space: attendees / url snippet. */
        if (bh > row_h + 2) {
            char sub[MAX_ATTENDEES_LEN + 8];
            if (e->attendees[0])
                snprintf(sub, sizeof(sub), "attend: %s", e->attendees);
            else if (e->url[0])
                snprintf(sub, sizeof(sub), "url: %s", e->url);
            else
                sub[0] = 0;
            if (sub[0])
                ui_draw_string(gx0 + 6, by + row_h + g_baseline + 2, sub, fg, bg);
        }
        if (g_day_events_count < MAX_EVENTS) {
            g_day_events[g_day_events_count].x      = gx0 + 2;
            g_day_events[g_day_events_count].y      = by + 1;
            g_day_events[g_day_events_count].w      = gw - 4;
            g_day_events[g_day_events_count].h      = bh - 2;
            g_day_events[g_day_events_count].ev_idx = i;
            g_day_events_count++;
        }
    }

    /* Detail pane: full record for the currently-selected event.
     * Surfaces attendees/URL/notes that don't fit in the block. */
    int detail_top = y0 + h - g_row_h * 5 - 2;
    if (detail_top < grid_top + DAY_MAX_HOURS * row_h + 4)
        detail_top = grid_top + DAY_MAX_HOURS * row_h + 4;
    int detail_h = y0 + h - detail_top - 2;
    if (detail_h < g_row_h * 5) detail_h = g_row_h * 5;
    ui_fill_rect(x0, detail_top, w, detail_h, PEN_BG);
    ui_draw_frame(x0, detail_top, w, detail_h, PEN_BORDER);

    if (g_cal_event_cursor >= 0 && g_cal_event_cursor < g_day_events_count) {
        Event *e = &g_events[g_day_events[g_cal_event_cursor].ev_idx];
        char st[8], et[8];
        fmt_hhmm(e->start_time, st, sizeof(st));
        fmt_hhmm(e->end_time,   et, sizeof(et));
        char line[256];
        int ly = detail_top + 2;

        snprintf(line, sizeof(line), "[%d] %s   (%s-%s  %s)",
                 e->id, e->title, st, et, recur_name(e->recur));
        ui_draw_string(x0 + 4, ly + g_baseline, line, PEN_FG, PEN_BG);
        ly += g_row_h;
        snprintf(line, sizeof(line), "Attendees: %s",
                 e->attendees[0] ? e->attendees : "(none)");
        ui_draw_string(x0 + 4, ly + g_baseline, line, PEN_MUTED, PEN_BG);
        ly += g_row_h;
        snprintf(line, sizeof(line), "URL:       %s",
                 e->url[0] ? e->url : "(none)");
        ui_draw_string(x0 + 4, ly + g_baseline, line, PEN_MUTED, PEN_BG);
        ly += g_row_h;
        snprintf(line, sizeof(line), "Tags:      %s",
                 e->tags[0] ? e->tags : "(none)");
        ui_draw_string(x0 + 4, ly + g_baseline, line, PEN_MUTED, PEN_BG);
        ly += g_row_h;
        snprintf(line, sizeof(line), "Notes:     %s",
                 e->notes[0] ? e->notes : "(none)");
        ui_draw_string(x0 + 4, ly + g_baseline, line, PEN_MUTED, PEN_BG);
    } else {
        ui_draw_string(x0 + 4, detail_top + g_baseline,
                       g_day_events_count
                       ? "Select an event (Up/Down) to see its details"
                       : "No events today - press N or click an hour to add one",
                       PEN_MUTED, PEN_BG);
    }
}

/* --- event editor ------------------------------------------------ */

/* Modal event editor. Opens a proper Intuition dialog (StringGadgets
 * for every field + OK/Cancel buttons) instead of the old sequential
 * CON: prompts. If id < 0 creates a new event pre-filled with
 * default_date and default_start. Returns 0 on OK, -1 on Cancel. */
int events_edit_interactive(int id, long default_date, int default_start)
{
    int idx = (id < 0) ? -1 : events_find_by_id(id);
    Event tmp;
    if (idx >= 0) {
        tmp = g_events[idx];
    } else {
        memset(&tmp, 0, sizeof(tmp));
        tmp.date       = default_date ? default_date : g_today;
        tmp.start_time = default_start;
        tmp.end_time   = (default_start >= 0) ? default_start + 100 : -1;
        tmp.recur      = RECUR_NONE;
        strncpy(tmp.title, "New event", sizeof(tmp.title) - 1);
    }

    if (!event_dialog_run(&tmp)) return -1;
    if (!*tmp.title) return -1;    /* empty title = cancel */

    int new_id;
    if (idx < 0) {
        new_id = events_add_full(tmp.title, tmp.date,
                                 tmp.start_time, tmp.end_time, tmp.recur,
                                 tmp.tags, tmp.attendees, tmp.url, tmp.notes);
    } else {
        g_events[idx] = tmp;
        new_id = tmp.id;
    }
    /* Sync calendar to the event's actual date so a surprising date
     * entry is immediately visible. */
    g_cal_selected = (tmp.date >= 19700000L && tmp.date <= 22001231L) ? tmp.date : g_today;
    g_cal_month    = g_cal_selected / 100L;
    snprintf(g_status, sizeof(g_status),
             "event %d '%s' saved  date=%ld  %02d:%02d-%02d:%02d  %s",
             new_id, tmp.title, g_cal_selected,
             tmp.start_time >= 0 ? tmp.start_time / 100 : 0,
             tmp.start_time >= 0 ? tmp.start_time % 100 : 0,
             tmp.end_time   >= 0 ? tmp.end_time   / 100 : 0,
             tmp.end_time   >= 0 ? tmp.end_time   % 100 : 0,
             recur_name(tmp.recur));
    return 0;
}

/* --- dispatch ---------------------------------------------------- */

void calendar_draw(int x0, int y0, int w, int h)
{
    if (g_cal_day_view) calendar_draw_day  (x0, y0, w, h);
    else                calendar_draw_month(x0, y0, w, h);
}

static int selected_event_id(void)
{
    if (g_cal_event_cursor < 0 || g_cal_event_cursor >= g_day_events_count) return -1;
    return g_events[g_day_events[g_cal_event_cursor].ev_idx].id;
}

int calendar_handle_key(UWORD raw)
{
    if (raw & 0x80) return 0;
    int c = raw & 0x7F;

    /* Universal keys (both views). */
    switch (c) {
    case 0x35: /* V toggle view */
        g_cal_day_view ^= 1;
        g_cal_event_cursor = 0;
        redraw_all();
        return 1;
    case 0x17: /* T = today */
        g_cal_selected = g_today;
        g_cal_month = g_today / 100L;
        redraw_all();
        return 1;
    default: break;
    }

    if (g_cal_day_view) {
        switch (c) {
        case 0x4C: /* Up */
            if (g_cal_event_cursor > 0) g_cal_event_cursor--; break;
        case 0x4D: /* Down */
            if (g_cal_event_cursor < g_day_events_count - 1) g_cal_event_cursor++; break;
        case 0x4E: /* Right */
        case 0x39: /* > . */
        case 0x34: step_day( 1); break;
        case 0x4F: /* Left */
        case 0x38: /* < , */
        case 0x33: step_day(-1); break;
        case 0x36: /* N new */
            events_edit_interactive(-1, g_cal_selected, -1);
            state_touched();
            return 1;
        case 0x12: /* E edit */
        case 0x44: /* Enter */ {
            int id = selected_event_id();
            if (id > 0) {
                events_edit_interactive(id, g_cal_selected, -1);
                state_touched();
                return 1;
            }
            break;
        }
        case 0x22: /* D delete */ {
            int id = selected_event_id();
            if (id > 0 && confirm("Delete event [%d] ?", NULL)) {
                events_delete(id);
                state_touched();
                return 1;
            }
            break;
        }
        default: return 0;
        }
        redraw_all();
        return 1;
    }

    /* Month view. */
    switch (c) {
    case 0x4C: /* Up */    g_cal_selected = ymd_add_days(g_cal_selected, -7); break;
    case 0x4D: /* Down */  g_cal_selected = ymd_add_days(g_cal_selected,  7); break;
    case 0x4E: /* Right */ g_cal_selected = ymd_add_days(g_cal_selected,  1); break;
    case 0x4F: /* Left */  g_cal_selected = ymd_add_days(g_cal_selected, -1); break;
    case 0x38: /* < , */
    case 0x33: step_month(-1); break;
    case 0x39: /* > . */
    case 0x34: step_month(+1); break;
    case 0x36: /* N */
        events_edit_interactive(-1, g_cal_selected, -1);
        state_touched();
        return 1;
    case 0x44: /* Enter drills into day view */
        g_cal_day_view = 1;
        g_cal_event_cursor = 0;
        redraw_all();
        return 1;
    default: return 0;
    }
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

    if (g_cal_day_view) {
        /* Try event blocks first — top-most (last-drawn) wins. */
        for (int i = g_day_events_count - 1; i >= 0; i--) {
            int ex = g_day_events[i].x, ey = g_day_events[i].y;
            int ew = g_day_events[i].w, eh = g_day_events[i].h;
            if (mx >= ex && mx < ex + ew && my >= ey && my < ey + eh) {
                g_cal_event_cursor = i;
                int id = g_events[g_day_events[i].ev_idx].id;
                events_edit_interactive(id, g_cal_selected, -1);
                state_touched();
                return 1;
            }
        }
        /* Otherwise, click on an hour row → new event at that hour. */
        for (int i = 0; i < DAY_MAX_HOURS; i++) {
            if (mx < g_day_grid_x0 || mx >= g_day_grid_x0 + g_day_grid_w) continue;
            if (my >= g_day_rows[i].y && my < g_day_rows[i].y + g_day_rows[i].h) {
                int hh = g_day_rows[i].hour * 100;
                events_edit_interactive(-1, g_cal_selected, hh);
                state_touched();
                return 1;
            }
        }
        return 0;
    }

    /* Month view: click a cell = select + drill into day view. Users
     * expected drill on single click (Enter also works). */
    int grid_top = cy + g_row_h + 6;
    int drill_h = g_row_h * 8;
    int grid_h  = ch - g_row_h - 6 - drill_h - 4;
    if (grid_h < g_row_h * 4) grid_h = ch - g_row_h - 6 - g_row_h * 2;
    int cell_w = cw / 7;
    int cell_h = grid_h / 7;
    if (my < grid_top + cell_h) return 0;
    int col = (mx - cx) / cell_w;
    int week = (my - grid_top - cell_h) / cell_h;
    if (col < 0 || col > 6 || week < 0 || week > 5) return 0;
    int first_dow = ymd_weekday(g_cal_month * 100L + 1L);
    int day = week * 7 + col - first_dow + 1;
    int dim = ymd_days_in_month(g_cal_month);
    if (day < 1 || day > dim) return 0;
    g_cal_selected = g_cal_month * 100L + day;
    g_cal_day_view = 1;      /* single click drills straight into day view */
    g_cal_event_cursor = 0;
    redraw_all();
    return 1;
}
