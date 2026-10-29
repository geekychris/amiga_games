// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * amiga_test_main.c - scairy_fun_test: the unit and gameplay tests on a
 * real 68k (or FS-UAE), reported over the bridge test harness so
 * devbench's amiga_run_tests collects PASS/FAIL per check.
 */
#include <stdio.h>
#include <proto/dos.h>
#include "bridge_client.h"
#include "../sc.h"

static int pass, fail;

static void check(int cond, const char *name)
{
    if (cond) pass++; else { fail++; printf("FAIL %s\n", name); }
    ab_test_assert(cond, name, "test_cases.c", 0);
}

int main(void)
{
    ab_init("scairy_fun_test");
    ab_test_begin("scairy_fun");
    sc_run_tests(check);
    ab_test_end();
    printf("scairy_fun_test: %ld passed, %ld failed\n", (long)pass, (long)fail);
    ab_cleanup();
    return fail ? 10 : 0;
}
