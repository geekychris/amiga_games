/*
 * Masterwork - main entry: window + event loop + shared state.
 *
 * Panes rendering, file operations, dialogs, and the button bar live
 * in their own files; this one just wires it all together and owns
 * the global state each file references via masterwork.h.
 */

#include <string.h>
#include <stdio.h>

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

#include "masterwork.h"
#include "bridge_client.h"

#ifndef __PPC__
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;
#endif

/* --- shared state (extern'd in masterwork.h) --------------------- */

int  g_win_w    = WIN_W_INIT;
int  g_win_h    = WIN_H_INIT;
int  g_row_h    = 10;
int  g_baseline = 8;
int  g_header_h = 24;
int  g_status_h = 14;
int  g_buttons_h = 40;
int  g_char_w   = 8;

Pane   panes[2];
int    active_pane = 0;
LONG   running     = 1;
LONG   bridge_ok   = 0;
char   status_msg[128] = "";
struct Window   *win = NULL;
struct RastPort *rp  = NULL;

/* --- render pipeline --------------------------------------------- */

static void draw_status(int x0, int y0, int w)
{
    SetAPen(rp, PEN_BG);
    RectFill(rp, x0, y0, x0 + w - 1, y0 + g_status_h - 1);
    SetAPen(rp, PEN_BORDER);
    Move(rp, x0, y0); Draw(rp, x0 + w - 1, y0);

    /* Prepend selection count so the user knows what an op will hit. */
    int sel0 = selected_count(&panes[0]);
    int sel1 = selected_count(&panes[1]);
    char sel_msg[192];
    snprintf(sel_msg, sizeof(sel_msg), "[L:%d sel  R:%d sel]  %s",
             sel0, sel1, status_msg);

    SetAPen(rp, PEN_FG);
    SetBPen(rp, PEN_BG);
    SetDrMd(rp, JAM2);
    Move(rp, x0 + 4, y0 + 2 + g_baseline);
    Text(rp, (STRPTR)sel_msg, (LONG)strlen(sel_msg));
}

void redraw_all(void)
{
    PaneRect r;
    pane_rect(0, &r); draw_pane(0, r.x0, r.y0, r.w, r.h);
    pane_rect(1, &r); draw_pane(1, r.x0, r.y0, r.w, r.h);
    int status_y = g_win_h - g_status_h - g_buttons_h;
    draw_status(0, status_y, g_win_w);
    draw_buttons();
}

/* Re-cache the drawable-area size from the window's current geometry.
 * Fires on IDCMP_NEWSIZE and once at startup. Everything downstream
 * derives from g_win_w/g_win_h so the layout re-flows for free. */
static void recompute_layout(void)
{
    if (!win) return;
    g_win_w = win->Width  - win->BorderLeft - win->BorderRight;
    g_win_h = win->Height - win->BorderTop  - win->BorderBottom;
    if (g_win_w < WIN_W_MIN) g_win_w = WIN_W_MIN;
    if (g_win_h < WIN_H_MIN) g_win_h = WIN_H_MIN;
}

/* --- input dispatch ---------------------------------------------- */

static void enter_selected(void)
{
    Pane *p = &panes[active_pane];
    if (p->cursor >= p->count) return;
    Entry *e = &p->entries[p->cursor];

    /* Volume-list mode: entry names already carry the ':' - just adopt
     * as the new path and refresh normally. */
    if (is_volume_view(p) && e->is_dir) {
        strncpy(p->path, e->name, MAX_PATH - 1);
        p->path[MAX_PATH - 1] = 0;
        refresh_pane(p);
        return;
    }

    if (!e->is_dir) {
        /* Non-dir Enter → try to View it (same as the button). */
        op_view();
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
    if (is_volume_view(p)) {
        snprintf(status_msg, sizeof(status_msg), "already at volume list");
        return;
    }
    if (is_at_root(p->path)) {
        /* Past root → drop to the volume picker. */
        refresh_volumes(p);
        return;
    }
    ascend_path(p->path);
    refresh_pane(p);
}

static void toggle_selection(void)
{
    Pane *p = &panes[active_pane];
    if (p->cursor < 0 || p->cursor >= p->count) return;
    if (strcmp(p->entries[p->cursor].name, "..") == 0) return;
    p->entries[p->cursor].selected ^= 1;
    /* Advance so multi-select feels fluid. */
    if (p->cursor < p->count - 1) p->cursor++;
}

static void handle_key(UWORD raw)
{
    if (raw & 0x80) return;
    int c = raw & 0x7F;
    Pane *p = &panes[active_pane];

    switch (c) {
        case 0x4C: /* Up   */ if (p->cursor > 0)              p->cursor--; break;
        case 0x4D: /* Down */ if (p->cursor < p->count - 1)   p->cursor++; break;
        case 0x4E: /* Right (page/pane) */ if (active_pane == 0) active_pane = 1; break;
        case 0x4F: /* Left               */ if (active_pane == 1) active_pane = 0; break;
        case 0x42: /* Tab       */ active_pane ^= 1; break;
        case 0x44: /* Return    */ enter_selected(); break;
        case 0x41: /* Backspace */ ascend(); break;
        case 0x40: /* Space     */ toggle_selection(); break;
        case 0x45: /* ESC       */ running = 0; break;

        /* Letter shortcuts also work - buttons.c dispatches via bar. */
        case 0x33: /* C */ op_copy(1);   break;
        case 0x36: /* M */ op_move(1);   break;
        case 0x22: /* D */ op_delete(1); break;
        case 0x13: /* R */ op_rename();  break;
        case 0x37: /* N (mkdir) */ op_mkdir(); break;
        case 0x35: /* V */ op_view();    break;
        case 0x39: /* space alt? already 0x40 */ break;
        case 0x10: /* Q */ running = 0; break;
        default: break;
    }
}

/* Double-click support for descend-into-dir. */
static int              last_click_pane   = -1;
static int              last_click_entry  = -1;
static struct DateStamp last_click_ts     = {0, 0, 0};

static LONG ds_delta_ms(const struct DateStamp *a, const struct DateStamp *b)
{
    LONG dm = (b->ds_Minute - a->ds_Minute) * 60000L;
    LONG dt = (b->ds_Tick - a->ds_Tick) * 20L;
    return dm + dt;
}

static void handle_click(int mx, int my)
{
    /* Buttons first, so a click on the bar doesn't misfire a pane hit. */
    int btn = hit_test_button(mx, my);
    if (btn >= 0) { run_button(btn); return; }

    /* Scrollbar strip: any click there is a page-up/down/jump — never
     * a row select. Focus the pane too so keyboard follows the mouse. */
    int sb_dir;
    int sb_pane = hit_test_scrollbar(mx, my, &sb_dir);
    if (sb_pane >= 0) {
        active_pane = sb_pane;
        scrollbar_scroll(sb_pane, sb_dir, my);
        return;
    }

    int pane, entry;
    if (!hit_test_pane(mx, my, &pane, &entry)) return;
    active_pane = pane;
    if (entry < 0) return;
    panes[pane].cursor = entry;

    struct DateStamp now; DateStamp(&now);
    int is_double =
        (pane == last_click_pane && entry == last_click_entry &&
         ds_delta_ms(&last_click_ts, &now) < 500);
    last_click_pane  = pane;
    last_click_entry = entry;
    last_click_ts    = now;
    if (is_double) enter_selected();
}

/* --- main -------------------------------------------------------- */

/* Crash bisector — writes a stage marker to a log file, flushing after
 * each write. Whatever line is last in the file is the last successful
 * stage; whatever step was ABOUT to run next is the one that DSI'd. */
static BPTR g_bootlog = (BPTR)0;
static void mark(const char *stage)
{
    if (!g_bootlog) return;
    char line[128];
    snprintf(line, sizeof(line), "%s\n", stage);
    Write(g_bootlog, (STRPTR)line, (LONG)strlen(line));
    Flush(g_bootlog);
}

int main(void)
{
    /* Try PROGDIR first, fall back to RAM: (writable everywhere). */
    g_bootlog = Open((STRPTR)"RAM:masterwork.log", MODE_NEWFILE);
    if (!g_bootlog) g_bootlog = Open((STRPTR)"T:masterwork.log", MODE_NEWFILE);
    mark("00 entered main");

    IntuitionBase = (struct IntuitionBase *)OpenLibrary(
        (CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) { mark("!! OpenLibrary intuition FAILED"); return 1; }
    mark("01 opened intuition.library");
    GfxBase = (struct GfxBase *)OpenLibrary(
        (CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) { mark("!! OpenLibrary graphics FAILED"); CloseLibrary((struct Library *)IntuitionBase); return 1; }
    mark("02 opened graphics.library");

    printf("Masterwork v%s\n", VERSION);
    mark("03 printed version");
    if (ab_init("masterwork") != 0) {
        printf("  Bridge: NOT FOUND\n"); bridge_ok = 0;
        mark("04 ab_init failed (bridge_ok=0)");
    } else {
        printf("  Bridge: CONNECTED\n"); bridge_ok = 1;
        mark("04 ab_init OK (bridge_ok=1)");
    }
    AB_I("Masterwork v%s starting", VERSION);
    mark("05 AB_I sent");
    ab_register_var("running",     AB_TYPE_I32, &running);
    ab_register_var("active_pane", AB_TYPE_I32, &active_pane);
    mark("06 registered vars");

    strcpy(panes[0].path, "RAM:");
    strcpy(panes[1].path, "SYS:");
    mark("07 pane paths set");

    win = OpenWindowTags(NULL,
        WA_Left,        4,
        WA_Top,         12,      /* top of Workbench, dock at bottom */
        WA_Width,       WIN_W_INIT,
        WA_Height,      WIN_H_INIT,
        WA_MinWidth,    WIN_W_MIN,
        WA_MinHeight,   WIN_H_MIN,
        WA_MaxWidth,    ~0UL,   /* no upper bound - drag as big as Workbench */
        WA_MaxHeight,   ~0UL,
        /* Plain ASCII hyphen - topaz doesn't render UTF-8 em-dash,
         * shows as garbage. Same for other non-ASCII in title bars. */
        WA_Title,       (ULONG)"Masterwork - file manager",
        WA_CloseGadget, TRUE,
        WA_DragBar,     TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget,  TRUE,
        WA_Activate,    TRUE,
        /* NEWSIZE: fired when user drags the size gadget. We
         * recompute layout globals and full-redraw.
         * REFRESHWINDOW: fired by Intuition when part of our window
         * was hidden then re-exposed (drag, other window covered it,
         * screen depth-arrange). We reply with BeginRefresh + full
         * redraw + EndRefresh so no ghost/blank patches remain. */
        WA_IDCMP,       IDCMP_CLOSEWINDOW | IDCMP_RAWKEY
                      | IDCMP_MOUSEBUTTONS | IDCMP_REFRESHWINDOW
                      | IDCMP_NEWSIZE,
        TAG_DONE);

    if (!win) {
        AB_E("OpenWindow failed");
        mark("!! OpenWindow returned NULL");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }
    mark("08 OpenWindow OK");
    rp = win->RPort;
    recompute_layout();
    mark("09 got RPort");

    if (rp->Font) {
        int fh = rp->Font->tf_YSize;
        int bl = rp->Font->tf_Baseline;
        int cw = rp->TxWidth;
        if (fh > 0) g_row_h    = fh + 2;
        if (bl > 0) g_baseline = bl;
        if (cw > 0) g_char_w   = cw;
        g_header_h = g_row_h + 8;
        g_status_h = g_row_h + 4;
    }
    g_buttons_h = buttons_height();
    mark("10 layout computed");

    refresh_pane(&panes[0]);
    mark("11 refresh_pane RAM: OK");
    refresh_pane(&panes[1]);
    mark("12 refresh_pane SYS: OK");
    strcpy(status_msg,
        "Space=select  Enter=descend  Tab=switch  or click a button");
    redraw_all();
    mark("13 first redraw_all OK - entering event loop");

    while (running) {
        WaitPort(win->UserPort);
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            ULONG cls  = msg->Class;
            UWORD code = msg->Code;
            WORD  mx   = msg->MouseX;
            WORD  my   = msg->MouseY;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW)         running = 0;
            else if (cls == IDCMP_RAWKEY)         handle_key(code);
            else if (cls == IDCMP_MOUSEBUTTONS &&
                     code == SELECTDOWN)          handle_click(mx, my);
            else if (cls == IDCMP_NEWSIZE) {
                /* User dragged the size gadget - re-cache the drawable
                 * area and blank the whole client rect before redraw so
                 * shrinks don't leave stale pixels outside the new
                 * layout. Grow paths get the same free clear. */
                recompute_layout();
                SetAPen(rp, PEN_BG);
                RectFill(rp, 0, 0, g_win_w - 1, g_win_h - 1);
                redraw_all();
            }
            else if (cls == IDCMP_REFRESHWINDOW) {
                /* Intuition-mandated refresh sandwich - BeginRefresh
                 * takes the damage lock, we paint, EndRefresh releases
                 * it. Missing this leaves parts of the window blank
                 * whenever another window covers ours (or the user
                 * dragged past a border). */
                BeginRefresh(win);
                redraw_all();
                EndRefresh(win, TRUE);
            }
        }
        redraw_all();
        if (bridge_ok) ab_poll();
    }

    AB_I("Masterwork shutting down");
    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
