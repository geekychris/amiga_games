// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ss_core.c - Sphere Siege's simulation (see ss.h): the ship's frame on
 * the shell, its shots through the centre, the enemies' trains out of
 * the centre (emerge, circle, swoop at the ship, back), orbiters on the
 * shell, their bullets, waves and siege waves, the autopilot.
 */
#include <string.h>
#include <stddef.h>
#include "ss.h"

const char *const ss_event_names[SSE_COUNT] = {
    "none", "shot", "kill", "hit", "efire", "hurt", "wave", "clear", "over", "life", "swoop"
};

/* ---------- vectors ---------- */
fix fv_dot(const FV *a, const FV *b) { return FMUL(a->x, b->x) + FMUL(a->y, b->y) + FMUL(a->z, b->z); }

void fv_cross(const FV *a, const FV *b, FV *o)
{
    FV r;
    r.x = FMUL(a->y, b->z) - FMUL(a->z, b->y);
    r.y = FMUL(a->z, b->x) - FMUL(a->x, b->z);
    r.z = FMUL(a->x, b->y) - FMUL(a->y, b->x);
    *o = r;
}

/* to unit length: a 32-bit length, then a Newton step for the last bits */
void fv_norm(FV *v)
{
    fix len = fhypot3(v->x, v->y, v->z), s;
    if (len < 16) { v->x = 0; v->y = 0; v->z = FONE; return; }
    v->x = FDIV(v->x, len); v->y = FDIV(v->y, len); v->z = FDIV(v->z, len);
    s = (3 * FONE - fv_dot(v, v)) / 2;
    v->x = FMUL(v->x, s); v->y = FMUL(v->y, s); v->z = FMUL(v->z, s);
}

/* a turned towards b by angle ang, b away from a: a turn in their plane */
static void turn(FV *a, FV *b, int ang)
{
    fix c = a3_cos(ang), s = a3_sin(ang);
    FV na, nb;
    na.x = FMUL(a->x, c) + FMUL(b->x, s); na.y = FMUL(a->y, c) + FMUL(b->y, s); na.z = FMUL(a->z, c) + FMUL(b->z, s);
    nb.x = FMUL(b->x, c) - FMUL(a->x, s); nb.y = FMUL(b->y, c) - FMUL(a->y, s); nb.z = FMUL(b->z, c) - FMUL(a->z, s);
    *a = na; *b = nb;
}

/* v less its part along unit u, made unit */
static void perp(FV *v, const FV *u)
{
    fix k = fv_dot(v, u);
    v->x -= FMUL(u->x, k); v->y -= FMUL(u->y, k); v->z -= FMUL(u->z, k);
    fv_norm(v);
}

static void orthonormal(SSGame *g)
{
    fv_norm(&g->F);
    perp(&g->R, &g->F);
    fv_cross(&g->F, &g->R, &g->U);
}

static uint32_t rnd(SSGame *g, uint32_t n) { return n ? a3_rand(&g->rng) % n : 0; }

/* a3_rand gives 15 bits: a value in 0..n-1 for n up to 32768 */
static fix rnd_fix(SSGame *g) { return (fix)(rnd(g, 32768) * 4) - FONE; }   /* -1..1 */

static void rand_dir(SSGame *g, FV *v)
{
    do { v->x = rnd_fix(g); v->y = rnd_fix(g); v->z = rnd_fix(g); }
    while (fhypot3(v->x, v->y, v->z) < FONE / 4 || fhypot3(v->x, v->y, v->z) > FONE);
    fv_norm(v);
}

static void push(SSGame *g, int type, int a, int b)
{
    if (g->nev < SS_MAX_EVENTS) { g->ev[g->nev].type = type; g->ev[g->nev].a = a; g->ev[g->nev].b = b; g->nev++; }
}

int ss_pop_event(SSGame *g, SSEvent *e)
{
    if (g->nev <= 0) return 0;
    *e = g->ev[0];
    memmove(g->ev, g->ev + 1, (size_t)(g->nev - 1) * sizeof(SSEvent));
    g->nev--;
    return 1;
}

/* ---------- positions ---------- */
void ss_ship_pos(const SSGame *g, FV *p)
{
    p->x = -g->F.x * SS_R; p->y = -g->F.y * SS_R; p->z = -g->F.z * SS_R;
}

void ss_enemy_pos(const SSGame *g, int k, FV *p)
{
    const Enemy *e = &g->en[k];
    p->x = FMUL(e->d.x, e->r); p->y = FMUL(e->d.y, e->r); p->z = FMUL(e->d.z, e->r);
}

int ss_enemy_live(const SSGame *g, int k)
{
    const Enemy *e = &g->en[k];
    return e->phase != PH_WAIT && e->phase != PH_DEAD && e->r >= SS_SAFE_R * FONE;
}

int ss_enemy_score(int type, int wave)
{
    static const int base[EN_TYPES] = { 100, 150, 200, 400 };
    return base[type] + 10 * (wave - 1);
}

void ss_local(const SSGame *g, const FV *d, fix *x, fix *y, fix *z)
{
    *x = fv_dot(d, &g->R); *y = fv_dot(d, &g->U); *z = fv_dot(d, &g->F);
}

void ss_face(SSGame *g, const FV *d)
{
    g->F = *d;
    orthonormal(g);
    g->vx = g->vy = 0;
}

/* ---------- waves ---------- */
void ss_start_wave(SSGame *g, int wave)
{
    int groups, per, gi, j, n = 0;
    g->wave = wave;
    g->siege = wave % SS_SIEGE == 0;
    g->speed = 256 + 18 * (wave - 1);
    if (g->speed > 520) g->speed = 520;
    groups = 3 + (wave - 1) / 2;
    if (groups > 6) groups = 6;
    per = g->siege ? 5 : 6;
    memset(g->en, 0, sizeof(g->en));
    for (gi = 0; gi < groups; gi++) {
        FV ax, d0;
        int type, dir = (gi & 1) ? -1 : 1;
        if (g->siege) type = (gi & 1) ? EN_WEAVER : EN_WARDEN;
        else if (wave >= 3 && gi % 3 == 2) type = EN_ORBITER;
        else if (wave >= 2 && (gi & 1)) type = EN_WEAVER;
        else type = EN_DART;
        rand_dir(g, &ax);
        rand_dir(g, &d0);
        perp(&d0, &ax);
        for (j = 0; j < per && n < SS_MAX_EN; j++, n++) {
            Enemy *e = &g->en[n];
            e->type = type;
            e->phase = PH_WAIT;
            e->hp = type == EN_WARDEN ? 3 : 1;
            e->timer = 30 + gi * 55 + j * 6;     /* a train out of the centre, one after another */
            e->d = d0;
            e->ax = ax;
            e->w = dir * (int)((1000L * g->speed) >> 8);
            e->r = 0;
            e->r_home = (type == EN_ORBITER ? 900 : 480 + (int32_t)rnd(g, 260)) * FONE;
            e->cool = 60 + (int)rnd(g, 90);
        }
    }
    g->nen = g->left = n;
    g->between = 0;
    g->wave_tick = 0;
    memset(g->bul, 0, sizeof(g->bul));
    push(g, SSE_WAVE, wave, g->siege);
}

void ss_new_game(SSGame *g, uint32_t seed, int wave)
{
    memset(g, 0, sizeof(*g));
    g->seed = seed;
    g->rng = seed * 2654435761UL + 1;
    g->R.x = FONE; g->U.y = FONE; g->F.z = FONE;
    g->lives = SS_LIVES;
    g->next_extra = SS_EXTRA;
    ss_start_wave(g, wave < 1 ? 1 : wave);
}

/* ---------- the ship ---------- */
static void hurt(SSGame *g, int cause)
{
    if (g->invuln > 0 || g->over) return;
    g->lives--;
    g->hurt++;
    g->invuln = SS_INVULN;
    memset(g->bul, 0, sizeof(g->bul));
    push(g, SSE_HURT, g->lives, cause);
    if (g->lives <= 0) { g->over = 1; push(g, SSE_OVER, 0, 0); }
}

static int approach(int v, int target, int step)
{
    if (v < target) return v + step > target ? target : v + step;
    if (v > target) return v - step < target ? target : v - step;
    return v;
}

static void move_ship(SSGame *g, unsigned in)
{
    int tx = (in & SS_IN_RIGHT ? SS_TURN : 0) - (in & SS_IN_LEFT ? SS_TURN : 0);
    int ty = (in & SS_IN_UP ? SS_TURN : 0) - (in & SS_IN_DOWN ? SS_TURN : 0);
    g->vx = approach(g->vx, tx, SS_ACCEL);
    g->vy = approach(g->vy, ty, SS_ACCEL);
    if (g->vx) turn(&g->R, &g->F, g->vx);     /* the ship towards +R: F away from R */
    if (g->vy) turn(&g->U, &g->F, g->vy);
    if (g->vx || g->vy) orthonormal(g);
}

static void fire(SSGame *g)
{
    int k;
    if (g->fire_cool > 0) return;
    for (k = 0; k < SS_MAX_SHOTS; k++)
        if (!g->shot[k].alive) {
            Mover *s = &g->shot[k];
            s->alive = 1;
            s->t = 0;
            ss_ship_pos(g, &s->p);
            s->v.x = g->F.x * SS_SHOT_SPEED; s->v.y = g->F.y * SS_SHOT_SPEED; s->v.z = g->F.z * SS_SHOT_SPEED;
            g->shots++;
            g->fire_cool = SS_FIRE_GAP;
            push(g, SSE_SHOT, k, 0);
            return;
        }
}

static void kill_enemy(SSGame *g, int k)
{
    Enemy *e = &g->en[k];
    e->phase = PH_DEAD;
    g->kills++;
    g->left--;
    g->score += ss_enemy_score(e->type, g->wave);
    push(g, SSE_KILL, k, e->type);
}

/* the shot's step from p - v to p against enemy k: its closest approach */
static int shot_hits(const SSGame *g, const Mover *s, int k)
{
    FV ep;
    int32_t ex, ey, ez, a0, a, d2;
    ss_enemy_pos(g, k, &ep);
    ex = (ep.x - s->p.x) >> 16; ey = (ep.y - s->p.y) >> 16; ez = (ep.z - s->p.z) >> 16;
    if (ex > 400 || ex < -400 || ey > 400 || ey < -400 || ez > 400 || ez < -400) return 0;
    a0 = (int32_t)(((int64_t)ex * s->v.x + (int64_t)ey * s->v.y + (int64_t)ez * s->v.z) / ((int64_t)SS_SHOT_SPEED * FONE));
    a = a0 > 0 ? 0 : a0 < -SS_SHOT_SPEED ? -SS_SHOT_SPEED : a0;
    d2 = ex * ex + ey * ey + ez * ez - 2 * a * a0 + a * a;
    return d2 < SS_HIT_R * SS_HIT_R;
}

static void move_shots(SSGame *g)
{
    int k, j;
    for (k = 0; k < SS_MAX_SHOTS; k++) {
        Mover *s = &g->shot[k];
        if (!s->alive) continue;
        s->p.x += s->v.x; s->p.y += s->v.y; s->p.z += s->v.z;
        if (++s->t * SS_SHOT_SPEED > 2 * SS_R + 20) { s->alive = 0; continue; }
        for (j = 0; j < g->nen; j++) {
            if (!ss_enemy_live(g, j) || !shot_hits(g, s, j)) continue;
            s->alive = 0;
            g->hits++;
            if (--g->en[j].hp <= 0) kill_enemy(g, j);
            else push(g, SSE_HIT, j, g->en[j].hp);
            break;
        }
    }
}

/* ---------- enemies ---------- */
static int32_t dist_units(const FV *a, const FV *b)
{
    int32_t dx = (a->x - b->x) >> 16, dy = (a->y - b->y) >> 16, dz = (a->z - b->z) >> 16;
    return (int32_t)a3_isqrt((uint32_t)(dx * dx + dy * dy + dz * dz));
}

static void enemy_fire(SSGame *g, int k, const FV *ship)
{
    FV p, v;
    int b, spd = (20 * g->speed) >> 8;
    ss_enemy_pos(g, k, &p);
    if (dist_units(&p, ship) < 220) return;     /* too close to dodge: hold fire */
    for (b = 0; b < SS_MAX_BUL; b++)
        if (!g->bul[b].alive) {
            Mover *m = &g->bul[b];
            v.x = ship->x - p.x; v.y = ship->y - p.y; v.z = ship->z - p.z;
            fv_norm(&v);
            m->alive = 1; m->t = 0; m->p = p;
            m->v.x = v.x * spd; m->v.y = v.y * spd; m->v.z = v.z * spd;
            push(g, SSE_EFIRE, k, b);
            return;
        }
}

static int fire_gap(const SSGame *g, int type)
{
    static const int gap[EN_TYPES] = { 140, 95, 75, 55 };
    return (int)(((int32_t)gap[type] * 256) / g->speed);
}

static void move_enemy(SSGame *g, int k, const FV *ship)
{
    Enemy *e = &g->en[k];
    FV t, n;
    int32_t step = (int32_t)g->speed * FONE / 256;
    if (e->phase == PH_DEAD) return;
    if (e->phase == PH_WAIT) {
        if (--e->timer <= 0) e->phase = PH_EMERGE;
        return;
    }
    /* along its great circle */
    fv_cross(&e->ax, &e->d, &n);
    turn(&e->d, &n, e->w);
    fv_norm(&e->d);
    perp(&e->ax, &e->d);

    t.x = -g->F.x; t.y = -g->F.y; t.z = -g->F.z;    /* the ship's direction */
    switch (e->phase) {
    case PH_EMERGE:
        e->r += 13 * step;
        if (e->r >= e->r_home) {
            e->r = e->r_home;
            e->phase = PH_ORBIT;
            e->timer = 50 + (int)rnd(g, 110);
            e->w = (e->w > 0 ? 1 : -1) * (e->type == EN_ORBITER ? 130 + (int)rnd(g, 110) : (int)(((300 + (int32_t)rnd(g, 220)) * g->speed) >> 8));
        }
        break;
    case PH_ORBIT:
        if (--e->timer > 0) break;
        if (e->type == EN_ORBITER) {             /* a new great circle now and then */
            rand_dir(g, &e->ax);
            perp(&e->ax, &e->d);
            e->timer = 80 + (int)rnd(g, 80);
        } else {
            e->phase = PH_SWOOP;
            e->timer = 110;
            e->swoops++;
            e->w = (int)((720L * g->speed) >> 8);
            push(g, SSE_SWOOP, k, 0);
        }
        break;
    case PH_SWOOP: {
        fix c = fv_dot(&e->d, &t);
        FV a;
        fv_cross(&e->d, &t, &a);                /* turning about d x t takes d towards the ship */
        if (fhypot3(a.x, a.y, a.z) > FONE / 64) { fv_norm(&a); e->ax = a; }
        if (e->r < 880 * FONE) e->r += 22 * step;
        if (c > FX(0.985) || --e->timer <= 0) { e->phase = PH_RETURN; e->timer = 22; }
        break;
    }
    case PH_RETURN:                              /* on past the ship, then back in */
        if (--e->timer > 0) break;
        if (e->r > e->r_home) { e->r -= 16 * step; if (e->r < e->r_home) e->r = e->r_home; break; }
        rand_dir(g, &e->ax);
        perp(&e->ax, &e->d);
        e->phase = PH_ORBIT;
        e->timer = 60 + (int)rnd(g, 100) - (g->wave_tick > 1500 ? 40 : 0);
        e->w = ((k & 1) ? -1 : 1) * (int)(((300 + (int32_t)rnd(g, 200)) * g->speed) >> 8);
        break;
    }
    /* fire */
    if (e->phase == PH_ORBIT || e->phase == PH_SWOOP || (e->phase == PH_RETURN && e->type != EN_DART)) {
        if (e->type != EN_DART || e->phase == PH_SWOOP) {
            if (--e->cool <= 0) {
                e->cool = fire_gap(g, e->type) + (int)rnd(g, 40);
                if (g->wave_tick > 40) enemy_fire(g, k, ship);
            }
        }
    }
    /* into the ship */
    if (e->r > 700 * FONE) {
        FV p;
        ss_enemy_pos(g, k, &p);
        if (dist_units(&p, ship) < 70 && g->invuln == 0) { kill_enemy(g, k); hurt(g, 1); }
    }
}

static void move_bullets(SSGame *g, const FV *ship)
{
    int k;
    for (k = 0; k < SS_MAX_BUL; k++) {
        Mover *m = &g->bul[k];
        if (!m->alive) continue;
        m->p.x += m->v.x; m->p.y += m->v.y; m->p.z += m->v.z;
        if (++m->t > 160 || fhypot3(m->p.x >> 4, m->p.y >> 4, m->p.z >> 4) > (SS_R + 40) * 4096) { m->alive = 0; continue; }
        if (dist_units(&m->p, ship) < SS_SHIP_R) { m->alive = 0; hurt(g, 0); if (g->over) return; }
    }
}

void ss_tick(SSGame *g, unsigned in)
{
    FV ship;
    int k;
    if (g->over) return;
    g->tick++;
    if (g->invuln > 0) g->invuln--;
    if (g->fire_cool > 0) g->fire_cool--;
    move_ship(g, in);
    if (in & SS_IN_FIRE) fire(g);
    g->prev_in = in;
    move_shots(g);
    if (g->between > 0) {
        if (--g->between == 0) ss_start_wave(g, g->wave + 1);
        return;
    }
    g->wave_tick++;
    ss_ship_pos(g, &ship);
    for (k = 0; k < g->nen && !g->over; k++) move_enemy(g, k, &ship);
    if (!g->over) move_bullets(g, &ship);
    if (g->score >= g->next_extra) { g->lives++; g->next_extra += SS_EXTRA; push(g, SSE_LIFE, g->lives, 0); }
    if (g->left <= 0 && !g->over) {
        g->score += 500 * g->wave;
        g->between = SS_BETWEEN;
        memset(g->bul, 0, sizeof(g->bul));
        push(g, SSE_CLEAR, g->wave, 0);
    }
}

/* ---------- the autopilot ---------- */
/* steer the line of fire onto the nearest enemy (for one on the far side
 * the frame turns the other way), dodge bullets close by, fire when one is
 * on the line */
unsigned ss_bot(const SSGame *g)
{
    unsigned in = 0;
    int k, best = -1;
    fix lx, ly, lz, bx = 0, by = 0, bz = 0, cost, bcost = 0x7FFFFFFF;
    FV ship;
    ss_ship_pos(g, &ship);
    for (k = 0; k < g->nen; k++) {
        fix lat;
        const Enemy *e = &g->en[k];
        FV n, pd;
        int lead;
        fix c, sn;
        if (!ss_enemy_live(g, k)) continue;
        /* lead it: where it will be when a shot gets there */
        ss_local(g, &e->d, &lx, &ly, &lz);
        lead = (int)((SS_R * FONE + FMUL(lz, e->r)) / (SS_SHOT_SPEED * FONE));
        fv_cross(&e->ax, &e->d, &n);
        c = a3_cos(e->w * lead); sn = a3_sin(e->w * lead);
        pd.x = FMUL(e->d.x, c) + FMUL(n.x, sn); pd.y = FMUL(e->d.y, c) + FMUL(n.y, sn); pd.z = FMUL(e->d.z, c) + FMUL(n.z, sn);
        ss_local(g, &pd, &lx, &ly, &lz);
        lat = (lx < 0 ? -lx : lx) + (ly < 0 ? -ly : ly);
        if (FMUL(lat, e->r) < 50 * FONE && g->fire_cool == 0) in |= SS_IN_FIRE;
        cost = lat + (e->phase == PH_SWOOP ? 0 : FONE / 8);
        if (cost < bcost) { bcost = cost; best = k; bx = lx; by = ly; bz = lz; }
    }
    if (best >= 0) {
        fix sx = bz > 0 ? -bx : bx, sy = bz > 0 ? -by : by;
        fix stopx = (fix)(((int32_t)g->vx * g->vx) / 35), stopy = (fix)(((int32_t)g->vy * g->vy) / 35);
        if (sx > FONE / 90 + (g->vx > 0 ? stopx : 0)) in |= SS_IN_RIGHT;
        else if (sx < -FONE / 90 - (g->vx < 0 ? stopx : 0)) in |= SS_IN_LEFT;
        if (sy > FONE / 90 + (g->vy > 0 ? stopy : 0)) in |= SS_IN_UP;
        else if (sy < -FONE / 90 - (g->vy < 0 ? stopy : 0)) in |= SS_IN_DOWN;
    }
    /* dodge: a bullet that would pass close in the next few ticks takes
     * over the stick: keep moving the way the ship goes, or start sideways */
    if (g->invuln == 0) {
        int danger = 0;
        for (k = 0; k < SS_MAX_BUL && !danger; k++) {
            int32_t rx, ry, rz, vx, vy, vz, vv, t, mx, my, mz;
            const Mover *m = &g->bul[k];
            if (!m->alive) continue;
            rx = (m->p.x - ship.x) >> 16; ry = (m->p.y - ship.y) >> 16; rz = (m->p.z - ship.z) >> 16;
            if (rx > 600 || rx < -600 || ry > 600 || ry < -600 || rz > 600 || rz < -600) continue;
            vx = m->v.x >> 12; vy = m->v.y >> 12; vz = m->v.z >> 12;           /* sixteenths a tick */
            vv = vx * vx + vy * vy + vz * vz;
            if (vv <= 0) continue;
            t = -((rx * vx + ry * vy + rz * vz) * 16) / vv;                    /* ticks to the closest point */
            if (t < 0 || t > 14) continue;
            mx = rx + vx * t / 16; my = ry + vy * t / 16; mz = rz + vz * t / 16;
            if (mx * mx + my * my + mz * mz < 75 * 75) danger = 1;
        }
        if (danger) {
            in &= ~(unsigned)(SS_IN_LEFT | SS_IN_RIGHT | SS_IN_UP | SS_IN_DOWN);
            if (g->vx > 0 || (g->vx == 0 && g->vy == 0 && (g->tick & 64))) in |= SS_IN_RIGHT;
            else if (g->vx < 0 || (g->vx == 0 && g->vy == 0)) in |= SS_IN_LEFT;
            else in |= g->vy > 0 ? SS_IN_UP : SS_IN_DOWN;
        }
    }
    return in;
}

uint32_t ss_state_hash(const SSGame *g)
{
    /* every field is 32 bits wide: hashed as words, the same either endian */
    const uint32_t *w = (const uint32_t *)(const void *)g;
    uint32_t h = 2166136261UL;
    size_t k, n = offsetof(SSGame, ev) / 4;
    for (k = 0; k < n; k++) { h ^= w[k]; h *= 16777619UL; }
    return h;
}
