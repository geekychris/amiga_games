// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* host_main.c - the test suite built natively: make host-test */
#include <stdio.h>
#include "../sc.h"

static int pass, fail;

static void check(int cond, const char *name)
{
    if (cond) pass++;
    else { fail++; printf("FAIL %s\n", name); }
}

int main(void)
{
    sc_run_tests(check);
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
