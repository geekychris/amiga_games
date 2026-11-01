// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * bt_core.c - Boing Twist's puzzle: the state, the two moves, scrambles,
 * undo and the solved test. No Amiga headers; tests/test_core.c runs it
 * on the host and the game's self-test hook runs bt_vector_hash() on the
 * Amiga to show both get the same answer.
 */
#include <string.h>
#include "bt_core.h"

uint32_t bt_rng(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

void bt_unique_sig(BtPuzzle *p)
{
    int h;
    for (h = 0; h < BT_MAX_T; h++) {
        p->sig[h][0] = (uint32_t)h * 2u + 1u;
        p->sig[h][1] = (uint32_t)h * 2u + 2u;
    }
}

void bt_reset(BtPuzzle *p)
{
    int i, n = p->bands * p->segs;
    for (i = 0; i < BT_MAX_T; i++) {
        p->slot[i].home = (uint8_t)(i < n ? i : 0);
        p->slot[i].orient = 0;
    }
    p->nhist = p->nscramble = p->moves = 0;
}

int bt_init(BtPuzzle *p, int bands, int segs)
{
    if (bands < 2 || bands > BT_MAX_B || segs < 4 || segs > BT_MAX_S || (segs & 3)) return 0;
    memset(p, 0, sizeof(*p));
    p->bands = bands;
    p->segs = segs;
    bt_unique_sig(p);
    bt_reset(p);
    return 1;
}

void bt_set_sig(BtPuzzle *p, int home, uint32_t upright, uint32_t turned)
{
    if (home < 0 || home >= BT_MAX_T) return;
    p->sig[home][0] = upright;
    p->sig[home][1] = turned;
}

int bt_valid(const BtPuzzle *p, int m)
{
    if (m < 0 || m > 0x1FF) return 0;
    if (BT_IS_FLIP(m)) return BT_HALF(m) < p->segs;
    return BT_BAND(m) < p->bands;
}

int bt_inverse(int m)
{
    return BT_IS_FLIP(m) ? m : (m ^ 1);
}

static int wrap(int s, int n)
{
    s %= n;
    return s < 0 ? s + n : s;
}

int bt_moved_slots(const BtPuzzle *p, int m, int *out)
{
    int n = 0, b, s;
    if (!bt_valid(p, m)) return 0;
    if (BT_IS_FLIP(m)) {
        for (b = 0; b < p->bands; b++)
            for (s = 0; s < p->segs / 2; s++)
                out[n++] = b * p->segs + wrap(BT_HALF(m) + s, p->segs);
    } else {
        for (s = 0; s < p->segs; s++) out[n++] = BT_BAND(m) * p->segs + s;
    }
    return n;
}

void bt_apply(BtPuzzle *p, int m)
{
    BtPiece old[BT_MAX_T];
    int S = p->segs, B = p->bands, b, s;
    if (!bt_valid(p, m)) return;
    memcpy(old, p->slot, sizeof(old));           /* 256 bytes: newlib's memcpy is fine at this size */
    if (BT_IS_FLIP(m)) {
        int k = BT_HALF(m);
        for (b = 0; b < B; b++)
            for (s = 0; s < S / 2; s++) {
                int from = wrap(k + s, S);
                int to = wrap(2 * k + S / 2 - 1 - from, S);
                BtPiece q = old[b * S + from];
                q.orient ^= 1;
                p->slot[(B - 1 - b) * S + to] = q;
            }
    } else {
        int d = BT_WEST(m) ? -1 : 1;
        b = BT_BAND(m);
        for (s = 0; s < S; s++) p->slot[b * S + wrap(s + d, S)] = old[b * S + s];
    }
}

static void push(BtPuzzle *p, int m)
{
    if (p->nhist >= BT_HIST) {                   /* full: forget the oldest (it can't be undone now) */
        memmove(p->hist, p->hist + 1, sizeof(p->hist) - sizeof(p->hist[0]));
        p->nhist--;
        if (p->nscramble > 0) p->nscramble--;
    }
    p->hist[p->nhist++] = (int16_t)m;
}

int bt_do(BtPuzzle *p, int m)
{
    if (!bt_valid(p, m)) return 0;
    bt_apply(p, m);
    push(p, m);
    p->moves++;
    return 1;
}

int bt_last_move(const BtPuzzle *p)
{
    return p->nhist > p->nscramble ? p->hist[p->nhist - 1] : -1;
}

int bt_undo(BtPuzzle *p)
{
    int m = bt_last_move(p);
    if (m < 0) return -1;
    p->nhist--;
    bt_apply(p, bt_inverse(m));
    if (p->moves > 0) p->moves--;
    return m;
}

int bt_random_move(const BtPuzzle *p, uint32_t *rng)
{
    uint32_t r = bt_rng(rng);
    if (r % 3u == 0) return BT_FLIP((int)((r >> 8) % (uint32_t)p->segs));
    return BT_TWIST((int)((r >> 8) % (uint32_t)p->bands), (int)((r >> 20) & 1));
}

void bt_scramble(BtPuzzle *p, uint32_t seed, int n)
{
    uint32_t rng = seed ? seed : 1;
    int i, prev = -1;
    bt_reset(p);
    for (i = 0; i < n; i++) {
        int m;
        do m = bt_random_move(p, &rng); while (prev >= 0 && m == bt_inverse(prev));
        bt_apply(p, m);
        push(p, m);
        prev = m;
    }
    p->nscramble = p->nhist;
    p->moves = 0;
}

int bt_identity(const BtPuzzle *p)
{
    int i, n = p->bands * p->segs;
    for (i = 0; i < n; i++)
        if (p->slot[i].home != i || p->slot[i].orient) return 0;
    return 1;
}

int bt_solved(const BtPuzzle *p)
{
    int S = p->segs, B = p->bands, r, b, s;
    for (r = 0; r < S; r++) {
        int ok = 1;
        for (b = 0; b < B && ok; b++)
            for (s = 0; s < S && ok; s++) {
                BtPiece q = p->slot[b * S + s];
                if (p->sig[q.home][q.orient & 1] != p->sig[b * S + wrap(s - r, S)][0]) ok = 0;
            }
        if (ok) return 1;
    }
    return 0;
}

uint32_t bt_hash(const BtPuzzle *p)
{
    uint32_t h = 2166136261u;
    int i, n = p->bands * p->segs;
    h = (h ^ (uint32_t)p->bands) * 16777619u;
    h = (h ^ (uint32_t)p->segs) * 16777619u;
    for (i = 0; i < n; i++) {
        h = (h ^ p->slot[i].home) * 16777619u;
        h = (h ^ p->slot[i].orient) * 16777619u;
    }
    return h;
}

/* scrambles on every size the game offers, a long random walk of the
 * player's moves with undos mixed in, and the solved test along the way:
 * all folded into one number */
uint32_t bt_vector_hash(void)
{
    static const int sizes[][2] = { { 4, 8 }, { 6, 12 }, { 8, 16 }, { 2, 4 }, { 4, 16 } };
    static BtPuzzle p;
    uint32_t acc = 0x12345678u, rng = 0xB0196u;
    unsigned z;
    int i;
    for (z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
        bt_init(&p, sizes[z][0], sizes[z][1]);
        bt_scramble(&p, 1000u + z, 200);
        acc = (acc ^ bt_hash(&p)) * 16777619u;
        for (i = 0; i < 3000; i++) {
            uint32_t r = bt_rng(&rng);
            if (r % 7u == 0) bt_undo(&p);
            else bt_do(&p, bt_random_move(&p, &rng));
            acc = (acc ^ bt_hash(&p)) * 16777619u;
            acc ^= (uint32_t)bt_solved(&p) << (i & 15);
        }
        acc = (acc ^ (uint32_t)p.moves) * 16777619u;
    }
    return acc;
}
