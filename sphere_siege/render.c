// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Sphere Siege's picture. The camera sits just behind and
 * above the ship and looks through the centre of the sphere, so it turns
 * with the ship's frame (no engine yaw/pitch: camera space is worked out
 * here from the frame, in quarter units, and the engine projects and
 * sorts it). Behind everything a starfield; then the shell as a dotted
 * grid of latitude rings and meridians, brighter near the camera; the
 * line of fire; the vortex at the centre; enemies as spinning shapes
 * facing the camera; shots, bullets and bursts; the ship. The HUD: score,
 * best, wave, ships left and the radar (enemies about the ship: red on
 * its own side of the centre, where the view doesn't reach).
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "ss.h"
#include "siege.h"

/* 0 black, 1 white, 2 panel, 3 grey, 4 space (gradient), 5-6 stars,
 * 7-9 the grid far to near, 10-11 the vortex, 12-14 the ship, 15 its
 * engine, 16-17 darts, 18-19 weavers, 20-21 orbiters, 22 wardens, 23
 * fire, 24 shots, 25 bullets, 26 red, 27 yellow, 28 green, 29 cyan, 30
 * a hurt warden, 31 the line of fire */
const uint16_t siege_palette[32] = {
    0x000, 0xFFF, 0x123, 0x99A, 0x013, 0x557, 0xCCE, 0x47C, 0x6AE, 0xAEF, 0x82C, 0xD7F,
    0xDDE, 0x99B, 0x557, 0x4CF, 0xF92, 0xFE6, 0x3E8, 0xBFD, 0xE4C, 0xFBE, 0xC33, 0xFA3,
    0x9FF, 0xF65, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0x822, 0x58A
};
#define PEN_SPACE  4
#define PEN_STAR   5
#define PEN_GRID   7
#define PEN_CORE   10
#define PEN_SHIP   12
#define PEN_ENGINE 15
#define PEN_DART   16
#define PEN_WEAVER 18
#define PEN_ORB    20
#define PEN_WARDEN 22
#define PEN_FIRE   23
#define PEN_SHOT   24
#define PEN_BUL    25
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_CYAN   29
#define PEN_WHURT  30
#define PEN_SIGHT  31

#define CAM_BACK  190                   /* the camera behind the ship */
#define CAM_LIFT  80                    /* and above it */
#define FOV       190
#define QS        14                    /* fix -> camera space in quarter units */

int grid_detail = 1;
static Cam cam;                         /* projection only */
static FV cR, cU, cF;                   /* the camera's frame */
static int32_t cpx, cpy, cpz;           /* its position, whole units */
static fix cpfx, cpfy, cpfz;            /* and 16.16 */

#define NRING  11                       /* rings at -75..75 degrees */
#define NLON   120                      /* round each, every 3 degrees */
#define NMER   24                       /* meridians, every 15 degrees */
#define NMLAT  57                       /* down each, -84..84 every 3 */
#define NGRID  (NRING * NLON + NMER * NMLAT)
#define NSTARS 140
static int16_t grid[NGRID][3];          /* whole units on the shell */
static int16_t star[NSTARS][3];         /* directions x 16384 */
static uint8_t star_pen[NSTARS];

void render_init(void)
{
    int i, j, n = 0;
    uint32_t s = 4242;
    for (i = 0; i < NRING; i++) {
        int lat = (i - 5) * 65536 / 24;  /* 15 degrees apart */
        int32_t cl = a3_cos(lat), sl = a3_sin(lat);
        for (j = 0; j < NLON; j++, n++) {
            int lon = j * 65536 / NLON;
            grid[n][0] = (int16_t)((((cl * SS_R) >> 16) * a3_cos(lon)) >> 16);
            grid[n][1] = (int16_t)((sl * SS_R) >> 16);
            grid[n][2] = (int16_t)((((cl * SS_R) >> 16) * a3_sin(lon)) >> 16);
        }
    }
    for (i = 0; i < NMER; i++) {
        int lon = i * 65536 / NMER;
        for (j = 0; j < NMLAT; j++, n++) {
            int lat = (j - 28) * 65536 / 120;
            int32_t cl = a3_cos(lat), sl = a3_sin(lat);
            grid[n][0] = (int16_t)((((cl * SS_R) >> 16) * a3_cos(lon)) >> 16);
            grid[n][1] = (int16_t)((sl * SS_R) >> 16);
            grid[n][2] = (int16_t)((((cl * SS_R) >> 16) * a3_sin(lon)) >> 16);
        }
    }
    for (i = 0; i < NSTARS; i++) {
        int32_t x, y, z, l;
        do {
            x = (int32_t)a3_rand(&s) - 16384; y = (int32_t)a3_rand(&s) - 16384; z = (int32_t)a3_rand(&s) - 16384;
            l = (int32_t)a3_isqrt((uint32_t)(x * x + y * y + z * z));
        } while (l < 4000 || l > 16384);
        star[i][0] = (int16_t)(x * 16384 / l); star[i][1] = (int16_t)(y * 16384 / l); star[i][2] = (int16_t)(z * 16384 / l);
        star_pen[i] = (uint8_t)(a3_rand(&s) % 5 == 0 ? PEN_STAR + 1 : PEN_STAR);
    }
}

/* ---------- the camera ---------- */
static void camera(const SSGame *g)
{
    FV ship;
    ss_ship_pos(g, &ship);
    cpfx = ship.x - g->F.x * CAM_BACK + g->U.x * CAM_LIFT;
    cpfy = ship.y - g->F.y * CAM_BACK + g->U.y * CAM_LIFT;
    cpfz = ship.z - g->F.z * CAM_BACK + g->U.z * CAM_LIFT;
    cpx = cpfx >> 16; cpy = cpfy >> 16; cpz = cpfz >> 16;
    cF.x = -cpfx; cF.y = -cpfy; cF.z = -cpfz;     /* at the centre */
    fv_norm(&cF);
    cR = g->R;
    fv_cross(&cF, &cR, &cU);
    memset(&cam, 0, sizeof(cam));
    cam_view(&cam, SCREEN_W, SCREEN_H, SCREEN_W / 2, SCREEN_H / 2, FOV);
}

/* a 16.16 world point to camera space (quarter units) */
static void xf(fix x, fix y, fix z, V3 *o)
{
    FV v;
    v.x = x - cpfx; v.y = y - cpfy; v.z = z - cpfz;
    o->x = fv_dot(&v, &cR) >> QS;
    o->y = fv_dot(&v, &cU) >> QS;
    o->z = fv_dot(&v, &cF) >> QS;
}

/* whole units, fast (the grid): (w - c) . axis stays in 32 bits */
static void xfi(int32_t x, int32_t y, int32_t z, V3 *o)
{
    x -= cpx; y -= cpy; z -= cpz;
    o->x = (x * cR.x + y * cR.y + z * cR.z) >> QS;
    o->y = (x * cU.x + y * cU.y + z * cU.z) >> QS;
    o->z = (x * cF.x + y * cF.y + z * cF.z) >> QS;
}

static int proj(const V3 *p, int *px, int *py)     /* in the drawing's pixels */
{
    int32_t qx, qy;
    if (p->z < NEAR_Z * 4) return 0;
    qx = p->x * FOV; qy = p->y * FOV;
    *px = SCREEN_W / 2 * A3_SUB + (qx / p->z) * A3_SUB + ((qx % p->z) * A3_SUB) / p->z;
    *py = SCREEN_H / 2 * A3_SUB - ((qy / p->z) * A3_SUB + ((qy % p->z) * A3_SUB) / p->z);
    return *px >= 0 && *px < PHYS_W && *py >= 0 && *py < PHYS_H;
}

static void dot(Canvas *cv, int px, int py, int size, int pen)
{
    int k;
    for (k = 0; k < size; k++) g_span_sub(cv, py + k, px, px + size - 1, pen);
}

/* ---------- the backdrop: stars and the shell ---------- */
static void stars(Canvas *cv)
{
    int i, px, py;
    for (i = 0; i < NSTARS; i++) {
        V3 p;
        int32_t x = star[i][0], y = star[i][1], z = star[i][2];
        p.z = (x * cF.x + y * cF.y + z * cF.z) >> 16;
        if (p.z < 2000) continue;
        p.x = (x * cR.x + y * cR.y + z * cR.z) >> 16;
        p.y = (x * cU.x + y * cU.y + z * cU.z) >> 16;
        if (proj(&p, &px, &py)) dot(cv, px, py, A3_SUB, star_pen[i]);
    }
}

static void shell(Canvas *cv)
{
    int i, px, py;
    if (!grid_detail) return;
    for (i = 0; i < NGRID; i++) {
        V3 p;
        xfi(grid[i][0], grid[i][1], grid[i][2], &p);
        if (p.z < 300 || !proj(&p, &px, &py)) continue;
        if (p.z > 6400) dot(cv, px, py, A3_SUB > 1 ? 2 : 1, PEN_GRID);
        else if (p.z > 3600) dot(cv, px, py, 2, PEN_GRID + 1);
        else dot(cv, px, py, A3_SUB > 1 ? 3 : 2, PEN_GRID + 2);
    }
}

/* the line of fire: from the ship through the centre */
static void fire_line(Canvas *cv, const SSGame *g)
{
    int t, px, py;
    FV s;
    ss_ship_pos(g, &s);
    for (t = 160; t < 2 * SS_R - 60; t += 90) {
        V3 p;
        if (t > SS_R - 70 && t < SS_R + 70) continue;
        xf(s.x + g->F.x * t, s.y + g->F.y * t, s.z + g->F.z * t, &p);
        if (proj(&p, &px, &py)) dot(cv, px, py, A3_SUB, PEN_SIGHT);
    }
}

/* ---------- 3D ---------- */
/* a flat shape facing the camera: n points round c, radius r (quarter
 * units) at angle a + k * 65536 / n, alternate points at r * inner / 256 */
static void shape(const V3 *c, int n, int32_t r, int inner, int a, int pen, int32_t bias)
{
    V3 p[8];
    int k;
    if (c->z < NEAR_Z * 4 + r / 2) return;
    if (n > 8) n = 8;
    for (k = 0; k < n; k++) {
        int ang = a + k * 65536 / n;
        int32_t rr = (k & 1) ? r * inner / 256 : r;
        p[k].x = c->x + ((a3_sin(ang) * rr) >> 16);
        p[k].y = c->y + ((a3_cos(ang) * rr) >> 16);
        p[k].z = c->z;
    }
    a3_poly(&cam, c->z + bias, pen, p, n);
}

static void vortex(const SSGame *g)
{
    V3 c;
    int32_t r = (55 + ((a3_sin((int)g->tick * 2600) * 15) >> 16)) * 4;
    xf(0, 0, 0, &c);
    shape(&c, 8, r, 140, (int)g->tick * 900, PEN_CORE, 40);
    shape(&c, 8, r / 2, 120, -(int)g->tick * 1500, PEN_CORE + 1, 39);
}

static void enemies(const SSGame *g)
{
    int k;
    for (k = 0; k < g->nen; k++) {
        const Enemy *e = &g->en[k];
        FV p;
        V3 c;
        int spin = (int)g->tick * 1400 + k * 5000;
        if (e->phase == PH_WAIT || e->phase == PH_DEAD) continue;
        ss_enemy_pos(g, k, &p);
        xf(p.x, p.y, p.z, &c);
        if (c.z < 200) continue;
        if (e->r < SS_SAFE_R * FONE) {          /* still warping in */
            int32_t r = (int32_t)(e->r >> 16) * 4 / 3 + 30;
            shape(&c, 8, r, 70, spin, ((g->tick + k) & 2) ? 1 : PEN_CYAN, 0);
            continue;
        }
        switch (e->type) {
        case EN_DART:
            shape(&c, 4, 150, 140, spin, PEN_DART, 0);
            shape(&c, 4, 60, 256, -spin, PEN_DART + 1, -2);
            break;
        case EN_WEAVER:
            shape(&c, 6, 140, 256, spin / 2, PEN_WEAVER, 0);
            shape(&c, 6, 120, 90, spin, PEN_WEAVER + 1, -2);
            break;
        case EN_ORBITER:
            shape(&c, 8, 150, 170, spin / 3, PEN_ORB, 0);
            shape(&c, 4, 70, 256, -spin, PEN_ORB + 1, -2);
            break;
        default:
            shape(&c, 8, 200, 256, spin / 4, e->hp >= 3 ? PEN_WARDEN : PEN_WHURT, 0);
            shape(&c, 4, 150, 60, -spin / 2, PEN_FIRE, -2);
            shape(&c, 4, 60, 256, spin, e->hp >= 2 ? PEN_YELLOW : PEN_RED, -4);
            break;
        }
    }
}

static void movers(const SSGame *g)
{
    int k;
    V3 c;
    for (k = 0; k < SS_MAX_SHOTS; k++) {
        const Mover *s = &g->shot[k];
        if (!s->alive) continue;
        xf(s->p.x, s->p.y, s->p.z, &c);
        shape(&c, 4, 64, 120, 0, PEN_SHOT, 0);
        xf(s->p.x - s->v.x / 2, s->p.y - s->v.y / 2, s->p.z - s->v.z / 2, &c);
        shape(&c, 4, 36, 120, 0, PEN_CYAN, 1);
    }
    for (k = 0; k < SS_MAX_BUL; k++) {
        const Mover *b = &g->bul[k];
        if (!b->alive) continue;
        xf(b->p.x, b->p.y, b->p.z, &c);
        shape(&c, 6, 64, 256, (int)g->tick * 3000, PEN_BUL, 0);
        shape(&c, 4, 28, 256, 0, PEN_YELLOW, -1);
    }
    for (k = 0; k < MAX_BURSTS; k++) {
        const Burst *b = &sg.burst[k];
        if (b->t <= 0) continue;
        xfi(b->x, b->y, b->z, &c);
        shape(&c, 8, (int32_t)b->size * 4 * (14 - b->t) / 10, 110, b->t * 1700,
              b->t > 7 ? PEN_YELLOW : b->t > 3 ? PEN_FIRE : PEN_RED, -6);
    }
}

/* the ship: a delta in its own frame (x along R, y along U, z along F) */
static void ship_pt(const SSGame *g, const FV *s, int x, int y, int z, V3 *o)
{
    x = x * 3 / 4; y = y * 3 / 4; z = z * 3 / 4;
    xf(s->x + g->R.x * x + g->U.x * y + g->F.x * z,
       s->y + g->R.y * x + g->U.y * y + g->F.y * z,
       s->z + g->R.z * x + g->U.z * y + g->F.z * z, o);
}

static void ship(const SSGame *g)
{
    static const int8_t v[8][3] = {
        { 0, 4, 60 }, { -48, -2, -12 }, { 48, -2, -12 }, { 0, 0, -20 }, { 0, 17, -2 },
        { -50, 12, -16 }, { 50, 12, -16 }, { 0, -6, 0 } };
    static const uint8_t f[8][4] = {
        { 0, 1, 4, PEN_SHIP }, { 0, 4, 2, PEN_SHIP + 1 }, { 1, 3, 4, PEN_SHIP + 2 }, { 4, 3, 2, PEN_SHIP + 1 },
        { 1, 5, 3, PEN_RED }, { 2, 3, 6, PEN_RED }, { 0, 7, 1, PEN_SHIP + 2 }, { 0, 2, 7, PEN_SHIP + 2 } };
    V3 p[8], t[3], c;
    FV s;
    int k;
    if (g->invuln > 0 && (g->invuln & 4)) return;
    ss_ship_pos(g, &s);
    for (k = 0; k < 8; k++) ship_pt(g, &s, v[k][0], v[k][1], v[k][2], &p[k]);
    for (k = 0; k < 8; k++) {
        t[0] = p[f[k][0]]; t[1] = p[f[k][1]]; t[2] = p[f[k][2]];
        a3_poly(&cam, ((t[0].z + t[1].z + t[2].z) / 3) - 200, f[k][3], t, 3);
    }
    ship_pt(g, &s, 0, 2, -24, &c);
    shape(&c, 6, 14 + (int32_t)((g->tick & 3) * 4), 160, (int)g->tick * 4000, PEN_ENGINE, -240);
}

static void scene(Canvas *cv, const SSGame *g)
{
    camera(g);
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_SPACE);
    g_sync();
    stars(cv);
    shell(cv);
    if (sg.state != GS_TITLE) fire_line(cv, g);
    a3_begin(12000);
    vortex(g);
    enemies(g);
    movers(g);
    if (!g->over) ship(g);
    a3_flush(cv);
}

/* ---------- the HUD ---------- */
#define RAD_X 286
#define RAD_Y 222
#define RAD_R 28
static void radar(Canvas *cv, const SSGame *g)
{
    int k;
    g_circle(cv, RAD_X, RAD_Y, RAD_R + 2, 3);
    g_circle(cv, RAD_X, RAD_Y, RAD_R, 2);
    g_hline(cv, RAD_X - RAD_R + 3, RAD_X + RAD_R - 3, RAD_Y, PEN_GRID + 1);
    g_vline(cv, RAD_X, RAD_Y - RAD_R + 3, RAD_Y + RAD_R - 3, PEN_GRID + 1);
    for (k = 0; k < g->nen; k++) {
        fix x, y, z;
        int px, py;
        if (g->en[k].phase == PH_WAIT || g->en[k].phase == PH_DEAD) continue;
        ss_local(g, &g->en[k].d, &x, &y, &z);
        px = RAD_X + (int)((x * (RAD_R - 2)) >> 16);
        py = RAD_Y - (int)((y * (RAD_R - 2)) >> 16);
        if (z < 0) g_rect(cv, px - 1, py - 1, px + 1, py + 1, g->en[k].phase == PH_SWOOP ? PEN_YELLOW : PEN_RED);
        else g_rect(cv, px, py, px, py, PEN_GREEN);
    }
    g_rect(cv, RAD_X - 1, RAD_Y - 1, RAD_X + 1, RAD_Y + 1, 1);
}

static void hud(Canvas *cv, const SSGame *g)
{
    char tmp[40];
    int k;
    sprintf(tmp, "%ld", (long)g->score);
    g_text_shadow(cv, 4, 10, 1, tmp);
    sprintf(tmp, "HI %ld", (long)(sg.best > g->score ? sg.best : g->score));
    g_text_centre(cv, 10, 3, tmp);
    sprintf(tmp, "WAVE %ld", (long)g->wave);
    g_text_shadow(cv, SCREEN_W - 4 - 8 * (int)strlen(tmp), 10, g->siege ? PEN_RED : PEN_YELLOW, tmp);
    for (k = 0; k < g->lives - 1 && k < 8; k++) {
        int16_t xs[3] = { (int16_t)(8 + k * 14), (int16_t)(14 + k * 14), (int16_t)(20 + k * 14) };
        int16_t ys[3] = { SCREEN_H - 6, SCREEN_H - 16, SCREEN_H - 6 };
        g_poly(cv, xs, ys, 3, PEN_SHIP, 0, SCREEN_H - 1);
    }
    radar(cv, g);
    if (sg.autoplay) g_text_shadow(cv, SCREEN_W - 104, SCREEN_H - 4, PEN_GREEN, "AUTO");
    if (sg.hurt_timer > 0) {
        g_rect(cv, 0, 0, SCREEN_W - 1, 3, PEN_RED);
        g_rect(cv, 0, SCREEN_H - 4, SCREEN_W - 1, SCREEN_H - 1, PEN_RED);
        g_rect(cv, 0, 0, 3, SCREEN_H - 1, PEN_RED);
        g_rect(cv, SCREEN_W - 4, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_RED);
    }
}

void render_game(Canvas *cv, const SSGame *g)
{
    char tmp[48];
    scene(cv, g);
    hud(cv, g);
    if (g->between > 0 && g->between < SS_BETWEEN - 10 && !g->over) {
        g_text_big(cv, 72, 70, PEN_GREEN, "WAVE CLEAR", 2);
        sprintf(tmp, "BONUS %ld", (long)(500 * g->wave));
        g_text_centre(cv, 104, 1, tmp);
        if ((g->wave + 1) % SS_SIEGE == 0) g_text_centre(cv, 120, PEN_RED, "NEXT: A SIEGE WAVE");
    } else if (g->wave_tick < 50 && !g->between) {
        sprintf(tmp, g->siege ? "SIEGE WAVE %ld" : "WAVE %ld", (long)g->wave);
        g_text_big(cv, (SCREEN_W - 16 * (int)strlen(tmp)) / 2, 70, g->siege ? PEN_RED : PEN_YELLOW, tmp, 2);
        if (g->siege) g_text_centre(cv, 104, 1, "WARDENS TAKE THREE HITS");
    }
    if (sg.state == GS_OVER) {
        g_box(cv, 50, 80, 270, 168, 2, PEN_RED);
        g_text_big(cv, 76, 88, PEN_RED, "GAME OVER", 2);
        sprintf(tmp, "WAVE %ld   %ld%% HITS", (long)g->wave, (long)(g->shots ? g->hits * 100 / g->shots : 0));
        g_text_centre(cv, 122, PEN_YELLOW, tmp);
        sprintf(tmp, "SCORE %ld", (long)g->score);
        g_text_centre(cv, 136, 1, tmp);
        if (sg.new_best) g_text_centre(cv, 150, (g->tick / 6) & 1 ? PEN_GREEN : PEN_YELLOW, "A NEW HIGH SCORE!");
        if (sg.state_timer == 0) g_text_centre(cv, 164, PEN_YELLOW, "PRESS FIRE");
    }
    if (sg.paused == 1) g_text_centre(cv, 180, 1, "PAUSED");
    if (sg.flash_timer > 0 && sg.flash[0]) g_text_centre(cv, 44, sg.flash_pen, sg.flash);
}

void render_title(Canvas *cv, const SSGame *g)
{
    char tmp[48];
    scene(cv, g);                        /* the autopilot plays behind the title */
    g_text_big(cv, 66, 26, 0, "SPHERE", 4);
    g_text_big(cv, 64, 24, PEN_CYAN, "SPHERE", 4);
    g_text_big(cv, 82, 62, 0, "SIEGE", 4);
    g_text_big(cv, 80, 60, PEN_CORE + 1, "SIEGE", 4);
    g_box(cv, 40, 186, 280, 252, 2, 3);
    sprintf(tmp, "START AT WAVE %ld (UP/DOWN)", (long)sg.start_wave);
    g_text_centre(cv, 200, PEN_GREEN, tmp);
    g_text_centre(cv, 216, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO PLAY");
    sprintf(tmp, "HIGH SCORE %ld", (long)sg.best);
    g_text_centre(cv, 232, 1, tmp);
    g_text_centre(cv, 246, 3, "HELP: HOW TO PLAY");
}

static const char *const help_lines[2][12] = {
    { "YOUR SHIP RIDES THE INSIDE OF A HOLLOW",
      "SPHERE, ALWAYS FACING ITS CENTRE.",
      "SHOTS CROSS THE CENTRE: THEY HIT WHAT",
      "IS ON THE LINE ON EITHER SIDE OF IT.",
      "",
      " DARTS     SPIRAL OUT AND SWOOP AT YOU",
      " WEAVERS   SWOOP AND SHOOT",
      " ORBITERS  RUN ALONG THE SHELL",
      " WARDENS   EVERY 4TH WAVE: 3 HITS",
      "",
      "THE RADAR SHOWS ENEMIES ON YOUR SIDE",
      "OF THE CENTRE IN RED." },
    { "ARROWS / JOYSTICK   SLIDE OVER THE SHELL",
      "SPACE / FIRE        FIRE (HOLD)",
      "P                   PAUSE",
      "B                   AUTOPILOT",
      "HELP / H            THESE PAGES",
      "ESC                 TITLE / QUIT",
      "",
      "DART 100, WEAVER 150, ORBITER 200,",
      "WARDEN 400, +10 A WAVE. WAVE CLEAR:",
      "500 x THE WAVE. A SHIP EVERY 25000.",
      "",
      "LEAD THE ONES ON THE FAR SIDE." },
};

void render_help(Canvas *cv, int page)
{
    int k;
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, 2);
    g_sync();
    g_text_big(cv, page == 1 ? 72 : 96, 14, PEN_YELLOW, page == 1 ? "HOW TO PLAY" : "CONTROLS", 2);
    for (k = 0; k < 12; k++) g_text(cv, 8, 58 + k * 14, 1, help_lines[page - 1][k]);
    g_text(cv, 8, 242, 3, page == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2");
    g_text(cv, 156, 242, PEN_YELLOW, "HELP: NEXT  FIRE: PLAY");
}
