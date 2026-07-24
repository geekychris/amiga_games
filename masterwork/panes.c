/*
 * panes.c — directory listing, per-pane render + hit-test.
 *
 * Uses `Examine` / `ExNext` walk (same as classic AmigaOS toolchain);
 * on OS4 the shim in masterwork.h aliases these to the SDK's
 * OBSOLETE* entry points which are still there for backwards compat.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include "masterwork.h"
#include "bridge_client.h"

/* --- layout helper ------------------------------------------------ */

void pane_rect(int idx, PaneRect *r)
{
    int pane_w = (WIN_W - PANE_MARGIN * 3) / 2;
    int pane_h = WIN_H - g_status_h - g_buttons_h - PANE_MARGIN * 2;
    r->x0 = (idx == 0) ? PANE_MARGIN : PANE_MARGIN * 2 + pane_w;
    r->y0 = PANE_MARGIN;
    r->w  = pane_w;
    r->h  = pane_h;
}

/* --- path utilities ---------------------------------------------- */

int is_at_root(const char *path)
{
    const char *colon = strchr(path, ':');
    if (!colon) return 1;
    return colon[1] == '\0';
}

void ascend_path(char *path)
{
    size_t len = strlen(path);
    if (len == 0) return;
    if (path[len - 1] == '/') { path[--len] = 0; }
    char *last_sep = NULL;
    for (int i = (int)len - 1; i >= 0; i--) {
        if (path[i] == '/' || path[i] == ':') { last_sep = &path[i]; break; }
    }
    if (!last_sep) return;
    if (*last_sep == ':') last_sep[1] = 0;
    else                  *last_sep    = 0;
}

void join_path(char *buf, size_t bufsz, const char *path, const char *name)
{
    size_t plen = strlen(path);
    int needs_sep = (plen > 0 && path[plen - 1] != ':' && path[plen - 1] != '/');
    snprintf(buf, bufsz, "%s%s%s", path, needs_sep ? "/" : "", name);
}

/* --- selection helpers ------------------------------------------- */

int selected_count(const Pane *p)
{
    int n = 0;
    for (int i = 0; i < p->count; i++) if (p->entries[i].selected) n++;
    return n;
}

void clear_selection(Pane *p)
{
    for (int i = 0; i < p->count; i++) p->entries[i].selected = 0;
}

/* --- directory refresh ------------------------------------------- */

static int cmp_entries(const void *a, const void *b)
{
    const Entry *ea = a, *eb = b;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    return stricmp((STRPTR)ea->name, (STRPTR)eb->name);
}

void refresh_pane(Pane *p)
{
    p->count = 0;
    p->cursor = 0;
    p->scroll = 0;

    BPTR lock = Lock((STRPTR)p->path, ACCESS_READ);
    if (!lock) {
        snprintf(status_msg, sizeof(status_msg), "Lock failed: %s", p->path);
        if (bridge_ok) AB_W("%s", status_msg);
        return;
    }

    struct FileInfoBlock *fib =
        (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib || !Examine(lock, fib)) {
        if (fib) FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        snprintf(status_msg, sizeof(status_msg), "Examine failed: %s", p->path);
        if (bridge_ok) AB_W("%s", status_msg);
        return;
    }

    if (!is_at_root(p->path) && p->count < MAX_ENTRIES) {
        strcpy(p->entries[p->count].name, "..");
        p->entries[p->count].is_dir = 1;
        p->entries[p->count].size = 0;
        p->entries[p->count].selected = 0;
        p->count++;
    }

    while (ExNext(lock, fib) && p->count < MAX_ENTRIES) {
        strncpy(p->entries[p->count].name, (char *)fib->fib_FileName, MAX_NAME - 1);
        p->entries[p->count].name[MAX_NAME - 1] = 0;
        p->entries[p->count].is_dir  = (fib->fib_DirEntryType > 0);
        p->entries[p->count].size    = fib->fib_Size;
        p->entries[p->count].selected = 0;
        p->count++;
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    int off = is_at_root(p->path) ? 0 : 1;
    if (p->count - off > 1) {
        qsort(&p->entries[off], p->count - off, sizeof(Entry), cmp_entries);
    }

    snprintf(status_msg, sizeof(status_msg), "%s — %d entries", p->path, p->count);
    if (bridge_ok) AB_I("refresh %s: %d entries", p->path, p->count);
}

/* --- rendering ---------------------------------------------------- */

static void draw_string(int x, int y, const char *s, UBYTE fg, UBYTE bg)
{
    SetAPen(rp, fg);
    SetBPen(rp, bg);
    SetDrMd(rp, JAM2);
    Move(rp, x, y);
    Text(rp, (STRPTR)s, (LONG)strlen(s));
}

void draw_pane(int idx, int x0, int y0, int w, int h)
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
        Move(rp, x0 + 1, y0 + 1);
        Draw(rp, x0 + w - 2, y0 + 1);
        Draw(rp, x0 + w - 2, y0 + h - 2);
        Draw(rp, x0 + 1, y0 + h - 2);
        Draw(rp, x0 + 1, y0 + 1);
    }

    /* Path header */
    draw_string(x0 + 4, y0 + 2 + g_baseline, p->path, PEN_FG, PEN_BG);
    SetAPen(rp, PEN_BORDER);
    int sep_y = y0 + g_header_h - 4;
    Move(rp, x0 + 2, sep_y);
    Draw(rp, x0 + w - 3, sep_y);

    /* Entries */
    int rows_visible = (h - g_header_h - 4) / g_row_h;
    if (rows_visible < 1) rows_visible = 1;

    if (p->cursor < p->scroll) p->scroll = p->cursor;
    if (p->cursor >= p->scroll + rows_visible)
        p->scroll = p->cursor - rows_visible + 1;

    for (int i = 0; i < rows_visible && i + p->scroll < p->count; i++) {
        int idx_e   = i + p->scroll;
        int row_top = y0 + g_header_h + i * g_row_h;
        int base    = row_top + g_baseline;
        UBYTE fg = PEN_FG, bg = PEN_BG;
        int is_cursor = (idx_e == p->cursor && is_active);
        int is_sel    = p->entries[idx_e].selected;

        if (is_cursor) {
            SetAPen(rp, PEN_HL_BG);
            RectFill(rp, x0 + 2, row_top, x0 + w - 3, row_top + g_row_h - 1);
            fg = PEN_HL_FG; bg = PEN_HL_BG;
        }

        char line[200];
        char mark = is_sel ? '*' : ' ';
        if (p->entries[idx_e].is_dir) {
            snprintf(line, sizeof(line), "%c [DIR]  %s",
                     mark, p->entries[idx_e].name);
        } else {
            snprintf(line, sizeof(line), "%c        %-32.32s %8ld",
                     mark, p->entries[idx_e].name,
                     (long)p->entries[idx_e].size);
        }
        int max_chars = (w - 12) / g_char_w;
        if ((int)strlen(line) > max_chars) line[max_chars] = 0;
        draw_string(x0 + 6, base, line, fg, bg);
    }
}

int hit_test_pane(int mx, int my, int *out_pane, int *out_entry)
{
    for (int i = 0; i < 2; i++) {
        PaneRect r;
        pane_rect(i, &r);
        if (mx < r.x0 || mx >= r.x0 + r.w) continue;
        if (my < r.y0 || my >= r.y0 + r.h) continue;
        *out_pane = i;
        int rows_top = r.y0 + g_header_h;
        if (my < rows_top) {
            *out_entry = -1;
        } else {
            int row = (my - rows_top) / g_row_h;
            int idx = row + panes[i].scroll;
            if (idx >= 0 && idx < panes[i].count) *out_entry = idx;
            else                                   *out_entry = -1;
        }
        return 1;
    }
    return 0;
}
