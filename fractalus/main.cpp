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
#include <dos/dos.h>
#include <dos/dosextens.h>

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
#include "sfx.h"

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
static Sfx       g_sfx;

/* Raw-key codes (Amiga rawkeycodes). Primary controls are WASD to
 * avoid FS-UAE's default arrow-keys-mapped-to-joystick behaviour.
 * Arrows still work if FS-UAE isn't intercepting them. */
#define RK_ESC     0x45
#define RK_LEFT    0x4F
#define RK_RIGHT   0x4E
#define RK_UP      0x4C
#define RK_DOWN    0x4D
#define RK_SPACE   0x40
#define RK_L       0x28
#define RK_W       0x11
#define RK_A       0x20
#define RK_S       0x21
#define RK_D       0x22
#define RK_Q       0x10
#define RK_Z       0x31
#define RK_UP_MASK 0x80

static UWORD input_flags = 0;

static void apply_key(UWORD code)
{
    UWORD released = code & RK_UP_MASK;
    UWORD raw      = code & 0x7F;
    UWORD bit = 0;

    switch (raw) {
    /* Turn (yaw) */
    case RK_A: case RK_LEFT:  bit = INPUT_LEFT;   break;
    case RK_D: case RK_RIGHT: bit = INPUT_RIGHT;  break;
    /* Thrust / brake */
    case RK_W: case RK_UP:    bit = INPUT_THRUST; break;
    case RK_S: case RK_DOWN:  bit = INPUT_BRAKE;  break;
    /* Pitch (secondary — Q/Z, near WASD) */
    case RK_Q:                bit = INPUT_UP;     break;
    case RK_Z:                bit = INPUT_DOWN;   break;
    /* Actions */
    case RK_SPACE:            bit = INPUT_FIRE;   break;
    case RK_L:                bit = INPUT_LAND;   break;
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
    Sfx       &sfx      = g_sfx;

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
    extern LONG g_bench_mask;
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
        /* Toggle from the bridge to isolate render bottlenecks —
         * see g_bench_mask BENCH_* bit table in render.cpp. */
        ab_register_var("bench_mask",     AB_TYPE_I32, &g_bench_mask);
    }

    render_init_math();
    /* Sound: optional — game still plays if audio.device is locked
     * or unavailable. */
    int sfx_ok = (sfx.init() == 0);
    if (bridge_ok) AB_I("sfx %s", sfx_ok ? "up" : "unavailable");
    game.bind_sfx(&sfx);
    combat.bind_sfx(&sfx);

    /* World reset — called on boot and on SPACE-restart from end
     * screens. Regenerates terrain from a fresh seed, respawns pilots
     * and saucers, resets ship + score + shield + fuel. */
    auto reset_world = [&](ULONG seed) {
        g_state.seed = seed;
        terrain.generate(seed);
        game.init(&g_state, &terrain, &pilots, &combat);
        pilots.spawn(FX16_TOINT(g_state.ship.x),
                     FX16_TOINT(g_state.ship.z),
                     seed ^ 0xA5A5A5A5UL, terrain);
        combat.init(seed ^ 0x33445566UL,
                    FX16_TOINT(g_state.ship.x),
                    g_state.ship.y,
                    FX16_TOINT(g_state.ship.z));
    };
    reset_world(0xC0FFEE01UL);

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

    /* Boot-time world init already done by reset_world above — the
     * per-pilot / per-saucer dump was diagnostic scaffolding, no
     * longer needed now that spawns are deterministic per seed. */
    if (bridge_ok) {
        AB_I("mission: rescue %ld of %ld pilots",
             (long)MISSION_WIN_PILOTS, (long)pilots.count());
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

        if (bridge_ok) ab_perf_frame_start();

        if (bridge_ok) ab_perf_section_start("game_tick");
        game.tick(input_flags);
        if (bridge_ok) ab_perf_section_end("game_tick");

        /* End-screen restart: after the mission has ended, if SPACE is
         * pressed AND the end screen has been showing for >30 ticks
         * (so a mid-airlock fire doesn't accidentally restart), regen
         * the world from a fresh seed. */
        if (g_state.mode != GM_PLAYING
            && (input_flags & INPUT_FIRE)
            && g_state.state_timer > 30) {
            ULONG next_seed = g_state.seed * 1103515245UL + 12345UL;
            if (bridge_ok) AB_I("restart: new mission, seed=%ld",
                                (long)next_seed);
            reset_world(next_seed);
            /* Clear input flags so the still-held SPACE doesn't
             * spray bullets into the fresh mission — player must
             * release + re-press to fire in the new game. */
            input_flags = 0;
        }

        if (bridge_ok) ab_poll();

        if (bridge_ok) ab_perf_section_start("render");
        renderer.render(g_state, terrain, pilots, combat);
        if (bridge_ok) ab_perf_section_end("render");

        if (bridge_ok) ab_perf_frame_end();

        g_frame_count++;
        if (bridge_ok) ab_poll();
        sfx.tick();          /* reap completed audio.device messages */

        /* Cheap heartbeat + real wall-clock FPS from DateStamp (50Hz
         * PAL ticks). If this stops arriving the game has FROZEN; if
         * it keeps arriving but with tiny FPS, the raycaster stalled
         * on some path; if fps stays healthy but input doesn't
         * respond, keyboard focus is the culprit not the game. */
        if (bridge_ok && (g_frame_count % 30) == 0) {
            struct DateStamp now;
            DateStamp(&now);
            static struct DateStamp prev = { 0, 0, 0 };
            static LONG prev_frame = 0;
            LONG dticks = (now.ds_Days   - prev.ds_Days)   * 24 * 60 * 3000
                        + (now.ds_Minute - prev.ds_Minute) * 3000
                        + (now.ds_Tick   - prev.ds_Tick);
            LONG dframes = g_frame_count - prev_frame;
            /* 50 ticks/sec → fps10 = 500 * dframes / dticks (fps × 10). */
            LONG fps10 = (dticks > 0) ? (500L * dframes) / dticks : 0;
            AB_I("hb: frame=%ld fps=%ld.%ld yaw=%ld spd=%ld resc=%ld sh=%ld",
                 (long)g_frame_count,
                 (long)(fps10 / 10), (long)(fps10 % 10),
                 (long)g_state.ship.yaw,
                 (long)g_state.ship.speed,
                 (long)g_state.rescue_state,
                 (long)g_state.shield);
            prev = now;
            prev_frame = g_frame_count;
        }
    }

    if (bridge_ok) {
        AB_I("fractalus shutting down (%ld frames)", (long)g_frame_count);
    }
    sfx.shutdown();
    renderer.close_display();
    if (bridge_ok) ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
