// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.h - Boing Twist's 3D: the ball and its room in OpenGL, through
 * Mesa's software rasteriser (OSMesa), into an A R G B frame.
 */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include "bt_core.h"

/* the room is a box round the ball: back wall, floor, side walls, ceiling */
enum { FACE_BACK, FACE_FLOOR, FACE_LEFT, FACE_RIGHT, FACE_CEIL, NFACES };

typedef struct {
    int on;
    uint32_t col, grid;        /* 0xRRGGBB; grid NO_GRID: none */
    float cell;                /* grid square, in ball radii */
    const uint8_t *tex;        /* R G B rows or 0: over the face's visible part */
    int tw, th;
    float repeat;              /* the texture this many times across (square tiles); 0 or 1: once, stretched */
} RdFace;
#define NO_GRID 0xFF000000UL

typedef struct {
    uint32_t bg;               /* behind everything */
    RdFace face[NFACES];
    int shadow_floor;          /* 0: on the back wall, up and right (the 1984 demo); 1: on the floor */
    int shadow_pct;            /* how dark: 0 none .. 100 black */
} RdTheme;

typedef struct {
    float yaw, pitch, roll;    /* the ball's turn, degrees */
    float x, y, dist;          /* its centre: across, up, distance from the eye */
} RdView;

/* flags for rd_draw */
#define RD_PEEK      1          /* every piece at home: the picture as it should be */
#define RD_SELECT    2          /* show the selected band and half */
#define RD_NO_ROOM   4          /* don't redraw the background (it's still in the frame) */

int  rd_open(int w, int h);
void rd_close(void);
uint32_t *rd_pixels(void);              /* the frame, w x h, A R G B */
int  rd_width(void);
int  rd_height(void);
const char *rd_renderer(void);

/* the picture: R G B, rows top (north) to bottom, w and h powers of two.
 * Kept (not copied) until the next call; tile edges are drawn into a copy
 * for this grid. sig[home][2] gets what each tile looks like (for bt_set_sig). */
int  rd_set_picture(const uint8_t *rgb, int w, int h, int bands, int segs, uint32_t sig[][2]);
void rd_set_theme(const RdTheme *t);    /* draws the room once into the background */

/* the ball: anim_move (-1 none) part done (0..1) */
void rd_draw(const BtPuzzle *p, const RdView *v, int anim_move, float anim_t,
             int sel_band, int sel_half, int flags);
/* where a pixel lands on the ball: 1 and lat (90 north .. -90), lon (0..360 east) */
int  rd_pick(const RdView *v, int mx, int my, float *lat, float *lon);
/* the ball's centre and radius on screen */
void rd_ball_screen(const RdView *v, int *cx, int *cy, int *r);

/* options and measurements */
extern int rd_lit;            /* 1: shaded (in C, after GL), 0: flat */
extern int rd_linear;         /* 1: GL_LINEAR, 0: GL_NEAREST */
extern int rd_detail;         /* sphere quads round the equator (16..64) */
extern int rd_zbuf;           /* 1: depth test the ball (0: a convex ball culled needs none) */
extern int rd_tris;           /* triangles sent last frame */
extern uint32_t rd_us_bg, rd_us_ball, rd_us_shade;   /* last frame: background + shadow, ball (with shading), shading */
extern unsigned long (*rd_clock)(void); /* microseconds, for the timings (0: none) */

#endif
