// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins

/*
 * System Monitor - AmigaBridge Client Demo
 *
 * Monitors Amiga system state (memory, tasks, CPU) and exposes
 * everything via the bridge daemon for MCP server to read.
 *
 * Demonstrates: logging, variable registration, heartbeat,
 * memory dumps, and custom EXEC commands.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
extern struct ExecBase *SysBase;

/* Monitored state - all exposed as variables.
 * The five stats reported via the "stats" memregion are packed into
 * one contiguous struct in the exact order documented on that region. */
struct stats_region {
    LONG chip_free;
    LONG fast_free;
    LONG chip_largest;
    LONG fast_largest;
    LONG task_count;
};
static struct stats_region g_stats = { 0, 0, 0, 0, 0 };
#define chip_free      (g_stats.chip_free)
#define fast_free      (g_stats.fast_free)
#define chip_largest   (g_stats.chip_largest)
#define fast_largest   (g_stats.fast_largest)
#define task_count     (g_stats.task_count)

static ULONG frame = 0;
static LONG chip_total = 0;
static LONG fast_total = 0;
static LONG cpu_usage = 0;       /* simulated 0-100 */
static LONG update_delay = 5;    /* ticks between updates (~10fps default) */
static LONG log_interval = 300;  /* frames between detailed stats log */
static char status_msg[64] = "starting";

/* Display state */
#define WIN_W 400
#define WIN_H 200
#define BAR_X 10
#define BAR_W 380
#define BAR_H 12
#define TEXT_Y 20

/* Hook: take a snapshot of all stats
 * NOTE: Do NOT call ab_log/ab_push_var inside hooks - it sends a message
 * to the daemon which is waiting for OUR reply, causing deadlock. */
static int hook_snapshot(const char *args, char *resultBuf, int bufSize)
{
    if (bufSize <= 0) return 0;
    snprintf(resultBuf, (size_t)bufSize,
             "chip=%ld fast=%ld tasks=%ld cpu=%ld%%",
             (long)chip_free, (long)fast_free,
             (long)task_count, (long)cpu_usage);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

/* Hook: set status message */
static int hook_set_status(const char *args, char *resultBuf, int bufSize)
{
    if (args && args[0]) {
        strncpy(status_msg, args, sizeof(status_msg) - 1);
        status_msg[sizeof(status_msg) - 1] = '\0';
        strncpy(resultBuf, "Status set", bufSize - 1);
    } else {
        strncpy(resultBuf, status_msg, bufSize - 1);
    }
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

/* Custom command handler */
static void cmd_handler(ULONG id, const char *data)
{
    if (strcmp(data, "snapshot") == 0) {
        /* Push all variables at once */
        ab_push_var("chip_free");
        ab_push_var("fast_free");
        ab_push_var("task_count");
        ab_push_var("cpu_usage");
        ab_push_var("status_msg");
        AB_I("Snapshot: chip=%ld fast=%ld tasks=%ld cpu=%ld%%",
             (long)chip_free, (long)fast_free, (long)task_count, (long)cpu_usage);
        ab_cmd_respond(id, "ok", "Snapshot sent");
    }
    else if (strcmp(data, "memmap") == 0) {
        /* Send a dump of low chip memory (ExecBase pointer region) */
        ab_send_mem((APTR)0x00000004, 64);
        AB_I("Memory map sent (ExecBase pointer region)");
        ab_cmd_respond(id, "ok", "Memory map sent");
    }
    else if (strncmp(data, "msg ", 4) == 0) {
        strncpy(status_msg, data + 4, sizeof(status_msg) - 1);
        status_msg[sizeof(status_msg) - 1] = '\0';
        AB_I("Status message set to: %s", status_msg);
        ab_cmd_respond(id, "ok", "Status message updated");
    }
    else {
        AB_W("Unknown command: %s", data);
        ab_cmd_respond(id, "err", "Unknown command (try: snapshot, memmap, msg <text>)");
    }
}

static LONG count_tasks(void)
{
    struct Node *node;
    LONG count = 0;

    Disable();
    for (node = SysBase->TaskReady.lh_Head;
         node->ln_Succ; node = node->ln_Succ) {
        count++;
    }
    for (node = SysBase->TaskWait.lh_Head;
         node->ln_Succ; node = node->ln_Succ) {
        count++;
    }
    count++; /* current task */
    Enable();

    return count;
}

static void update_stats(void)
{
    chip_free = (LONG)AvailMem(MEMF_CHIP);
    fast_free = (LONG)AvailMem(MEMF_FAST);
    chip_largest = (LONG)AvailMem(MEMF_CHIP | MEMF_LARGEST);
    fast_largest = (LONG)AvailMem(MEMF_FAST | MEMF_LARGEST);
    chip_total = (LONG)AvailMem(MEMF_CHIP | MEMF_TOTAL);
    fast_total = (LONG)AvailMem(MEMF_FAST | MEMF_TOTAL);
    task_count = count_tasks();
}

static void draw_bar(struct RastPort *rp, LONG y, LONG value, LONG max,
                     UBYTE color, const char *label)
{
    LONG bar_len;
    char text[80];

    if (max <= 0) max = 1;
    /* Divide first to avoid overflow when value*BAR_W would exceed LONG range
     * (e.g., memory byte counts). This keeps precision reasonable while
     * ensuring the multiply stays within LONG. */
    if (value <= 0) {
        bar_len = 0;
    } else if (value >= max) {
        bar_len = BAR_W;
    } else {
        bar_len = (value / (max / BAR_W ? max / BAR_W : 1));
        if (bar_len > BAR_W) bar_len = BAR_W;
        if (bar_len < 0) bar_len = 0;
    }

    /* Background */
    SetAPen(rp, 0);
    RectFill(rp, BAR_X, y, BAR_X + BAR_W, y + BAR_H);

    /* Bar */
    SetAPen(rp, color);
    if (bar_len > 0) {
        RectFill(rp, BAR_X, y, BAR_X + bar_len, y + BAR_H);
    }

    /* Label */
    snprintf(text, sizeof(text), "%s: %ld / %ld",
             label, (long)value, (long)max);
    SetAPen(rp, 1);
    Move(rp, BAR_X, y + BAR_H - 2);
    Text(rp, (CONST_STRPTR)text, strlen(text));
}

static void draw_text(struct RastPort *rp, LONG y, const char *text)
{
    LONG len = strlen(text);
    /* Clear line */
    SetAPen(rp, 0);
    RectFill(rp, BAR_X, y - 8, BAR_X + BAR_W, y + 2);
    /* Draw text */
    SetAPen(rp, 1);
    Move(rp, BAR_X, y);
    Text(rp, (CONST_STRPTR)text, len);
}

int main(void)
{
    struct Window *win;
    struct IntuiMessage *msg;
    struct RastPort *rp;
    ULONG class;
    BOOL loop = TRUE;
    char buf[80];
    LONG prev_chip = 0;
    LONG prev_tasks = 0;

    /* Connect to AmigaBridge daemon FIRST, before any fallible init */
    if (ab_init("system_monitor") != 0) {
        printf("Bridge: NOT FOUND\n");
    } else {
        printf("Bridge: CONNECTED\n");
    }

    IntuitionBase = (struct IntuitionBase *)
        OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) { ab_cleanup(); return 1; }

    GfxBase = (struct GfxBase *)
        OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        ab_cleanup();
        return 1;
    }

    AB_I("System Monitor starting");

    /* Register all variables */
    ab_register_var("frame", AB_TYPE_U32, &frame);
    ab_register_var("chip_free", AB_TYPE_I32, &chip_free);
    ab_register_var("fast_free", AB_TYPE_I32, &fast_free);
    ab_register_var("chip_largest", AB_TYPE_I32, &chip_largest);
    ab_register_var("fast_largest", AB_TYPE_I32, &fast_largest);
    ab_register_var("chip_total", AB_TYPE_I32, &chip_total);
    ab_register_var("fast_total", AB_TYPE_I32, &fast_total);
    ab_register_var("task_count", AB_TYPE_I32, &task_count);
    ab_register_var("cpu_usage", AB_TYPE_I32, &cpu_usage);
    ab_register_var("update_delay", AB_TYPE_I32, &update_delay);
    ab_register_var("log_interval", AB_TYPE_I32, &log_interval);
    ab_register_var("status_msg", AB_TYPE_STR, status_msg);

    ab_set_cmd_handler(cmd_handler);

    /* Register hooks */
    ab_register_hook("snapshot", "Push all stats and return summary", hook_snapshot);
    ab_register_hook("set_status", "Set or get status message", hook_set_status);

    /* Register memory region for stats inspection.
     * g_stats is a contiguous struct laid out in exactly this order:
     * chip_free, fast_free, chip_largest, fast_largest, task_count. */
    ab_register_memregion("stats", &g_stats, sizeof(g_stats),
                          "System stats (chip_free, fast_free, chip_largest, fast_largest, task_count)");

    /* Get initial stats for display scale */
    update_stats();
    prev_chip = chip_free;
    prev_tasks = task_count;

    AB_I("Initial chip: %ld, fast: %ld, tasks: %ld",
           (long)chip_free, (long)fast_free, (long)task_count);

    /* Open window */
    win = OpenWindowTags(NULL,
        WA_Left, 20,
        WA_Top, 20,
        WA_Width, WIN_W,
        WA_Height, WIN_H,
        WA_Title, (ULONG)"System Monitor",
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_Activate, TRUE,
        WA_GimmeZeroZero, TRUE,
        TAG_DONE);

    if (!win) {
        AB_E("Failed to open window");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    rp = win->RPort;
    AB_I("Window opened");
    strncpy(status_msg, "running", sizeof(status_msg));

    /* Main loop */
    while (loop) {
        /* Check window messages */
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = msg->Class;
            ReplyMsg((struct Message *)msg);
            if (class == IDCMP_CLOSEWINDOW) {
                loop = FALSE;
                AB_I("Close requested");
            }
        }
        if (!loop) break;

        /* Check for CTRL-C break signal */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            AB_I("Break signal received");
            loop = FALSE;
            break;
        }

        /* Update system stats */
        update_stats();

        /* Simulate CPU usage (varies randomly) */
        cpu_usage += ((LONG)(frame * 7 + 13) % 11) - 5;
        if (cpu_usage < 0) cpu_usage = 0;
        if (cpu_usage > 100) cpu_usage = 100;

        frame++;

        /* Draw UI */
        snprintf(buf, sizeof(buf), "Frame: %lu  Status: %s",
                 (unsigned long)frame, status_msg);
        draw_text(rp, TEXT_Y, buf);

        draw_bar(rp, TEXT_Y + 10, chip_free, chip_total > 0 ? chip_total : 512000, 2, "Chip");
        draw_bar(rp, TEXT_Y + 30, fast_free, fast_total > 0 ? fast_total : 1048576, 3, "Fast");
        draw_bar(rp, TEXT_Y + 50, cpu_usage, 100, 1, "CPU%");

        snprintf(buf, sizeof(buf), "Tasks: %ld  Chip largest: %ld  Fast largest: %ld",
                 (long)task_count, (long)chip_largest, (long)fast_largest);
        draw_text(rp, TEXT_Y + 80, buf);

        /* Log significant changes */
        if (chip_free < prev_chip - 10000) {
            AB_W("Chip memory dropped: %ld -> %ld (-%ld)",
                   (long)prev_chip, (long)chip_free,
                   (long)(prev_chip - chip_free));
        }
        if (task_count != prev_tasks) {
            AB_I("Task count changed: %ld -> %ld",
                 (long)prev_tasks, (long)task_count);
        }
        prev_chip = chip_free;
        prev_tasks = task_count;

        /* Heartbeat + push key vars every 60 frames */
        if (frame % 60 == 0) {
            ab_push_var("chip_free");
            ab_push_var("fast_free");
            ab_push_var("chip_total");
            ab_push_var("fast_total");
            ab_push_var("task_count");
            ab_push_var("cpu_usage");
            ab_heartbeat();
        }

        /* Detailed stats log every log_interval frames */
        if (log_interval > 0 && (frame % (ULONG)log_interval) == 0) {
            AB_I("Stats: chip=%ld fast=%ld tasks=%ld cpu=%ld%%",
                   (long)chip_free, (long)fast_free,
                   (long)task_count, (long)cpu_usage);
        }

        /* Poll for commands from bridge daemon */
        ab_poll();

        /* Adjustable update rate */
        Delay(update_delay < 1 ? 1 : update_delay);
    }

    strncpy(status_msg, "shutting down", sizeof(status_msg));
    AB_I("System Monitor shutting down");

    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
