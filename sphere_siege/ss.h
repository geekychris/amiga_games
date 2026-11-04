// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ss.h - Sphere Siege's game logic: a shooter inside a hollow sphere. The
 * ship rides the inside of the shell, always facing the centre, and fires
 * inward: a shot crosses the centre and carries on to the far wall, so it
 * can hit what is on either side. Enemies come out of the centre in
 * spiralling trains, circle, swoop out at the ship and shoot back;
 * orbiters run along the shell itself. Every fourth wave is a siege wave
 * of armoured wardens.
 *
 * The ship's place and heading are a rotation frame (right R, up U,
 * forward F towards the centre; the ship sits at -F * SS_R), turned a
 * little each tick and re-orthonormalised, so there is no pole. Moving
 * right turns the frame about U, so the ship slides towards +R.
 *
 * World units are 16.16 fixed point (the shell's radius is 1000); unit
 * vectors are 16.16 too. One tick 40 ms. No Amiga headers: tested on the
 * host too.
 */
#ifndef SS_GAME_H
#define SS_GAME_H

#include <stdint.h>
#include "a3d.h"
#include "a3d_fix.h"

#define SS_R          1000              /* the shell's radius */
#define SS_LIVES      3
#define SS_TURN       440               /* the ship's top turn a tick (16-bit angle: 2.4 degrees) */
#define SS_ACCEL      110
#define SS_SHOT_SPEED 110
#define SS_MAX_SHOTS  7
#define SS_FIRE_GAP   4
#define SS_MAX_EN     48
#define SS_MAX_BUL    20
#define SS_HIT_R      62                /* a shot this close to an enemy hits it */
#define SS_SHIP_R     48                /* a bullet this close to the ship hits it */
#define SS_SAFE_R     150               /* nearer the centre than this, enemies are still warping in */
#define SS_BETWEEN    70                /* ticks between waves */
#define SS_INVULN     60
#define SS_EXTRA      25000             /* an extra ship every */
#define SS_SIEGE      4                 /* every fourth wave is a siege wave */

/* input bits */
#define SS_IN_LEFT  1
#define SS_IN_RIGHT 2
#define SS_IN_UP    4
#define SS_IN_DOWN  8
#define SS_IN_FIRE  16

typedef struct { fix x, y, z; } FV;

enum { EN_DART, EN_WEAVER, EN_ORBITER, EN_WARDEN, EN_TYPES };
enum { PH_WAIT, PH_EMERGE, PH_ORBIT, PH_SWOOP, PH_RETURN, PH_DEAD };
enum { SSE_NONE, SSE_SHOT, SSE_KILL, SSE_HIT, SSE_EFIRE, SSE_HURT, SSE_WAVE, SSE_CLEAR, SSE_OVER, SSE_LIFE,
       SSE_SWOOP, SSE_COUNT };
typedef struct { int type, a, b; } SSEvent;
#define SS_MAX_EVENTS 24

typedef struct {
    int type, phase, hp, timer, cool, w, swoops;
    FV d, ax;                           /* direction from the centre, and the axis it turns about */
    fix r, r_home;                      /* distance from the centre (units, 16.16), and where it circles */
} Enemy;

typedef struct { int alive, t; FV p, v; } Mover;   /* shots and bullets: position, velocity a tick */

typedef struct {
    uint32_t seed, rng;
    int wave, over, between, siege;
    FV R, U, F;                         /* the ship's frame */
    int vx, vy;                         /* its turn rates (16-bit angle a tick) */
    int lives, invuln, fire_cool;
    int speed;                          /* enemies' speed, x/256 */
    Enemy en[SS_MAX_EN];
    int nen, left;                      /* enemies this wave, and still to kill */
    Mover shot[SS_MAX_SHOTS], bul[SS_MAX_BUL];
    int32_t score, next_extra, tick, wave_tick;
    int shots, hits, kills, hurt;
    unsigned prev_in;
    SSEvent ev[SS_MAX_EVENTS];
    int nev;
} SSGame;

extern const char *const ss_event_names[SSE_COUNT];

void ss_new_game(SSGame *g, uint32_t seed, int wave);
void ss_start_wave(SSGame *g, int wave);
void ss_tick(SSGame *g, unsigned in);
int  ss_pop_event(SSGame *g, SSEvent *e);
unsigned ss_bot(const SSGame *g);       /* the autopilot's input */
uint32_t ss_state_hash(const SSGame *g);

/* vectors */
fix  fv_dot(const FV *a, const FV *b);
void fv_cross(const FV *a, const FV *b, FV *out);
void fv_norm(FV *v);
void ss_ship_pos(const SSGame *g, FV *p);           /* -F * SS_R */
void ss_enemy_pos(const SSGame *g, int k, FV *p);
int  ss_enemy_live(const SSGame *g, int k);         /* out of the centre and not dead */
int  ss_enemy_score(int type, int wave);
/* where a direction from the centre lies in the ship's frame: x along R, y
 * along U, z along F (z > 0: the far side) */
void ss_local(const SSGame *g, const FV *d, fix *x, fix *y, fix *z);
/* turn the frame so that direction d (from the centre) is on the line of
 * fire on the far side (the aim hook and the tests) */
void ss_face(SSGame *g, const FV *d);

/* tests (tests/test_cases.c) */
int run_tests(void (*check)(int cond, const char *name));

#endif
