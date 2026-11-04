// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Sphere Siege's tests: the ship's frame (unit and square
 * after thousands of turns, over the poles, the controls' directions),
 * the waves and their trains out of the centre, shots through the centre
 * to either side, the warp-in immunity, wardens' three hits, bullets and
 * collisions, lives and game over, clearing waves and the siege wave, the
 * autopilot clearing waves, and a long autopilot game whose final state
 * hash (tests/vectors.h, from `make vectors` on the host) the Amiga must
 * reproduce exactly. On the host (make host-test), on the Amiga
 * (sphere_siege_test) and inside the game (the selftest hook).
 */
#include <string.h>
#include "../ss.h"
#define SS_TRACE_TICKS 6000             /* four minutes */
#include "vectors.h"

const char test_client[] = "sphere_siege_test";
static SSGame tg;                       /* off the Amiga's stack */

static int count_events(SSGame *g, int type)
{
    SSEvent e;
    int n = 0;
    while (ss_pop_event(g, &e)) if (e.type == type) n++;
    return n;
}

static fix fabs_(fix v) { return v < 0 ? -v : v; }

static int frame_ok(const SSGame *g)
{
    const fix tol = FONE / 256;
    return fabs_(fv_dot(&g->R, &g->R) - FONE) < tol && fabs_(fv_dot(&g->U, &g->U) - FONE) < tol &&
           fabs_(fv_dot(&g->F, &g->F) - FONE) < tol && fabs_(fv_dot(&g->R, &g->U)) < tol &&
           fabs_(fv_dot(&g->R, &g->F)) < tol && fabs_(fv_dot(&g->U, &g->F)) < tol;
}

/* wave 1 cut to one enemy k = 0 of `type`, out at radius r in direction d,
 * holding still and holding fire */
static void one_enemy(SSGame *g, int type, const FV *d, int r)
{
    ss_new_game(g, 5, 1);
    memset(g->en, 0, sizeof(g->en));
    g->nen = g->left = 1;
    g->en[0].type = type;
    g->en[0].phase = PH_ORBIT;
    g->en[0].timer = 9999;
    g->en[0].cool = 9999;
    g->en[0].hp = type == EN_WARDEN ? 3 : 1;
    g->en[0].d = *d;
    g->en[0].ax.x = FONE;
    if (d->x > FONE / 2 || d->x < -FONE / 2) { g->en[0].ax.x = 0; g->en[0].ax.y = FONE; }
    g->en[0].w = 0;
    g->en[0].r = g->en[0].r_home = r * FONE;
    g->wave_tick = 100;
    g->nev = 0;
}

static int fire_until_gone(SSGame *g, int ticks)
{
    int t;
    for (t = 0; t < ticks && g->en[0].phase != PH_DEAD; t++) ss_tick(g, SS_IN_FIRE);
    return g->en[0].phase == PH_DEAD;
}

static int play_waves(SSGame *g, uint32_t seed, int wave, int waves, int max)
{
    int t, cleared = 0;
    ss_new_game(g, seed, wave);
    for (t = 0; t < max && !g->over && cleared < waves; t++) {
        SSEvent e;
        ss_tick(g, ss_bot(g));
        while (ss_pop_event(g, &e)) if (e.type == SSE_CLEAR) cleared++;
    }
    return cleared;
}

int run_tests(void (*check)(int cond, const char *name))
{
    SSGame *g = &tg;
    int k, ok, n;
    FV p0, p1, d;
    fix x, y, z;
    uint32_t h1, s;

    /* the frame */
    ss_new_game(g, 7, 1);
    check(frame_ok(g) && g->F.z == FONE, "the ship starts at -z facing the centre");
    ss_ship_pos(g, &p0);
    for (k = 0; k < 10; k++) ss_tick(g, SS_IN_RIGHT);
    ss_ship_pos(g, &p1);
    check(p1.x > p0.x + 20 * FONE, "RIGHT slides the ship towards +R");
    ss_new_game(g, 7, 1);
    for (k = 0; k < 10; k++) ss_tick(g, SS_IN_UP);
    ss_ship_pos(g, &p1);
    check(p1.y > 20 * FONE, "UP slides it towards +U");
    check(g->vy == SS_TURN, "and it speeds up to its top turn rate");
    for (k = 0; k < 10; k++) ss_tick(g, 0);
    check(g->vy == 0, "let go, it stops");
    ss_new_game(g, 7, 1);
    for (k = 0; k < 400; k++) ss_tick(g, SS_IN_UP);
    check(frame_ok(g), "up over the pole and on: the frame stays square (no pole)");
    for (ok = 1, s = 99, k = 0; k < 5000; k++) {
        ss_tick(g, a3_rand(&s) & 15);
        if (!frame_ok(g)) ok = 0;
    }
    check(ok, "5000 ticks of random flying: still unit and square");
    ss_ship_pos(g, &p1);
    n = (int)(fhypot3(p1.x >> 4, p1.y >> 4, p1.z >> 4) >> 12);
    check(n >= SS_R - 2 && n <= SS_R + 2, "and the ship still on the shell");

    /* waves */
    for (ok = 1, k = 1; k <= 8; k++) {
        ss_new_game(g, 3, k);
        if (g->nen < 15 || g->nen > SS_MAX_EN || g->en[0].phase != PH_WAIT || g->lives != SS_LIVES) ok = 0;
        if ((k % SS_SIEGE == 0) != g->siege) ok = 0;
    }
    check(ok, "every wave: trains of enemies waiting in the centre; every fourth a siege");
    ss_new_game(g, 3, 4);
    for (ok = 0, k = 0; k < g->nen; k++) if (g->en[k].type == EN_WARDEN && g->en[k].hp == 3) ok = 1;
    check(ok, "the siege wave brings wardens");
    ss_new_game(g, 3, 1);
    for (k = 0; k < 40; k++) ss_tick(g, 0);
    check(g->en[0].phase == PH_EMERGE && g->en[0].r > 0 && g->en[0].r < 300 * FONE && g->en[5].phase == PH_WAIT,
          "they come out of the centre one after another");
    for (k = 0; k < 60; k++) ss_tick(g, 0);
    check(g->en[0].phase == PH_ORBIT && g->en[0].r == g->en[0].r_home, "and settle into circling");
    for (ok = 0, k = 0; k < 600 && !ok; k++) {
        int j;
        FV ship, p;
        ss_tick(g, 0);
        if (g->invuln) g->invuln = 0, g->lives = SS_LIVES;
        ss_ship_pos(g, &ship);
        for (j = 0; j < g->nen; j++) {
            if (g->en[j].phase != PH_SWOOP && g->en[j].phase != PH_RETURN) continue;
            ss_enemy_pos(g, j, &p);
            p.x = (p.x - ship.x) >> 16; p.y = (p.y - ship.y) >> 16; p.z = (p.z - ship.z) >> 16;
            if (p.x * p.x + p.y * p.y + p.z * p.z < 300 * 300) ok = 1;
        }
    }
    check(ok, "then swoop out at the ship");

    /* shooting */
    d.x = 0; d.y = 0; d.z = FONE;
    one_enemy(g, EN_DART, &d, 700);
    check(fire_until_gone(g, 30) && g->kills == 1 && g->score >= ss_enemy_score(EN_DART, 1),
          "a shot through the centre: the enemy on the far side");
    check(g->left == 0, "the last one: the wave is clear");
    d.z = -FONE;
    one_enemy(g, EN_WEAVER, &d, 500);
    check(fire_until_gone(g, 30), "and one on the near side of the centre");
    d.x = FX(0.3); d.y = 0; d.z = FX(0.954);
    fv_norm(&d);
    one_enemy(g, EN_DART, &d, 700);
    check(!fire_until_gone(g, 40) && g->hits == 0 && g->shots > 3, "off the line of fire: missed");
    ss_face(g, &d);
    check(fire_until_gone(g, 30), "the ship moved round under it: hit");
    ss_local(g, &d, &x, &y, &z);
    check(z > FX(0.99) && fabs_(x) < FONE / 100 && fabs_(y) < FONE / 100, "ss_face puts a direction on the far line");
    d.x = 0; d.y = 0; d.z = FONE;
    one_enemy(g, EN_DART, &d, SS_SAFE_R - 40);
    g->en[0].phase = PH_EMERGE;
    g->en[0].w = 0;
    ss_tick(g, SS_IN_FIRE);
    for (k = 0; k < 3; k++) ss_tick(g, 0);
    check(g->en[0].phase != PH_DEAD && g->hits == 0, "still warping in at the centre: shots pass through");
    one_enemy(g, EN_WARDEN, &d, 700);
    ss_tick(g, SS_IN_FIRE);
    for (k = 0; k < 22; k++) ss_tick(g, 0);
    check(g->en[0].hp == 2 && g->en[0].phase != PH_DEAD, "a warden takes three hits: one");
    check(fire_until_gone(g, 60) && g->hits == 3, "three");
    one_enemy(g, EN_DART, &d, 700);
    for (k = 0; k < 60; k++) ss_tick(g, SS_IN_FIRE);
    check(g->shots <= 60 / SS_FIRE_GAP + 1 && g->shots >= 60 / SS_FIRE_GAP - 1, "held, the gun fires every 4 ticks");

    /* bullets */
    d.x = 0; d.y = 0; d.z = FONE;
    one_enemy(g, EN_WEAVER, &d, 600);
    g->en[0].cool = 1;
    ss_tick(g, 0);
    check(g->bul[0].alive && count_events(g, SSE_EFIRE) == 1, "an enemy fires at the ship");
    for (k = 0; k < 120 && g->hurt == 0; k++) ss_tick(g, 0);
    check(g->hurt == 1 && g->lives == SS_LIVES - 1 && g->invuln > 0, "left alone, it hits: a life lost");
    one_enemy(g, EN_WEAVER, &d, 600);
    g->en[0].cool = 1;
    ss_tick(g, 0);
    for (k = 0; k < 120 && g->bul[0].alive; k++) ss_tick(g, SS_IN_RIGHT);
    check(g->hurt == 0, "the ship slides out of its way: missed");
    one_enemy(g, EN_DART, &d, 600);
    g->lives = 1;
    g->bul[0].alive = 1;
    ss_ship_pos(g, &g->bul[0].p);
    ss_tick(g, 0);
    check(g->over && g->lives == 0 && count_events(g, SSE_OVER) == 1, "the last ship lost: over");
    d.z = -FONE;
    one_enemy(g, EN_DART, &d, 950);
    ss_tick(g, 0);
    check(g->hurt == 1 && g->en[0].phase == PH_DEAD, "an enemy flown into the ship: both lost");

    /* the flow */
    d.z = FONE;
    one_enemy(g, EN_DART, &d, 700);
    fire_until_gone(g, 30);
    check(g->between > 0 && g->score == ss_enemy_score(EN_DART, 1) + 500, "a wave cleared: 500 x the wave");
    for (k = 0; k < SS_BETWEEN + 2; k++) ss_tick(g, 0);
    check(g->wave == 2 && g->nen > 0 && g->between == 0, "then the next wave");
    ss_new_game(g, 5, 1);
    g->score = SS_EXTRA - 10;
    g->score += 10;
    ss_tick(g, 0);
    check(g->lives == SS_LIVES + 1 && g->next_extra == 2 * SS_EXTRA, "an extra ship every 25000");

    /* the autopilot */
    for (n = 0, k = 1; k <= 3; k++) if (play_waves(g, (uint32_t)k, 1, 3, 9000) == 3) n++;
    check(n == 3, "autopilot: clears waves 1 to 3");
    check(play_waves(g, 9, 4, 1, 6000) == 1, "autopilot: clears a siege wave");

    /* determinism: the host and the Amiga must agree */
    ss_new_game(g, 1234, 1);
    for (k = 0; k < SS_TRACE_TICKS && !g->over; k++) ss_tick(g, ss_bot(g));
    h1 = ss_state_hash(g);
    check(g->wave >= 4, "autopilot: four waves or more in four minutes");
    check(h1 == SS_TRACE_HASH, "the long autopilot game matches the host's (tests/vectors.h)");
    return 0;
}

#ifdef SS_TRACE_MAIN
#include <stdio.h>
int main(void)
{
    int k;
    ss_new_game(&tg, 1234, 1);
    for (k = 0; k < SS_TRACE_TICKS && !tg.over; k++) ss_tick(&tg, ss_bot(&tg));
    printf("/* make vectors: seed 1234 from wave 1, %d ticks: %s, wave %d, lives %d, score %ld, %d/%d hits, %d kills */\n",
           k, tg.over ? "over" : "playing", tg.wave, tg.lives, (long)tg.score, tg.hits, tg.shots, tg.kills);
    printf("#define SS_TRACE_HASH 0x%08lXUL\n", (unsigned long)ss_state_hash(&tg));
    return 0;
}
#endif

#ifdef SS_SIM_MAIN
#include <stdio.h>
int main(void)
{
    uint32_t seed;
    for (seed = 1; seed <= 8; seed++) {
        int k, last_wave = 0;
        ss_new_game(&tg, seed, 1);
        for (k = 0; k < 20000 && !tg.over; k++) {
            SSEvent e;
            ss_tick(&tg, ss_bot(&tg));
            while (ss_pop_event(&tg, &e)) if (e.type == SSE_HURT) printf("    hurt t %d by %s wave %d\n", k, e.b ? "collision" : "bullet", tg.wave);
            if (tg.wave != last_wave) { printf("  seed %u t %5d wave %d lives %d score %ld\n", (unsigned)seed, k, tg.wave, tg.lives, (long)tg.score); last_wave = tg.wave; }
        }
        printf("seed %u: %s at %d, wave %d, score %ld, %d/%d hits, kills %d, hurt %d\n", (unsigned)seed,
               tg.over ? "over" : "alive", k, tg.wave, (long)tg.score, tg.hits, tg.shots, tg.kills, tg.hurt);
    }
    return 0;
}
#endif
