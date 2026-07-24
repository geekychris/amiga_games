/*
 * storage.c — persistence to S:organizer/*.txt.
 *
 * File format (all files):
 *   Line 1:  # organizer <name> v1
 *   Line 2+: TypeChar|field|field|...\n
 *
 * Files:
 *   S:organizer/notes.txt   — records start with 'N'
 *   S:organizer/tasks.txt   — records start with 'T'
 *   S:organizer/events.txt  — records start with 'E'
 *
 * Fields with | inside them get replaced with '~' on write (loss-tolerant,
 * user-editable). Empty fields OK. Unknown lines are skipped so future
 * versions can add fields without breaking older tools.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "organizer.h"
#include "bridge_client.h"

#define NOTES_PATH  "S:organizer/notes.txt"
#define TASKS_PATH  "S:organizer/tasks.txt"
#define EVENTS_PATH "S:organizer/events.txt"

/* --- helpers ------------------------------------------------------- */

static void sanitize_field(char *s)
{
    for (; *s; s++) {
        if (*s == '|' || *s == '\n' || *s == '\r') *s = '~';
    }
}

/* Split `s` in place on '|', filling up to `max` field ptrs. Returns
 * the actual count. The buffer is mutated (delims → NUL). */
static int split_pipes(char *s, char **fields, int max)
{
    int n = 0;
    fields[n++] = s;
    for (char *p = s; *p; p++) {
        if (*p == '|') {
            *p = 0;
            if (n < max) fields[n++] = p + 1;
        } else if (*p == '\n' || *p == '\r') {
            *p = 0;
        }
    }
    return n;
}

/* --- init --------------------------------------------------------- */

int storage_init(void)
{
    /* Create S:organizer/ if missing. CreateDir returns a lock we
     * immediately UnLock — we only wanted the side effect. */
    BPTR lk = CreateDir((STRPTR)"S:organizer");
    if (lk) { UnLock(lk); return 0; }
    /* Already existed? Test by locking. */
    BPTR test = Lock((STRPTR)"S:organizer", ACCESS_READ);
    if (test) { UnLock(test); return 0; }
    return -1;
}

/* --- today -------------------------------------------------------- */

void storage_today_string(char *out, size_t outsz)
{
    int y, m, d;
    ymd_split(g_today, &y, &m, &d);
    snprintf(out, outsz, "%04d-%02d-%02d", y, m, d);
}

void storage_stamp_today(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    /* Amiga days are since 1978-01-01. Convert to Y/M/D via a manual
     * walk — dos.library has StamptoStr but that's locale-formatted. */
    long days = ds.ds_Days;
    int y = 1978, m = 1, d = 1;
    static const int mdays_normal[13] = { 0, 31,28,31,30,31,30,31,31,30,31,30,31 };
    while (1) {
        int leap = ((y % 400) == 0) || (((y % 100) != 0) && ((y % 4) == 0));
        int yd = leap ? 366 : 365;
        if (days < yd) break;
        days -= yd;
        y++;
    }
    while (1) {
        int leap = ((y % 400) == 0) || (((y % 100) != 0) && ((y % 4) == 0));
        int md = mdays_normal[m] + (m == 2 && leap ? 1 : 0);
        if (days < md) break;
        days -= md;
        m++;
        if (m > 12) { m = 1; y++; }
    }
    d = 1 + (int)days;
    g_today = ymd_make(y, m, d);
}

/* --- notes ------------------------------------------------------- */

int storage_save_notes(void)
{
    BPTR f = Open((STRPTR)NOTES_PATH, MODE_NEWFILE);
    if (!f) return -1;
    const char *hdr = "# organizer notes v1\n";
    Write(f, (STRPTR)hdr, (LONG)strlen(hdr));
    char line[MAX_TITLE_LEN + MAX_BODY_LEN + MAX_TAGS_LEN + 64];
    for (int i = 0; i < g_notes_count; i++) {
        Note *n = &g_notes[i];
        char title[MAX_TITLE_LEN], body[MAX_BODY_LEN], tags[MAX_TAGS_LEN];
        strncpy(title, n->title, sizeof(title)); title[sizeof(title)-1] = 0;
        strncpy(body,  n->body,  sizeof(body));  body [sizeof(body)-1]  = 0;
        strncpy(tags,  n->tags,  sizeof(tags));  tags [sizeof(tags)-1]  = 0;
        sanitize_field(title);
        sanitize_field(body);
        sanitize_field(tags);
        snprintf(line, sizeof(line), "N|%d|%ld|%s|%s|%s\n",
                 n->id, n->created, tags, title, body);
        Write(f, (STRPTR)line, (LONG)strlen(line));
    }
    Close(f);
    return 0;
}

static int load_notes_file(void)
{
    g_notes_count = 0;
    BPTR f = Open((STRPTR)NOTES_PATH, MODE_OLDFILE);
    if (!f) return 0;   /* absent file = empty state, that's fine */
    char line[MAX_TITLE_LEN + MAX_BODY_LEN + MAX_TAGS_LEN + 64];
    while (FGets(f, (STRPTR)line, sizeof(line))) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (line[0] != 'N') continue;
        if (g_notes_count >= MAX_NOTES) break;
        char *fields[8];
        int nf = split_pipes(line, fields, 8);
        if (nf < 5) continue;
        Note *n = &g_notes[g_notes_count++];
        n->id      = atoi(fields[1]);
        n->created = atol(fields[2]);
        strncpy(n->tags,  fields[3], sizeof(n->tags));  n->tags [sizeof(n->tags)-1]  = 0;
        strncpy(n->title, fields[4], sizeof(n->title)); n->title[sizeof(n->title)-1] = 0;
        if (nf >= 6) { strncpy(n->body, fields[5], sizeof(n->body)); n->body[sizeof(n->body)-1] = 0; }
        else         { n->body[0] = 0; }
    }
    Close(f);
    return g_notes_count;
}

/* --- tasks ------------------------------------------------------- */

/* Two formats supported on read:
 *   'T' (v1): id|state|prio|due|recur|tags|title
 *   'U' (v2): id|state|prio|due|recur|effort|sched|start|tags|title|notes
 * All writes emit 'U'. Missing v2 fields default to 0/empty. */
int storage_save_tasks(void)
{
    BPTR f = Open((STRPTR)TASKS_PATH, MODE_NEWFILE);
    if (!f) return -1;
    const char *hdr = "# organizer tasks v2\n";
    Write(f, (STRPTR)hdr, (LONG)strlen(hdr));
    char line[MAX_TITLE_LEN + MAX_TAGS_LEN + MAX_NOTES_LEN + 128];
    for (int i = 0; i < g_tasks_count; i++) {
        Task *t = &g_tasks[i];
        char title[MAX_TITLE_LEN], tags[MAX_TAGS_LEN], notes[MAX_NOTES_LEN];
        strncpy(title, t->title, sizeof(title)); title[sizeof(title)-1] = 0;
        strncpy(tags,  t->tags,  sizeof(tags));  tags [sizeof(tags)-1]  = 0;
        strncpy(notes, t->notes, sizeof(notes)); notes[sizeof(notes)-1] = 0;
        sanitize_field(title);
        sanitize_field(tags);
        sanitize_field(notes);
        snprintf(line, sizeof(line),
                 "U|%d|%d|%d|%ld|%d|%d|%ld|%d|%s|%s|%s\n",
                 t->id, t->state, t->priority, t->due, t->recur,
                 t->effort_min, t->scheduled_date, t->scheduled_start,
                 tags, title, notes);
        Write(f, (STRPTR)line, (LONG)strlen(line));
    }
    Close(f);
    return 0;
}

static int load_tasks_file(void)
{
    g_tasks_count = 0;
    BPTR f = Open((STRPTR)TASKS_PATH, MODE_OLDFILE);
    if (!f) return 0;
    char line[MAX_TITLE_LEN + MAX_TAGS_LEN + MAX_NOTES_LEN + 128];
    while (FGets(f, (STRPTR)line, sizeof(line))) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (line[0] != 'T' && line[0] != 'U') continue;
        if (g_tasks_count >= MAX_TASKS) break;
        char *fields[16];
        int nf = split_pipes(line, fields, 16);
        Task *t = &g_tasks[g_tasks_count];
        memset(t, 0, sizeof(*t));
        t->scheduled_start = -1;   /* -1 = unscheduled (default) */
        if (line[0] == 'T') {
            /* v1: id|state|prio|due|recur|tags|title */
            if (nf < 8) continue;
            t->id       = atoi(fields[1]);
            t->state    = atoi(fields[2]);
            t->priority = atoi(fields[3]);
            t->due      = atol(fields[4]);
            t->recur    = atoi(fields[5]);
            strncpy(t->tags,  fields[6], sizeof(t->tags));  t->tags [sizeof(t->tags)-1]  = 0;
            strncpy(t->title, fields[7], sizeof(t->title)); t->title[sizeof(t->title)-1] = 0;
        } else {
            /* v2: id|state|prio|due|recur|effort|sched|start|tags|title|notes */
            if (nf < 11) continue;
            t->id             = atoi(fields[1]);
            t->state          = atoi(fields[2]);
            t->priority       = atoi(fields[3]);
            t->due            = atol(fields[4]);
            t->recur          = atoi(fields[5]);
            t->effort_min     = atoi(fields[6]);
            t->scheduled_date = atol(fields[7]);
            t->scheduled_start= atoi(fields[8]);
            strncpy(t->tags,  fields[9],  sizeof(t->tags));  t->tags [sizeof(t->tags)-1]  = 0;
            strncpy(t->title, fields[10], sizeof(t->title)); t->title[sizeof(t->title)-1] = 0;
            if (nf > 11) {
                strncpy(t->notes, fields[11], sizeof(t->notes));
                t->notes[sizeof(t->notes)-1] = 0;
            }
        }
        g_tasks_count++;
    }
    Close(f);
    return g_tasks_count;
}

/* --- events ------------------------------------------------------ */

/* Two formats supported on read:
 *   'E' (v1): id|date|time|recur|tags|title
 *   'F' (v2): id|date|start|end|recur|tags|title|attendees|url|notes
 * All writes emit 'F'. Missing v2 fields default to empty. */
int storage_save_events(void)
{
    BPTR f = Open((STRPTR)EVENTS_PATH, MODE_NEWFILE);
    if (!f) return -1;
    const char *hdr = "# organizer events v2\n";
    Write(f, (STRPTR)hdr, (LONG)strlen(hdr));
    char line[MAX_TITLE_LEN + MAX_TAGS_LEN + MAX_ATTENDEES_LEN + MAX_URL_LEN + MAX_NOTES_LEN + 128];
    for (int i = 0; i < g_events_count; i++) {
        Event *e = &g_events[i];
        char title[MAX_TITLE_LEN], tags[MAX_TAGS_LEN];
        char att[MAX_ATTENDEES_LEN], url[MAX_URL_LEN], notes[MAX_NOTES_LEN];
        strncpy(title, e->title,     sizeof(title)); title[sizeof(title)-1] = 0;
        strncpy(tags,  e->tags,      sizeof(tags));  tags [sizeof(tags)-1]  = 0;
        strncpy(att,   e->attendees, sizeof(att));   att  [sizeof(att)-1]   = 0;
        strncpy(url,   e->url,       sizeof(url));   url  [sizeof(url)-1]   = 0;
        strncpy(notes, e->notes,     sizeof(notes)); notes[sizeof(notes)-1] = 0;
        sanitize_field(title);
        sanitize_field(tags);
        sanitize_field(att);
        sanitize_field(url);
        sanitize_field(notes);
        snprintf(line, sizeof(line),
                 "F|%d|%ld|%d|%d|%d|%s|%s|%s|%s|%s\n",
                 e->id, e->date, e->start_time, e->end_time, e->recur,
                 tags, title, att, url, notes);
        Write(f, (STRPTR)line, (LONG)strlen(line));
    }
    Close(f);
    return 0;
}

static int load_events_file(void)
{
    g_events_count = 0;
    BPTR f = Open((STRPTR)EVENTS_PATH, MODE_OLDFILE);
    if (!f) return 0;
    char line[MAX_TITLE_LEN + MAX_TAGS_LEN + MAX_ATTENDEES_LEN + MAX_URL_LEN + MAX_NOTES_LEN + 128];
    while (FGets(f, (STRPTR)line, sizeof(line))) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (line[0] != 'E' && line[0] != 'F') continue;
        if (g_events_count >= MAX_EVENTS) break;
        char *fields[16];
        int nf = split_pipes(line, fields, 16);
        Event *e = &g_events[g_events_count];
        memset(e, 0, sizeof(*e));
        if (line[0] == 'E') {
            /* v1: id|date|time|recur|tags|title */
            if (nf < 7) continue;
            e->id         = atoi(fields[1]);
            e->date       = atol(fields[2]);
            e->start_time = atoi(fields[3]);
            e->end_time   = -1;
            e->recur      = atoi(fields[4]);
            strncpy(e->tags,  fields[5], sizeof(e->tags));  e->tags [sizeof(e->tags)-1]  = 0;
            strncpy(e->title, fields[6], sizeof(e->title)); e->title[sizeof(e->title)-1] = 0;
        } else {
            /* v2: id|date|start|end|recur|tags|title|attendees|url|notes */
            if (nf < 8) continue;
            e->id         = atoi(fields[1]);
            e->date       = atol(fields[2]);
            e->start_time = atoi(fields[3]);
            e->end_time   = atoi(fields[4]);
            e->recur      = atoi(fields[5]);
            strncpy(e->tags,  fields[6], sizeof(e->tags));  e->tags [sizeof(e->tags)-1]  = 0;
            strncpy(e->title, fields[7], sizeof(e->title)); e->title[sizeof(e->title)-1] = 0;
            if (nf >  8) { strncpy(e->attendees, fields[8], sizeof(e->attendees));
                           e->attendees[sizeof(e->attendees)-1] = 0; }
            if (nf >  9) { strncpy(e->url, fields[9], sizeof(e->url));
                           e->url[sizeof(e->url)-1] = 0; }
            if (nf > 10) { strncpy(e->notes, fields[10], sizeof(e->notes));
                           e->notes[sizeof(e->notes)-1] = 0; }
        }
        g_events_count++;
    }
    Close(f);
    return g_events_count;
}

/* --- batch ------------------------------------------------------- */

int storage_load_all(void)
{
    int n = load_notes_file();
    int t = load_tasks_file();
    int e = load_events_file();
    return n + t + e;
}

int storage_save_all(void)
{
    int rc = 0;
    if (storage_save_notes()  != 0) rc |= 1;
    if (storage_save_tasks()  != 0) rc |= 2;
    if (storage_save_events() != 0) rc |= 4;
    return rc;
}
