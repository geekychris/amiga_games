// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Sphere Siege's shell on the shared engine: input, the title
 * with the autopilot playing behind it, play, game over, the high score
 * (kept in PROGDIR:sphere_siege.hi), the help pages, sound and bursts for
 * the events, and everything exposed over the AmigaBridge so the game can
 * be driven and tested from the host:
 *
 *   vars   score wave lives state over autoplay paused help best enemies
 *          kills shots hits hurt start_wave grid fps polys us_update
 *          us_draw (20: the bridge takes 32)
 *   hooks  state (all of it as name=value: read test state here, the
 *          variables are looked up by name only), status, start [wave],
 *          title, autoplay <on|off>, aim [hold] (the ship under the
 *          nearest enemy; hold: it stops there), lives <n>, clear (the
 *          wave's enemies gone), grid <0|1>, help <0-2>, pause <on|off>,
 *          selftest, activate
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "ss.h"
#include "siege.h"

const char game_title[] = "Sphere Siege";
const char game_client[] = "sphere_siege";
const char game_music[] = "";
const char game_voice[] = "";

#define HI_FILE "PROGDIR:sphere_siege.hi"
#define PEN_FIRE_BURST 23

#define IN_LEFT   0x0001
#define IN_RIGHT  0x0002
#define IN_UP     0x0004
#define IN_DOWN   0x0008
#define IN_FIRE   0x0010
#define IN_PAUSE  0x0020
#define IN_ESC    0x0040
#define IN_START  0x0080
#define IN_HELP   0x0100
#define IN_AUTO   0x0200

enum { SFX_SHOT, SFX_BOOM, SFX_HIT, SFX_EFIRE, SFX_HURT, SFX_WAVE, SFX_CLEAR, SFX_OVER, SFX_LIFE, SFX_SWOOP };

Siege sg;
SSGame game;
static unsigned prev_in, play_in;
static uint32_t next_seed = 1;
static int32_t t_last, t_fps;
static int frames, last_state = -1;
static int32_t v_fps, v_polys, us_update, us_draw;
static int selftest_pending, test_pass, test_fail;

unsigned game_key_bit(unsigned code)
{
    switch (code) {
    case 0x4F: case 0x20: return IN_LEFT;      /* cursor left, A */
    case 0x4E: case 0x22: return IN_RIGHT;     /* cursor right, D */
    case 0x4C: case 0x11: return IN_UP;        /* cursor up, W */
    case 0x4D: case 0x21: return IN_DOWN;      /* cursor down, S */
    case 0x40: case 0x63: return IN_FIRE;      /* space, ctrl */
    case 0x19: return IN_PAUSE;                /* P */
    case 0x35: return IN_AUTO;                 /* B */
    case 0x45: return IN_ESC;
    case 0x44: case 0x50: return IN_START;     /* return, F1 */
    case 0x5F: case 0x25: return IN_HELP;      /* HELP, H */
    }
    return 0;
}

unsigned game_joy_bits(unsigned j)
{
    return (j & A3_JOY_LEFT ? IN_LEFT : 0) | (j & A3_JOY_RIGHT ? IN_RIGHT : 0) |
           (j & A3_JOY_UP ? IN_UP : 0) | (j & A3_JOY_DOWN ? IN_DOWN : 0) | (j & A3_JOY_FIRE ? IN_FIRE : 0);
}

static void flash(const char *s, int pen)
{
    size_t n = strlen(s);
    if (n > sizeof(sg.flash) - 1) n = sizeof(sg.flash) - 1;
    memcpy(sg.flash, s, n);
    sg.flash[n] = 0;
    sg.flash_timer = 35;
    sg.flash_pen = pen;
}

static void add_burst(const FV *p, int size, int pen)
{
    int k;
    for (k = 0; k < MAX_BURSTS; k++)
        if (sg.burst[k].t <= 0) {
            sg.burst[k].t = 12;
            sg.burst[k].x = p->x >> 16; sg.burst[k].y = p->y >> 16; sg.burst[k].z = p->z >> 16;
            sg.burst[k].size = size; sg.burst[k].pen = pen;
            return;
        }
}

/* ---------- the high score ---------- */
static void load_best(void)
{
    FILE *f = fopen(HI_FILE, "r");
    long v = 0;
    if (!f) return;
    if (fscanf(f, "%ld", &v) == 1 && v > 0) sg.best = (int32_t)v;
    fclose(f);
}

static void save_best(void)
{
    FILE *f = fopen(HI_FILE, "w");
    if (!f) return;
    fprintf(f, "%ld\n", (long)sg.best);
    fclose(f);
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_SHOT, 600, 34, 10))) a3_tone(d, 600, 2200, 900, 70, 450);
    if ((d = a3_sfx_define(SFX_BOOM, 3200, 60, 40))) a3_noise(d, 3200, 127, 6, 2);
    if ((d = a3_sfx_define(SFX_HIT, 700, 50, 25))) a3_tone(d, 700, 900, 1400, 90, 500);
    if ((d = a3_sfx_define(SFX_EFIRE, 900, 30, 12))) a3_tone(d, 900, 500, 260, 70, 600);
    if ((d = a3_sfx_define(SFX_HURT, 4000, 64, 55))) { a3_noise(d, 2000, 127, 3, 2); a3_tone(d + 2000, 2000, 200, 60, 110, 1600); }
    if ((d = a3_sfx_define(SFX_WAVE, 3600, 56, 45))) {
        a3_tone(d, 1200, 392, 392, 100, 80);
        a3_tone(d + 1200, 1200, 523, 523, 100, 80);
        a3_tone(d + 2400, 1200, 784, 784, 100, 900);
    }
    if ((d = a3_sfx_define(SFX_CLEAR, 4800, 60, 55))) {
        a3_tone(d, 1200, 523, 523, 100, 80);
        a3_tone(d + 1200, 1200, 659, 659, 100, 80);
        a3_tone(d + 2400, 1200, 784, 784, 100, 80);
        a3_tone(d + 3600, 1200, 1046, 1046, 110, 1000);
    }
    if ((d = a3_sfx_define(SFX_OVER, 6000, 60, 60))) a3_tone(d, 6000, 440, 90, 110, 3000);
    if ((d = a3_sfx_define(SFX_LIFE, 2400, 56, 50))) { a3_tone(d, 1200, 1046, 1046, 100, 80); a3_tone(d + 1200, 1200, 1568, 1568, 100, 900); }
    if ((d = a3_sfx_define(SFX_SWOOP, 1600, 26, 8))) a3_tone(d, 1600, 1500, 400, 60, 800);
}

static void handle_events(void)
{
    SSEvent e;
    int quiet = sg.state == GS_TITLE;
    FV p;
    char tmp[32];
    while (ss_pop_event(&game, &e)) {
        switch (e.type) {                    /* what is seen happens in the demo too */
        case SSE_KILL: ss_enemy_pos(&game, e.a, &p); add_burst(&p, e.b == EN_WARDEN ? 70 : 45, PEN_FIRE_BURST); break;
        case SSE_HIT: ss_enemy_pos(&game, e.a, &p); add_burst(&p, 25, PEN_FIRE_BURST); break;
        case SSE_HURT: ss_ship_pos(&game, &p); add_burst(&p, 60, PEN_FIRE_BURST); if (!quiet) sg.hurt_timer = 8; break;
        }
        if (quiet) continue;
        switch (e.type) {
        case SSE_SHOT: a3_sfx(SFX_SHOT); break;
        case SSE_KILL: a3_sfx(SFX_BOOM); break;
        case SSE_HIT: a3_sfx(SFX_HIT); break;
        case SSE_EFIRE: a3_sfx(SFX_EFIRE); break;
        case SSE_SWOOP: a3_sfx(SFX_SWOOP); break;
        case SSE_HURT: a3_sfx(SFX_HURT); break;
        case SSE_WAVE: a3_sfx(SFX_WAVE); break;
        case SSE_CLEAR: a3_sfx(SFX_CLEAR); break;
        case SSE_OVER: a3_sfx(SFX_OVER); break;
        case SSE_LIFE: a3_sfx(SFX_LIFE); sprintf(tmp, "EXTRA SHIP"); flash(tmp, 28); break;
        }
        if (e.type == SSE_WAVE || e.type == SSE_CLEAR || e.type == SSE_HURT || e.type == SSE_OVER || e.type == SSE_LIFE)
            AB_I("EV %s a=%ld b=%ld score=%ld wave=%ld lives=%ld", ss_event_names[e.type], (long)e.a, (long)e.b,
                 (long)game.score, (long)game.wave, (long)game.lives);
    }
}

/* ---------- game flow ---------- */
static void start_game(int wave)
{
    ss_new_game(&game, next_seed++, wave);
    sg.state = GS_PLAY;
    sg.paused = 0;
    sg.help = 0;
    sg.autoplay = 0;
    sg.new_best = 0;
    memset(sg.burst, 0, sizeof(sg.burst));
    AB_I("START wave %ld seed %ld", (long)wave, (long)game.seed);
}

static void to_title(void)
{
    sg.state = GS_TITLE;
    ss_new_game(&game, 777 + next_seed, 1);
    memset(sg.burst, 0, sizeof(sg.burst));
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int count_live(void)
{
    int k, n = 0;
    for (k = 0; k < game.nen; k++) if (ss_enemy_live(&game, k)) n++;
    return n;
}

static int hook_state(const char *args, char *buf, int bufsz)
{
    char tmp[400];
    (void)args;
    sprintf(tmp, "state=%ld wave=%ld siege=%ld lives=%ld score=%ld best=%ld left=%ld live=%ld kills=%ld shots=%ld "
            "hits=%ld hurt=%ld over=%ld between=%ld invuln=%ld autoplay=%ld paused=%ld help=%ld start_wave=%ld "
            "grid=%ld vx=%ld vy=%ld fz=%ld tick=%ld fps=%ld us_draw=%ld us_update=%ld polys=%ld",
            (long)sg.state, (long)game.wave, (long)game.siege, (long)game.lives, (long)game.score, (long)sg.best,
            (long)game.left, (long)count_live(), (long)game.kills, (long)game.shots, (long)game.hits, (long)game.hurt,
            (long)game.over, (long)game.between, (long)game.invuln, (long)sg.autoplay, (long)sg.paused, (long)sg.help,
            (long)sg.start_wave, (long)grid_detail, (long)game.vx, (long)game.vy, (long)game.F.z, (long)game.tick,
            (long)v_fps, (long)us_draw, (long)us_update, (long)v_polys);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[120];
    static const char *const st[] = { "title", "play", "over" };
    (void)args;
    sprintf(tmp, "%s wave %ld%s | lives %ld score %ld | %ld left | %ld/%ld hits kills %ld",
            st[sg.state], (long)game.wave, game.siege ? " SIEGE" : "", (long)game.lives, (long)game.score,
            (long)game.left, (long)game.hits, (long)game.shots, (long)game.kills);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int wave = args && args[0] ? atoi(args) : 1;
    char tmp[64];
    if (wave < 1) wave = 1;
    if (wave > 99) wave = 99;
    start_game(wave);
    sprintf(tmp, "wave %ld, %ld enemies, seed %ld", (long)wave, (long)game.nen, (long)game.seed);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_autoplay(const char *args, char *buf, int bufsz)
{
    sg.autoplay = !(args && (args[0] == '0' || !strcmp(args, "off")));
    reply(buf, bufsz, sg.autoplay ? "autopilot on" : "autopilot off");
    return 0;
}

/* the ship turned so the nearest enemy out of the centre is on the line of
 * fire; "hold": that enemy stops there (a still target for a test) */
static int hook_aim(const char *args, char *buf, int bufsz)
{
    int k, best = -1;
    fix bd = -0x7FFFFFFF, x, y, z;
    char tmp[48];
    for (k = 0; k < game.nen; k++) {
        if (!ss_enemy_live(&game, k)) continue;
        ss_local(&game, &game.en[k].d, &x, &y, &z);
        if (z > bd) { bd = z; best = k; }
    }
    if (best >= 0) {
        if (args && !strcmp(args, "hold")) {
            game.en[best].w = 0;
            game.en[best].phase = PH_ORBIT;
            game.en[best].timer = 30000;
            game.en[best].cool = 30000;
            if (game.en[best].r > 800 * FONE) game.en[best].r = 800 * FONE;
        }
        ss_face(&game, &game.en[best].d);
    }
    sprintf(tmp, best < 0 ? "nothing out yet" : "on enemy %ld", (long)best);
    reply(buf, bufsz, tmp);
    return best < 0;
}

static int hook_lives(const char *args, char *buf, int bufsz)
{
    game.lives = args && args[0] ? atoi(args) : SS_LIVES;
    reply(buf, bufsz, "lives set");
    return 0;
}

static int hook_clear(const char *args, char *buf, int bufsz)
{
    int k;
    (void)args;
    for (k = 0; k < game.nen; k++) if (game.en[k].phase != PH_DEAD) { game.en[k].phase = PH_DEAD; game.left--; }
    reply(buf, bufsz, "wave cleared");
    return 0;
}

static int hook_grid(const char *args, char *buf, int bufsz)
{
    grid_detail = !(args && args[0] == '0');
    reply(buf, bufsz, grid_detail ? "grid on" : "grid off");
    return 0;
}

static int hook_help(const char *args, char *buf, int bufsz)
{
    sg.help = args && args[0] ? atoi(args) : 1;
    if (sg.help < 0 || sg.help > 2) sg.help = 0;
    reply(buf, bufsz, sg.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    sg.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, sg.paused ? "paused" : "running");
    return 0;
}

static int hook_activate(const char *args, char *buf, int bufsz)
{
    (void)args;
    plat_activate();
    reply(buf, bufsz, "window active");
    return 0;
}

static void bridge_check(int cond, const char *name)
{
    if (cond) test_pass++; else test_fail++;
    ab_test_assert(cond, name, "test_cases.c", 0);
}

static int hook_selftest(const char *args, char *buf, int bufsz)
{
    (void)args;
    selftest_pending = 1;
    reply(buf, bufsz, "self-test queued; results stream as TEST_ messages");
    return 0;
}

static void register_bridge(void)
{
    ab_register_var("score",      AB_TYPE_I32, &game.score);
    ab_register_var("wave",       AB_TYPE_I32, &game.wave);
    ab_register_var("lives",      AB_TYPE_I32, &game.lives);
    ab_register_var("state",      AB_TYPE_I32, &sg.state);
    ab_register_var("over",       AB_TYPE_I32, &game.over);
    ab_register_var("autoplay",   AB_TYPE_I32, &sg.autoplay);
    ab_register_var("paused",     AB_TYPE_I32, &sg.paused);
    ab_register_var("help",       AB_TYPE_I32, &sg.help);
    ab_register_var("best",       AB_TYPE_I32, &sg.best);
    ab_register_var("enemies",    AB_TYPE_I32, &game.left);
    ab_register_var("kills",      AB_TYPE_I32, &game.kills);
    ab_register_var("shots",      AB_TYPE_I32, &game.shots);
    ab_register_var("hits",       AB_TYPE_I32, &game.hits);
    ab_register_var("hurt",       AB_TYPE_I32, &game.hurt);
    ab_register_var("start_wave", AB_TYPE_I32, &sg.start_wave);
    ab_register_var("grid",       AB_TYPE_I32, &grid_detail);
    ab_register_var("fps",        AB_TYPE_I32, &v_fps);
    ab_register_var("polys",      AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",  AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",    AB_TYPE_I32, &us_draw);

    ab_register_hook("state",    "All the game's state as name=value", hook_state);
    ab_register_hook("status",   "One-line status", hook_status);
    ab_register_hook("start",    "start [wave]", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("autoplay", "autoplay on/off: the autopilot plays", hook_autoplay);
    ab_register_hook("aim",      "aim [hold]: the nearest enemy onto the line of fire (hold: it stops)", hook_aim);
    ab_register_hook("lives",    "lives N", hook_lives);
    ab_register_hook("clear",    "The wave's enemies gone (the wave clears)", hook_clear);
    ab_register_hook("grid",     "grid 0/1: the shell's dotted grid", hook_grid);
    ab_register_hook("help",     "help N: page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);
}

/* ---------- the frame ---------- */
void game_init(void)
{
    plat_palette(siege_palette);
    plat_sky(4, 0x013, 0x103, 256);
    render_init();
    sg.start_wave = 1;
    load_best();
    register_bridge();
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;

    if (edge & IN_HELP) { sg.help = (sg.help + 1) % 3; return 1; }
    if (sg.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) sg.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (sg.state == GS_TITLE) running = 0;
        else to_title();
        return running;
    }
    play_in = 0;
    switch (sg.state) {
    case GS_TITLE:
        if ((edge & IN_UP) && sg.start_wave < 9) sg.start_wave++;
        if ((edge & IN_DOWN) && sg.start_wave > 1) sg.start_wave--;
        if (edge & (IN_FIRE | IN_START)) start_game(sg.start_wave);
        break;
    case GS_PLAY:
        if (edge & IN_PAUSE) sg.paused = !sg.paused;
        if (edge & IN_AUTO) sg.autoplay = !sg.autoplay;
        play_in = (in & IN_LEFT ? SS_IN_LEFT : 0) | (in & IN_RIGHT ? SS_IN_RIGHT : 0) | (in & IN_UP ? SS_IN_UP : 0) |
                  (in & IN_DOWN ? SS_IN_DOWN : 0) | (in & IN_FIRE ? SS_IN_FIRE : 0);
        break;
    case GS_OVER:
        if (sg.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
    return running;
}

void game_update(unsigned in)
{
    int32_t now = plat_ticks();
    uint32_t t0 = plat_clock();
    int steps = (int)((now - t_last) / 2), k;
    (void)in;
    if (steps > 0) t_last += steps * 2;
    if (steps > 4 || steps < 0) steps = 4;
    while (steps-- > 0) {
        if (sg.help || sg.paused) continue;
        if (sg.state == GS_TITLE) {
            if (game.over || game.wave > 6) ss_new_game(&game, game.seed + 1, 1 + (int)(game.seed % 4));
            ss_tick(&game, ss_bot(&game));
        } else if (sg.state == GS_PLAY) {
            ss_tick(&game, sg.autoplay ? ss_bot(&game) : play_in);
        } else
            game.tick++;
        handle_events();
        for (k = 0; k < MAX_BURSTS; k++) if (sg.burst[k].t > 0) sg.burst[k].t--;
        if (sg.flash_timer > 0) sg.flash_timer--;
        if (sg.state_timer > 0) sg.state_timer--;
        if (sg.hurt_timer > 0) sg.hurt_timer--;
        if (sg.state == GS_PLAY && game.over) {
            sg.state = GS_OVER;
            sg.state_timer = 40;
            if (game.score > sg.best) { sg.best = game.score; sg.new_best = 1; save_best(); }
        }
    }
    us_update = plat_us_since(t0);
    if (sg.state != last_state) {
        static const char *const st[] = { "TITLE", "PLAY", "OVER" };
        AB_I("STATE %s score %ld wave %ld", st[sg.state], (long)game.score, (long)game.wave);
        last_state = sg.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (sg.help) render_help(cv, sg.help);
    else if (sg.state == GS_TITLE) render_title(cv, &game);
    else render_game(cv, &game);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    v_polys = a3_npolys;
    frames++;
    if (now - t_fps >= 100) {
        v_fps = (frames * 50L) / (now - t_fps);
        frames = 0;
        t_fps = now;
    }
    ab_poll();
    if (selftest_pending) {
        int32_t t0 = plat_ticks();
        selftest_pending = 0;
        test_pass = test_fail = 0;
        ab_test_begin("sphere_siege_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        t_last = plat_ticks();
    }
}
