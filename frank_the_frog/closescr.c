// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins

/*
 * Close all custom screens named "Frank the Frog"
 * Run this to clean up orphan screens from crashed game instances.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <string.h>
#include <stdio.h>

#include "bridge_client.h"

struct IntuitionBase *IntuitionBase = NULL;

#define MAX_WINDOWS 32

int main(void)
{
    struct Screen *scr;
    int closed = 0;
    ULONG lock;
    int bridge_initialized = 0;

    if (ab_init("closescr") == 0) {
        bridge_initialized = 1;
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) {
        if (bridge_initialized) ab_cleanup();
        return 1;
    }

    /* Keep closing until none left */
    for (;;) {
        struct Screen *target = NULL;
        struct Window *wins[MAX_WINDOWS];
        int nwins = 0;
        int i;

        /* Snapshot target screen and its window list while holding the lock */
        lock = LockIBase(0);
        scr = IntuitionBase->FirstScreen;
        while (scr) {
            if (scr->Title && strcmp((char *)scr->Title, "Frank the Frog") == 0) {
                target = scr;
                break;
            }
            scr = scr->NextScreen;
        }
        if (target) {
            struct Window *w = target->FirstWindow;
            while (w && nwins < MAX_WINDOWS) {
                wins[nwins++] = w;
                w = w->NextWindow;
            }
        }
        UnlockIBase(lock);

        if (!target) break;

        /* Close snapshotted windows, then the screen. Must not hold the lock
         * across CloseWindow/CloseScreen. */
        for (i = 0; i < nwins; i++) {
            CloseWindow(wins[i]);
        }

        CloseScreen(target);
        closed++;
    }

    if (closed > 0) {
        char buf[64];
        sprintf(buf, "Closed %ld orphan screen(s)\n", (long)closed);
        Write(Output(), buf, strlen(buf));
    } else {
        Write(Output(), "No orphan screens found\n", 24);
    }

    CloseLibrary((struct Library *)IntuitionBase);
    if (bridge_initialized) ab_cleanup();
    return 0;
}
