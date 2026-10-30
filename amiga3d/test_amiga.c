// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* test_amiga.c - a game's tests (run_tests in its test cases) on a classic
 * Amiga, reported over the bridge test harness too: NAME_test */
#include <stdio.h>
#include "bridge_client.h"

int run_tests(void (*check)(int cond, const char *name));
extern const char test_client[];

static int pass, fail;

static void check(int cond, const char *name)
{
    if (cond) pass++; else { fail++; printf("FAIL %s\n", name); }
    ab_test_assert(cond, name, "test_cases.c", 0);
}

int main(void)
{
    ab_init(test_client);
    ab_test_begin(test_client);
    run_tests(check);
    ab_test_end();
    printf("%s: %ld passed, %ld failed\n", test_client, (long)pass, (long)fail);
    ab_cleanup();
    return fail ? 10 : 0;
}
