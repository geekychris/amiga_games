// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * sc_math.c - fixed-point trig with 16-bit angles, the camera transform,
 * projection and polygon clipping for ScAIry Fun (the clipping and the span
 * converter come from Terra Strike's ts_math.c).
 */
#include "sc.h"

/* sin(i * 2pi / 256) * 65536 */
const int32_t sc_sin_tab[256] = {
        0,   1608,   3216,   4821,   6424,   8022,   9616,  11204,
    12785,  14359,  15924,  17479,  19024,  20557,  22078,  23586,
    25080,  26558,  28020,  29466,  30893,  32303,  33692,  35062,
    36410,  37736,  39040,  40320,  41576,  42806,  44011,  45190,
    46341,  47464,  48559,  49624,  50660,  51665,  52639,  53581,
    54491,  55368,  56212,  57022,  57798,  58538,  59244,  59914,
    60547,  61145,  61705,  62228,  62714,  63162,  63572,  63944,
    64277,  64571,  64827,  65043,  65220,  65358,  65457,  65516,
    65536,  65516,  65457,  65358,  65220,  65043,  64827,  64571,
    64277,  63944,  63572,  63162,  62714,  62228,  61705,  61145,
    60547,  59914,  59244,  58538,  57798,  57022,  56212,  55368,
    54491,  53581,  52639,  51665,  50660,  49624,  48559,  47464,
    46341,  45190,  44011,  42806,  41576,  40320,  39040,  37736,
    36410,  35062,  33692,  32303,  30893,  29466,  28020,  26558,
    25080,  23586,  22078,  20557,  19024,  17479,  15924,  14359,
    12785,  11204,   9616,   8022,   6424,   4821,   3216,   1608,
        0,  -1608,  -3216,  -4821,  -6424,  -8022,  -9616, -11204,
   -12785, -14359, -15924, -17479, -19024, -20557, -22078, -23586,
   -25080, -26558, -28020, -29466, -30893, -32303, -33692, -35062,
   -36410, -37736, -39040, -40320, -41576, -42806, -44011, -45190,
   -46341, -47464, -48559, -49624, -50660, -51665, -52639, -53581,
   -54491, -55368, -56212, -57022, -57798, -58538, -59244, -59914,
   -60547, -61145, -61705, -62228, -62714, -63162, -63572, -63944,
   -64277, -64571, -64827, -65043, -65220, -65358, -65457, -65516,
   -65536, -65516, -65457, -65358, -65220, -65043, -64827, -64571,
   -64277, -63944, -63572, -63162, -62714, -62228, -61705, -61145,
   -60547, -59914, -59244, -58538, -57798, -57022, -56212, -55368,
   -54491, -53581, -52639, -51665, -50660, -49624, -48559, -47464,
   -46341, -45190, -44011, -42806, -41576, -40320, -39040, -37736,
   -36410, -35062, -33692, -32303, -30893, -29466, -28020, -26558,
   -25080, -23586, -22078, -20557, -19024, -17479, -15924, -14359,
   -12785, -11204,  -9616,  -8022,  -6424,  -4821,  -3216,  -1608
};

/* atan(t / 128) for t = 0..128, in 1/65536 turn */
static const uint16_t atan_tab[129] = {
    0, 81, 163, 244, 326, 407, 489, 570, 651, 732, 813, 894,
    975, 1056, 1136, 1217, 1297, 1377, 1457, 1537, 1617, 1696, 1775, 1854,
    1933, 2012, 2090, 2168, 2246, 2324, 2401, 2478, 2555, 2632, 2708, 2784,
    2860, 2935, 3010, 3085, 3159, 3233, 3307, 3380, 3453, 3526, 3599, 3670,
    3742, 3813, 3884, 3955, 4025, 4095, 4164, 4233, 4302, 4370, 4438, 4505,
    4572, 4639, 4705, 4771, 4836, 4901, 4966, 5030, 5094, 5157, 5220, 5282,
    5344, 5406, 5467, 5528, 5589, 5649, 5708, 5768, 5826, 5885, 5943, 6000,
    6058, 6114, 6171, 6227, 6282, 6337, 6392, 6446, 6500, 6554, 6607, 6660,
    6712, 6764, 6815, 6867, 6917, 6968, 7018, 7068, 7117, 7166, 7214, 7262,
    7310, 7358, 7405, 7451, 7498, 7544, 7589, 7635, 7679, 7724, 7768, 7812,
    7856, 7899, 7942, 7984, 8026, 8068, 8110, 8151, 8192,
};

int32_t sc_sin(int a)
{
    int i = (a >> 8) & 255, f = a & 255;
    int32_t s0 = sc_sin_tab[i], s1 = sc_sin_tab[(i + 1) & 255];
    return s0 + (((s1 - s0) * f) >> 8);
}

int32_t sc_cos(int a)
{
    return sc_sin(a + 16384);
}

int sc_atan_ratio(int32_t num, int32_t den)
{
    int32_t a = num < 0 ? -num : num, t, f;
    int r;
    if (den <= 0) return 0;
    if (a > den) a = den;
    t = (a * 128 * 16) / den;          /* 1/16 steps of the table */
    f = t & 15;
    t >>= 4;
    r = atan_tab[t] + (t < 128 ? ((atan_tab[t + 1] - atan_tab[t]) * f) >> 4 : 0);
    return num < 0 ? -r : r;
}

uint32_t sc_rand(uint32_t *state)
{
    *state = *state * 1103515245UL + 12345UL;
    return (*state >> 16) & 0x7FFF;
}

uint32_t sc_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dUL;
    x ^= x >> 15;
    x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}

void cam_setup(Cam *c)
{
    c->cy = sc_cos(c->yaw);
    c->sy = sc_sin(c->yaw);
    c->cp = sc_cos(c->pitch);
    c->sp = sc_sin(c->pitch);
}

/* World point -> camera space. |delta| stays below 2^15 for anything the
 * renderer draws, so the products fit in 32 bits. */
void cam_xform(const Cam *c, int32_t wx, int32_t wy, int32_t wz, V3 *out)
{
    int32_t dx = wx - c->x, dz = wz - c->z, dy = wy - c->y;
    int32_t rx, rz;

    rx = (dx * (c->cy >> 4) - dz * (c->sy >> 4)) >> 12;
    rz = (dx * (c->sy >> 4) + dz * (c->cy >> 4)) >> 12;
    out->x = rx;
    out->y = (dy * (c->cp >> 4) + rz * (c->sp >> 4)) >> 12;
    out->z = (rz * (c->cp >> 4) - dy * (c->sp >> 4)) >> 12;
}

void cam_project(const V3 *p, int32_t *sx, int32_t *sy)
{
    *sx = VIEW_CX + (p->x * FOV) / p->z;
    *sy = VIEW_CY - (p->y * FOV) / p->z;
}

int cam_horizon(const Cam *c)
{
    return VIEW_CY - (int)((FOV * (c->sp >> 4)) / (c->cp >> 4));
}

int clip_near(V3 *in, int n, V3 *out)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        V3 *a = &in[i], *b = &in[(i + 1) % n];
        int ain = a->z >= NEAR_Z, bin = b->z >= NEAR_Z;
        if (ain && m < MAX_PV) out[m++] = *a;
        if (ain != bin && m < MAX_PV) {
            /* interpolate from the inside end, so a shared edge clips to the
             * same point in both polygons whichever way they wind it */
            V3 *p = ain ? a : b, *q = ain ? b : a;
            int32_t dz = q->z - p->z;
            int32_t t = NEAR_Z - p->z;
            out[m].x = p->x + (int32_t)(((int64_t)(q->x - p->x) * t) / dz);
            out[m].y = p->y + (int32_t)(((int64_t)(q->y - p->y) * t) / dz);
            out[m].z = NEAR_Z;
            m++;
        }
    }
    return m;
}

/* One Sutherland-Hodgman pass against an axis-aligned edge.
 * axis 0 = x, 1 = y; keep values >= lim when ge, <= lim otherwise. */
static int clip_edge(int32_t *xs, int32_t *ys, int n, int32_t *ox, int32_t *oy,
                     int axis, int32_t lim, int ge)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int32_t av = axis ? ys[i] : xs[i];
        int32_t bv = axis ? ys[j] : xs[j];
        int ain = ge ? av >= lim : av <= lim;
        int bin = ge ? bv >= lim : bv <= lim;
        if (ain && m < MAX_PV) { ox[m] = xs[i]; oy[m] = ys[i]; m++; }
        if (ain != bin && m < MAX_PV) {
            /* interpolate from the inside end (see clip_near) */
            int p = ain ? i : j, q = ain ? j : i;
            int32_t pv = axis ? ys[p] : xs[p], qv = axis ? ys[q] : xs[q];
            int64_t t = lim - pv, d = qv - pv;
            if (axis) {
                ox[m] = xs[p] + (int32_t)(((int64_t)(xs[q] - xs[p]) * t) / d);
                oy[m] = lim;
            } else {
                ox[m] = lim;
                oy[m] = ys[p] + (int32_t)(((int64_t)(ys[q] - ys[p]) * t) / d);
            }
            m++;
        }
    }
    return m;
}

int clip_rect(int32_t *xs, int32_t *ys, int n, int32_t x0, int32_t y0,
              int32_t x1, int32_t y1)
{
    int32_t tx[MAX_PV], ty[MAX_PV];
    int i, inside = 1;

    for (i = 0; i < n; i++)
        if (xs[i] < x0 || xs[i] > x1 || ys[i] < y0 || ys[i] > y1) { inside = 0; break; }
    if (inside) return n;

    n = clip_edge(xs, ys, n, tx, ty, 0, x0, 1); if (n < 3) return 0;
    n = clip_edge(tx, ty, n, xs, ys, 0, x1, 0); if (n < 3) return 0;
    n = clip_edge(xs, ys, n, tx, ty, 1, y0, 1); if (n < 3) return 0;
    n = clip_edge(tx, ty, n, xs, ys, 1, y1, 0); if (n < 3) return 0;
    return n;
}

/* Scan-convert a convex polygon into per-row spans. Rows ymin..ymax get
 * xl[y - ymin]..xr[y - ymin] (inclusive). Returns the row count. Edges are
 * stepped in 16.16 fixed point; both end rows of each edge are included so
 * neighbouring polygons never leave a gap. */
int poly_spans(const int16_t *xs, const int16_t *ys, int n,
               int16_t *xl, int16_t *xr, int *ymin_out)
{
    int i, y, ymin = ys[0], ymax = ys[0], rows;

    for (i = 1; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    rows = ymax - ymin + 1;
    if (rows > SPAN_MAX) rows = SPAN_MAX;
    for (y = 0; y < rows; y++) { xl[y] = 32767; xr[y] = -32768; }

    for (i = 0; i < n; i++) {
        int x0 = xs[i], y0 = ys[i], x1 = xs[(i + 1) % n], y1 = ys[(i + 1) % n];
        int32_t x, dx;
        if (y0 > y1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        x = (int32_t)x0 << 16;
        dx = y1 > y0 ? (((int32_t)(x1 - x0)) << 16) / (y1 - y0) : 0;
        for (y = y0; y <= y1; y++, x += dx) {
            int r = y - ymin, px = (int)((x + 0x8000) >> 16);
            if (y == y1) px = x1;
            if (r < 0 || r >= rows) continue;
            if (px < xl[r]) xl[r] = (int16_t)px;
            if (px > xr[r]) xr[r] = (int16_t)px;
        }
    }
    *ymin_out = ymin;
    return rows;
}
