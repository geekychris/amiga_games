/*
 * tasks.c — Tasks tab.
 *
 * Row: [id] state P# due-yy-mm-dd  title  (sched/effort/tags)
 * Keys:
 *   N              = new (opens editor)
 *   Enter          = edit selected (opens editor)
 *   Space          = toggle done
 *   D              = delete
 *   Up / Down      = cursor
 * Click:
 *   single-click   = select
 *   double-click   = edit
 *
 * All the per-attribute prompts (P/U/E/T) are gone — the modal
 * editor covers all fields at once. `set_task` bridge hook keeps
 * numeric setters available for scripting.
 */

#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <graphics/rastport.h>
#include <proto/dos.h>
#include <proto/graphics.h>

#include "organizer.h"
#include "bridge_client.h"

static int g_next_id = 1;

int tasks_find_by_id(int id)
{
    for (int i = 0; i < g_tasks_count; i++)
        if (g_tasks[i].id == id) return i;
    return -1;
}

static int next_id(void)
{
    int max = 0;
    for (int i = 0; i < g_tasks_count; i++)
        if (g_tasks[i].id > max) max = g_tasks[i].id;
    if (g_next_id <= max) g_next_id = max + 1;
    return g_next_id++;
}

int tasks_add(const char *title)
{
    if (g_tasks_count >= MAX_TASKS) return -1;
    Task *t = &g_tasks[g_tasks_count++];
    memset(t, 0, sizeof(*t));
    t->id              = next_id();
    t->state           = ST_OPEN;
    t->priority        = 2;
    t->due             = 0;
    t->recur           = RECUR_NONE;
    t->effort_min      = 0;
    t->scheduled_date  = 0;
    t->scheduled_start = -1;
    strncpy(t->title, title ? title : "New task", sizeof(t->title) - 1);
    t->title[sizeof(t->title) - 1] = 0;
    g_tasks_cursor = g_tasks_count - 1;
    AB_I("task added id=%ld title=%s", (long)t->id, t->title);
    return t->id;
}

int tasks_delete(int id)
{
    int idx = tasks_find_by_id(id);
    if (idx < 0) return -1;
    for (int i = idx; i < g_tasks_count - 1; i++)
        g_tasks[i] = g_tasks[i + 1];
    g_tasks_count--;
    if (g_tasks_cursor >= g_tasks_count && g_tasks_cursor > 0) g_tasks_cursor--;
    return 0;
}

int tasks_set_state(int id, int state)
{
    int idx = tasks_find_by_id(id);
    if (idx < 0) return -1;
    if (state < ST_OPEN || state > ST_DONE) return -1;
    g_tasks[idx].state = state;
    return 0;
}

int tasks_set_priority(int id, int prio)
{
    int idx = tasks_find_by_id(id);
    if (idx < 0) return -1;
    if (prio < 1) prio = 1;
    if (prio > 3) prio = 3;
    g_tasks[idx].priority = prio;
    return 0;
}

int tasks_set_due(int id, long ymd)
{
    int idx = tasks_find_by_id(id);
    if (idx < 0) return -1;
    g_tasks[idx].due = ymd;
    return 0;
}

int tasks_set_recur(int id, int recur)
{
    int idx = tasks_find_by_id(id);
    if (idx < 0) return -1;
    if (recur < RECUR_NONE || recur > RECUR_YEARLY) return -1;
    g_tasks[idx].recur = recur;
    return 0;
}

int tasks_count_due_by(long ymd)
{
    int n = 0;
    for (int i = 0; i < g_tasks_count; i++) {
        Task *t = &g_tasks[i];
        if (t->state == ST_DONE) continue;
        if (t->due == 0) continue;
        if (t->recur != RECUR_NONE) {
            if (recur_fires_on(t->recur, t->due, ymd) ||
                recur_fires_on(t->recur, t->due, ymd_add_days(ymd, -1))) n++;
        } else if (t->due <= ymd) {
            n++;
        }
    }
    return n;
}

/* --- editing --------------------------------------------------- */

static int edit_selected(void)
{
    if (g_tasks_cursor < 0 || g_tasks_cursor >= g_tasks_count) return 0;
    Task tmp = g_tasks[g_tasks_cursor];
    if (task_dialog_run(&tmp)) {
        g_tasks[g_tasks_cursor] = tmp;
        snprintf(g_status, sizeof(g_status),
                 "task %d '%s' saved  state=%d prio=%d due=%ld eff=%dm sched=%ld",
                 tmp.id, tmp.title, tmp.state, tmp.priority,
                 tmp.due, tmp.effort_min, tmp.scheduled_date);
        state_touched();
        return 1;
    }
    return 0;
}

/* --- rendering ---------------------------------------------------- */

static char state_char(int s)
{
    switch (s) { case ST_OPEN: return '.'; case ST_DOING: return '>'; case ST_DONE: return 'x'; }
    return '?';
}

static int rows_visible(int h) { int r = (h - g_row_h - 4) / g_row_h; return r < 1 ? 1 : r; }

void tasks_draw(int x0, int y0, int w, int h)
{
    char hdr[128];
    snprintf(hdr, sizeof(hdr),
             "N)ew  Enter=edit  Space=done  D)elete  %d task%s",
             g_tasks_count, g_tasks_count == 1 ? "" : "s");
    ui_draw_string(x0 + 4, y0 + g_baseline, hdr, PEN_FG, PEN_BG);
    int list_top = y0 + g_row_h + 4;

    int rv = rows_visible(h - g_row_h - 4);
    if (g_tasks_cursor < g_tasks_scroll) g_tasks_scroll = g_tasks_cursor;
    if (g_tasks_cursor >= g_tasks_scroll + rv) g_tasks_scroll = g_tasks_cursor - rv + 1;

    for (int i = 0; i < rv && i + g_tasks_scroll < g_tasks_count; i++) {
        int idx = i + g_tasks_scroll;
        Task *t = &g_tasks[idx];
        char left[MAX_TITLE_LEN + 64], right[MAX_TAGS_LEN + 48];
        int y, m, d; ymd_split(t->due, &y, &m, &d);
        char due_s[16];
        if (t->due == 0) strcpy(due_s, "     -");
        else             snprintf(due_s, sizeof(due_s), "%04d-%02d-%02d", y, m, d);
        snprintf(left, sizeof(left), "[%3d] %c P%d %s  %s",
                 t->id, state_char(t->state), t->priority, due_s, t->title);
        /* Right side: recur / sched / effort / tags. */
        char sched_s[32] = "";
        if (t->scheduled_date) {
            int sy, sm, sd; ymd_split(t->scheduled_date, &sy, &sm, &sd);
            if (t->scheduled_start >= 0)
                snprintf(sched_s, sizeof(sched_s), "@%02d/%02d %02d:%02d ",
                         sm, sd, t->scheduled_start / 100, t->scheduled_start % 100);
            else
                snprintf(sched_s, sizeof(sched_s), "@%02d/%02d ", sm, sd);
        }
        char effort_s[16] = "";
        if (t->effort_min > 0) snprintf(effort_s, sizeof(effort_s), "%dm ", t->effort_min);
        snprintf(right, sizeof(right), "%s%s%s%s%s",
                 sched_s,
                 effort_s,
                 t->recur ? recur_name(t->recur) : "",
                 (t->recur && t->tags[0]) ? " " : "",
                 t->tags);
        ui_draw_row(x0, list_top + i * g_row_h, w, left, right, idx == g_tasks_cursor);
    }
}

/* --- input -------------------------------------------------------- */

static Task *cur_task(void)
{
    if (g_tasks_cursor < 0 || g_tasks_cursor >= g_tasks_count) return NULL;
    return &g_tasks[g_tasks_cursor];
}

int tasks_handle_key(UWORD raw)
{
    if (raw & 0x80) return 0;
    int c = raw & 0x7F;
    Task *t;
    switch (c) {
    case 0x4C: /* Up   */ if (g_tasks_cursor > 0)                 g_tasks_cursor--; break;
    case 0x4D: /* Down */ if (g_tasks_cursor < g_tasks_count - 1) g_tasks_cursor++; break;
    case 0x44: /* Enter -> edit */
        return edit_selected() ? 1 : 0;
    case 0x36: /* N -> new + edit */ {
        int id = tasks_add("New task");
        if (id < 0) return 0;
        edit_selected();
        state_touched();
        return 1;
    }
    case 0x40: /* Space -> toggle done */
        t = cur_task(); if (!t) return 0;
        t->state = (t->state == ST_DONE) ? ST_OPEN : ST_DONE;
        state_touched(); return 1;
    case 0x22: /* D delete */
        t = cur_task(); if (!t) return 0;
        if (confirm("Delete task [%s] ?", t->title)) {
            tasks_delete(t->id); state_touched(); return 1;
        }
        break;
    default: return 0;
    }
    redraw_all();
    return 1;
}

/* Double-click state — matches notes.c and calendar.c convention. */
static int              last_click_row = -1;
static struct DateStamp last_click_ts  = { 0, 0, 0 };
static LONG ds_delta_ms(const struct DateStamp *a, const struct DateStamp *b)
{
    LONG dm = (b->ds_Minute - a->ds_Minute) * 60000L;
    LONG dt = (b->ds_Tick   - a->ds_Tick)   * 20L;
    return dm + dt;
}

int tasks_handle_click(int mx, int my)
{
    int cx, cy, cw, ch;
    ui_content_rect(&cx, &cy, &cw, &ch);
    if (mx < cx || mx >= cx + cw) return 0;
    int list_top = cy + g_row_h + 4;
    if (my < list_top) return 0;
    int row = (my - list_top) / g_row_h;
    int idx = row + g_tasks_scroll;
    if (idx < 0 || idx >= g_tasks_count) return 0;

    g_tasks_cursor = idx;
    struct DateStamp now; DateStamp(&now);
    int is_double =
        (idx == last_click_row &&
         ds_delta_ms(&last_click_ts, &now) < 500);
    last_click_row = idx;
    last_click_ts  = now;
    if (is_double) edit_selected();
    else           redraw_all();
    return 1;
}
