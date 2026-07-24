/*
 * dialogs.c — string prompts (CON:) + yes/no confirms (EasyRequest).
 *
 * Why CON: instead of a StringGadget popup?
 *   - No gadtools.library dependency required — works on any Amiga
 *     with a Shell (i.e. every one from 2.x onward).
 *   - The console handler owns line editing (arrow keys, backspace,
 *     history-ish) for free; no per-key input state to manage.
 *   - Sits in front of Workbench modally without needing a whole
 *     Intuition sub-window with its own event loop.
 *
 * Downside: it takes focus away from the main Masterwork window
 * while the user types. Fine for a rename or mkdir; would be wrong
 * for something interactive like a live filter box.
 */

#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include "masterwork.h"

int prompt_string(const char *title, const char *deflt,
                  char *out, size_t outsz)
{
    if (!out || outsz < 2) return 1;
    out[0] = 0;

    /* Compose the CON: URL — CON:X/Y/W/H/Title/CLOSE/AUTO gives us a
     * 80×3 line window that auto-opens on read, closes on write, and
     * has a proper title bar. AUTO means Wait until first I/O. */
    char conurl[MAX_PATH + 96];
    snprintf(conurl, sizeof(conurl),
             "CON:60/60/560/80/%s/CLOSE/AUTO/WAIT", title);

    BPTR con = Open((STRPTR)conurl, MODE_NEWFILE);
    if (!con) {
        snprintf(status_msg, sizeof(status_msg),
                 "prompt: CON: open failed");
        return 1;
    }

    /* Show the default value on its own line so the user can just
     * press Return to accept it (they'd still have to retype though —
     * plain CON: doesn't pre-populate the input buffer). */
    if (deflt && deflt[0]) {
        FPuts(con, (STRPTR)"Default: ");
        FPuts(con, (STRPTR)deflt);
        FPuts(con, (STRPTR)"\n");
    }
    FPuts(con, (STRPTR)"> ");
    Flush(con);

    if (!FGets(con, (STRPTR)out, (LONG)outsz - 1)) {
        Close(con);
        snprintf(status_msg, sizeof(status_msg), "prompt: cancelled");
        return 1;
    }
    Close(con);

    /* Strip trailing newline / spaces. */
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' ||
                     out[n - 1] == ' '  || out[n - 1] == '\t')) {
        out[--n] = 0;
    }

    if (n == 0 && deflt && deflt[0]) {
        /* Empty response with a default → use the default. */
        strncpy(out, deflt, outsz - 1);
        out[outsz - 1] = 0;
    }

    return (out[0] == 0) ? 1 : 0;
}

/*
 * Modal yes/no via EasyRequest. The vararg version needs a struct
 * that lives across the call — declared static to keep things simple.
 * Body accepts one printf-style %s substitution because most callers
 * want to include a filename in the prompt.
 */
int confirm(const char *fmt, const char *arg1)
{
    struct EasyStruct es;
    es.es_StructSize   = sizeof(es);
    es.es_Flags        = 0;
    es.es_Title        = (STRPTR)"Masterwork";
    es.es_TextFormat   = (STRPTR)fmt;
    es.es_GadgetFormat = (STRPTR)"Yes|Cancel";

    ULONG r = EasyRequestArgs(win, &es, NULL, (APTR)&arg1);
    return (r == 1);
}
