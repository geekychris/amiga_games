// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * TERRA STRIKE for the classic Amiga (68020+)
 *
 * A Zarch / Carrier Command style landscape shooter: lift off from the
 * landing pad, fly over a wrapping fractal island and destroy the tanks,
 * buildings and ships. Land on the pad again to refuel and repair.
 *
 *   Arrows / joystick   turn and climb / dive
 *   A / Z               throttle up / down
 *   Space / fire        cannon
 *   P                   pause        Esc   title / quit
 *
 * Everything interesting is exposed over the AmigaBridge so the game can
 * be driven, tuned and tested from the host (devbench MCP / REST):
 *   vars   score lives level targets state pstate fuel shield speed yaw
 *          alt px pz fps polys + tunables god_mode inf_fuel enemy_fire ...
 *   hooks  scene <sea|tanks|city|landing|lowfuel|pad>, selftest, status,
 *          spawn <tank|ship|building> [dist], start, clear, regen <seed>
 *   mem    heightmap (64x64 WORD heights)
 */
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <devices/timer.h>
#include <proto/timer.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "ts.h"
#include "models.h"
#include "render.h"
#include "title_image.h"
#include "sound.h"
#include "ptplayer.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
struct Device *TimerBase = NULL;
static struct timerequest timer_io;
static ULONG eclock_khz = 709;

extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

#define SCREEN_W 320
#define SCREEN_H 256

static struct Screen *screen;
static struct Window *window;
static struct ScreenBuffer *sbuf[2];
static struct RastPort rp_buf[2];
static struct MsgPort *db_port[2];
static WORD cur_buf;

static UBYTE *mod_data;
static ULONG mod_size;
static int music_on;

static World world;
static Cam cam;
static int paused;

/* Title: the artwork for TITLE_IMAGE_TICKS, then the 3D flyover as attract
 * mode for TITLE_ATTRACT_TICKS, round and round (ticks are 1/50 s). */
#define TITLE_IMAGE_TICKS   500
#define TITLE_ATTRACT_TICKS 750
static LONG title_phase;            /* 0 = artwork, 1 = attract flyover */
static LONG title_t0;
static int  pal_shown = -1;         /* 0 = game palette, 1 = title palette */

/* bridge mirrors */
/* stage timings in microseconds, read over the bridge */
static LONG us_update, us_build, us_draw, us_hud;
static LONG v_alt, v_px, v_pz, v_fps, v_polys, v_frame, v_pstate;

static const UWORD palette[32] = {
    0x000, 0xFFF, 0x333, 0x999,
    0x027, 0x04A,            /* sea */
    0xB95, 0xDB7,            /* sand */
    0x392, 0x5B3,            /* grass */
    0x161, 0x283,            /* forest */
    0x654, 0x987,            /* rock */
    0xAAB, 0xEEF,            /* snow */
    0xF30, 0x900, 0xFE0, 0xF80,
    0x9EF, 0x59B, 0x357,     /* craft */
    0xC64, 0x732,            /* tank */
    0xDDC, 0x998, 0xB43,     /* building, roof */
    0xABC, 0x567,            /* ship */
    0x2C3, 0x111             /* tree, shadow */
};

/* ---------- input ---------- */
static unsigned keys;

static unsigned key_bit(UWORD code)
{
    switch (code) {
    case 0x4F: return IN_LEFT;
    case 0x4E: return IN_RIGHT;
    case 0x4C: return IN_UP;
    case 0x4D: return IN_DOWN;
    case 0x20: return IN_FAST;     /* A */
    case 0x31: return IN_SLOW;     /* Z */
    case 0x40: return IN_FIRE;     /* space */
    case 0x64: return IN_FIRE;     /* left alt */
    case 0x45: return IN_ESC;
    case 0x50: return IN_START;    /* F1 */
    case 0x44: return IN_START;    /* return */
    case 0x19: return IN_PAUSE;    /* P */
    }
    return 0;
}

static unsigned read_joystick(void)
{
    UWORD joy = custom.joy1dat;
    unsigned r = 0;
    UWORD right = (joy >> 1) & 1, left = (joy >> 9) & 1;
    if (right) r |= IN_RIGHT;
    if (left) r |= IN_LEFT;
    if (((joy >> 1) ^ joy) & 1) r |= IN_DOWN;
    if (((joy >> 9) ^ (joy >> 8)) & 1) r |= IN_UP;
    if (!(ciaa.ciapra & 0x80)) r |= IN_FIRE;
    return r;
}

/* ---------- display ---------- */
static int setup_display(void)
{
    int i;
    screen = OpenScreenTags(NULL,
        SA_Width, SCREEN_W, SA_Height, SCREEN_H, SA_Depth, 5,
        SA_DisplayID, LORES_KEY, SA_Title, (ULONG)"Terra Strike",
        SA_ShowTitle, FALSE, SA_Quiet, TRUE, SA_Type, CUSTOMSCREEN, TAG_DONE);
    if (!screen) return 0;
    for (i = 0; i < 32; i++)
        SetRGB4(&screen->ViewPort, i, (palette[i] >> 8) & 15, (palette[i] >> 4) & 15, palette[i] & 15);

    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    db_port[0] = CreateMsgPort();
    db_port[1] = CreateMsgPort();
    if (!sbuf[0] || !sbuf[1] || !db_port[0] || !db_port[1]) return 0;
    for (i = 0; i < 2; i++) {
        sbuf[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = db_port[i];
        InitRastPort(&rp_buf[i]);
        rp_buf[i].BitMap = sbuf[i]->sb_BitMap;
        render_attach(&rp_buf[i]);
        SetRast(&rp_buf[i], 0);
    }
    window = OpenWindowTags(NULL,
        WA_Left, 0, WA_Top, 0, WA_Width, SCREEN_W, WA_Height, SCREEN_H,
        WA_CustomScreen, (ULONG)screen, WA_Borderless, TRUE, WA_Backdrop, TRUE,
        WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_IDCMP, IDCMP_RAWKEY, TAG_DONE);
    if (!window) return 0;
    cur_buf = 1;
    return 1;
}

/* Show the buffer we just drew, then wait for ITS SafeMessage: that is
 * the signal that the previously displayed bitmap - the one we draw into
 * next - is really off screen. (Waiting on the other buffer's message, as
 * the code this started from did, lets the next frame's blitter clear
 * run on the visible bitmap: planes vanish one by one on screen.) */
static void swap_buffers(void)
{
    WaitBlit();
    if (ChangeScreenBuffer(screen, sbuf[cur_buf])) {
        while (!GetMsg(db_port[cur_buf])) WaitPort(db_port[cur_buf]);
    }
    cur_buf ^= 1;
}

static void cleanup_display(void)
{
    int i;
    if (window) CloseWindow(window);
    for (i = 0; i < 2; i++) {
        if (sbuf[i]) FreeScreenBuffer(screen, sbuf[i]);
        if (db_port[i]) DeleteMsgPort(db_port[i]);
    }
    if (screen) CloseScreen(screen);
}

static UBYTE *load_chip(const char *path, ULONG *size)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    UBYTE *buf;
    LONG len;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    buf = len > 0 ? (UBYTE *)AllocMem(len, MEMF_CHIP) : NULL;
    if (buf && Read(fh, buf, len) != len) { FreeMem(buf, len); buf = NULL; }
    Close(fh);
    *size = (ULONG)len;
    return buf;
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int hook_scene(const char *args, char *buf, int bufsz)
{
    char tmp[80];
    const char *name = (args && args[0]) ? args : "pad";
    if (!world_scene(&world, name)) {
        sprintf(tmp, "no such scene or no target: %s", name);
        reply(buf, bufsz, tmp);
        return 1;
    }
    paused = 0;
    sprintf(tmp, "scene %s: craft at %ld,%ld alt %ld yaw %ld", name,
            (long)(world.p.x >> FBITS), (long)(world.p.z >> FBITS),
            (long)(world.p.y >> FBITS), (long)world.p.yaw);
    AB_I("SCENE %s", name);
    reply(buf, bufsz, tmp);
    return 0;
}

static int test_pass, test_fail;
static void bridge_check(int cond, const char *name)
{
    if (cond) test_pass++; else test_fail++;
    ab_test_assert(cond, name, "test_cases.c", 0);
}

/* The suite takes a few seconds on a 68060 - longer than devbench waits
 * for a hook reply - so the hook only queues it; the main loop runs it and
 * the results stream back through the bridge test harness. */
static int selftest_pending;

static int hook_selftest(const char *args, char *buf, int bufsz)
{
    (void)args;
    selftest_pending = 1;
    reply(buf, bufsz, "self-test queued; results stream as TEST_ messages");
    return 0;
}

static void run_selftest(void)
{
    LONG t0 = 0;
    struct DateStamp ds;
    DateStamp(&ds);
    t0 = ds.ds_Minute * 3000L + ds.ds_Tick;
    test_pass = test_fail = 0;
    ab_test_begin("terra_strike_selftest");
    ts_run_tests(bridge_check);
    ab_test_end();
    DateStamp(&ds);
    AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
         (long)((ds.ds_Minute * 3000L + ds.ds_Tick - t0) * 20));
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[120];
    static const char *gs[] = { "title", "play", "levelclear", "gameover" };
    static const char *ps[] = { "landed", "flying", "dead" };
    (void)args;
    sprintf(tmp, "%s lv%ld score %ld lives %ld targets %ld | craft %s fuel %ld shield %ld alt %ld | fps %ld",
            gs[world.state], (long)world.level, (long)world.score, (long)world.lives,
            (long)world.targets_left, ps[world.p.state], (long)world.p.fuel,
            (long)world.p.shield, (long)v_alt, (long)v_fps);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_spawn(const char *args, char *buf, int bufsz)
{
    char tmp[80];
    int type = ENT_TANK, i;
    int32_t dist = 700, x, z;
    if (args && args[0] == 's') type = ENT_SHIP;
    if (args && args[0] == 'b') type = ENT_BUILDING;
    if (args) { const char *sp = strchr(args, ' '); if (sp) dist = atol(sp + 1); }
    if (world.state != GS_PLAY) world_start_game(&world);
    x = (world.p.x >> FBITS) + ((TS_SIN(world.p.yaw) * dist) >> 16);
    z = (world.p.z >> FBITS) + ((TS_COS(world.p.yaw) * dist) >> 16);
    i = world_spawn(&world, type, x, z, (int)world.p.yaw + 128);
    sprintf(tmp, i < 0 ? "no free slot" : "spawned %s #%ld at %ld,%ld", ent_name(type),
            (long)i, (long)ts_wrap(x), (long)ts_wrap(z));
    AB_I("SPAWN %s", tmp);
    reply(buf, bufsz, tmp);
    return i < 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    (void)args;
    world_start_game(&world);
    paused = 0;
    reply(buf, bufsz, "game started");
    return 0;
}

static int hook_clear(const char *args, char *buf, int bufsz)
{
    int i;
    (void)args;
    for (i = 0; i < MAX_ENTS; i++)
        if (world.ents[i].type != ENT_NONE && world.ents[i].type != ENT_TREE) {
            world_explode(&world, world.ents[i].x >> FBITS, (world.ents[i].y >> FBITS) + 40,
                          world.ents[i].z >> FBITS, 6, PEN_YELLOW);
            world.ents[i].type = ENT_NONE;
        }
    world.targets_left = 0;
    reply(buf, bufsz, "all targets destroyed");
    return 0;
}

static LONG now_ticks(void);

static int hook_title(const char *args, char *buf, int bufsz)
{
    world.state = GS_TITLE;
    title_phase = (args && args[0] == 'a') ? 1 : 0;
    title_t0 = now_ticks();
    paused = 0;
    reply(buf, bufsz, title_phase ? "title: attract flyover" : "title: main screen artwork");
    return 0;
}

static void show_palette(int which)
{
    if (which == pal_shown) return;
    LoadRGB4(&screen->ViewPort, (UWORD *)(which ? title_palette : palette), 32);
    if (!which) render_hud_reset();          /* the artwork covered the HUD panel */
    pal_shown = which;
}

static int hook_regen(const char *args, char *buf, int bufsz)
{
    char tmp[48];
    uint32_t seed = (args && args[0]) ? (uint32_t)atol(args) : (uint32_t)(world.seed + 1);
    world_init(&world, seed);
    sprintf(tmp, "new island, seed %ld", (long)seed);
    AB_I("REGEN seed %ld pad at tile %ld,%ld", (long)seed, (long)world.pad_tx, (long)world.pad_tz);
    reply(buf, bufsz, tmp);
    return 0;
}

static void register_bridge(void)
{
    ab_register_var("score",       AB_TYPE_I32, &world.score);
    ab_register_var("lives",       AB_TYPE_I32, &world.lives);
    ab_register_var("level",       AB_TYPE_I32, &world.level);
    ab_register_var("targets",     AB_TYPE_I32, &world.targets_left);
    ab_register_var("state",       AB_TYPE_I32, &world.state);
    ab_register_var("pstate",      AB_TYPE_I32, &v_pstate);
    ab_register_var("fuel",        AB_TYPE_I32, &world.p.fuel);
    ab_register_var("shield",      AB_TYPE_I32, &world.p.shield);
    ab_register_var("speed",       AB_TYPE_I32, &world.p.speed);
    ab_register_var("yaw",         AB_TYPE_I32, &world.p.yaw);
    ab_register_var("alt",         AB_TYPE_I32, &v_alt);
    ab_register_var("px",          AB_TYPE_I32, &v_px);
    ab_register_var("pz",          AB_TYPE_I32, &v_pz);
    ab_register_var("fps",         AB_TYPE_I32, &v_fps);
    ab_register_var("polys",       AB_TYPE_I32, &v_polys);
    ab_register_var("frame",       AB_TYPE_I32, &v_frame);
    ab_register_var("title_phase", AB_TYPE_I32, &title_phase);
    ab_register_var("us_update",   AB_TYPE_I32, &us_update);
    ab_register_var("us_build",    AB_TYPE_I32, &us_build);
    ab_register_var("us_draw",     AB_TYPE_I32, &us_draw);
    ab_register_var("us_hud",      AB_TYPE_I32, &us_hud);
    ab_register_var("god_mode",    AB_TYPE_I32, &world.tune.god_mode);
    ab_register_var("inf_fuel",    AB_TYPE_I32, &world.tune.inf_fuel);
    ab_register_var("enemy_fire",  AB_TYPE_I32, &world.tune.enemy_fire);
    ab_register_var("enemy_range", AB_TYPE_I32, &world.tune.enemy_range);
    ab_register_var("turn_rate",   AB_TYPE_I32, &world.tune.turn_rate);
    ab_register_var("lift",        AB_TYPE_I32, &world.tune.lift);
    ab_register_var("gravity",     AB_TYPE_I32, &world.tune.gravity);
    ab_register_var("max_speed",   AB_TYPE_I32, &world.tune.max_speed);

    ab_register_hook("scene",    "Set up a scene: sea|tanks|city|landing|lowfuel|pad", hook_scene);
    ab_register_hook("selftest", "Run the unit tests inside the game", hook_selftest);
    ab_register_hook("status",   "One-line game status", hook_status);
    ab_register_hook("spawn",    "spawn <tank|ship|building> [dist] ahead of the craft", hook_spawn);
    ab_register_hook("start",    "Start a new game", hook_start);
    ab_register_hook("clear",    "Destroy every target (finishes the level)", hook_clear);
    ab_register_hook("title",    "title [image|attract]: show the main screen", hook_title);
    ab_register_hook("regen",    "regen [seed]: generate a new island", hook_regen);

    ab_register_memregion("heightmap", world.h, sizeof(world.h), "64x64 WORD terrain heights");
}

/* ---------- events -> sound + log ---------- */
static void handle_events(void)
{
    Event e;
    while (world_pop_event(&world, &e)) {
        switch (e.type) {
        case EV_TAKEOFF: AB_I("TAKEOFF fuel=%ld", (long)e.a); break;
        case EV_LANDED:  AB_I("LANDED on pad fuel=%ld", (long)e.a); break;
        case EV_FIRE:    sfx_gunfire(); break;
        case EV_KILL:
            sfx_explosion();
            AB_I("KILL %s +%ld score=%ld targets_left=%ld", ent_name(e.a),
                 (long)ent_points(e.a), (long)world.score, (long)e.b);
            break;
        case EV_HIT_PLAYER: sfx_hit(); AB_W("HIT shield=%ld", (long)e.a); break;
        case EV_CRASH:   sfx_die(); AB_W("CRASH lives=%ld", (long)e.a); break;
        case EV_RESPAWN: AB_I("RESPAWN on pad lives=%ld", (long)e.a); break;
        case EV_LEVEL_CLEAR:
            sfx_explosion();
            AB_I("LEVEL %ld CLEAR score=%ld", (long)e.a, (long)e.b);
            break;
        case EV_GAME_OVER: AB_I("GAME OVER score=%ld level=%ld", (long)e.a, (long)e.b); break;
        case EV_LOW_FUEL: AB_W("LOW FUEL"); break;
        }
    }
}

static ULONG eclock_lo(void)
{
    struct EClockVal ev;
    ReadEClock(&ev);
    return ev.ev_lo;
}

static LONG us_since(ULONG t0)
{
    return (LONG)(((eclock_lo() - t0) * 1000UL) / eclock_khz);
}

static LONG now_ticks(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Minute * 3000L + ds.ds_Tick;
}

int main(void)
{
    int running = 1, frames = 0, last_state = -1;
    LONG t_fps, t_last, t_log;
    unsigned prev_in = 0;
    ULONG t0;

    /* A previous instance may still be unregistering (devbench's amiga_run
     * stops it and relaunches 0.3 s later), so retry for up to 3 s. */
    {
        int tries;
        for (tries = 0; tries < 30 && ab_init("terra_strike") != 0; tries++) Delay(5);
        AB_I("Terra Strike starting (bridge after %ld retries)", (long)tries);
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!IntuitionBase || !GfxBase) { AB_E("needs OS 3.0"); goto out; }

    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&timer_io, 0) == 0) {
        struct EClockVal ev;
        TimerBase = timer_io.tr_node.io_Device;
        eclock_khz = ReadEClock(&ev) / 1000;
    }
    world_init(&world, 1987);
    register_bridge();
    if (!render_init() || !setup_display()) { AB_E("display setup failed"); goto out; }

    sound_init();
    mod_data = load_chip("PROGDIR:terra_strike.mod", &mod_size);
    if (mod_data) {
        mt_install_cia(CUSTOM_BASE, NULL, 1);
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;
        mt_Enable = 1;
        music_on = 1;
        AB_I("music loaded (%ld bytes)", (long)mod_size);
    } else {
        AB_W("terra_strike.mod not found - no music");
    }

    AB_I("island seed %ld, pad at tile %ld,%ld height %ld, %ld targets",
         (long)world.seed, (long)world.pad_tx, (long)world.pad_tz, (long)world.pad_h,
         (long)world.targets_left);
    SetSignal(0L, SIGBREAKF_CTRL_C);
    t_fps = t_last = t_log = now_ticks();

    while (running) {
        struct IntuiMessage *msg;
        struct RastPort *rp = &rp_buf[cur_buf];
        unsigned in;
        LONG now;
        int steps;

        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            if (msg->Class == IDCMP_RAWKEY) {
                unsigned b = key_bit(msg->Code & 0x7F);
                if (msg->Code & 0x80) keys &= ~b; else keys |= b;
            }
            ReplyMsg((struct Message *)msg);
        }
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) running = 0;

        in = keys | read_joystick();
        if ((in & ~prev_in) & IN_PAUSE) { paused = !paused; AB_I(paused ? "PAUSED" : "RESUMED"); }
        if ((in & ~prev_in) & IN_ESC) {
            if (world.state == GS_TITLE) running = 0;
            else { world.state = GS_TITLE; AB_I("back to title"); }
        }
        prev_in = in;

        /* fixed 25 Hz simulation, whatever the frame rate */
        now = now_ticks();
        steps = (int)((now - t_last) / 2);
        if (steps > 0) t_last += steps * 2;
        if (steps > 4 || steps < 0) steps = 4;

        t0 = eclock_lo();
        while (steps-- > 0 && !paused) {
            world_update(&world, in);
            handle_events();
        }
        us_update = us_since(t0);

        if (world.state != last_state) {
            static const char *gs[] = { "TITLE", "PLAY", "LEVELCLEAR", "GAMEOVER" };
            AB_I("STATE %s level %ld", gs[world.state], (long)world.level);
            if (world.state == GS_TITLE) { title_phase = 0; title_t0 = now; }
            last_state = world.state;
        }
        if (world.state == GS_TITLE &&
            now - title_t0 >= (title_phase ? TITLE_ATTRACT_TICKS : TITLE_IMAGE_TICKS)) {
            title_phase ^= 1;
            title_t0 = now;
            AB_I("TITLE %s", title_phase ? "attract flyover" : "main screen");
        }

        if (world.state == GS_TITLE && title_phase == 0) {
            t0 = eclock_lo();
            show_palette(1);
            render_title_image(rp, world.frame);
            us_draw = us_since(t0);
            us_build = us_hud = 0;
        } else {
            show_palette(0);
            t0 = eclock_lo();
            camera_follow(&cam, &world);
            render_build(&world, &cam);
            us_build = us_since(t0);
            t0 = eclock_lo();
            render_draw(rp);
            us_draw = us_since(t0);
            t0 = eclock_lo();
            render_hud(rp, cur_buf, &world, (int)v_fps, paused);
            if (world.state == GS_TITLE) render_title(rp, &world);
            us_hud = us_since(t0);
        }

        /* bridge mirrors */
        v_pstate = world.p.state;
        v_px = world.p.x >> FBITS;
        v_pz = world.p.z >> FBITS;
        v_alt = (world.p.y >> FBITS) - ground_height(&world, v_px, v_pz);
        v_polys = render_stats.polys;
        v_frame = world.frame;
        frames++;
        if (now - t_fps >= 100) {
            v_fps = (frames * 50L) / (now - t_fps);
            frames = 0;
            t_fps = now;
        }
        if (now - t_log >= 500) {
            AB_D("fps=%ld polys=%ld tiles=%ld models=%ld dropped=%ld", (long)v_fps,
                 (long)render_stats.polys, (long)render_stats.tiles,
                 (long)render_stats.models, (long)render_stats.dropped);
            t_log = now;
        }
        ab_poll();
        if (selftest_pending) { selftest_pending = 0; run_selftest(); }
        swap_buffers();
    }

    AB_I("Terra Strike shutting down, score %ld", (long)world.score);
out:
    if (music_on) { mt_end(CUSTOM_BASE); mt_remove_cia(CUSTOM_BASE); }
    if (mod_data) FreeMem(mod_data, mod_size);
    sound_cleanup();
    cleanup_display();
    render_cleanup();
    ab_cleanup();
    if (TimerBase) CloseDevice((struct IORequest *)&timer_io);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
