/*
 * tests.c — ab_test suite: recur math, date arithmetic, storage roundtrip.
 *
 * Wired as a bridge hook (hook_test in main.c → tests_run_all). Trigger
 * via `amiga_call_hook client=organizer hook=test`. Results stream over
 * the bridge as TEST_* messages; `amiga_run_tests client=organizer`
 * gives you a structured pass/fail report.
 *
 * Intentionally standalone — no UI dependency, safe to run any time.
 */

#include <string.h>
#include <stdio.h>

#include "organizer.h"
#include "bridge_client.h"

/* Save + restore of any state we might touch, so running tests never
 * corrupts the user's real data. */
static Note   saved_notes[MAX_NOTES];
static Task   saved_tasks[MAX_TASKS];
static Event  saved_events[MAX_EVENTS];
static int    saved_nc, saved_tc, saved_ec;

static void save_state(void)
{
    memcpy(saved_notes,  g_notes,  sizeof(saved_notes));
    memcpy(saved_tasks,  g_tasks,  sizeof(saved_tasks));
    memcpy(saved_events, g_events, sizeof(saved_events));
    saved_nc = g_notes_count;
    saved_tc = g_tasks_count;
    saved_ec = g_events_count;
}

static void restore_state(void)
{
    memcpy(g_notes,  saved_notes,  sizeof(g_notes));
    memcpy(g_tasks,  saved_tasks,  sizeof(g_tasks));
    memcpy(g_events, saved_events, sizeof(g_events));
    g_notes_count = saved_nc;
    g_tasks_count = saved_tc;
    g_events_count = saved_ec;
}

int tests_run_all(void)
{
    if (!g_bridge_ok) return 0;   /* nothing to report to */

    save_state();

    ab_test_begin("organizer");

    /* --- ymd math ------------------------------------------------ */
    AB_ASSERT(ymd_make(2026, 7, 24) == 20260724L, "ymd_make sanity");
    { int y,m,d; ymd_split(20250101L, &y, &m, &d);
      AB_ASSERT(y == 2025 && m == 1 && d == 1, "ymd_split sanity"); }
    AB_ASSERT(ymd_add_days(20260131L, 1) == 20260201L, "add 1 rolls month");
    AB_ASSERT(ymd_add_days(20261231L, 1) == 20270101L, "add 1 rolls year");
    AB_ASSERT(ymd_add_days(20260301L, -1) == 20260228L, "sub 1 rolls month (non-leap)");
    AB_ASSERT(ymd_add_days(20240301L, -1) == 20240229L, "sub 1 in leap year");
    AB_ASSERT(ymd_days_in_month(202602L) == 28, "feb 2026 = 28 days");
    AB_ASSERT(ymd_days_in_month(202402L) == 29, "feb 2024 = 29 days");
    AB_ASSERT(ymd_days_in_month(210002L) == 28, "feb 2100 = 28 days (century rule)");
    AB_ASSERT(ymd_days_in_month(200002L) == 29, "feb 2000 = 29 days (400 rule)");

    /* --- weekday ------------------------------------------------ */
    AB_ASSERT(ymd_weekday(20000101L) == 6, "2000-01-01 was Saturday");
    AB_ASSERT(ymd_weekday(20260724L) == 5, "2026-07-24 was Friday");

    /* --- recur -------------------------------------------------- */
    AB_ASSERT( recur_fires_on(RECUR_NONE,   20260724L, 20260724L),  "none fires on base");
    AB_ASSERT(!recur_fires_on(RECUR_NONE,   20260724L, 20260725L),  "none doesn't fire elsewhere");
    AB_ASSERT( recur_fires_on(RECUR_DAILY,  20260724L, 20260731L),  "daily fires forward");
    AB_ASSERT(!recur_fires_on(RECUR_DAILY,  20260724L, 20260723L),  "daily doesn't fire backward");
    AB_ASSERT( recur_fires_on(RECUR_WEEKLY, 20260724L, 20260731L),  "weekly fires 7 days later");
    AB_ASSERT(!recur_fires_on(RECUR_WEEKLY, 20260724L, 20260728L),  "weekly doesn't fire 4 days later");
    AB_ASSERT( recur_fires_on(RECUR_MONTHLY,20260101L, 20260401L),  "monthly matches day-of-month");
    AB_ASSERT( recur_fires_on(RECUR_MONTHLY,20260131L, 20260228L),  "monthly Jan 31 rolls to Feb 28");
    AB_ASSERT( recur_fires_on(RECUR_YEARLY, 20250724L, 20260724L),  "yearly fires next year");
    AB_ASSERT( recur_fires_on(RECUR_YEARLY, 20240229L, 20250228L),  "yearly Feb 29 rolls to Feb 28 non-leap");

    /* --- storage roundtrip ------------------------------------- */
    /* Wipe, add sample data, save, wipe, load — verify counts. */
    g_notes_count = g_tasks_count = g_events_count = 0;
    notes_add ("Test note one");
    notes_add ("Test note two");
    tasks_add ("Test task one");
    tasks_add ("Test task two");
    tasks_set_due     (g_tasks[0].id, 20260801L);
    tasks_set_priority(g_tasks[0].id, 3);
    tasks_set_recur   (g_tasks[0].id, RECUR_WEEKLY);
    events_add        ("Test event", 20260805L);

    int saved_nc2 = g_notes_count;
    int saved_tc2 = g_tasks_count;
    int saved_ec2 = g_events_count;

    AB_ASSERT(storage_save_all() == 0, "save round succeeded");
    g_notes_count = g_tasks_count = g_events_count = 0;
    storage_load_all();
    AB_ASSERT(g_notes_count  == saved_nc2, "notes count survived roundtrip");
    AB_ASSERT(g_tasks_count  == saved_tc2, "tasks count survived roundtrip");
    AB_ASSERT(g_events_count == saved_ec2, "events count survived roundtrip");
    if (g_tasks_count > 0) {
        AB_ASSERT(g_tasks[0].due      == 20260801L,    "task due survived");
        AB_ASSERT(g_tasks[0].priority == 3,            "task priority survived");
        AB_ASSERT(g_tasks[0].recur    == RECUR_WEEKLY, "task recur survived");
    }

    /* --- tasks_count_due_by ------------------------------------ */
    g_notes_count = g_tasks_count = g_events_count = 0;
    tasks_add("A"); tasks_set_due(g_tasks[0].id, 20260724L);
    tasks_add("B"); tasks_set_due(g_tasks[1].id, 20260725L);
    tasks_add("C"); tasks_set_due(g_tasks[2].id, 20260723L);
    AB_ASSERT(tasks_count_due_by(20260724L) == 2, "due-by counts prior + today");

    ab_test_end();

    /* Restore user's real data. */
    restore_state();
    storage_save_all();
    return 0;
}
