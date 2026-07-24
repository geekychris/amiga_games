/*
 * event_dialog.c — modal event editor window.
 *
 * Replaces the sequential-CON:-prompts approach with a proper Intuition
 * dialog: one StringGadget per field, all visible at once, OK/Cancel
 * buttons at the bottom. Window is centred on the current screen, has
 * a close gadget (== cancel), and blocks its own event loop until the
 * user picks a button.
 *
 * Layout uses static Gadget/StringInfo/IntuiText structs — no gadtools
 * dependency, portable to both 68k and PPC. The `undo` buffer is shared
 * across all fields (only the currently-active field uses it).
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include "organizer.h"

/* ---- layout ---------------------------------------------------- */

#define DLG_W       560
#define DLG_H       300
#define LBL_X       10
#define LBL_W       90
#define GAD_H       14
#define ROW_H       22
#define TOP_Y       26     /* clears title bar */
#define BTN_H       18
#define BTN_W       80
#define GAD_FULL_X  (LBL_X + LBL_W)
#define GAD_FULL_W  (DLG_W - GAD_FULL_X - 16)
#define GAD_HALF_W  ((GAD_FULL_W - 8 - LBL_W) / 2)
#define GAD_RIGHT_X (GAD_FULL_X + GAD_HALF_W + 8 + LBL_W)

/* ---- string buffers + StringInfo ------------------------------- */

static UBYTE undo[MAX_NOTES_LEN + 4];

#define STR_FIELD(name, size) \
    static UBYTE  buf_##name[(size) + 2]; \
    static struct StringInfo si_##name = { \
        buf_##name, undo, 0, (size), \
        0, 0, 0, 0, 0, 0, NULL, 0, NULL \
    }

STR_FIELD(title,     MAX_TITLE_LEN);
STR_FIELD(date,      12);
STR_FIELD(start,     8);
STR_FIELD(end,       8);
STR_FIELD(recur,     4);
STR_FIELD(tags,      MAX_TAGS_LEN);
STR_FIELD(attendees, MAX_ATTENDEES_LEN);
STR_FIELD(url,       MAX_URL_LEN);
STR_FIELD(notes,     MAX_NOTES_LEN);

/* ---- gadget IDs ------------------------------------------------ */

enum {
    GID_TITLE = 1, GID_DATE, GID_RECUR,
    GID_START, GID_END,
    GID_TAGS, GID_ATTENDEES, GID_URL, GID_NOTES,
    GID_OK, GID_CANCEL
};

/* ---- OK/Cancel buttons ---------------------------------------- */

static struct IntuiText it_ok = {
    1, 0, JAM1, 30, 5, NULL, (UBYTE*)"OK", NULL
};
static struct IntuiText it_cancel = {
    1, 0, JAM1, 18, 5, NULL, (UBYTE*)"Cancel", NULL
};

static struct Gadget g_cancel = {
    NULL,
    DLG_W - BTN_W - 16, DLG_H - BTN_H - 12,
    BTN_W, BTN_H,
    GFLG_GADGHCOMP,
    GACT_RELVERIFY,
    GTYP_BOOLGADGET,
    NULL, NULL, &it_cancel, 0,
    NULL, GID_CANCEL, NULL
};
static struct Gadget g_ok = {
    &g_cancel,
    16, DLG_H - BTN_H - 12,
    BTN_W, BTN_H,
    GFLG_GADGHCOMP,
    GACT_RELVERIFY,
    GTYP_BOOLGADGET,
    NULL, NULL, &it_ok, 0,
    NULL, GID_OK, NULL
};

/* ---- string field gadgets (chained tail-to-head) --------------- */

static struct Gadget dg_notes = {
    &g_ok,
    GAD_FULL_X, TOP_Y + ROW_H * 6,
    GAD_FULL_W, GAD_H,
    GFLG_GADGHCOMP,
    GACT_RELVERIFY | GACT_STRINGLEFT,
    GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &si_notes, GID_NOTES, NULL
};
static struct Gadget g_url = {
    &dg_notes,
    GAD_FULL_X, TOP_Y + ROW_H * 5,
    GAD_FULL_W, GAD_H,
    GFLG_GADGHCOMP,
    GACT_RELVERIFY | GACT_STRINGLEFT,
    GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &si_url, GID_URL, NULL
};
static struct Gadget g_attendees = {
    &g_url,
    GAD_FULL_X, TOP_Y + ROW_H * 4,
    GAD_FULL_W, GAD_H,
    GFLG_GADGHCOMP,
    GACT_RELVERIFY | GACT_STRINGLEFT,
    GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &si_attendees, GID_ATTENDEES, NULL
};
static struct Gadget g_tags = {
    &g_attendees,
    GAD_FULL_X, TOP_Y + ROW_H * 3,
    GAD_FULL_W, GAD_H,
    GFLG_GADGHCOMP,
    GACT_RELVERIFY | GACT_STRINGLEFT,
    GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &si_tags, GID_TAGS, NULL
};
static struct Gadget g_end = {
    &g_tags,
    GAD_RIGHT_X, TOP_Y + ROW_H * 2,
    GAD_HALF_W, GAD_H,
    GFLG_GADGHCOMP,
    GACT_RELVERIFY | GACT_STRINGLEFT,
    GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &si_end, GID_END, NULL
};
static struct Gadget g_start = {
    &g_end,
    GAD_FULL_X, TOP_Y + ROW_H * 2,
    GAD_HALF_W, GAD_H,
    GFLG_GADGHCOMP,
    GACT_RELVERIFY | GACT_STRINGLEFT,
    GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &si_start, GID_START, NULL
};
static struct Gadget g_recur = {
    &g_start,
    GAD_RIGHT_X, TOP_Y + ROW_H * 1,
    GAD_HALF_W, GAD_H,
    GFLG_GADGHCOMP,
    GACT_RELVERIFY | GACT_STRINGLEFT,
    GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &si_recur, GID_RECUR, NULL
};
static struct Gadget g_date = {
    &g_recur,
    GAD_FULL_X, TOP_Y + ROW_H * 1,
    GAD_HALF_W, GAD_H,
    GFLG_GADGHCOMP,
    GACT_RELVERIFY | GACT_STRINGLEFT,
    GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &si_date, GID_DATE, NULL
};
static struct Gadget g_title = {
    &g_date,
    GAD_FULL_X, TOP_Y + ROW_H * 0,
    GAD_FULL_W, GAD_H,
    GFLG_GADGHCOMP,
    GACT_RELVERIFY | GACT_STRINGLEFT,
    GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &si_title, GID_TITLE, NULL
};

/* ---- static labels rendered after window opens ----------------- */

static struct {
    const char *label;
    int         row;
    int         side;   /* 0 = full-row start, 1 = right column */
} g_labels[] = {
    { "Title:",     0, 0 },
    { "Date:",      1, 0 },
    { "Recur:",     1, 1 },
    { "Start HHMM:",2, 0 },
    { "End HHMM:",  2, 1 },
    { "Tags:",      3, 0 },
    { "Attendees:", 4, 0 },
    { "URL:",       5, 0 },
    { "Notes:",     6, 0 },
};

/* Amiga StringGadgets are borderless by default. Painting a 1-pixel
 * rectangle around each gadget's rect makes the click-target visible,
 * otherwise users see labels floating in space with no visible input
 * field. We inset the rect by 1 pixel on each side so the border
 * doesn't paint over the gadget's content area. */
static void draw_gadget_borders(struct Window *w)
{
    struct RastPort *r = w->RPort;
    SetAPen(r, 1);
    static const int rects[9][4] = {
        { GAD_FULL_X,   TOP_Y + ROW_H * 0, GAD_FULL_W, GAD_H },  /* title */
        { GAD_FULL_X,   TOP_Y + ROW_H * 1, GAD_HALF_W, GAD_H },  /* date */
        { GAD_RIGHT_X,  TOP_Y + ROW_H * 1, GAD_HALF_W, GAD_H },  /* recur */
        { GAD_FULL_X,   TOP_Y + ROW_H * 2, GAD_HALF_W, GAD_H },  /* start */
        { GAD_RIGHT_X,  TOP_Y + ROW_H * 2, GAD_HALF_W, GAD_H },  /* end */
        { GAD_FULL_X,   TOP_Y + ROW_H * 3, GAD_FULL_W, GAD_H },  /* tags */
        { GAD_FULL_X,   TOP_Y + ROW_H * 4, GAD_FULL_W, GAD_H },  /* attendees */
        { GAD_FULL_X,   TOP_Y + ROW_H * 5, GAD_FULL_W, GAD_H },  /* url */
        { GAD_FULL_X,   TOP_Y + ROW_H * 6, GAD_FULL_W, GAD_H },  /* notes */
    };
    for (int i = 0; i < 9; i++) {
        int x = rects[i][0] - 2;
        int y = rects[i][1] - 2;
        int w = rects[i][2] + 4;
        int h = rects[i][3] + 4;
        Move(r, x, y);
        Draw(r, x + w - 1, y);
        Draw(r, x + w - 1, y + h - 1);
        Draw(r, x,         y + h - 1);
        Draw(r, x,         y);
    }
}

static void draw_labels_and_hint(struct Window *w)
{
    struct RastPort *r = w->RPort;
    SetAPen(r, 1); SetBPen(r, 0); SetDrMd(r, JAM2);
    int baseline = r->TxBaseline;
    for (int i = 0; i < (int)(sizeof(g_labels)/sizeof(g_labels[0])); i++) {
        int y = TOP_Y + g_labels[i].row * ROW_H + baseline + 1;
        int x = (g_labels[i].side == 0) ? LBL_X : (GAD_RIGHT_X - LBL_W);
        Move(r, x, y);
        Text(r, (STRPTR)g_labels[i].label, (LONG)strlen(g_labels[i].label));
    }
    /* Recur hint. */
    Move(r, LBL_X, TOP_Y + ROW_H * 7 + baseline + 4);
    const char *hint = "Recur: 0=none 1=daily 2=weekly 3=monthly 4=yearly     "
                       "Start/End HHMM (-1 for all-day)";
    Text(r, (STRPTR)hint, (LONG)strlen(hint));

    draw_gadget_borders(w);
}

/* ---- buffer <-> event marshalling ----------------------------- */

static void set_str_buf(UBYTE *buf, int bufsz, struct StringInfo *si, const char *src)
{
    strncpy((char*)buf, src ? src : "", bufsz - 1);
    buf[bufsz - 1] = 0;
    int len = (int)strlen((char*)buf);
    si->BufferPos = len;
    si->NumChars  = len;
    si->DispPos   = 0;
}

static void set_int_buf(UBYTE *buf, int bufsz, struct StringInfo *si, long v)
{
    snprintf((char*)buf, bufsz, "%ld", v);
    int len = (int)strlen((char*)buf);
    si->BufferPos = len;
    si->NumChars  = len;
    si->DispPos   = 0;
}

static void load_event(const Event *e)
{
    set_str_buf(buf_title,     sizeof(buf_title),     &si_title,     e->title);
    set_int_buf(buf_date,      sizeof(buf_date),      &si_date,      e->date);
    set_int_buf(buf_start,     sizeof(buf_start),     &si_start,     e->start_time);
    set_int_buf(buf_end,       sizeof(buf_end),       &si_end,       e->end_time);
    set_int_buf(buf_recur,     sizeof(buf_recur),     &si_recur,     e->recur);
    set_str_buf(buf_tags,      sizeof(buf_tags),      &si_tags,      e->tags);
    set_str_buf(buf_attendees, sizeof(buf_attendees), &si_attendees, e->attendees);
    set_str_buf(buf_url,       sizeof(buf_url),       &si_url,       e->url);
    set_str_buf(buf_notes,     sizeof(buf_notes),     &si_notes,     e->notes);
}

static void save_event(Event *e)
{
    strncpy(e->title, (char*)buf_title, sizeof(e->title) - 1);
    e->title[sizeof(e->title) - 1] = 0;
    e->date       = atol((char*)buf_date);
    e->start_time = atoi((char*)buf_start);
    e->end_time   = atoi((char*)buf_end);
    e->recur      = atoi((char*)buf_recur);
    strncpy(e->tags, (char*)buf_tags, sizeof(e->tags) - 1);
    e->tags[sizeof(e->tags) - 1] = 0;
    strncpy(e->attendees, (char*)buf_attendees, sizeof(e->attendees) - 1);
    e->attendees[sizeof(e->attendees) - 1] = 0;
    strncpy(e->url, (char*)buf_url, sizeof(e->url) - 1);
    e->url[sizeof(e->url) - 1] = 0;
    strncpy(e->notes, (char*)buf_notes, sizeof(e->notes) - 1);
    e->notes[sizeof(e->notes) - 1] = 0;
}

/* ---- public entry point --------------------------------------- */

int event_dialog_run(Event *inout)
{
    load_event(inout);

    int scr_w = (g_win && g_win->WScreen) ? g_win->WScreen->Width  : 640;
    int scr_h = (g_win && g_win->WScreen) ? g_win->WScreen->Height : 480;
    int wx = (scr_w - DLG_W) / 2;
    int wy = (scr_h - DLG_H) / 2;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;

    struct Window *dlg = OpenWindowTags(NULL,
        WA_Left,        wx,
        WA_Top,         wy,
        WA_Width,       DLG_W,
        WA_Height,      DLG_H,
        WA_Title,       (ULONG)"Edit Event",
        WA_DragBar,     TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_Activate,    TRUE,
        WA_Gadgets,     (ULONG)&g_title,
        WA_IDCMP,       IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW,
        TAG_DONE);
    if (!dlg) return 0;

    draw_labels_and_hint(dlg);

    /* Force the first field active so a cursor is immediately visible
     * — that also confirms visually that gadgets accept input.
     * Tab / Shift-Tab cycles between them per Intuition's default. */
    ActivateGadget(&g_title, dlg, NULL);

    int result  = 0;
    int running = 1;
    while (running) {
        WaitPort(dlg->UserPort);
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(dlg->UserPort))) {
            ULONG cls = msg->Class;
            struct Gadget *gad = (struct Gadget *)msg->IAddress;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) {
                running = 0; break;
            } else if (cls == IDCMP_GADGETUP) {
                if (gad && gad->GadgetID == GID_OK)     { result = 1; running = 0; break; }
                if (gad && gad->GadgetID == GID_CANCEL) { result = 0; running = 0; break; }
                /* Enter in a StringGadget - nothing to do; buffer keeps value. */
            } else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(dlg);
                draw_labels_and_hint(dlg);
                EndRefresh(dlg, TRUE);
            }
        }
    }

    if (result) save_event(inout);
    CloseWindow(dlg);
    return result;
}
