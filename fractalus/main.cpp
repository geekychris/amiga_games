/*
 * fractalus — Amiga homage to Rescue on Fractalus (Lucasfilm, 1985).
 *
 * Phase 1: fractal terrain, cockpit HUD, flight controls (yaw/pitch/thrust).
 * Later phases add pilot rescue, Jaggi enemies, combat, sound.
 *
 * Built as C++ (no exceptions/RTTI/STL) targeting m68020, AGA 320x256x8bpp.
 * Bridge daemon integration for live variable inspection & remote control.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <devices/inputevent.h>
#include <libraries/dos.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

extern "C" {
#include "bridge_client.h"
}

#include "render.h"
#include "terrain.h"
#include "game.h"
#include "pilots.h"
#include "combat.h"

/* Amiga library bases — declared in render.cpp as extern. */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;

/*
 * Ask gcc's amiga startup for a fatter stack — Terrain::heights is 16 KB
 * on its own, plus smooth()'s scratch is another 16 KB (also 16-bit
 * scratch), plus normal C++ frames. Default CLI stack is 4 KB, so
 * anything less than ~48 KB will blow up in main()'s prologue.
 */
extern "C" { ULONG __stack = 65536; }

/*
 * File-scope so heights/scratch land in BSS instead of on main()'s
 * stack. Terrain alone is 16 KB — putting it in main() would still
 * overflow even with the bigger stack request during nested calls.
 */
static Terrain   g_terrain;
static Renderer  g_renderer;
static Game      g_game;
static PilotList g_pilots;
static Combat    g_combat;

/* Raw-key codes (from devices/inputevent.h / rawkeycode). Flight-sim
 * mapping: arrows FLY the ship — up/down = thrust/brake, left/right =
 * turn. Pitch moves to Q/A. Space fires (Phase 4+). L lands. ESC quits. */
#define RK_ESC     0x45
#define RK_LEFT    0x4F
#define RK_RIGHT   0x4E
#define RK_UP      0x4C
#define RK_DOWN    0x4D
#define RK_SPACE   0x40
#define RK_L       0x28
#define RK_Q       0x10
#define RK_A       0x20
#define RK_UP_MASK 0x80

static UWORD input_flags = 0;

static void apply_key(UWORD code)
{
    UWORD released = code & RK_UP_MASK;
    UWORD raw      = code & 0x7F;
    UWORD bit = 0;

    switch (raw) {
    case RK_LEFT:  bit = INPUT_LEFT;   break;
    case RK_RIGHT: bit = INPUT_RIGHT;  break;
    case RK_UP:    bit = INPUT_THRUST; break;  /* arrow-up = go */
    case RK_DOWN:  bit = INPUT_BRAKE;  break;  /* arrow-down = brake */
    case RK_Q:     bit = INPUT_UP;     break;  /* Q = nose up */
    case RK_A:     bit = INPUT_DOWN;   break;  /* A = nose down */
    case RK_SPACE: bit = INPUT_FIRE;   break;  /* Phase 4 */
    case RK_L:     bit = INPUT_LAND;   break;
    default: break;
    }

    if (!bit) return;
    if (released) input_flags &= ~bit;
    else          input_flags |= bit;
}

/* Bridge-exposed state, so we can poke values live during dev. */
static GameState g_state;
static ULONG     g_frame_count = 0;

int main(void)
{
    Terrain   &terrain  = g_terrain;
    Renderer  &renderer = g_renderer;
    Game      &game     = g_game;
    PilotList &pilots   = g_pilots;
    Combat    &combat   = g_combat;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary(
                        (CONST_STRPTR)"intuition.library", 39);
    if (!IntuitionBase) { printf("no intuition\n"); return 20; }
    GfxBase = (struct GfxBase *)OpenLibrary(
                  (CONST_STRPTR)"graphics.library", 39);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 20;
    }

    /* Optional bridge — we don't hard-fail if the daemon isn't running,
     * so the game plays standalone from Workbench too. */
    int bridge_ok = (ab_init("fractalus") == 0);
    if (bridge_ok) {
        AB_I("fractalus v0.1 (Phase 1) starting");
        ab_register_var("frame_count",    AB_TYPE_I32, &g_frame_count);
        ab_register_var("ship_x",         AB_TYPE_I32, &g_state.ship.x);
        ab_register_var("ship_z",         AB_TYPE_I32, &g_state.ship.z);
        ab_register_var("ship_y",         AB_TYPE_I32, &g_state.ship.y);
        ab_register_var("ship_yaw",       AB_TYPE_I32, &g_state.ship.yaw);
        ab_register_var("ship_pitch",     AB_TYPE_I32, &g_state.ship.pitch);
        ab_register_var("ship_speed",     AB_TYPE_I32, &g_state.ship.speed);
        ab_register_var("fuel",           AB_TYPE_I32, &g_state.fuel);
        ab_register_var("shield",         AB_TYPE_I32, &g_state.shield);
        ab_register_var("seed",           AB_TYPE_I32, (LONG *)&g_state.seed);
        ab_register_var("running",        AB_TYPE_I32, (LONG *)&g_state.running);
        ab_register_var("rescue_state",   AB_TYPE_I32, (LONG *)&g_state.rescue_state);
        ab_register_var("pilots_saved",   AB_TYPE_I32, &g_state.pilots_rescued);
        ab_register_var("pilots_lost",    AB_TYPE_I32, &g_state.pilots_lost);
        ab_register_var("score",          AB_TYPE_I32, &g_state.score);
    }

    render_init_math();
    g_state.seed = 0xC0FFEE01UL;
    terrain.generate(g_state.seed);

    /* Sanity-check terrain generation — useful when tuning fractal
     * parameters. Bridge picks up the log line automatically. */
    if (bridge_ok) {
        LONG mn = 255, mx = 0;
        ULONG sum = 0;
        const UBYTE *H = terrain.raw();
        for (LONG i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++) {
            if (H[i] < mn) mn = H[i];
            if (H[i] > mx) mx = H[i];
            sum += H[i];
        }
        AB_I("terrain: min=%ld max=%ld mean=%ld",
             (long)mn, (long)mx,
             (long)(sum / (TERRAIN_SIZE * TERRAIN_SIZE)));
    }

    int err = renderer.open_display();
    if (err) {
        printf("display open failed: %d\n", err);
        if (bridge_ok) ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 20;
    }

    game.init(&g_state, &terrain, &pilots, &combat);
    pilots.spawn(FX16_TOINT(g_state.ship.x),
                 FX16_TOINT(g_state.ship.z),
                 g_state.seed ^ 0xA5A5A5A5UL, terrain);
    combat.init(g_state.seed ^ 0x33445566UL,
                FX16_TOINT(g_state.ship.x),
                g_state.ship.y,
                FX16_TOINT(g_state.ship.z));
    if (bridge_ok) {
        AB_I("spawned %ld pilots", (long)pilots.count());
        for (LONG pi = 0; pi < pilots.count(); pi++) {
            AB_I("  pilot %ld: x=%ld z=%ld jaggi=%ld", (long)pi,
                 (long)pilots[pi].x, (long)pilots[pi].z,
                 (long)pilots[pi].is_jaggi);
        }
        for (LONG si = 0; si < combat.saucer_count(); si++) {
            AB_I("  saucer %ld: x=%ld y=%ld z=%ld hp=%ld",
                 (long)si,
                 (long)combat.saucer(si).x,
                 (long)combat.saucer(si).y,
                 (long)combat.saucer(si).z,
                 (long)combat.saucer(si).hp);
        }
    }

    /* Main loop. Input is polled from IDCMP each frame; game logic runs
     * once per frame; renderer VBLANK-syncs internally. */
    struct MsgPort *up = renderer.user_port();
    while (g_state.running) {
        struct IntuiMessage *msg;
        ULONG sig = SetSignal(0L, 0L);
        if (sig & SIGBREAKF_CTRL_C) { g_state.running = 0; break; }

        while ((msg = (struct IntuiMessage *)GetMsg(up))) {
            ULONG cls  = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_RAWKEY) {
                if ((code & 0x7F) == RK_ESC) { g_state.running = 0; break; }
                apply_key(code);
            }
        }
        if (!g_state.running) break;

        /* Poll bridge before AND after render — the raycaster can chew
         * hundreds of ms per frame, and one-poll-per-frame lets the
         * host's command timeouts fire while we're mid-scanline. */
        if (bridge_ok) ab_poll();
        game.tick(input_flags);
        if (bridge_ok) ab_poll();
        renderer.render(g_state, terrain, pilots, combat);
        g_frame_count++;
        if (bridge_ok) ab_poll();
    }

    if (bridge_ok) {
        AB_I("fractalus shutting down (%ld frames)", (long)g_frame_count);
    }
    renderer.close_display();
    if (bridge_ok) ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
