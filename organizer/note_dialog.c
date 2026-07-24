/*
 * note_dialog.c — modal note editor with multi-line body + format toolbar.
 *
 * Fields:
 *   Title  — single-line StringGadget
 *   Tags   — single-line StringGadget
 *   Body   — NOTE_BODY_ROWS stacked StringGadgets, each NOTE_BODY_COLS chars
 *   Format toolbar — B, I, H, L, C buttons that insert markdown tokens
 *   OK / Cancel
 *
 * Rich-text approach: we store markdown-ish tokens in the plain-text
 * body. Preview panes in the notes list render them by switching
 * pens (bold → PEN_ACTIVE, code → PEN_MUTED). The format buttons in
 * this dialog insert the raw tokens at the cursor position of the
 * currently-focused body line — press B and `**` gets inserted where
 * you were typing. Not a full rich editor, but enough to feel useful.
 *
 * Body → single `body` char field marshalling uses "\\n" as the newline
 * escape (real newlines don't survive our pipe-delimited storage). We
 * split on it when loading into the row gadgets, and rejoin with '\n'
 * when saving back to the Note.
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

#define ND_W        620
#define ND_H        420
#define ND_LBL_X    10
#define ND_LBL_W    70
#define ND_GAD_X    (ND_LBL_X + ND_LBL_W)
#define ND_GAD_W    (ND_W - ND_GAD_X - 16)
#define ND_GAD_H    14
#define ND_ROW_H    20
#define ND_TOP      26
#define ND_TOOLBAR_Y (ND_TOP + ND_ROW_H * 2 + 6)
#define ND_TOOLBAR_H 18
#define ND_BODY_Y   (ND_TOOLBAR_Y + ND_TOOLBAR_H + 10)
#define ND_BODY_W   (ND_W - ND_LBL_X - 16)
#define ND_BTN_W    80
#define ND_BTN_H    20
#define ND_BTN_Y    (ND_H - ND_BTN_H - 12)

/* ---- string buffers ------------------------------------------- */

static UBYTE nd_undo[NOTE_BODY_COLS + 8];

#define STR_FIELD(name, size) \
    static UBYTE  nd_buf_##name[(size) + 2]; \
    static struct StringInfo nd_si_##name = { \
        nd_buf_##name, nd_undo, 0, (size), \
        0, 0, 0, 0, 0, 0, NULL, 0, NULL \
    }

STR_FIELD(title, MAX_TITLE_LEN);
STR_FIELD(tags,  MAX_TAGS_LEN);

/* Body: NOTE_BODY_ROWS individual buffers + StringInfos. We don't
 * use STR_FIELD macro here since the count is compile-time constant. */
static UBYTE  nd_body_bufs[NOTE_BODY_ROWS][NOTE_BODY_COLS + 2];
static struct StringInfo nd_body_sis[NOTE_BODY_ROWS];

/* ---- gadget IDs ------------------------------------------------ */

enum {
    NDID_TITLE = 1,
    NDID_TAGS,
    NDID_BODY_BASE,               /* body row 0..NOTE_BODY_ROWS-1 */
    NDID_TB_BOLD = 100,           /* toolbar buttons */
    NDID_TB_ITALIC,
    NDID_TB_HEADER,
    NDID_TB_LIST,
    NDID_TB_CODE,
    NDID_OK,
    NDID_CANCEL
};

/* ---- toolbar buttons ------------------------------------------ */

static struct IntuiText nd_it_bold  = { 1, 0, JAM1,  6, 4, NULL, (UBYTE*)"**B**",     NULL };
static struct IntuiText nd_it_ital  = { 1, 0, JAM1,  8, 4, NULL, (UBYTE*)"_I_",       NULL };
static struct IntuiText nd_it_hdr   = { 1, 0, JAM1,  6, 4, NULL, (UBYTE*)"# H1",      NULL };
static struct IntuiText nd_it_list  = { 1, 0, JAM1,  6, 4, NULL, (UBYTE*)"- List",    NULL };
static struct IntuiText nd_it_code  = { 1, 0, JAM1,  8, 4, NULL, (UBYTE*)"`code`",    NULL };
static struct IntuiText nd_it_ok    = { 1, 0, JAM1, 30, 6, NULL, (UBYTE*)"OK",        NULL };
static struct IntuiText nd_it_can   = { 1, 0, JAM1, 20, 6, NULL, (UBYTE*)"Cancel",    NULL };

#define TB_BTN_W  70
#define TB_X0     ND_GAD_X

static struct Gadget nd_g_cancel = {
    NULL,
    ND_W - ND_BTN_W - 16, ND_BTN_Y,
    ND_BTN_W, ND_BTN_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    NULL, NULL, &nd_it_can, 0, NULL, NDID_CANCEL, NULL
};
static struct Gadget nd_g_ok = {
    &nd_g_cancel,
    16, ND_BTN_Y,
    ND_BTN_W, ND_BTN_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    NULL, NULL, &nd_it_ok, 0, NULL, NDID_OK, NULL
};

/* Toolbar buttons (right-to-left so we can chain via NextGadget). */
static struct Gadget nd_g_tb_code = {
    &nd_g_ok,
    TB_X0 + TB_BTN_W * 4 + 4 * 4, ND_TOOLBAR_Y,
    TB_BTN_W, ND_TOOLBAR_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    NULL, NULL, &nd_it_code, 0, NULL, NDID_TB_CODE, NULL
};
static struct Gadget nd_g_tb_list = {
    &nd_g_tb_code,
    TB_X0 + TB_BTN_W * 3 + 4 * 3, ND_TOOLBAR_Y,
    TB_BTN_W, ND_TOOLBAR_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    NULL, NULL, &nd_it_list, 0, NULL, NDID_TB_LIST, NULL
};
static struct Gadget nd_g_tb_hdr = {
    &nd_g_tb_list,
    TB_X0 + TB_BTN_W * 2 + 4 * 2, ND_TOOLBAR_Y,
    TB_BTN_W, ND_TOOLBAR_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    NULL, NULL, &nd_it_hdr, 0, NULL, NDID_TB_HEADER, NULL
};
static struct Gadget nd_g_tb_ital = {
    &nd_g_tb_hdr,
    TB_X0 + TB_BTN_W * 1 + 4 * 1, ND_TOOLBAR_Y,
    TB_BTN_W, ND_TOOLBAR_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    NULL, NULL, &nd_it_ital, 0, NULL, NDID_TB_ITALIC, NULL
};
static struct Gadget nd_g_tb_bold = {
    &nd_g_tb_ital,
    TB_X0, ND_TOOLBAR_Y,
    TB_BTN_W, ND_TOOLBAR_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    NULL, NULL, &nd_it_bold, 0, NULL, NDID_TB_BOLD, NULL
};

/* Body row gadgets — one per row. Filled by init_body_gadgets(). */
static struct Gadget nd_body_gads[NOTE_BODY_ROWS];

/* Title / Tags gadgets (linked to first body row via NextGadget). */
static struct Gadget nd_g_tags = {
    NULL,  /* filled in init to point at first body gadget */
    ND_GAD_X, ND_TOP + ND_ROW_H,
    ND_GAD_W, ND_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0, &nd_si_tags, NDID_TAGS, NULL
};
static struct Gadget nd_g_title = {
    &nd_g_tags,
    ND_GAD_X, ND_TOP,
    ND_GAD_W, ND_GAD_H,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT, GTYP_STRGADGET,
    NULL, NULL, NULL, 0, &nd_si_title, NDID_TITLE, NULL
};

/* Track which body row was last active — used by the toolbar buttons
 * to know where to insert format tokens. -1 = none focused yet. */
static int nd_active_body_row = -1;

/* Compile the body-row gadget list into `nd_body_gads` and link
 * them: tags → body[0] → body[1] → ... → body[N-1] → toolbar_bold. */
static void init_body_gadgets(void)
{
    for (int i = 0; i < NOTE_BODY_ROWS; i++) {
        nd_body_sis[i].Buffer     = nd_body_bufs[i];
        nd_body_sis[i].UndoBuffer = nd_undo;
        nd_body_sis[i].BufferPos  = 0;
        nd_body_sis[i].MaxChars   = NOTE_BODY_COLS;
        nd_body_sis[i].DispPos    = 0;
        nd_body_sis[i].UndoPos    = 0;
        nd_body_sis[i].NumChars   = 0;
        nd_body_sis[i].DispCount  = 0;
        nd_body_sis[i].CLeft      = 0;
        nd_body_sis[i].CTop       = 0;
        nd_body_sis[i].Extension  = NULL;
        nd_body_sis[i].LongInt    = 0;
        nd_body_sis[i].AltKeyMap  = NULL;

        nd_body_gads[i].NextGadget    = (i + 1 < NOTE_BODY_ROWS)
                                        ? &nd_body_gads[i + 1]
                                        : &nd_g_tb_bold;
        nd_body_gads[i].LeftEdge      = ND_LBL_X;
        nd_body_gads[i].TopEdge       = ND_BODY_Y + i * ND_ROW_H;
        nd_body_gads[i].Width         = ND_BODY_W;
        nd_body_gads[i].Height        = ND_GAD_H;
        nd_body_gads[i].Flags         = GFLG_GADGHCOMP;
        nd_body_gads[i].Activation    = GACT_RELVERIFY | GACT_STRINGLEFT;
        nd_body_gads[i].GadgetType    = GTYP_STRGADGET;
        nd_body_gads[i].GadgetRender  = NULL;
        nd_body_gads[i].SelectRender  = NULL;
        nd_body_gads[i].GadgetText    = NULL;
        nd_body_gads[i].MutualExclude = 0;
        nd_body_gads[i].SpecialInfo   = &nd_body_sis[i];
        nd_body_gads[i].GadgetID      = NDID_BODY_BASE + i;
        nd_body_gads[i].UserData      = NULL;
    }
    /* Wire tags → first body row. */
    nd_g_tags.NextGadget = &nd_body_gads[0];
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

/* Split note->body on real \n into up to NOTE_BODY_ROWS row buffers. */
static void load_note(const Note *n)
{
    set_str(nd_buf_title, sizeof(nd_buf_title), &nd_si_title, n->title);
    set_str(nd_buf_tags,  sizeof(nd_buf_tags),  &nd_si_tags,  n->tags);

    const char *src = n->body;
    for (int row = 0; row < NOTE_BODY_ROWS; row++) {
        char *dst = (char*)nd_body_bufs[row];
        int   j   = 0;
        while (*src && *src != '\n' && j < NOTE_BODY_COLS) {
            dst[j++] = *src++;
        }
        dst[j] = 0;
        if (*src == '\n') src++;
        nd_body_sis[row].BufferPos = j;
        nd_body_sis[row].NumChars  = j;
        nd_body_sis[row].DispPos   = 0;
    }
}

/* Rejoin row buffers with '\n' into note->body, dropping trailing
 * empties. */
static void save_note(Note *n)
{
    strncpy(n->title, (char*)nd_buf_title, sizeof(n->title) - 1);
    n->title[sizeof(n->title) - 1] = 0;
    strncpy(n->tags,  (char*)nd_buf_tags,  sizeof(n->tags) - 1);
    n->tags [sizeof(n->tags)  - 1] = 0;

    /* Find last non-empty row so we don't stash a trailing pile
     * of blank lines. */
    int last = -1;
    for (int row = 0; row < NOTE_BODY_ROWS; row++) {
        if (nd_body_bufs[row][0]) last = row;
    }
    char *dst = n->body;
    int   cap = (int)sizeof(n->body);
    int   pos = 0;
    for (int row = 0; row <= last; row++) {
        int len = (int)strlen((char*)nd_body_bufs[row]);
        if (pos + len + 2 >= cap) len = cap - pos - 2;
        if (len < 0) len = 0;
        memcpy(dst + pos, nd_body_bufs[row], len);
        pos += len;
        if (row < last && pos + 1 < cap) dst[pos++] = '\n';
    }
    dst[pos] = 0;
}

/* ---- format-token insertion ----------------------------------- */

/* Insert `tok` into the currently-active body row's buffer at
 * BufferPos, then refresh that gadget and re-activate it so the
 * user keeps typing right where they left off. Silently no-ops if
 * no body row has been activated yet. */
static void insert_token(struct Window *w, const char *tok)
{
    if (nd_active_body_row < 0 || nd_active_body_row >= NOTE_BODY_ROWS) {
        /* Default to first row so the button is never a total no-op. */
        nd_active_body_row = 0;
    }
    struct StringInfo *si  = &nd_body_sis[nd_active_body_row];
    UBYTE *buf             = nd_body_bufs[nd_active_body_row];
    int    tlen            = (int)strlen(tok);
    int    cur             = si->BufferPos;
    int    len             = (int)strlen((char*)buf);
    if (len + tlen >= NOTE_BODY_COLS) return;         /* row is full */
    /* Shift right to make room. */
    for (int i = len; i >= cur; i--) buf[i + tlen] = buf[i];
    for (int i = 0; i < tlen; i++)   buf[cur + i]  = tok[i];
    si->NumChars  = len + tlen;
    si->BufferPos = cur + tlen;
    /* Remove + re-add the gadget so Intuition redraws it with the new
     * content. Cheap on a single-gadget list. */
    RefreshGList(&nd_body_gads[nd_active_body_row], w, NULL, 1);
    ActivateGadget(&nd_body_gads[nd_active_body_row], w, NULL);
}

/* ---- labels + hint -------------------------------------------- */

static void draw_labels_and_borders(struct Window *w)
{
    struct RastPort *r = w->RPort;
    SetAPen(r, 1); SetBPen(r, 0); SetDrMd(r, JAM2);
    int bl = r->TxBaseline;

    Move(r, ND_LBL_X, ND_TOP + bl + 1);
    Text(r, (STRPTR)"Title:", 6);
    Move(r, ND_LBL_X, ND_TOP + ND_ROW_H + bl + 1);
    Text(r, (STRPTR)"Tags:", 5);

    Move(r, ND_LBL_X, ND_BODY_Y - g_row_h + bl - 4);
    Text(r, (STRPTR)"Body (Tab / Shift-Tab between rows):", 36);

    Move(r, ND_LBL_X, ND_H - ND_BTN_H - g_row_h - 4 + bl);
    const char *hint =
        "Format tokens: **bold**  _italic_  # H1  - list  `code`";
    Text(r, (STRPTR)hint, (LONG)strlen(hint));

    /* Draw 1-px borders around all editable gadgets so the click
     * targets are visible. */
    SetAPen(r, 1);
    static const int rects[][4] = {
        { ND_GAD_X, ND_TOP,             ND_GAD_W, ND_GAD_H },
        { ND_GAD_X, ND_TOP + ND_ROW_H,  ND_GAD_W, ND_GAD_H },
    };
    for (int i = 0; i < (int)(sizeof(rects)/sizeof(rects[0])); i++) {
        int x = rects[i][0] - 2, y = rects[i][1] - 2;
        int W = rects[i][2] + 4, H = rects[i][3] + 4;
        Move(r, x, y); Draw(r, x + W - 1, y);
        Draw(r, x + W - 1, y + H - 1); Draw(r, x, y + H - 1); Draw(r, x, y);
    }
    for (int i = 0; i < NOTE_BODY_ROWS; i++) {
        int x = ND_LBL_X - 2, y = ND_BODY_Y + i * ND_ROW_H - 2;
        int W = ND_BODY_W + 4, H = ND_GAD_H + 4;
        Move(r, x, y); Draw(r, x + W - 1, y);
        Draw(r, x + W - 1, y + H - 1); Draw(r, x, y + H - 1); Draw(r, x, y);
    }
}

/* ---- public entry --------------------------------------------- */

int note_dialog_run(Note *inout)
{
    init_body_gadgets();
    load_note(inout);
    nd_active_body_row = 0;

    int scr_w = (g_win && g_win->WScreen) ? g_win->WScreen->Width  : 640;
    int scr_h = (g_win && g_win->WScreen) ? g_win->WScreen->Height : 480;
    int wx = (scr_w - ND_W) / 2, wy = (scr_h - ND_H) / 2;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;

    struct Window *dlg = OpenWindowTags(NULL,
        WA_Left,        wx,
        WA_Top,         wy,
        WA_Width,       ND_W,
        WA_Height,      ND_H,
        WA_Title,       (ULONG)"Edit Note",
        WA_DragBar,     TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_Activate,    TRUE,
        WA_Gadgets,     (ULONG)&nd_g_title,
        WA_IDCMP,       IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN
                      | IDCMP_REFRESHWINDOW,
        TAG_DONE);
    if (!dlg) return 0;

    draw_labels_and_borders(dlg);
    ActivateGadget(&nd_g_title, dlg, NULL);

    int result = 0, running = 1;
    while (running) {
        WaitPort(dlg->UserPort);
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(dlg->UserPort))) {
            ULONG cls = msg->Class;
            struct Gadget *gad = (struct Gadget *)msg->IAddress;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) { running = 0; break; }
            else if (cls == IDCMP_GADGETDOWN) {
                if (gad) {
                    int id = gad->GadgetID;
                    if (id >= NDID_BODY_BASE &&
                        id <  NDID_BODY_BASE + NOTE_BODY_ROWS) {
                        nd_active_body_row = id - NDID_BODY_BASE;
                    }
                }
            }
            else if (cls == IDCMP_GADGETUP) {
                if (!gad) continue;
                int id = gad->GadgetID;
                if (id == NDID_OK)      { result = 1; running = 0; break; }
                if (id == NDID_CANCEL)  { result = 0; running = 0; break; }
                if (id == NDID_TB_BOLD)   { insert_token(dlg, "****");        continue; }
                if (id == NDID_TB_ITALIC) { insert_token(dlg, "__");          continue; }
                if (id == NDID_TB_HEADER) { insert_token(dlg, "# ");          continue; }
                if (id == NDID_TB_LIST)   { insert_token(dlg, "- ");          continue; }
                if (id == NDID_TB_CODE)   { insert_token(dlg, "``");          continue; }
                /* Enter in a StringGadget just returns the buffer;
                 * nothing to do here — the row that emitted GADGETUP
                 * stays as the last-active row. */
                if (id >= NDID_BODY_BASE &&
                    id <  NDID_BODY_BASE + NOTE_BODY_ROWS) {
                    nd_active_body_row = id - NDID_BODY_BASE;
                }
            }
            else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(dlg);
                draw_labels_and_borders(dlg);
                EndRefresh(dlg, TRUE);
            }
        }
    }

    if (result) save_note(inout);
    CloseWindow(dlg);
    return result;
}
