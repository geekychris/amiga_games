/*
 * Masterwork — dual-pane file manager for AmigaOS
 *
 * Grew out of opus/ into a fuller Directory Opus-style tool.
 * See main.c for the event loop; other files own their subsystem:
 *   panes.c    — directory listing + render + hit test
 *   fileops.c  — copy / delete / rename / mkdir (shell-command-backed)
 *   dialogs.c  — CON:-based prompts + EasyRequest confirm
 *   buttons.c  — clickable button bar at the bottom of the window
 */

#ifndef MASTERWORK_H
#define MASTERWORK_H

#include <exec/types.h>
#include <intuition/intuition.h>

/* OS4 shims — same pattern as amiga-bridge/src/fs_access.c and opus.
 * On PPC the classic names Examine/ExNext/Execute/DeleteFile are
 * marked obsolete; the SDK still ships OBSOLETE* entry points. */
#ifdef __PPC__
#define Examine    OBSOLETEExamine
#define ExNext     OBSOLETEExNext
#define Execute    OBSOLETEExecute
#define DeleteFile Delete
#define ACCESS_READ SHARED_LOCK
/* Flush() → FFlush() on OS4 — the classic name isn't shipped in the
 * interface's method table. FFlush has identical semantics. */
#define Flush      FFlush
#endif

#define VERSION "0.1"

/* Window sizing. 640x380 fits inside the standard OS4 Workbench
 * screen (usually 640x480) without the button bar disappearing under
 * the OS4 dock at the bottom. If your Workbench is wider you can
 * drag the window over — the layout re-flows for whatever fits. */
#define WIN_W          640
#define WIN_H          380
#define PANE_MARGIN    6

#define MAX_ENTRIES    512
#define MAX_PATH       512
#define MAX_NAME       108   /* AmigaOS FIB fib_FileName */

/* Palette pens — inherit Workbench's, so this works on any screen. */
#define PEN_BG         0
#define PEN_FG         1
#define PEN_BORDER     1
#define PEN_ACTIVE     3
#define PEN_HL_BG      3
#define PEN_HL_FG      0
#define PEN_SELECTED   2   /* filled/paler than FG; marks * selection */

typedef struct {
    char name[MAX_NAME];
    int  is_dir;
    LONG size;
    int  selected;   /* 0 or 1 — space-bar toggles */
} Entry;

typedef struct {
    char   path[MAX_PATH];
    Entry  entries[MAX_ENTRIES];
    int    count;
    int    cursor;
    int    scroll;    /* index of first visible row */
} Pane;

/* Cached layout globals — populated from RastPort font at startup so
 * every subsystem draws with matching row heights + baselines. */
extern int g_row_h;
extern int g_baseline;
extern int g_header_h;
extern int g_status_h;
extern int g_buttons_h;
extern int g_char_w;

/* Runtime state shared across files. */
extern Pane   panes[2];
extern int    active_pane;
extern LONG   running;
extern LONG   bridge_ok;
extern char   status_msg[128];
extern struct Window   *win;
extern struct RastPort *rp;

/* Pane rect — same math on draw and hit sides. */
typedef struct { int x0, y0, w, h; } PaneRect;
void pane_rect(int idx, PaneRect *r);

/* --- panes.c --- */
void refresh_pane(Pane *p);
/* Populate a pane with all currently-mounted DOS volumes + assigns
 * (Enter jumps into the picked one). Signalled by path[0] == 0. */
void refresh_volumes(Pane *p);
/* True when the pane is in volume-list mode (path == ""). */
int  is_volume_view(const Pane *p);
void draw_pane(int idx, int x0, int y0, int w, int h);
int  hit_test_pane(int mx, int my, int *out_pane, int *out_entry);
void ascend_path(char *path);
int  is_at_root(const char *path);
void join_path(char *buf, size_t bufsz, const char *path, const char *name);

/* Selection helpers — count how many entries are ticked in a pane.
 * When no selection exists, ops fall back to the cursor entry. */
int  selected_count(const Pane *p);
void clear_selection(Pane *p);

/* --- fileops.c --- */
/* Each returns 0 on success, nonzero on failure. Status message is
 * updated for the user regardless. Recursive versions use C:Copy ALL
 * or C:Delete ALL FORCE so subdirectories come along / disappear. */
int op_copy(int recursive);   /* selected (or cursor) src pane → dst pane */
int op_move(int recursive);
int op_delete(int recursive_and_force);
int op_view(void);            /* open cursor file in C:More */
int op_rename(void);          /* prompts via CON: for new name */
int op_mkdir(void);           /* prompts via CON: for dir name */

/* --- dialogs.c --- */
/* Prompt the user for a string via a CON: window. Returns 0 on
 * success (out has the trimmed result), nonzero if cancelled/failed.
 * `outsz` is buf size incl. terminator. */
int prompt_string(const char *title, const char *deflt, char *out, size_t outsz);

/* Blocking yes/no via EasyRequest. Returns 1 for OK, 0 for cancel. */
int confirm(const char *fmt, const char *arg1);

/* --- buttons.c --- */
/* Redraw the button bar into the current window's RastPort. */
void draw_buttons(void);
/* Test if (mx,my) hit a button. Returns index or -1. */
int  hit_test_button(int mx, int my);
/* Invoke the numbered button's action. */
void run_button(int idx);
/* How tall the button bar is — set by buttons.c init at startup. */
int  buttons_height(void);

/* --- main.c --- */
/* Repaint everything (panes + status + buttons). */
void redraw_all(void);

#endif /* MASTERWORK_H */
