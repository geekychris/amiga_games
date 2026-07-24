/*
 * dialogs.c — CON: prompts + EasyRequest confirmations.
 *
 * Same shape as masterwork/dialogs.c — a CON: window is a portable
 * fallback that doesn't need gadtools.library. Slight downside is
 * the shell window pops up, but on OS4 it slots into the dock like
 * any other console.
 */

#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include "organizer.h"

int prompt_string(const char *title, const char *deflt, char *out, size_t outsz)
{
    if (!out || outsz == 0) return -1;
    char con_spec[192];
    snprintf(con_spec, sizeof(con_spec),
             "CON:60/60/560/80/%s/CLOSE/AUTO/WAIT",
             title ? title : "Organizer");
    BPTR c = Open((STRPTR)con_spec, MODE_NEWFILE);
    if (!c) return -1;
    if (deflt && *deflt) {
        char line[MAX_TITLE_LEN + 40];
        snprintf(line, sizeof(line), "(default: %s)\n> ", deflt);
        Write(c, (STRPTR)line, (LONG)strlen(line));
    } else {
        Write(c, (STRPTR)"> ", 2);
    }
    char buf[512];
    LONG n = Read(c, (STRPTR)buf, (LONG)(sizeof(buf) - 1));
    Close(c);
    if (n <= 0) return -1;
    buf[n] = 0;
    /* Trim CR/LF. */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    if (n == 0) {
        if (deflt) { strncpy(out, deflt, outsz - 1); out[outsz - 1] = 0; return 0; }
        return -1;
    }
    strncpy(out, buf, outsz - 1);
    out[outsz - 1] = 0;
    return 0;
}

int prompt_int(const char *title, long deflt, long *out)
{
    char s[64], deflt_s[32];
    snprintf(deflt_s, sizeof(deflt_s), "%ld", deflt);
    if (prompt_string(title, deflt_s, s, sizeof(s)) != 0) return -1;
    *out = atol(s);
    return 0;
}

int confirm(const char *fmt, const char *arg)
{
    struct EasyStruct es;
    char body[192];
    snprintf(body, sizeof(body), fmt, arg ? arg : "");
    es.es_StructSize   = sizeof(es);
    es.es_Flags        = 0;
    es.es_Title        = (STRPTR)"Organizer";
    es.es_TextFormat   = (STRPTR)body;
    es.es_GadgetFormat = (STRPTR)"OK|Cancel";
    LONG rc = EasyRequestArgs(g_win, &es, NULL, NULL);
    return (rc == 1) ? 1 : 0;
}
