/*
 * tasks.c — Tasks tab: state / priority / due date / recur / tags.
 *
 * Row: [id] [P] due  title  (tags)  state
 * Keys: n=new, r=rename, d=delete, space=toggle done, p=priority,
 *       u=due date, e=recur, t=tags.
 */

#include <string.h>
#include <stdio.h>

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
    t->id       = next_id();
    t->state    = ST_OPEN;
    t->priority = 2;
    t->due      = 0;
    t->recur    = RECUR_NONE;
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
             "N)ew  R)ename  Space=done  P)rio  U)due  E)recur  T)ags  D)el  %d task%s",
             g_tasks_count, g_tasks_count == 1 ? "" : "s");
    ui_draw_string(x0 + 4, y0 + g_baseline, hdr, PEN_FG, PEN_BG);
    int list_top = y0 + g_row_h + 4;

    int rv = rows_visible(h - g_row_h - 4);
    if (g_tasks_cursor < g_tasks_scroll) g_tasks_scroll = g_tasks_cursor;
    if (g_tasks_cursor >= g_tasks_scroll + rv) g_tasks_scroll = g_tasks_cursor - rv + 1;

    for (int i = 0; i < rv && i + g_tasks_scroll < g_tasks_count; i++) {
        int idx = i + g_tasks_scroll;
        Task *t = &g_tasks[idx];
        char left[MAX_TITLE_LEN + 48], right[MAX_TAGS_LEN + 32];
        int y, m, d; ymd_split(t->due, &y, &m, &d);
        char due_s[16];
        if (t->due == 0) strcpy(due_s, "     -");
        else             snprintf(due_s, sizeof(due_s), "%04d-%02d-%02d", y, m, d);
        snprintf(left, sizeof(left), "[%3d] %c P%d %s  %s",
                 t->id, state_char(t->state), t->priority, due_s, t->title);
        snprintf(right, sizeof(right), "%s%s%s",
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
    case 0x36: /* N */ {
        char s[MAX_TITLE_LEN];
        if (prompt_string("Organizer - New task title", "", s, sizeof(s)) == 0 && *s) {
            tasks_add(s);
            state_touched();
            return 1;
        }
        break;
    }
    case 0x13: /* R (rename) */
        t = cur_task(); if (!t) return 0;
        {
            char s[MAX_TITLE_LEN];
            if (prompt_string("Organizer - Rename task", t->title, s, sizeof(s)) == 0 && *s) {
                strncpy(t->title, s, sizeof(t->title) - 1);
                t->title[sizeof(t->title) - 1] = 0;
                state_touched(); return 1;
            }
        }
        break;
    case 0x40: /* Space -> toggle done */
        t = cur_task(); if (!t) return 0;
        t->state = (t->state == ST_DONE) ? ST_OPEN : ST_DONE;
        state_touched(); return 1;
    case 0x19: /* P (priority) */
        t = cur_task(); if (!t) return 0;
        {
            long p; if (prompt_int("Organizer - Priority (1-3)", t->priority, &p) == 0) {
                tasks_set_priority(t->id, (int)p); state_touched(); return 1;
            }
        }
        break;
    case 0x1C: /* U (due date) */
        t = cur_task(); if (!t) return 0;
        {
            long due; if (prompt_int("Organizer - Due YYYYMMDD (0=none)", t->due ? t->due : g_today, &due) == 0) {
                tasks_set_due(t->id, due); state_touched(); return 1;
            }
        }
        break;
    case 0x12: /* E (recur) */
        t = cur_task(); if (!t) return 0;
        {
            long r;
            if (prompt_int("Recur: 0=none 1=daily 2=weekly 3=monthly 4=yearly",
                           t->recur, &r) == 0) {
                tasks_set_recur(t->id, (int)r); state_touched(); return 1;
            }
        }
        break;
    case 0x14: /* T (tags) */
        t = cur_task(); if (!t) return 0;
        {
            char s[MAX_TAGS_LEN];
            if (prompt_string("Organizer - Tags (csv)", t->tags, s, sizeof(s)) == 0) {
                strncpy(t->tags, s, sizeof(t->tags) - 1);
                t->tags[sizeof(t->tags) - 1] = 0;
                state_touched(); return 1;
            }
        }
        break;
    case 0x22: /* D (delete) */
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

int tasks_handle_click(int mx, int my)
{
    int cx, cy, cw, ch;
    ui_content_rect(&cx, &cy, &cw, &ch);
    if (mx < cx || mx >= cx + cw) return 0;
    int list_top = cy + g_row_h + 4;
    if (my < list_top) return 0;
    int row = (my - list_top) / g_row_h;
    int idx = row + g_tasks_scroll;
    if (idx >= 0 && idx < g_tasks_count) {
        g_tasks_cursor = idx;
        redraw_all();
        return 1;
    }
    return 0;
}
