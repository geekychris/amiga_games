/*
 * notes.c — Notes tab: scrollable list, add / edit / delete.
 *
 * Row: [id] title  (tags)
 * Keys: n = new, e = edit body, r = rename title, d = delete,
 *       t = edit tags, up/down = cursor, Enter = edit body.
 */

#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "organizer.h"
#include "bridge_client.h"

static int g_next_id = 1;

int notes_find_by_id(int id)
{
    for (int i = 0; i < g_notes_count; i++)
        if (g_notes[i].id == id) return i;
    return -1;
}

static int next_id(void)
{
    int max = 0;
    for (int i = 0; i < g_notes_count; i++)
        if (g_notes[i].id > max) max = g_notes[i].id;
    if (g_next_id <= max) g_next_id = max + 1;
    return g_next_id++;
}

int notes_add(const char *title)
{
    if (g_notes_count >= MAX_NOTES) return -1;
    Note *n = &g_notes[g_notes_count++];
    memset(n, 0, sizeof(*n));
    n->id = next_id();
    n->created = g_today;
    strncpy(n->title, title ? title : "New note", sizeof(n->title) - 1);
    n->title[sizeof(n->title) - 1] = 0;
    g_notes_cursor = g_notes_count - 1;
    AB_I("note added id=%ld title=%s", (long)n->id, n->title);
    return n->id;
}

int notes_delete(int id)
{
    int idx = notes_find_by_id(id);
    if (idx < 0) return -1;
    for (int i = idx; i < g_notes_count - 1; i++)
        g_notes[i] = g_notes[i + 1];
    g_notes_count--;
    if (g_notes_cursor >= g_notes_count && g_notes_cursor > 0) g_notes_cursor--;
    return 0;
}

int notes_edit_title(int id, const char *title)
{
    int idx = notes_find_by_id(id);
    if (idx < 0 || !title) return -1;
    strncpy(g_notes[idx].title, title, sizeof(g_notes[idx].title) - 1);
    g_notes[idx].title[sizeof(g_notes[idx].title) - 1] = 0;
    return 0;
}

int notes_edit_body(int id, const char *body)
{
    int idx = notes_find_by_id(id);
    if (idx < 0 || !body) return -1;
    strncpy(g_notes[idx].body, body, sizeof(g_notes[idx].body) - 1);
    g_notes[idx].body[sizeof(g_notes[idx].body) - 1] = 0;
    return 0;
}

/* --- rendering ---------------------------------------------------- */

static int rows_visible(int h) { int r = (h - g_row_h - 4) / g_row_h; return r < 1 ? 1 : r; }

void notes_draw(int x0, int y0, int w, int h)
{
    /* Header row: instructions. */
    char hdr[128];
    snprintf(hdr, sizeof(hdr),
             "N)ew  R)ename  B)ody  T)ags  D)elete  %d note%s",
             g_notes_count, g_notes_count == 1 ? "" : "s");
    ui_draw_string(x0 + 4, y0 + g_baseline, hdr, PEN_FG, PEN_BG);
    int list_top = y0 + g_row_h + 4;

    int rv = rows_visible(h - g_row_h - 4);
    if (g_notes_cursor < g_notes_scroll) g_notes_scroll = g_notes_cursor;
    if (g_notes_cursor >= g_notes_scroll + rv) g_notes_scroll = g_notes_cursor - rv + 1;

    for (int i = 0; i < rv && i + g_notes_scroll < g_notes_count; i++) {
        int idx = i + g_notes_scroll;
        Note *n = &g_notes[idx];
        char left[MAX_TITLE_LEN + 16], right[MAX_TAGS_LEN + 8];
        snprintf(left, sizeof(left), "[%3d]  %s", n->id, n->title);
        snprintf(right, sizeof(right), "%s", n->tags[0] ? n->tags : "");
        ui_draw_row(x0, list_top + i * g_row_h, w, left, right, idx == g_notes_cursor);
    }

    /* Preview pane at the bottom for the current row's body. */
    int preview_top = y0 + h - g_row_h * 3;
    if (preview_top > list_top + rv * g_row_h + 8) {
        SetAPen(g_rp, PEN_BORDER);
        Move(g_rp, x0, preview_top - 2);
        Draw(g_rp, x0 + w - 1, preview_top - 2);
        if (g_notes_cursor >= 0 && g_notes_cursor < g_notes_count) {
            Note *n = &g_notes[g_notes_cursor];
            char lbl[80];
            snprintf(lbl, sizeof(lbl), "Body of [%d] %s:", n->id, n->title);
            ui_draw_string(x0 + 4, preview_top + g_baseline, lbl, PEN_MUTED, PEN_BG);
            ui_draw_string(x0 + 4, preview_top + g_row_h + g_baseline,
                           n->body[0] ? n->body : "(empty - press B to edit)",
                           PEN_FG, PEN_BG);
        }
    }
}

/* --- input -------------------------------------------------------- */

int notes_handle_key(UWORD raw)
{
    if (raw & 0x80) return 0;
    int c = raw & 0x7F;
    switch (c) {
    case 0x4C: /* Up   */ if (g_notes_cursor > 0)                 g_notes_cursor--; break;
    case 0x4D: /* Down */ if (g_notes_cursor < g_notes_count - 1) g_notes_cursor++; break;
    case 0x36: /* N */ {
        char t[MAX_TITLE_LEN];
        if (prompt_string("Organizer - New note title", "", t, sizeof(t)) == 0 && *t) {
            notes_add(t);
            state_touched();
            return 1;
        }
        break;
    }
    case 0x13: /* R (rename) */ {
        if (g_notes_cursor < 0 || g_notes_cursor >= g_notes_count) return 0;
        Note *n = &g_notes[g_notes_cursor];
        char t[MAX_TITLE_LEN];
        if (prompt_string("Organizer - Rename note", n->title, t, sizeof(t)) == 0 && *t) {
            notes_edit_title(n->id, t);
            state_touched();
            return 1;
        }
        break;
    }
    case 0x35: /* B (body) */
    case 0x44: /* Enter    */ {
        if (g_notes_cursor < 0 || g_notes_cursor >= g_notes_count) return 0;
        Note *n = &g_notes[g_notes_cursor];
        char b[MAX_BODY_LEN];
        if (prompt_string("Organizer - Edit body", n->body, b, sizeof(b)) == 0) {
            notes_edit_body(n->id, b);
            state_touched();
            return 1;
        }
        break;
    }
    case 0x14: /* T (tags) */ {
        if (g_notes_cursor < 0 || g_notes_cursor >= g_notes_count) return 0;
        Note *n = &g_notes[g_notes_cursor];
        char t[MAX_TAGS_LEN];
        if (prompt_string("Organizer - Tags (csv)", n->tags, t, sizeof(t)) == 0) {
            strncpy(n->tags, t, sizeof(n->tags) - 1);
            n->tags[sizeof(n->tags) - 1] = 0;
            state_touched();
            return 1;
        }
        break;
    }
    case 0x22: /* D */ {
        if (g_notes_cursor < 0 || g_notes_cursor >= g_notes_count) return 0;
        Note *n = &g_notes[g_notes_cursor];
        if (confirm("Delete note [%s] ?", n->title)) {
            notes_delete(n->id);
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

int notes_handle_click(int mx, int my)
{
    int cx, cy, cw, ch;
    ui_content_rect(&cx, &cy, &cw, &ch);
    if (mx < cx || mx >= cx + cw) return 0;
    int list_top = cy + g_row_h + 4;
    if (my < list_top) return 0;
    int row = (my - list_top) / g_row_h;
    int idx = row + g_notes_scroll;
    if (idx >= 0 && idx < g_notes_count) {
        g_notes_cursor = idx;
        redraw_all();
        return 1;
    }
    return 0;
}
