// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * miner-meteor — clean-room single-screen tile platformer, dual-target
 * (classic 68k + PPC OS4). No copyrighted asset / code from the games
 * it's inspired by; mechanics only. See README.md.
 *
 * Controls (rawkey codes):
 *   LEFT / RIGHT   walk
 *   SPACE          jump (also start / continue on title / end screens)
 *   ESC            quit
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <devices/inputevent.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"
#include "game.h"
#include "render.h"
#include "audio.h"

#ifndef __PPC__
/* Classic 68k needs the app to own the library bases; on OS4 the
 * proto headers extern these already. */
struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;
#endif

/* Bigger stack: 64K covers the level ASCII tables + render buffers
 * with a safe margin. Matches void_trader / fractalus. */
ULONG __stack = 65536;

/* Raw-key codes. */
#define RK_ESC     0x45
#define RK_SPACE   0x40
#define RK_LEFT    0x4F
#define RK_RIGHT   0x4E
#define RK_UP      0x4C
#define RK_A       0x20
#define RK_D       0x22

/* Input handler port + IO. We tap input.device to sniff RAW_KEY events
 * across the whole system — matches the pattern used by fractalus and
 * void_trader. This is polite: we don't consume events, we just watch. */
static struct MsgPort    *input_port  = NULL;
static struct IOStdReq   *input_req   = NULL;
static struct Interrupt   input_int;
static UBYTE              key_state[128];   /* 1 = held */

/* Called from input.device interrupt context. Keep it tiny + reentrant. */
static struct InputEvent *__saveds __asm input_handler(
    register __a0 struct InputEvent *events,
    register __a1 APTR /*userdata*/)
{
    for (struct InputEvent *e = events; e; e = e->ie_NextEvent) {
        if (e->ie_Class == IECLASS_RAWKEY) {
            UBYTE code = e->ie_Code & 0x7F;
            UBYTE up   = (e->ie_Code & IECODE_UP_PREFIX) ? 1 : 0;
            key_state[code] = up ? 0 : 1;
        }
    }
    return events;
}

#ifdef __PPC__
/* PPC has no register-argument __asm syntax — use a plain hook instead.
 * The full input-hook setup is a follow-up on OS4; for now we poll
 * keys via a stubbed reader that reads nothing. Movement is still
 * exercised via the bridge (see the "input_flags" register_var). */
static UBYTE ppc_input_flags = 0;
#endif

static UBYTE read_input_flags(void)
{
#ifdef __PPC__
    /* Bridge-driven; ppc_input_flags is written via ab_hook. */
    return ppc_input_flags;
#else
    UBYTE f = 0;
    if (key_state[RK_LEFT]  || key_state[RK_A]) f |= INPUT_LEFT;
    if (key_state[RK_RIGHT] || key_state[RK_D]) f |= INPUT_RIGHT;
    if (key_state[RK_SPACE] || key_state[RK_UP]) f |= INPUT_JUMP;
    if (key_state[RK_SPACE]) f |= INPUT_START;
    return f;
#endif
}

#ifdef __PPC__
static LONG hook_input(const char *args)
{
    /* args = "flags" — decimal integer. */
    if (!args) return 0;
    ppc_input_flags = (UBYTE)atoi(args);
    return 0;
}
#endif

#ifndef __PPC__
static LONG open_input_device(void)
{
    input_port = CreateMsgPort();
    if (!input_port) return 1;
    input_req = (struct IOStdReq *)CreateIORequest(input_port, sizeof(struct IOStdReq));
    if (!input_req) return 2;
    if (OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)input_req, 0)) return 3;

    input_int.is_Node.ln_Type = NT_INTERRUPT;
    input_int.is_Node.ln_Pri  = 51;                         /* above default keymap */
    input_int.is_Node.ln_Name = (STRPTR)"miner-meteor-input";
    input_int.is_Data         = NULL;
    input_int.is_Code         = (void (*)())input_handler;

    input_req->io_Command = IND_ADDHANDLER;
    input_req->io_Data    = (APTR)&input_int;
    DoIO((struct IORequest *)input_req);
    return 0;
}

static void close_input_device(void)
{
    if (input_req) {
        input_req->io_Command = IND_REMHANDLER;
        input_req->io_Data    = (APTR)&input_int;
        DoIO((struct IORequest *)input_req);
        CloseDevice((struct IORequest *)input_req);
        DeleteIORequest((struct IORequest *)input_req);
        input_req = NULL;
    }
    if (input_port) {
        DeleteMsgPort(input_port);
        input_port = NULL;
    }
}
#endif

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

#ifndef __PPC__
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 33);
    GfxBase       = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 33);
    if (!IntuitionBase || !GfxBase) return 20;
#endif

    if (ab_init((char *)"miner-meteor")) {
        /* Bridge unavailable — game still runs, just uninstrumented. */
    }

    if (render_open() != 0) {
        AB_E("render: open failed");
        return 20;
    }

    /* Bridge instrumentation. */
    GameState state;
    memset(&state, 0, sizeof(state));

    MinerGame game;
    game.init(&state);

    ab_register_var((char *)"mode",           AB_TYPE_I32, &state.mode);
    ab_register_var((char *)"level",          AB_TYPE_I32, &state.level_index);
    ab_register_var((char *)"lives",          AB_TYPE_I32, &state.lives);
    ab_register_var((char *)"score",          AB_TYPE_I32, &state.score);
    ab_register_var((char *)"air",            AB_TYPE_I32, &state.air);
    ab_register_var((char *)"keys_remaining", AB_TYPE_I32, &state.keys_remaining);

#ifdef __PPC__
    ab_register_hook((char *)"input", (char *)"input_flags integer", hook_input);
#else
    if (open_input_device() != 0) {
        AB_E("input.device: open failed");
    }
#endif

    audio_init();

    AB_I("miner-meteor: entering main loop");

    LONG running = 1;
    UBYTE prev_input = 0;

    while (running) {
        ab_poll();

        UBYTE input = read_input_flags();
        UBYTE edge  = input & ~prev_input;
        prev_input  = input;

        /* SPACE is JUMP during play + START on title / end screens.
         * Only pass INPUT_START on rising edge so the same press
         * doesn't repeatedly re-start the game frame after frame. */
        UBYTE tick_input = (UBYTE)(input & (INPUT_LEFT | INPUT_RIGHT | INPUT_JUMP));
        if (edge & INPUT_START) tick_input |= INPUT_START;

        game.tick(tick_input);

        /* SFX edges. */
        if ((edge & INPUT_JUMP) && state.player.on_ground == 0) sfx_jump();

        /* Draw. */
        if (state.mode == GM_TITLE) {
            render_title(state);
        } else if (state.mode == GM_WIN || state.mode == GM_LOSE) {
            render_endscreen(state);
        } else {
            render_frame(state);
        }

        ab_poll();

        /* Simple frame cap: WaitTOF gives 50Hz on classic PAL; on OS4
         * WaitTOF is a no-op on some RTG boards. Our tick counter is
         * still deterministic per-frame, so gameplay speed varies but
         * mechanics stay correct. Real timer-based cap is a follow-up. */
        WaitTOF();

#ifndef __PPC__
        /* ESC to quit on classic (interrupt handler already captured it). */
        if (key_state[RK_ESC]) running = 0;
#else
        /* On PPC quit via bridge STOP; the daemon delivers SIGBREAKF_CTRL_C. */
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) running = 0;
#endif
    }

    AB_I("miner-meteor: shutting down");
    audio_shutdown();
    render_close();
#ifndef __PPC__
    close_input_device();
#endif
    ab_cleanup();

#ifndef __PPC__
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
#endif
    return 0;
}
