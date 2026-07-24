/*
 * main.c — window + event loop + tab dispatch.
 *
 * Bridge is optional: everything works if the daemon isn't running,
 * we just miss out on remote inspection + scripting.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include "organizer.h"
#include "bridge_client.h"

#ifndef __PPC__
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;
#endif

/* --- runtime globals (declared extern in organizer.h) -------------- */

int    g_win_w   = WIN_W_INIT;
int    g_win_h   = WIN_H_INIT;
int    g_ox      = 4;   /* refreshed by recompute_layout from BorderLeft */
int    g_oy      = 12;  /* refreshed from BorderTop */
int    g_row_h   = 10;
int    g_baseline = 8;
int    g_char_w  = 8;

Tab    g_current_tab = TAB_NOTES;
LONG   g_running     = 1;
LONG   g_bridge_ok   = 0;
char   g_status[128] = "";
long   g_today       = 0;

struct Window   *g_win = NULL;
struct RastPort *g_rp  = NULL;

Note   g_notes[MAX_NOTES];
int    g_notes_count = 0;
Task   g_tasks[MAX_TASKS];
int    g_tasks_count = 0;
Event  g_events[MAX_EVENTS];
int    g_events_count = 0;

int    g_notes_cursor = 0;
int    g_notes_scroll = 0;
int    g_tasks_cursor = 0;
int    g_tasks_scroll = 0;
long   g_cal_month    = 0;
long   g_cal_selected = 0;
int    g_cal_day_view = 0;
int    g_cal_event_cursor = 0;

/* --- redraw --------------------------------------------------------- */

/* Forward-declare so redraw_all can pick up the current window dims on
 * every paint (IDCMP_REFRESHWINDOW may fire on resize without NEWSIZE
 * on OS4, so relying on the NEWSIZE handler alone leaves stale layout). */
static void recompute_layout(void);

void redraw_all(void)
{
    recompute_layout();
    ui_fill_rect(g_ox, g_oy, g_win_w, g_win_h, PEN_BG);
    ui_draw_tabs(g_ox, g_oy, g_win_w);

    int cx, cy, cw, ch;
    ui_content_rect(&cx, &cy, &cw, &ch);
    ui_draw_frame(cx - 1, cy - 1, cw + 2, ch + 2, PEN_BORDER);

    switch (g_current_tab) {
    case TAB_NOTES:    notes_draw   (cx, cy, cw, ch); break;
    case TAB_TASKS:    tasks_draw   (cx, cy, cw, ch); break;
    case TAB_CALENDAR: calendar_draw(cx, cy, cw, ch); break;
    default: break;
    }

    ui_draw_status();
}

/* Anything that mutates data goes through here — persists to disk,
 * refreshes the status counters, and repaints. Keeps the "did I save?"
 * question out of the caller's head. */
void state_touched(void)
{
    storage_save_all();
    snprintf(g_status, sizeof(g_status),
             "N:%d  T:%d  E:%d  (due today: %d)  %s",
             g_notes_count, g_tasks_count, g_events_count,
             tasks_count_due_by(g_today),
             (g_bridge_ok ? "bridge:on" : "bridge:off"));
    redraw_all();
}

/* --- layout re-cache on resize ------------------------------------- */

static void recompute_layout(void)
{
    if (!g_win) return;
    g_ox    = g_win->BorderLeft;
    g_oy    = g_win->BorderTop;
    g_win_w = g_win->Width  - g_win->BorderLeft - g_win->BorderRight;
    g_win_h = g_win->Height - g_win->BorderTop  - g_win->BorderBottom;
    if (g_win_w < WIN_W_MIN) g_win_w = WIN_W_MIN;
    if (g_win_h < WIN_H_MIN) g_win_h = WIN_H_MIN;
}

/* --- input dispatch ------------------------------------------------- */

static void handle_key(UWORD raw)
{
    if (raw & 0x80) return;   /* key-up */
    int c = raw & 0x7F;

    /* Global keys — always available regardless of tab. */
    switch (c) {
    case 0x50: /* F1 */ g_current_tab = TAB_NOTES;    redraw_all(); return;
    case 0x51: /* F2 */ g_current_tab = TAB_TASKS;    redraw_all(); return;
    case 0x52: /* F3 */ g_current_tab = TAB_CALENDAR; redraw_all(); return;
    case 0x42: /* Tab */
        g_current_tab = (Tab)((g_current_tab + 1) % TAB_COUNT);
        redraw_all();
        return;
    case 0x45: /* ESC */ g_running = 0; return;
    default: break;
    }

    /* Tab-local. */
    switch (g_current_tab) {
    case TAB_NOTES:    notes_handle_key(raw);    break;
    case TAB_TASKS:    tasks_handle_key(raw);    break;
    case TAB_CALENDAR: calendar_handle_key(raw); break;
    default: break;
    }
}

static void handle_click(int mx, int my)
{
    int tab = ui_hit_tab(mx, my);
    if (tab >= 0) {
        g_current_tab = (Tab)tab;
        redraw_all();
        return;
    }
    switch (g_current_tab) {
    case TAB_NOTES:    notes_handle_click(mx, my);    break;
    case TAB_TASKS:    tasks_handle_click(mx, my);    break;
    case TAB_CALENDAR: calendar_handle_click(mx, my); break;
    default: break;
    }
}

/* --- bridge hooks (allow remote scripting) ------------------------- */

/* Args come in as one string. For hooks that need multi-arg, split on
 * '|' (matches the storage record delimiter — familiar to anyone
 * hand-editing the data files). */

/* Hook signature: (const char *args, char *resultBuf, int bufSize).
 * We fill resultBuf with a human-readable success/error line so
 * amiga_call_hook shows something useful in the MCP response. */

static int hook_add_note(const char *args, char *out, int outsz)
{
    if (!args || !*args) { snprintf(out, outsz, "missing title"); return -1; }
    int id = notes_add(args);
    state_touched();
    if (id < 0) { snprintf(out, outsz, "full"); return -1; }
    snprintf(out, outsz, "note %d added", id);
    return 0;
}

static int hook_add_task(const char *args, char *out, int outsz)
{
    if (!args || !*args) { snprintf(out, outsz, "missing title"); return -1; }
    int id = tasks_add(args);
    state_touched();
    if (id < 0) { snprintf(out, outsz, "full"); return -1; }
    snprintf(out, outsz, "task %d added", id);
    return 0;
}

/* add_event syntax: "YYYYMMDD|title" */
static int hook_add_event(const char *args, char *out, int outsz)
{
    if (!args) { snprintf(out, outsz, "missing args"); return -1; }
    char buf[MAX_TITLE_LEN + 32];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char *bar = strchr(buf, '|');
    if (!bar) { snprintf(out, outsz, "syntax: YYYYMMDD|title"); return -1; }
    *bar = 0;
    long date = atol(buf);
    int id = events_add(bar + 1, date);
    state_touched();
    if (id < 0) { snprintf(out, outsz, "full"); return -1; }
    snprintf(out, outsz, "event %d added on %ld", id, date);
    return 0;
}

/* delete syntax: "notes|17"  "tasks|3"  "events|9" */
static int hook_delete(const char *args, char *out, int outsz)
{
    if (!args) { snprintf(out, outsz, "missing args"); return -1; }
    char buf[64];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char *bar = strchr(buf, '|');
    if (!bar) { snprintf(out, outsz, "syntax: kind|id"); return -1; }
    *bar = 0;
    int id = atoi(bar + 1);
    int rc = -1;
    if      (strcmp(buf, "notes")  == 0) rc = notes_delete(id);
    else if (strcmp(buf, "tasks")  == 0) rc = tasks_delete(id);
    else if (strcmp(buf, "events") == 0) rc = events_delete(id);
    state_touched();
    snprintf(out, outsz, rc == 0 ? "deleted %s %d" : "not found %s %d", buf, id);
    return rc;
}

/* set_task syntax: "id|state|prio|due|recur" — leave field empty to skip */
static int hook_set_task(const char *args, char *out, int outsz)
{
    if (!args) { snprintf(out, outsz, "missing args"); return -1; }
    char buf[64];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int  fields[5] = { 0, 0, 0, 0, 0 };
    long dfields[5] = { 0, 0, 0, 0, 0 };
    int fi = 0;
    char *p = buf, *q = buf;
    while (fi < 5) {
        if (*p == '|' || *p == 0) {
            char save = *p;
            *p = 0;
            if (q < p) { fields[fi] = 1; dfields[fi] = atol(q); }
            fi++;
            if (save == 0) break;
            q = p + 1;
        }
        p++;
    }
    if (!fields[0]) { snprintf(out, outsz, "id required"); return -1; }
    int id = (int)dfields[0];
    if (fields[1]) tasks_set_state   (id, (int)dfields[1]);
    if (fields[2]) tasks_set_priority(id, (int)dfields[2]);
    if (fields[3]) tasks_set_due     (id, dfields[3]);
    if (fields[4]) tasks_set_recur   (id, (int)dfields[4]);
    state_touched();
    snprintf(out, outsz, "task %d updated", id);
    return 0;
}

static int hook_test(const char *args, char *out, int outsz)
{
    (void)args;
    int rc = tests_run_all();
    snprintf(out, outsz, rc == 0 ? "tests ran (see TEST_* log)" : "tests failed");
    return rc;
}

static int hook_reload(const char *args, char *out, int outsz)
{
    (void)args;
    int n = storage_load_all();
    state_touched();
    snprintf(out, outsz, "reloaded %d records", n);
    return 0;
}

/* --- main ---------------------------------------------------------- */

int main(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary(
        (CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;
    GfxBase = (struct GfxBase *)OpenLibrary(
        (CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) { CloseLibrary((struct Library *)IntuitionBase); return 1; }

    printf("Organizer v%s\n", VERSION);
    if (ab_init("organizer") != 0) {
        printf("  Bridge: NOT FOUND (running standalone)\n");
        g_bridge_ok = 0;
    } else {
        printf("  Bridge: CONNECTED\n");
        g_bridge_ok = 1;
    }
    AB_I("Organizer v%s starting", VERSION);

    /* Register observable state so amiga_get_var / amiga_watch_status
     * work out of the box. amiga_client_info(client="organizer")
     * enumerates all of these. */
    ab_register_var("current_tab",  AB_TYPE_I32, &g_current_tab);
    ab_register_var("note_count",   AB_TYPE_I32, &g_notes_count);
    ab_register_var("task_count",   AB_TYPE_I32, &g_tasks_count);
    ab_register_var("event_count",  AB_TYPE_I32, &g_events_count);
    ab_register_var("today",        AB_TYPE_I32, &g_today);
    ab_register_var("cal_month",    AB_TYPE_I32, &g_cal_month);
    ab_register_var("cal_selected", AB_TYPE_I32, &g_cal_selected);
    ab_register_var("cal_day_view", AB_TYPE_I32, &g_cal_day_view);
    ab_register_var("running",      AB_TYPE_I32, &g_running);

    /* Register scriptable actions. amiga_call_hook drives all of these. */
    ab_register_hook("add_note",  "add note; args: title",              hook_add_note);
    ab_register_hook("add_task",  "add task; args: title",              hook_add_task);
    ab_register_hook("add_event", "add event; args: YYYYMMDD|title",    hook_add_event);
    ab_register_hook("delete",    "delete row; args: notes|id",         hook_delete);
    ab_register_hook("set_task",  "modify task; args: id|st|pr|due|rc", hook_set_task);
    ab_register_hook("test",      "run the ab_test suite",              hook_test);
    ab_register_hook("reload",    "reload S:organizer/*.txt from disk", hook_reload);

    /* Expose the underlying tables as memregions so the host can
     * inspect the raw arrays without needing a hook per field. */
    ab_register_memregion("notes",  g_notes,  sizeof(g_notes),  "Note table (bounded)");
    ab_register_memregion("tasks",  g_tasks,  sizeof(g_tasks),  "Task table (bounded)");
    ab_register_memregion("events", g_events, sizeof(g_events), "Event table (bounded)");

    if (storage_init() != 0) {
        AB_W("storage_init failed - starting with empty state");
    }
    storage_load_all();
    storage_stamp_today();
    g_cal_month = g_today / 100;   /* start on today's month */
    g_cal_selected = g_today;

    g_win = OpenWindowTags(NULL,
        WA_Left,        4,
        WA_Top,         12,
        WA_Width,       WIN_W_INIT,
        WA_Height,      WIN_H_INIT,
        WA_MinWidth,    WIN_W_MIN,
        WA_MinHeight,   WIN_H_MIN,
        WA_MaxWidth,    ~0UL,
        WA_MaxHeight,   ~0UL,
        WA_Title,       (ULONG)"Organizer - notes, tasks, calendar",
        WA_CloseGadget, TRUE,
        WA_DragBar,     TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget,  TRUE,
        WA_Activate,    TRUE,
        WA_IDCMP,       IDCMP_CLOSEWINDOW | IDCMP_RAWKEY
                      | IDCMP_MOUSEBUTTONS | IDCMP_REFRESHWINDOW
                      | IDCMP_NEWSIZE,
        TAG_DONE);

    if (!g_win) {
        AB_E("OpenWindow failed");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }
    g_rp = g_win->RPort;
    recompute_layout();

    /* Cache font metrics — makes drawing font-agnostic. */
    if (g_rp->Font) {
        int fh = g_rp->Font->tf_YSize;
        int bl = g_rp->Font->tf_Baseline;
        int cw = g_rp->TxWidth;
        if (fh > 0) g_row_h    = fh + 2;
        if (bl > 0) g_baseline = bl;
        if (cw > 0) g_char_w   = cw;
    }

    state_touched();   /* refresh status + draw first frame */

    while (g_running) {
        WaitPort(g_win->UserPort);
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(g_win->UserPort))) {
            ULONG cls  = msg->Class;
            UWORD code = msg->Code;
            WORD  mx   = msg->MouseX;
            WORD  my   = msg->MouseY;
            ReplyMsg((struct Message *)msg);

            if      (cls == IDCMP_CLOSEWINDOW)   g_running = 0;
            else if (cls == IDCMP_RAWKEY)        handle_key(code);
            else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN)
                                                 handle_click(mx, my);
            else if (cls == IDCMP_NEWSIZE) {
                recompute_layout();
                redraw_all();
            }
            else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(g_win);
                redraw_all();
                EndRefresh(g_win, TRUE);
            }
        }
        if (g_bridge_ok) ab_poll();
    }

    AB_I("Organizer shutting down");
    storage_save_all();
    CloseWindow(g_win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
