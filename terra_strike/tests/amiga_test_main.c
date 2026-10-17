// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * amiga_test_main.c - terra_strike_test: the unit tests on real 68k
 * hardware (or FS-UAE), reported over the bridge test harness so
 * devbench's amiga_run_tests collects PASS/FAIL per check.
 */
#include <stdio.h>
#include <proto/dos.h>
#include "bridge_client.h"
#include "../ts.h"

static int pass, fail;

static void check(int cond, const char *name)
{
    if (cond) pass++; else { fail++; printf("FAIL %s\n", name); }
    ab_test_assert(cond, name, "test_cases.c", 0);
    /* pace the results so they stream visibly in the devbench log */
    Delay(2);
}

int main(void)
{
    ab_init("terra_strike_test");
    ab_test_begin("terra_strike");
    ts_run_tests(check);
    ab_test_end();
    printf("terra_strike_test: %ld passed, %ld failed\n", (long)pass, (long)fail);
    ab_cleanup();
    return fail ? 10 : 0;
}
