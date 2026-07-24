/*
 * task_dialog.c — modal task editor.
 *
 * Fields (all StringGadgets):
 *   Title / State (0/1/2) / Priority (1-3) / Due (YYYYMMDD) / Effort (min)
 *   Scheduled date + start (YYYYMMDD + HHMM, -1 = unscheduled)
 *   Recur (0-4) / Tags / Notes
 * Plus OK / Cancel buttons.
 *
 * Same static-Gadget layout as event_dialog.c — see that file for the
 * pattern. When scheduled_date is non-zero the calendar day view
 * renders the task as a time-block alongside events.
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

#define TD_W       560
#define TD_H       340
#define TD_LBL_X   10
#define TD_LBL_W   90
#define TD_GAD_H   14
#define TD_ROW_H   22
#define TD_TOP     26
#define TD_BTN_H   18
#define TD_BTN_W   80

#define TD_GAD_X       (TD_LBL_X + TD_LBL_W)
#define TD_FULL_W      (TD_W - TD_GAD_X - 16)
#define TD_HALF_W      ((TD_FULL_W - 8 - TD_LBL_W) / 2)
#define TD_RIGHT_X     (TD_GAD_X + TD_HALF_W + 8 + TD_LBL_W)

/* ---- buffers -------------------------------------------------- */

static UBYTE td_undo[MAX_NOTES_LEN + 4];

#define STR_FIELD(name, size) \
    static UBYTE  td_buf_##name[(size) + 2]; \
    static struct StringInfo td_si_##name = { \
        td_buf_##name, td_undo, 0, (size), \
        0, 0, 0, 0, 0, 0, NULL, 0, NULL \
    }

STR_FIELD(title,     MAX_TITLE_LEN);
STR_FIELD(state,     4);
STR_FIELD(priority,  4);
STR_FIELD(due,       12);
STR_FIELD(effort,    8);
STR_FIELD(sched,     12);
STR_FIELD(start,     8);
STR_FIELD(recur,     4);
STR_FIELD(tags,      MAX_TAGS_LEN);
STR_FIELD(notes,     MAX_NOTES_LEN);

/* ---- gadget IDs ----------------------------------------------- */

enum {
    TDID_TITLE = 1, TDID_STATE, TDID_PRIORITY, TDID_DUE, TDID_EFFORT,
    TDID_SCHED, TDID_START, TDID_RECUR,
    TDID_TAGS, TDID_NOTES,
    TDID_OK, TDID_CANCEL
};

/* ---- OK/Cancel ------------------------------------------------ */

static struct IntuiText td_it_ok = {
    1, 0, JAM1, 30, 5, NULL, (UBYTE*)"OK", NULL
};
static struct IntuiText td_it_cancel = {
    1, 0, JAM1, 18, 5, NULL, (UBYTE*)"Cancel", NULL
};

static struct Gadget td_g_cancel = {
    NULL,
    TD_W - TD_BTN_W - 16, TD_H - TD_BTN_H - 12,
    TD_BTN_W, TD_BTN_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    NULL, NULL, &td_it_cancel, 0,
    NULL, TDID_CANCEL, NULL
};
static struct Gadget td_g_ok = {
    &td_g_cancel,
    16, TD_H - TD_BTN_H - 12,
    TD_BTN_W, TD_BTN_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    NULL, NULL, &td_it_ok, 0,
    NULL, TDID_OK, NULL
};

/* ---- string field gadgets (chained tail-to-head) --------------- */

static struct Gadget td_g_notes = {
    &td_g_ok,
    TD_GAD_X, TD_TOP + TD_ROW_H * 8,
    TD_FULL_W, TD_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &td_si_notes, TDID_NOTES, NULL
};
static struct Gadget td_g_tags = {
    &td_g_notes,
    TD_GAD_X, TD_TOP + TD_ROW_H * 7,
    TD_FULL_W, TD_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &td_si_tags, TDID_TAGS, NULL
};
static struct Gadget td_g_recur = {
    &td_g_tags,
    TD_GAD_X, TD_TOP + TD_ROW_H * 6,
    TD_HALF_W, TD_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &td_si_recur, TDID_RECUR, NULL
};
static struct Gadget td_g_start = {
    &td_g_recur,
    TD_RIGHT_X, TD_TOP + TD_ROW_H * 5,
    TD_HALF_W, TD_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &td_si_start, TDID_START, NULL
};
static struct Gadget td_g_sched = {
    &td_g_start,
    TD_GAD_X, TD_TOP + TD_ROW_H * 5,
    TD_HALF_W, TD_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &td_si_sched, TDID_SCHED, NULL
};
static struct Gadget td_g_effort = {
    &td_g_sched,
    TD_RIGHT_X, TD_TOP + TD_ROW_H * 4,
    TD_HALF_W, TD_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &td_si_effort, TDID_EFFORT, NULL
};
static struct Gadget td_g_due = {
    &td_g_effort,
    TD_GAD_X, TD_TOP + TD_ROW_H * 4,
    TD_HALF_W, TD_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &td_si_due, TDID_DUE, NULL
};
static struct Gadget td_g_priority = {
    &td_g_due,
    TD_RIGHT_X, TD_TOP + TD_ROW_H * 3,
    TD_HALF_W, TD_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &td_si_priority, TDID_PRIORITY, NULL
};
static struct Gadget td_g_state = {
    &td_g_priority,
    TD_GAD_X, TD_TOP + TD_ROW_H * 3,
    TD_HALF_W, TD_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &td_si_state, TDID_STATE, NULL
};
static struct Gadget td_g_title = {
    &td_g_state,
    TD_GAD_X, TD_TOP + TD_ROW_H * 0,
    TD_FULL_W, TD_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0,
    &td_si_title, TDID_TITLE, NULL
};

/* ---- labels --------------------------------------------------- */

static struct {
    const char *label;
    int         row;
    int         side;
} td_labels[] = {
    { "Title:",       0, 0 },
    { "State:",       3, 0 }, { "Priority:", 3, 1 },
    { "Due YYYYMMDD:",4, 0 }, { "Effort min:",4, 1 },
    { "Sched date:",  5, 0 }, { "Start HHMM:",5, 1 },
    { "Recur:",       6, 0 },
    { "Tags:",        7, 0 },
    { "Notes:",       8, 0 },
};

static void draw_labels_and_borders(struct Window *w)
{
    struct RastPort *r = w->RPort;
    SetAPen(r, 1); SetBPen(r, 0); SetDrMd(r, JAM2);
    int bl = r->TxBaseline;
    for (int i = 0; i < (int)(sizeof(td_labels)/sizeof(td_labels[0])); i++) {
        int y = TD_TOP + td_labels[i].row * TD_ROW_H + bl + 1;
        int x = (td_labels[i].side == 0) ? TD_LBL_X : (TD_RIGHT_X - TD_LBL_W);
        Move(r, x, y);
        Text(r, (STRPTR)td_labels[i].label, (LONG)strlen(td_labels[i].label));
    }
    /* Section separator + hint. */
    SetAPen(r, PEN_BORDER);
    int sep_y = TD_TOP + TD_ROW_H * 2 - 4;
    Move(r, TD_LBL_X, sep_y); Draw(r, TD_W - 16, sep_y);
    Move(r, TD_LBL_X, TD_TOP + TD_ROW_H * 2 + bl + 2);
    const char *hint = "State: 0=open 1=doing 2=done   "
                       "Priority: 1=low..3=high   "
                       "Recur: 0=none 1=daily 2=wk 3=mo 4=yr   "
                       "Start=-1 for all-day";
    Text(r, (STRPTR)hint, (LONG)strlen(hint));

    /* Field-rect borders — 1-px frame around every StringGadget. */
    SetAPen(r, 1);
    static const int rects[10][4] = {
        { TD_GAD_X,   TD_TOP + TD_ROW_H * 0, TD_FULL_W, TD_GAD_H }, /* title */
        { TD_GAD_X,   TD_TOP + TD_ROW_H * 3, TD_HALF_W, TD_GAD_H }, /* state */
        { TD_RIGHT_X, TD_TOP + TD_ROW_H * 3, TD_HALF_W, TD_GAD_H }, /* priority */
        { TD_GAD_X,   TD_TOP + TD_ROW_H * 4, TD_HALF_W, TD_GAD_H }, /* due */
        { TD_RIGHT_X, TD_TOP + TD_ROW_H * 4, TD_HALF_W, TD_GAD_H }, /* effort */
        { TD_GAD_X,   TD_TOP + TD_ROW_H * 5, TD_HALF_W, TD_GAD_H }, /* sched */
        { TD_RIGHT_X, TD_TOP + TD_ROW_H * 5, TD_HALF_W, TD_GAD_H }, /* start */
        { TD_GAD_X,   TD_TOP + TD_ROW_H * 6, TD_HALF_W, TD_GAD_H }, /* recur */
        { TD_GAD_X,   TD_TOP + TD_ROW_H * 7, TD_FULL_W, TD_GAD_H }, /* tags */
        { TD_GAD_X,   TD_TOP + TD_ROW_H * 8, TD_FULL_W, TD_GAD_H }, /* notes */
    };
    for (int i = 0; i < 10; i++) {
        int x = rects[i][0] - 2, y = rects[i][1] - 2;
        int W = rects[i][2] + 4, H = rects[i][3] + 4;
        Move(r, x, y);
        Draw(r, x + W - 1, y);
        Draw(r, x + W - 1, y + H - 1);
        Draw(r, x, y + H - 1);
        Draw(r, x, y);
    }
}

/* ---- marshalling ---------------------------------------------- */

static void set_str(UBYTE *buf, int bufsz, struct StringInfo *si, const char *src)
{
    strncpy((char*)buf, src ? src : "", bufsz - 1);
    buf[bufsz - 1] = 0;
    int len = (int)strlen((char*)buf);
    si->BufferPos = len;
    si->NumChars  = len;
    si->DispPos   = 0;
}
static void set_int(UBYTE *buf, int bufsz, struct StringInfo *si, long v)
{
    snprintf((char*)buf, bufsz, "%ld", v);
    int len = (int)strlen((char*)buf);
    si->BufferPos = len;
    si->NumChars  = len;
    si->DispPos   = 0;
}

static void load_task(const Task *t)
{
    set_str(td_buf_title,    sizeof(td_buf_title),    &td_si_title,    t->title);
    set_int(td_buf_state,    sizeof(td_buf_state),    &td_si_state,    t->state);
    set_int(td_buf_priority, sizeof(td_buf_priority), &td_si_priority, t->priority);
    set_int(td_buf_due,      sizeof(td_buf_due),      &td_si_due,      t->due);
    set_int(td_buf_effort,   sizeof(td_buf_effort),   &td_si_effort,   t->effort_min);
    set_int(td_buf_sched,    sizeof(td_buf_sched),    &td_si_sched,    t->scheduled_date);
    set_int(td_buf_start,    sizeof(td_buf_start),    &td_si_start,    t->scheduled_start);
    set_int(td_buf_recur,    sizeof(td_buf_recur),    &td_si_recur,    t->recur);
    set_str(td_buf_tags,     sizeof(td_buf_tags),     &td_si_tags,     t->tags);
    set_str(td_buf_notes,    sizeof(td_buf_notes),    &td_si_notes,    t->notes);
}

static void save_task(Task *t)
{
    strncpy(t->title, (char*)td_buf_title, sizeof(t->title) - 1);
    t->title[sizeof(t->title) - 1] = 0;
    t->state          = atoi((char*)td_buf_state);
    t->priority       = atoi((char*)td_buf_priority);
    t->due            = atol((char*)td_buf_due);
    t->effort_min     = atoi((char*)td_buf_effort);
    t->scheduled_date = atol((char*)td_buf_sched);
    t->scheduled_start= atoi((char*)td_buf_start);
    t->recur          = atoi((char*)td_buf_recur);
    strncpy(t->tags,  (char*)td_buf_tags,  sizeof(t->tags)  - 1);
    t->tags [sizeof(t->tags)  - 1] = 0;
    strncpy(t->notes, (char*)td_buf_notes, sizeof(t->notes) - 1);
    t->notes[sizeof(t->notes) - 1] = 0;

    /* Clamp obviously-broken values so downstream code doesn't crash. */
    if (t->state    < ST_OPEN)     t->state    = ST_OPEN;
    if (t->state    > ST_DONE)     t->state    = ST_DONE;
    if (t->priority < 1)           t->priority = 1;
    if (t->priority > 3)           t->priority = 3;
    if (t->recur    < RECUR_NONE)  t->recur    = RECUR_NONE;
    if (t->recur    > RECUR_YEARLY)t->recur    = RECUR_YEARLY;
    if (t->effort_min < 0)         t->effort_min = 0;
}

/* ---- entry ---------------------------------------------------- */

int task_dialog_run(Task *inout)
{
    load_task(inout);

    int scr_w = (g_win && g_win->WScreen) ? g_win->WScreen->Width  : 640;
    int scr_h = (g_win && g_win->WScreen) ? g_win->WScreen->Height : 480;
    int wx = (scr_w - TD_W) / 2, wy = (scr_h - TD_H) / 2;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;

    struct Window *dlg = OpenWindowTags(NULL,
        WA_Left,        wx,
        WA_Top,         wy,
        WA_Width,       TD_W,
        WA_Height,      TD_H,
        WA_Title,       (ULONG)"Edit Task",
        WA_DragBar,     TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_Activate,    TRUE,
        WA_Gadgets,     (ULONG)&td_g_title,
        WA_IDCMP,       IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW,
        TAG_DONE);
    if (!dlg) return 0;

    draw_labels_and_borders(dlg);
    ActivateGadget(&td_g_title, dlg, NULL);

    int result = 0, running = 1;
    while (running) {
        WaitPort(dlg->UserPort);
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(dlg->UserPort))) {
            ULONG cls = msg->Class;
            struct Gadget *gad = (struct Gadget *)msg->IAddress;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) { running = 0; break; }
            else if (cls == IDCMP_GADGETUP) {
                if (gad && gad->GadgetID == TDID_OK)     { result = 1; running = 0; break; }
                if (gad && gad->GadgetID == TDID_CANCEL) { result = 0; running = 0; break; }
            }
            else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(dlg);
                draw_labels_and_borders(dlg);
                EndRefresh(dlg, TRUE);
            }
        }
    }

    if (result) save_task(inout);
    CloseWindow(dlg);
    return result;
}
