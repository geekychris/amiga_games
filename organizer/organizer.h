/*
 * Organizer — notes, tasks, calendar for AmigaOS.
 *
 * Three-tab single-window app. Each tab is its own subsystem:
 *   notes.c    — notes list (title + body + tags)
 *   tasks.c    — tasks list (state + priority + due + recur + tags)
 *   calendar.c — month grid + day drilldown (aggregates tasks/events)
 *
 * Storage lives in S:organizer/  as line-based pipe-delimited
 * records. Format is stable + human-editable; format doc lives at the
 * top of storage.c so future you (or another app) can hand-edit safely.
 *
 * Bridge integration: registers vars for observable state and hooks
 * for scripted actions (add_note, add_task, add_event, delete, test).
 * See tests.c for the ab_test suite exposed via the `test` hook.
 */

#ifndef ORGANIZER_H
#define ORGANIZER_H

#include <stddef.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <dos/dos.h>

/* OS4 obsolete-shim block (same pattern used in masterwork). */
#ifdef __PPC__
#define Examine    OBSOLETEExamine
#define ExNext     OBSOLETEExNext
#define DeleteFile Delete
#define ACCESS_READ SHARED_LOCK
#define Flush      FFlush
#endif

#define VERSION "0.1"

/* Layout. Users can drag-resize; g_win_w/g_win_h track live dimensions. */
#define WIN_W_INIT     640
#define WIN_H_INIT     420
#define WIN_W_MIN      480
#define WIN_H_MIN      300
#define TAB_H          22
#define BUTTON_H       26
#define STATUS_H       14
#define MARGIN         6

/* Palette pens — inherit Workbench. */
#define PEN_BG         0
#define PEN_FG         1
#define PEN_BORDER     1
#define PEN_ACTIVE     3
#define PEN_HL_BG      3
#define PEN_HL_FG      0
#define PEN_MUTED      2

/* Limits. Bounded arrays keep the code simple; if the user ever needs
 * more, bump these and rebuild — the file format is length-tolerant. */
#define MAX_NOTES      256
#define MAX_TASKS      256
#define MAX_EVENTS     256
#define MAX_TAGS_LEN     64
#define MAX_TITLE_LEN    80
#define MAX_BODY_LEN     512   /* multi-line via encoded newlines */
#define NOTE_BODY_ROWS   6     /* dialog stacks this many StringGadgets */
#define NOTE_BODY_COLS   80    /* each row's char capacity */
#define MAX_ATTENDEES_LEN 160
#define MAX_URL_LEN      160
#define MAX_NOTES_LEN    240
#define MAX_PATH         256

/* Day-view hour range. Events outside this window still exist and get
 * shown in the drilldown list, they just don't render as time blocks. */
#define DAY_HOUR_START   6
#define DAY_HOUR_END     22   /* exclusive: shows 6:00 through 21:xx */

/* Tabs — order controls tab-bar layout AND the tab-key cycle. */
typedef enum {
    TAB_NOTES    = 0,
    TAB_TASKS    = 1,
    TAB_CALENDAR = 2,
    TAB_COUNT
} Tab;

/* Task lifecycle. Values are stored in the file, don't renumber. */
typedef enum {
    ST_OPEN    = 0,
    ST_DOING   = 1,
    ST_DONE    = 2
} TaskState;

/* Priority: 1 = low, 2 = normal, 3 = high. */
typedef enum {
    RECUR_NONE   = 0,
    RECUR_DAILY  = 1,
    RECUR_WEEKLY = 2,
    RECUR_MONTHLY = 3,
    RECUR_YEARLY = 4
} RecurKind;

/* Records. All ids monotonically assigned; deleted rows are compacted
 * on save so ids may be reused (fine, we don't cross-reference). */
typedef struct {
    int  id;
    long created;                /* YYYYMMDD */
    char tags[MAX_TAGS_LEN];     /* csv */
    char title[MAX_TITLE_LEN];
    char body[MAX_BODY_LEN];
} Note;

typedef struct {
    int  id;
    int  state;                    /* TaskState */
    int  priority;                 /* 1..3 */
    long due;                      /* YYYYMMDD, 0 = none */
    int  recur;                    /* RecurKind */
    char tags[MAX_TAGS_LEN];
    char title[MAX_TITLE_LEN];
    /* Extended attributes (storage v2). Older records default these
     * to 0 / empty on load. */
    int  effort_min;               /* estimated effort in minutes, 0 = none */
    long scheduled_date;           /* YYYYMMDD to work on, 0 = unscheduled */
    int  scheduled_start;          /* HHMM, -1 = all-day / unscheduled */
    char notes[MAX_NOTES_LEN];     /* free-form description */
} Task;

typedef struct {
    int  id;
    long date;                            /* YYYYMMDD */
    int  start_time;                      /* HHMM, -1 = all-day */
    int  end_time;                        /* HHMM, -1 = no end / all-day */
    int  recur;                           /* RecurKind */
    char tags[MAX_TAGS_LEN];
    char title[MAX_TITLE_LEN];
    char attendees[MAX_ATTENDEES_LEN];    /* csv "; " separated */
    char url[MAX_URL_LEN];                /* Zoom / Meet / etc */
    char notes[MAX_NOTES_LEN];            /* free-form notes */
} Event;

/* --- runtime state (extern'd; owned by main.c) ---------------------- */
extern int    g_win_w;
extern int    g_win_h;
extern int    g_ox;              /* window BorderLeft — origin of drawable area */
extern int    g_oy;              /* window BorderTop  — origin of drawable area */
extern int    g_row_h;
extern int    g_baseline;
extern int    g_char_w;

extern Tab    g_current_tab;
extern LONG   g_running;
extern LONG   g_bridge_ok;
extern char   g_status[128];
extern long   g_today;           /* YYYYMMDD stamped at boot */

extern struct Window   *g_win;
extern struct RastPort *g_rp;

extern Note   g_notes[MAX_NOTES];
extern int    g_notes_count;
extern Task   g_tasks[MAX_TASKS];
extern int    g_tasks_count;
extern Event  g_events[MAX_EVENTS];
extern int    g_events_count;

extern int    g_notes_cursor;
extern int    g_notes_scroll;
extern int    g_tasks_cursor;
extern int    g_tasks_scroll;
extern long   g_cal_month;       /* YYYYMM for month view */
extern long   g_cal_selected;    /* YYYYMMDD, 0 = none */
extern int    g_cal_day_view;    /* 0 = month grid, 1 = hourly day view */
extern int    g_cal_event_cursor;/* row index into filtered day events */

/* --- ui.c ---------------------------------------------------------- */
void ui_draw_string(int x, int y, const char *s, UBYTE fg, UBYTE bg);
void ui_draw_frame(int x0, int y0, int w, int h, UBYTE pen);
void ui_fill_rect(int x0, int y0, int w, int h, UBYTE pen);
void ui_draw_tabs(int x0, int y0, int w);
int  ui_hit_tab(int mx, int my);
/* Common content-area helpers — the area between tab bar and buttons. */
void ui_content_rect(int *x0, int *y0, int *w, int *h);
void ui_draw_status(void);
/* Compact single-line list-row painter, used by notes/tasks/calendar
 * for consistency. Highlights when `selected`. */
void ui_draw_row(int x0, int y0, int w, const char *left, const char *right, int selected);

/* --- storage.c ----------------------------------------------------- */
/* Ensure S:organizer/ exists; called at startup. Returns 0 on success. */
int  storage_init(void);
/* Reload all three tables from disk. Returns count loaded (any -1 on I/O err). */
int  storage_load_all(void);
/* Persist all three tables. 0 on success. */
int  storage_save_all(void);
/* Individual save helpers — call after a mutation. */
int  storage_save_notes(void);
int  storage_save_tasks(void);
int  storage_save_events(void);
/* Human-readable today string YYYY-MM-DD, useful for status bar. */
void storage_today_string(char *out, size_t outsz);
/* Stamp today's YYYYMMDD into g_today via DateStamp. */
void storage_stamp_today(void);

/* --- notes.c ------------------------------------------------------- */
void notes_draw(int x0, int y0, int w, int h);
int  notes_handle_key(UWORD raw);      /* 1 = handled, 0 = pass through */
int  notes_handle_click(int mx, int my); /* same convention */
int  notes_add(const char *title);     /* returns new id or -1 */
int  notes_delete(int id);
int  notes_edit_title(int id, const char *title);
int  notes_edit_body(int id, const char *body);
int  notes_find_by_id(int id);         /* returns array index or -1 */

/* --- tasks.c ------------------------------------------------------- */
void tasks_draw(int x0, int y0, int w, int h);
int  tasks_handle_key(UWORD raw);
int  tasks_handle_click(int mx, int my);
int  tasks_add(const char *title);
int  tasks_delete(int id);
int  tasks_set_state(int id, int state);
int  tasks_set_priority(int id, int prio);
int  tasks_set_due(int id, long ymd);
int  tasks_set_recur(int id, int recur);
int  tasks_find_by_id(int id);
/* Count how many active (non-done) tasks are due on or before `ymd`. */
int  tasks_count_due_by(long ymd);

/* --- calendar.c ---------------------------------------------------- */
void calendar_draw(int x0, int y0, int w, int h);
int  calendar_handle_key(UWORD raw);
int  calendar_handle_click(int mx, int my);
int  events_add(const char *title, long date);
int  events_add_full(const char *title, long date, int start, int end,
                     int recur, const char *tags, const char *attendees,
                     const char *url, const char *notes);
int  events_delete(int id);
int  events_find_by_id(int id);
/* Multi-section prompt-driven editor. If id < 0 creates a new event. */
int  events_edit_interactive(int id, long default_date, int default_start);
/* Open the modal Intuition dialog for editing an event in-place.
 * Returns 1 on OK (writes back), 0 on Cancel/Close. Defined in
 * event_dialog.c. */
int  event_dialog_run(Event *inout);
/* Modal note editor. Returns 1 if user hit OK (writes back to *inout),
 * 0 on Cancel/Close. Body supports \n-encoded newlines and simple
 * markdown-style tokens; see note_dialog.c for the token reference. */
int  note_dialog_run(Note *inout);
/* Modal task editor. Returns 1 on OK (writes back), 0 on Cancel/Close. */
int  task_dialog_run(Task *inout);
/* Build list of event array indices firing on `ymd`. Returns count.
 * `out` must be at least MAX_EVENTS long. */
int  events_on_day(long ymd, int *out);

/* --- recur.c ------------------------------------------------------- */
/* Does a recurring item that started on `base` fire on `check`?
 * `kind` is RecurKind. Returns 1 for yes, 0 for no. */
int  recur_fires_on(int kind, long base, long check);
/* Convert a YYYYMMDD to weekday (0 = Sunday..6 = Saturday). */
int  ymd_weekday(long ymd);
/* Days-in-month for a YYYYMM value. Handles leap years. */
int  ymd_days_in_month(long ym);
/* Move a YYYYMMDD forward/backward by `n` days. */
long ymd_add_days(long ymd, int n);
/* Compose/decompose. */
long ymd_make(int y, int m, int d);
void ymd_split(long ymd, int *y, int *m, int *d);
/* Human names. */
const char *month_name(int m);          /* 1..12 → "Jan".."Dec" */
const char *weekday_short(int w);       /* 0..6 → "Sun".."Sat" */
const char *recur_name(int kind);

/* --- dialogs.c ----------------------------------------------------- */
int  prompt_string(const char *title, const char *deflt, char *out, size_t outsz);
int  prompt_int   (const char *title, long deflt, long *out);
int  confirm     (const char *fmt, const char *arg);

/* --- tests.c ------------------------------------------------------- */
/* Full ab_test suite covering recur + date math + storage roundtrip.
 * Wired as a bridge hook — call via amiga_call_hook client=organizer
 * hook=test. */
int  tests_run_all(void);

/* --- main.c -------------------------------------------------------- */
void redraw_all(void);
/* Called after any data mutation — persists, refreshes status, redraws. */
void state_touched(void);

#endif /* ORGANIZER_H */
