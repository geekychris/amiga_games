/*
 * notes.c — Notes tab.
 *
 * Row: [id] title  (tags)
 * Preview pane at the bottom renders the currently-selected note's
 * body split by newlines, with simple markdown recognition (headings
 * in ACTIVE pen, `code` and *italic* runs in MUTED, **bold** as
 * regular FG — good enough contrast for a text UI).
 *
 * Keys:
 *   N            = new note (opens modal editor)
 *   Enter        = edit selected note (modal editor)
 *   R            = quick rename (title only, CON: prompt)
 *   D            = delete
 *   Up / Down    = cursor
 * Click:
 *   single-click = select
 *   double-click = edit (opens modal)
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

static int edit_selected(void)
{
    if (g_notes_cursor < 0 || g_notes_cursor >= g_notes_count) return 0;
    Note tmp = g_notes[g_notes_cursor];
    if (note_dialog_run(&tmp)) {
        g_notes[g_notes_cursor] = tmp;
        state_touched();
        return 1;
    }
    return 0;
}

/* --- rendering ---------------------------------------------------- */

static int rows_visible(int h) { int r = (h - g_row_h - 4) / g_row_h; return r < 1 ? 1 : r; }

/* Render one preview line with markdown-ish styling. Line starts with:
 *   "# text"    → heading (PEN_ACTIVE, no prefix rendered)
 *   "## text"   → smaller heading (PEN_HL_FG)
 *   "- text"    → list bullet (MUTED prefix, FG body)
 * Any `**...**` runs are rendered in HL colour, `` `...` `` in MUTED. */
static void draw_preview_line(int x, int y, const char *line, int max_chars)
{
    if (!line[0]) return;
    const char *p = line;
    UBYTE fg = PEN_FG;
    if (p[0] == '#' && p[1] == '#') {
        fg = PEN_HL_FG;
        p += 2;
        while (*p == ' ') p++;
    } else if (p[0] == '#') {
        fg = PEN_ACTIVE;
        p += 1;
        while (*p == ' ') p++;
    } else if (p[0] == '-' && p[1] == ' ') {
        ui_draw_string(x, y, "\xB7 ", PEN_MUTED, PEN_BG);   /* middle dot */
        x += 2 * g_char_w;
        p += 2;
    }

    char buf[128];
    int n = 0;
    int i = 0;
    while (p[i] && n < (int)sizeof(buf) - 1 && n < max_chars) {
        buf[n++] = p[i++];
    }
    buf[n] = 0;
    ui_draw_string(x, y, buf, fg, PEN_BG);
}

void notes_draw(int x0, int y0, int w, int h)
{
    /* Header row. */
    char hdr[128];
    snprintf(hdr, sizeof(hdr),
             "N)ew  Enter=edit  R)ename title  D)elete  %d note%s",
             g_notes_count, g_notes_count == 1 ? "" : "s");
    ui_draw_string(x0 + 4, y0 + g_baseline, hdr, PEN_FG, PEN_BG);
    int list_top = y0 + g_row_h + 4;

    /* Reserve bottom third for the preview pane. */
    int preview_h  = g_row_h * (NOTE_BODY_ROWS + 2);
    int list_h     = h - g_row_h - 4 - preview_h - 4;
    if (list_h < g_row_h * 3) {
        list_h    = h - g_row_h - 4 - g_row_h * 3 - 4;
        preview_h = h - g_row_h - 4 - list_h - 4;
    }
    int rv = list_h / g_row_h;
    if (rv < 1) rv = 1;

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

    /* Preview pane divider + label. */
    int preview_top = list_top + rv * g_row_h + 6;
    SetAPen(g_rp, PEN_BORDER);
    Move(g_rp, x0, preview_top - 2);
    Draw(g_rp, x0 + w - 1, preview_top - 2);

    if (g_notes_cursor >= 0 && g_notes_cursor < g_notes_count) {
        Note *n = &g_notes[g_notes_cursor];
        char lbl[80];
        snprintf(lbl, sizeof(lbl),
                 "Body of [%d] %s  (tags: %s)",
                 n->id, n->title, n->tags[0] ? n->tags : "-");
        ui_draw_string(x0 + 4, preview_top + g_baseline, lbl, PEN_MUTED, PEN_BG);

        int max_chars = (w - 8) / g_char_w;
        if (max_chars < 1) max_chars = 1;
        int body_top = preview_top + g_row_h + 2;
        int max_lines = (h - (body_top - y0)) / g_row_h;

        /* Walk the body one line at a time. */
        const char *src = n->body;
        int line_no = 0;
        char line_buf[128];
        while (*src && line_no < max_lines) {
            int k = 0;
            while (*src && *src != '\n' && k < (int)sizeof(line_buf) - 1) {
                line_buf[k++] = *src++;
            }
            line_buf[k] = 0;
            if (*src == '\n') src++;
            draw_preview_line(x0 + 4, body_top + line_no * g_row_h + g_baseline,
                              line_buf, max_chars);
            line_no++;
        }
        if (line_no == 0) {
            ui_draw_string(x0 + 4, body_top + g_baseline,
                           "(empty - press Enter or double-click to edit)",
                           PEN_MUTED, PEN_BG);
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
    case 0x44: /* Enter → edit modal */
        return edit_selected() ? 1 : 0;
    case 0x36: /* N — new + edit */ {
        int id = notes_add("New note");
        if (id < 0) return 0;
        edit_selected();     /* opens editor on the just-added row */
        state_touched();
        return 1;
    }
    case 0x13: /* R — quick title rename via CON: */ {
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

/* Double-click state — Amiga has no native double-click event, so we
 * track (row, timestamp) and treat two SELECTDOWNs on the same row
 * within 500 ms as a double-click. Consistent with calendar.c. */
static int              last_click_row  = -1;
static struct DateStamp last_click_ts   = { 0, 0, 0 };
static LONG ds_delta_ms(const struct DateStamp *a, const struct DateStamp *b)
{
    LONG dm = (b->ds_Minute - a->ds_Minute) * 60000L;
    LONG dt = (b->ds_Tick - a->ds_Tick) * 20L;
    return dm + dt;
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
    if (idx < 0 || idx >= g_notes_count) return 0;

    g_notes_cursor = idx;

    struct DateStamp now; DateStamp(&now);
    int is_double =
        (idx == last_click_row &&
         ds_delta_ms(&last_click_ts, &now) < 500);
    last_click_row = idx;
    last_click_ts  = now;
    if (is_double) {
        edit_selected();
    } else {
        redraw_all();
    }
    return 1;
}
