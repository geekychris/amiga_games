/*
 * opus — dual-pane file manager inspired by Directory Opus.
 *
 * MVP feature set:
 *   - Two side-by-side panes, each with its own path + directory listing
 *   - Keyboard nav: Up/Down cursor, Enter to descend (into dirs), Backspace
 *     to ascend to parent, Tab to switch active pane
 *   - Actions (targeting the active pane's cursor entry): C=copy to other
 *     pane, D=delete (with confirmation), R=refresh, Q/ESC=quit
 *   - Status line at the bottom showing free space + hint
 *   - Bridge var / log integration so devbench can observe activity
 *
 * The renderer is text-into-RastPort (no gadtools ListView) so the same
 * source builds and runs on classic 68k and PPC OS4 without extra library
 * dependencies. Portable via ARCH=ppc CFLAGS in the Makefile.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bridge_client.h"

#ifdef __PPC__
/* On OS4 the classic DOS names Examine/ExNext/DeleteFile/Execute are
 * marked obsolete; the SDK still defines the entry points as OBSOLETE*.
 * Same pattern amiga-bridge/src/fs_access.c uses. */
#define Examine    OBSOLETEExamine
#define ExNext     OBSOLETEExNext
#define Execute    OBSOLETEExecute
#define DeleteFile Delete
#define ACCESS_READ SHARED_LOCK
#endif

#ifndef __PPC__
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;
#endif

#define VERSION "0.1"

/* Layout */
#define WIN_W          640
#define WIN_H          400
#define ROW_H          10
#define HEADER_H       24
#define STATUS_H       14
#define PANE_MARGIN    6

#define MAX_ENTRIES    256
#define MAX_PATH       260
#define MAX_NAME       108   /* AmigaOS FIB fib_FileName is 108 bytes */

/* Pen indices — inherited from Workbench screen palette (usually 0=grey,
 * 1=black, 2=white, 3=orange highlight). Good enough without a custom
 * screen; we're a lightweight tool, not a game. */
#define PEN_BG         0
#define PEN_FG         1
#define PEN_BORDER     1
#define PEN_ACTIVE     3
#define PEN_HL_BG      3
#define PEN_HL_FG      0

typedef struct {
    char name[MAX_NAME];
    int  is_dir;
    LONG size;
} Entry;

typedef struct {
    char   path[MAX_PATH];
    Entry  entries[MAX_ENTRIES];
    int    count;
    int    cursor;
    int    scroll;   /* index of first visible entry */
} Pane;

static Pane  panes[2];
static int   active_pane = 0;
static struct Window *win = NULL;
static struct RastPort *rp = NULL;
static LONG  running = 1;
static LONG  bridge_ok = 0;
static char  status_msg[128] = "";

/* ---- directory listing ------------------------------------------- */

static int cmp_entries(const void *a, const void *b)
{
    const Entry *ea = a, *eb = b;
    /* Dirs first, then alphabetical (case-insensitive) */
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    return stricmp((STRPTR)ea->name, (STRPTR)eb->name);
}

/* True if path ends at a volume root ("Work:" or similar) — no ".." */
static int is_at_root(const char *path)
{
    const char *colon = strchr(path, ':');
    if (!colon) return 1;
    return colon[1] == '\0';
}

static void refresh_pane(Pane *p)
{
    p->count = 0;
    p->cursor = 0;
    p->scroll = 0;

    BPTR lock = Lock((STRPTR)p->path, ACCESS_READ);
    if (!lock) {
        snprintf(status_msg, sizeof(status_msg),
                 "Lock failed: %s", p->path);
        if (bridge_ok) AB_W("%s", status_msg);
        return;
    }

    struct FileInfoBlock *fib = (struct FileInfoBlock *)
        AllocDosObject(DOS_FIB, NULL);
    if (!fib || !Examine(lock, fib)) {
        if (fib) FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        snprintf(status_msg, sizeof(status_msg),
                 "Examine failed: %s", p->path);
        if (bridge_ok) AB_W("%s", status_msg);
        return;
    }

    /* Prepend ".." unless we're already at a volume root */
    if (!is_at_root(p->path) && p->count < MAX_ENTRIES) {
        strcpy(p->entries[p->count].name, "..");
        p->entries[p->count].is_dir = 1;
        p->entries[p->count].size = 0;
        p->count++;
    }

    while (ExNext(lock, fib) && p->count < MAX_ENTRIES) {
        strncpy(p->entries[p->count].name, (char *)fib->fib_FileName, MAX_NAME - 1);
        p->entries[p->count].name[MAX_NAME - 1] = 0;
        p->entries[p->count].is_dir = (fib->fib_DirEntryType > 0);
        p->entries[p->count].size = fib->fib_Size;
        p->count++;
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    /* Sort: dirs first, alpha. Skip the ".." at index 0 if present. */
    int off = is_at_root(p->path) ? 0 : 1;
    if (p->count - off > 1) {
        qsort(&p->entries[off], p->count - off, sizeof(Entry), cmp_entries);
    }

    snprintf(status_msg, sizeof(status_msg),
             "%s — %d entries", p->path, p->count);
    if (bridge_ok) AB_I("refresh %s: %d entries", p->path, p->count);
}

/* Compose "path/name" into buf. Handles trailing slash on `path`. */
static void join_path(char *buf, size_t bufsz, const char *path, const char *name)
{
    size_t plen = strlen(path);
    int needs_sep = (plen > 0 && path[plen - 1] != ':' && path[plen - 1] != '/');
    snprintf(buf, bufsz, "%s%s%s", path, needs_sep ? "/" : "", name);
}

/* Ascend one level. path="Work:foo/bar" → "Work:foo"; "Work:foo" → "Work:" */
static void ascend_path(char *path)
{
    size_t len = strlen(path);
    if (len == 0) return;
    /* strip trailing / if any */
    if (path[len - 1] == '/') { path[--len] = 0; }
    /* find last / or : and truncate */
    char *last_sep = NULL;
    for (int i = (int)len - 1; i >= 0; i--) {
        if (path[i] == '/' || path[i] == ':') { last_sep = &path[i]; break; }
    }
    if (!last_sep) return;
    if (*last_sep == ':') last_sep[1] = 0;   /* Work:foo → Work: */
    else                  *last_sep    = 0;  /* Work:foo/bar → Work:foo */
}

/* ---- rendering --------------------------------------------------- */

static void draw_string(int x, int y, const char *s, UBYTE fg, UBYTE bg)
{
    SetAPen(rp, fg);
    SetBPen(rp, bg);
    SetDrMd(rp, JAM2);
    Move(rp, x, y);
    Text(rp, (STRPTR)s, (LONG)strlen(s));
}

static void draw_pane(int idx, int x0, int y0, int w, int h)
{
    Pane *p = &panes[idx];
    int is_active = (active_pane == idx);

    /* Clear background */
    SetAPen(rp, PEN_BG);
    RectFill(rp, x0, y0, x0 + w - 1, y0 + h - 1);

    /* Border — thicker on active pane */
    SetAPen(rp, is_active ? PEN_ACTIVE : PEN_BORDER);
    Move(rp, x0, y0);
    Draw(rp, x0 + w - 1, y0);
    Draw(rp, x0 + w - 1, y0 + h - 1);
    Draw(rp, x0, y0 + h - 1);
    Draw(rp, x0, y0);
    if (is_active) {
        /* Inner border for extra emphasis */
        Move(rp, x0 + 1, y0 + 1);
        Draw(rp, x0 + w - 2, y0 + 1);
        Draw(rp, x0 + w - 2, y0 + h - 2);
        Draw(rp, x0 + 1, y0 + h - 2);
        Draw(rp, x0 + 1, y0 + 1);
    }

    /* Path header */
    draw_string(x0 + 4, y0 + 12, p->path, PEN_FG, PEN_BG);
    SetAPen(rp, PEN_BORDER);
    Move(rp, x0 + 2, y0 + 16);
    Draw(rp, x0 + w - 3, y0 + 16);

    /* Entries — clip to visible rows */
    int rows_visible = (h - HEADER_H - 4) / ROW_H;

    /* Keep cursor visible (scroll if needed) */
    if (p->cursor < p->scroll) p->scroll = p->cursor;
    if (p->cursor >= p->scroll + rows_visible)
        p->scroll = p->cursor - rows_visible + 1;

    for (int i = 0; i < rows_visible && i + p->scroll < p->count; i++) {
        int idx_e = i + p->scroll;
        int y = y0 + HEADER_H + i * ROW_H;
        UBYTE fg = PEN_FG, bg = PEN_BG;
        int is_cursor = (idx_e == p->cursor && is_active);
        if (is_cursor) { fg = PEN_HL_FG; bg = PEN_HL_BG; }

        /* Highlight the entire row on cursor */
        if (is_cursor) {
            SetAPen(rp, PEN_HL_BG);
            RectFill(rp, x0 + 2, y - 8, x0 + w - 3, y + 1);
        }

        char line[160];
        if (p->entries[idx_e].is_dir) {
            snprintf(line, sizeof(line), "[DIR]  %s", p->entries[idx_e].name);
        } else {
            snprintf(line, sizeof(line), "       %-32.32s %8ld",
                     p->entries[idx_e].name, (long)p->entries[idx_e].size);
        }
        /* Truncate to fit width */
        int max_chars = (w - 12) / 8;
        if ((int)strlen(line) > max_chars) line[max_chars] = 0;
        draw_string(x0 + 6, y, line, fg, bg);
    }
}

static void draw_status(int x0, int y0, int w)
{
    SetAPen(rp, PEN_BG);
    RectFill(rp, x0, y0, x0 + w - 1, y0 + STATUS_H - 1);
    SetAPen(rp, PEN_BORDER);
    Move(rp, x0, y0);
    Draw(rp, x0 + w - 1, y0);
    draw_string(x0 + 4, y0 + 10, status_msg, PEN_FG, PEN_BG);
}

static void redraw_all(void)
{
    int pane_w = (WIN_W - PANE_MARGIN * 3) / 2;
    int pane_h = WIN_H - STATUS_H - PANE_MARGIN * 2;
    int pane_y = PANE_MARGIN;

    draw_pane(0, PANE_MARGIN, pane_y, pane_w, pane_h);
    draw_pane(1, PANE_MARGIN * 2 + pane_w, pane_y, pane_w, pane_h);
    draw_status(0, WIN_H - STATUS_H, WIN_W);
}

/* ---- actions ----------------------------------------------------- */

static void enter_selected(void)
{
    Pane *p = &panes[active_pane];
    if (p->cursor >= p->count) return;
    Entry *e = &p->entries[p->cursor];

    if (!e->is_dir) {
        snprintf(status_msg, sizeof(status_msg),
                 "Not a dir: %s (Enter is dir-only for now)", e->name);
        return;
    }

    if (strcmp(e->name, "..") == 0) {
        ascend_path(p->path);
    } else {
        char newpath[MAX_PATH];
        join_path(newpath, sizeof(newpath), p->path, e->name);
        strncpy(p->path, newpath, MAX_PATH - 1);
        p->path[MAX_PATH - 1] = 0;
    }
    refresh_pane(p);
}

static void ascend(void)
{
    Pane *p = &panes[active_pane];
    if (is_at_root(p->path)) {
        snprintf(status_msg, sizeof(status_msg), "already at root: %s", p->path);
        return;
    }
    ascend_path(p->path);
    refresh_pane(p);
}

/* Copy the cursor entry from active pane to the other pane's directory.
 * Handles files (via Copy DOS command) but skips directories to keep
 * the MVP simple. */
static void copy_selected(void)
{
    Pane *src = &panes[active_pane];
    Pane *dst = &panes[active_pane ^ 1];
    if (src->cursor >= src->count) return;
    Entry *e = &src->entries[src->cursor];
    if (strcmp(e->name, "..") == 0) {
        snprintf(status_msg, sizeof(status_msg), "can't copy \"..\"");
        return;
    }
    if (e->is_dir) {
        snprintf(status_msg, sizeof(status_msg),
                 "dir copy not implemented — skip %s", e->name);
        return;
    }

    char src_full[MAX_PATH], dst_full[MAX_PATH];
    join_path(src_full, sizeof(src_full), src->path, e->name);
    join_path(dst_full, sizeof(dst_full), dst->path, e->name);

    /* Use DOS Copy via a shell command since we don't want to reimplement
     * chunked buffered read/write with error handling here. Runs
     * synchronously via Execute; blocking during copy is acceptable for
     * the MVP. */
    char cmd[MAX_PATH * 2 + 32];
    snprintf(cmd, sizeof(cmd), "C:Copy \"%s\" \"%s\"", src_full, dst_full);
    if (bridge_ok) AB_I("copy: %s -> %s", src_full, dst_full);

    BPTR nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    LONG ok = 0;
    if (nil) {
        ok = Execute((STRPTR)cmd, (BPTR)0, nil);
        Close(nil);
    }
    if (!ok) {
        snprintf(status_msg, sizeof(status_msg),
                 "copy failed: %s", e->name);
    } else {
        snprintf(status_msg, sizeof(status_msg),
                 "copied %s -> %s:", e->name, dst->path);
        refresh_pane(dst);
    }
}

/* Delete cursor entry from active pane. No confirmation prompt in MVP;
 * status message tells you what happened. */
static void delete_selected(void)
{
    Pane *p = &panes[active_pane];
    if (p->cursor >= p->count) return;
    Entry *e = &p->entries[p->cursor];
    if (strcmp(e->name, "..") == 0) {
        snprintf(status_msg, sizeof(status_msg), "can't delete \"..\"");
        return;
    }

    char full[MAX_PATH];
    join_path(full, sizeof(full), p->path, e->name);
    if (bridge_ok) AB_W("delete: %s", full);

    LONG ok = DeleteFile((STRPTR)full);
    if (!ok) {
        snprintf(status_msg, sizeof(status_msg),
                 "delete failed: %s (IoErr %ld)", e->name, (long)IoErr());
    } else {
        snprintf(status_msg, sizeof(status_msg),
                 "deleted %s", e->name);
        refresh_pane(p);
    }
}

/* ---- input dispatch --------------------------------------------- */

static void handle_key(UWORD raw)
{
    /* RAWKEY codes we care about — same set the ballblazer / hello_world
     * examples use. bit 0x80 = key up event; only act on key-down. */
    if (raw & 0x80) return;
    int c = raw & 0x7F;

    Pane *p = &panes[active_pane];

    switch (c) {
        case 0x4C: /* Up */
            if (p->cursor > 0) p->cursor--;
            break;
        case 0x4D: /* Down */
            if (p->cursor < p->count - 1) p->cursor++;
            break;
        case 0x4E: /* Right */
            if (active_pane == 0) active_pane = 1;
            break;
        case 0x4F: /* Left */
            if (active_pane == 1) active_pane = 0;
            break;
        case 0x42: /* Tab */
            active_pane ^= 1;
            break;
        case 0x44: /* Return / Enter (main keyboard) */
            enter_selected();
            break;
        case 0x41: /* Backspace */
            ascend();
            break;
        case 0x45: /* ESC */
            running = 0;
            break;
        case 0x33: /* C */
            copy_selected();
            break;
        case 0x22: /* D */
            delete_selected();
            break;
        case 0x13: /* R */
            refresh_pane(p);
            break;
        case 0x10: /* Q */
            running = 0;
            break;
        default:
            /* leave status untouched */
            break;
    }
}

/* ---- main -------------------------------------------------------- */

int main(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) { CloseLibrary((struct Library *)IntuitionBase); return 1; }

    printf("opus v%s\n", VERSION);
    if (ab_init("opus") != 0) {
        printf("  Bridge: NOT FOUND\n"); bridge_ok = 0;
    } else {
        printf("  Bridge: CONNECTED\n"); bridge_ok = 1;
    }
    AB_I("opus v%s starting", VERSION);
    ab_register_var("running",     AB_TYPE_I32, &running);
    ab_register_var("active_pane", AB_TYPE_I32, &active_pane);

    /* Seed both panes with a starting path. RAM: is safe on any Amiga;
     * DH1: is our OS4 dev drive but doesn't exist on classic. */
    strcpy(panes[0].path, "RAM:");
    strcpy(panes[1].path, "SYS:");

    win = OpenWindowTags(NULL,
        WA_Left,         40,
        WA_Top,          20,
        WA_Width,        WIN_W,
        WA_Height,       WIN_H,
        WA_Title,        (ULONG)"opus — dual-pane file manager",
        WA_CloseGadget,  TRUE,
        WA_DragBar,      TRUE,
        WA_DepthGadget,  TRUE,
        WA_SizeGadget,   FALSE,
        WA_Activate,     TRUE,
        WA_IDCMP,        IDCMP_CLOSEWINDOW | IDCMP_RAWKEY,
        TAG_DONE);

    if (!win) {
        AB_E("OpenWindow failed");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }
    rp = win->RPort;

    refresh_pane(&panes[0]);
    refresh_pane(&panes[1]);
    strcpy(status_msg,
        "Tab=switch  Enter=descend  Backspace=up  C=copy  D=delete  R=refresh  Q=quit");
    redraw_all();

    while (running) {
        /* Block on the window's UserPort — no busy-poll, no Delay(1). */
        WaitPort(win->UserPort);
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            ULONG cls = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) {
                running = 0;
            } else if (cls == IDCMP_RAWKEY) {
                handle_key(code);
            }
        }
        redraw_all();
        if (bridge_ok) ab_poll();
    }

    AB_I("opus shutting down");
    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
