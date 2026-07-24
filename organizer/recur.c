/*
 * recur.c — YYYYMMDD arithmetic + recurrence rules.
 *
 * Everything here is pure integer math over YYYYMMDD-encoded dates —
 * no dependency on the AmigaOS DateStamp/tm.h split. Makes the tests
 * trivial to run: pass integers in, integers out.
 */

#include "organizer.h"

/* --- compose / decompose ------------------------------------------ */

long ymd_make(int y, int m, int d)
{
    return (long)y * 10000L + (long)m * 100L + (long)d;
}

void ymd_split(long ymd, int *y, int *m, int *d)
{
    *y = (int)(ymd / 10000L);
    *m = (int)((ymd / 100L) % 100L);
    *d = (int)(ymd % 100L);
}

/* --- calendars ---------------------------------------------------- */

static int is_leap(int y)
{
    if ((y % 400) == 0) return 1;
    if ((y % 100) == 0) return 0;
    if ((y %   4) == 0) return 1;
    return 0;
}

static int month_days(int y, int m)
{
    static const int t[13] = { 0, 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m < 1 || m > 12) return 30;
    if (m == 2 && is_leap(y)) return 29;
    return t[m];
}

int ymd_days_in_month(long ym)
{
    int y = (int)(ym / 100L);
    int m = (int)(ym % 100L);
    return month_days(y, m);
}

/* Zeller's-ish congruence via serial-day trick.
 * Reference epoch: 2000-01-01 was a Saturday (weekday 6, Sun=0..Sat=6).
 * Compute days since epoch, mod 7. Works for any year >= 1900. */
static long days_since_epoch(long ymd)
{
    int y, m, d;
    ymd_split(ymd, &y, &m, &d);
    long days = 0;
    for (int yy = 2000; yy < y; yy++)         days += 365 + is_leap(yy);
    for (int yy = y; yy < 2000; yy++)         days -= 365 + is_leap(yy);
    for (int mm = 1; mm < m; mm++)            days += month_days(y, mm);
    days += (d - 1);
    return days;
}

int ymd_weekday(long ymd)
{
    long days = days_since_epoch(ymd);
    /* 2000-01-01 was Saturday, weekday index 6 (Sun=0). */
    long w = (6 + days) % 7;
    if (w < 0) w += 7;
    return (int)w;
}

long ymd_add_days(long ymd, int n)
{
    int y, m, d;
    ymd_split(ymd, &y, &m, &d);
    d += n;
    while (d > month_days(y, m)) {
        d -= month_days(y, m);
        m++;
        if (m > 12) { m = 1; y++; }
    }
    while (d < 1) {
        m--;
        if (m < 1) { m = 12; y--; }
        d += month_days(y, m);
    }
    return ymd_make(y, m, d);
}

/* --- recurrence --------------------------------------------------- */

int recur_fires_on(int kind, long base, long check)
{
    if (kind == RECUR_NONE) return (base == check);
    if (check < base)        return 0;
    if (check == base)       return 1;

    int by, bm, bd, cy, cm, cd;
    ymd_split(base, &by, &bm, &bd);
    ymd_split(check, &cy, &cm, &cd);

    switch (kind) {
    case RECUR_DAILY:
        return 1;
    case RECUR_WEEKLY:
        return ymd_weekday(base) == ymd_weekday(check);
    case RECUR_MONTHLY:
        /* Fires when the day-of-month matches. Feb 29 base rolls back
         * to Feb 28 in non-leap years (avoids "never fires"). */
        if (bd == cd) return 1;
        if (bd > month_days(cy, cm) && cd == month_days(cy, cm)) return 1;
        return 0;
    case RECUR_YEARLY:
        if (bm == cm && bd == cd) return 1;
        /* Same Feb 29 rollback logic. */
        if (bm == 2 && bd == 29 && cm == 2 && cd == 28 && !is_leap(cy)) return 1;
        return 0;
    }
    return 0;
}

/* --- names -------------------------------------------------------- */

const char *month_name(int m)
{
    static const char *names[13] = {
        "?", "Jan","Feb","Mar","Apr","May","Jun",
             "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    if (m < 1 || m > 12) return "?";
    return names[m];
}

const char *weekday_short(int w)
{
    static const char *names[7] = {
        "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
    };
    if (w < 0 || w > 6) return "?";
    return names[w];
}

const char *recur_name(int kind)
{
    switch (kind) {
    case RECUR_NONE:    return "-";
    case RECUR_DAILY:   return "daily";
    case RECUR_WEEKLY:  return "weekly";
    case RECUR_MONTHLY: return "monthly";
    case RECUR_YEARLY:  return "yearly";
    default:            return "?";
    }
}
