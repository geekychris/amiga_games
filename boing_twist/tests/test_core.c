// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_core.c - Boing Twist's puzzle core on the host.
 *   make host-test   (and make ubsan: the same under UBSan and ASan)
 *   make vectors     prints bt_vector_hash() for tests/vectors.h
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../bt_core.h"
#include "vectors.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const int sizes[][2] = { { 4, 8 }, { 6, 12 }, { 8, 16 }, { 2, 4 }, { 4, 16 }, { 8, 4 } };
#define NSIZES ((int)(sizeof(sizes) / sizeof(sizes[0])))

static int same(const BtPuzzle *a, const BtPuzzle *b)
{
    return memcmp(a->slot, b->slot, sizeof(a->slot)) == 0;
}

/* every move a size has */
static int all_moves(const BtPuzzle *p, int *out)
{
    int n = 0, i;
    for (i = 0; i < p->bands; i++) { out[n++] = BT_TWIST(i, 0); out[n++] = BT_TWIST(i, 1); }
    for (i = 0; i < p->segs; i++) out[n++] = BT_FLIP(i);
    return n;
}

static void test_sizes(void)
{
    BtPuzzle p;
    CHECK(!bt_init(&p, 1, 8), "1 band refused");
    CHECK(!bt_init(&p, 9, 8), "9 bands refused");
    CHECK(!bt_init(&p, 4, 6), "6 segments refused (not a multiple of 4)");
    CHECK(!bt_init(&p, 4, 20), "20 segments refused");
    CHECK(bt_init(&p, 4, 8) && bt_identity(&p) && bt_solved(&p), "4x8 starts solved");
    CHECK(!bt_valid(&p, BT_TWIST(4, 0)) && !bt_valid(&p, BT_FLIP(8)) && !bt_valid(&p, -1), "out of range moves");
    CHECK(bt_valid(&p, BT_TWIST(3, 1)) && bt_valid(&p, BT_FLIP(7)), "in range moves");
}

static void test_cycles(void)
{
    int z;
    for (z = 0; z < NSIZES; z++) {
        BtPuzzle p, ref;
        int b, k, i;
        bt_init(&p, sizes[z][0], sizes[z][1]);
        bt_scramble(&p, 77u + (uint32_t)z, 40);
        ref = p;
        for (b = 0; b < p.bands; b++) {
            for (i = 0; i < p.segs; i++) bt_apply(&p, BT_TWIST(b, 0));
            CHECK(same(&p, &ref), "%dx%d: %d east twists of band %d are no move", p.bands, p.segs, p.segs, b);
            for (i = 0; i < p.segs; i++) bt_apply(&p, BT_TWIST(b, 1));
            CHECK(same(&p, &ref), "%dx%d: %d west twists of band %d are no move", p.bands, p.segs, p.segs, b);
            /* S/4 twists are a quarter turn: four of them come back */
            for (i = 0; i < 4 * (p.segs / 4); i++) bt_apply(&p, BT_TWIST(b, 0));
            CHECK(same(&p, &ref), "%dx%d: four quarter turns of band %d", p.bands, p.segs, b);
            bt_apply(&p, BT_TWIST(b, 0));
            CHECK(!same(&p, &ref), "%dx%d: one twist of band %d changes it", p.bands, p.segs, b);
            bt_apply(&p, BT_TWIST(b, 1));
        }
        for (k = 0; k < p.segs; k++) {
            bt_apply(&p, BT_FLIP(k));
            CHECK(!same(&p, &ref), "%dx%d: flip %d changes the ball", p.bands, p.segs, k);
            bt_apply(&p, BT_FLIP(k));
            CHECK(same(&p, &ref), "%dx%d: two flips of half %d are no move", p.bands, p.segs, k);
        }
    }
}

static void test_inverses(void)
{
    int z;
    for (z = 0; z < NSIZES; z++) {
        BtPuzzle p, ref;
        int mv[64], n, i, t;
        uint32_t rng = 5u + (uint32_t)z;
        bt_init(&p, sizes[z][0], sizes[z][1]);
        n = all_moves(&p, mv);
        for (t = 0; t < 20; t++) {
            bt_scramble(&p, bt_rng(&rng), 30);
            ref = p;
            for (i = 0; i < n; i++) {
                bt_apply(&p, mv[i]);
                bt_apply(&p, bt_inverse(mv[i]));
                CHECK(same(&p, &ref), "%dx%d: move %x then its inverse", p.bands, p.segs, mv[i]);
            }
        }
        CHECK(bt_inverse(bt_inverse(BT_TWIST(1, 0))) == BT_TWIST(1, 0), "inverse of inverse");
        CHECK(bt_inverse(BT_FLIP(3)) == BT_FLIP(3), "a flip is its own inverse");
    }
}

/* a move is a permutation that keeps the piece count and turns flipped pieces */
static void test_permutation(void)
{
    BtPuzzle p;
    int seen[BT_MAX_T], i, k, flips = 0;
    bt_init(&p, 6, 12);
    bt_scramble(&p, 99, 500);
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < 72; i++) seen[p.slot[i].home]++;
    for (i = 0; i < 72; i++) CHECK(seen[i] == 1, "piece %d once after 500 moves", i);

    bt_init(&p, 4, 8);
    bt_apply(&p, BT_FLIP(0));
    for (i = 0; i < 32; i++) flips += p.slot[i].orient;
    CHECK(flips == 16, "a flip turns half the ball's pieces (%d)", flips);
    /* half 0 of 4x8 is segments 0..3; (0,0) goes to (3,3), turned */
    CHECK(p.slot[3 * 8 + 3].home == 0 && p.slot[3 * 8 + 3].orient == 1, "(0,0) -> (3,3) turned");
    CHECK(p.slot[0 * 8 + 0].home == 3 * 8 + 3, "(3,3) -> (0,0)");
    CHECK(p.slot[0 * 8 + 4].home == 4 && !p.slot[4].orient, "the other half stays");
    /* a half that wraps round longitude 0: half 6 is segments 6,7,0,1 */
    bt_init(&p, 4, 8);
    bt_apply(&p, BT_FLIP(6));
    CHECK(p.slot[3 * 8 + 1].home == 6 && p.slot[3 * 8 + 6].home == 1, "half 6: (0,6)<->(3,1)");
    CHECK(p.slot[3 * 8 + 0].home == 7 && p.slot[3 * 8 + 7].home == 0, "half 6: (0,7)<->(3,0)");
    CHECK(p.slot[2].home == 2 && p.slot[5].home == 5, "half 6 leaves segments 2..5");
    k = bt_moved_slots(&p, BT_FLIP(6), seen);
    CHECK(k == 16, "flip moves 16 slots of 4x8");
    k = bt_moved_slots(&p, BT_TWIST(2, 1), seen);
    CHECK(k == 8 && seen[0] == 16 && seen[7] == 23, "twist moves its band's slots");
    /* twist east: (1,0) -> (1,1) */
    bt_init(&p, 4, 8);
    bt_apply(&p, BT_TWIST(1, 0));
    CHECK(p.slot[1 * 8 + 1].home == 8 && p.slot[1 * 8 + 0].home == 15, "east twist moves pieces east");
}

static void test_scramble_solvable(void)
{
    int z, t;
    for (z = 0; z < NSIZES; z++)
        for (t = 0; t < 25; t++) {
            BtPuzzle p;
            int i, n;
            bt_init(&p, sizes[z][0], sizes[z][1]);
            bt_scramble(&p, (uint32_t)(t * 7919 + z), 5 + t * 8);
            CHECK(p.nscramble == 5 + t * 8 && p.moves == 0, "scramble length recorded");
            for (i = 1; i < p.nscramble; i++)
                CHECK(p.hist[i] != bt_inverse(p.hist[i - 1]), "scramble never undoes its last move");
            /* the player plays a few moves, then everything is replayed backwards */
            for (i = 0; i < t; i++) bt_do(&p, BT_TWIST(i % p.bands, i & 1));
            for (n = p.nhist - 1; n >= 0; n--) bt_apply(&p, bt_inverse(p.hist[n]));
            CHECK(bt_identity(&p) && bt_solved(&p), "%dx%d seed %d: replaying the inverses solves it", p.bands, p.segs, t);
        }
}

static void test_scramble_deterministic(void)
{
    BtPuzzle a, b;
    bt_init(&a, 6, 12);
    bt_init(&b, 6, 12);
    bt_scramble(&a, 1234, 50);
    bt_scramble(&b, 1234, 50);
    CHECK(same(&a, &b) && bt_hash(&a) == bt_hash(&b), "same seed, same scramble");
    bt_scramble(&b, 1235, 50);
    CHECK(!same(&a, &b), "another seed, another scramble");
    CHECK(!bt_solved(&a), "a 50 move scramble isn't solved");
}

static void test_undo(void)
{
    BtPuzzle p, ref;
    int i;
    bt_init(&p, 4, 8);
    bt_scramble(&p, 42, 20);
    ref = p;
    CHECK(bt_undo(&p) == -1 && same(&p, &ref), "nothing to undo after a scramble (it isn't the player's)");
    bt_do(&p, BT_TWIST(0, 0));
    bt_do(&p, BT_FLIP(5));
    bt_do(&p, BT_TWIST(3, 1));
    CHECK(p.moves == 3 && bt_last_move(&p) == BT_TWIST(3, 1), "three moves");
    CHECK(bt_undo(&p) == BT_TWIST(3, 1), "undo gives back the last move");
    CHECK(bt_undo(&p) == BT_FLIP(5), "then the one before");
    CHECK(bt_undo(&p) == BT_TWIST(0, 0), "then the first");
    CHECK(bt_undo(&p) == -1, "then nothing");
    CHECK(same(&p, &ref) && p.moves == 0, "back to the scramble");
    CHECK(!bt_do(&p, BT_TWIST(9, 0)) && p.moves == 0, "illegal move refused");
    /* a full history drops its oldest move */
    for (i = 0; i < BT_HIST + 10; i++) bt_do(&p, BT_TWIST(i % 4, 0));
    CHECK(p.nhist == BT_HIST, "history stays at %d", BT_HIST);
}

static void test_solved(void)
{
    BtPuzzle p;
    int b, s, i;
    bt_init(&p, 4, 8);
    for (b = 0; b < 4; b++) bt_apply(&p, BT_TWIST(b, 0));
    CHECK(!bt_identity(&p) && bt_solved(&p), "the whole ball spun round is solved");
    bt_apply(&p, BT_TWIST(2, 0));
    CHECK(!bt_solved(&p), "one band off isn't");
    bt_init(&p, 4, 8);
    bt_apply(&p, BT_FLIP(0));
    bt_apply(&p, BT_FLIP(4));
    CHECK(!bt_solved(&p), "the whole ball upside down isn't solved");

    /* a checkerboard: tiles of one colour look the same either way up */
    bt_init(&p, 4, 8);
    for (b = 0; b < 4; b++)
        for (s = 0; s < 8; s++) bt_set_sig(&p, b * 8 + s, (b + s) & 1, (b + s) & 1);
    bt_apply(&p, BT_TWIST(1, 0));
    CHECK(!bt_solved(&p), "checker: one band off by one isn't solved");
    bt_apply(&p, BT_TWIST(1, 0));
    CHECK(bt_solved(&p), "checker: one band off by two is");
    bt_apply(&p, BT_FLIP(2));
    bt_apply(&p, BT_FLIP(5));
    CHECK(bt_solved(&p), "checker: flips of a 4x8 checker keep the pattern");
    bt_init(&p, 4, 8);
    for (i = 0; i < 32; i++) bt_set_sig(&p, i, 7, 7);
    bt_scramble(&p, 3, 30);
    CHECK(bt_solved(&p), "a plain ball is always solved");
}

static void test_vector(void)
{
    uint32_t h = bt_vector_hash();
    CHECK(h == bt_vector_hash(), "vector hash is repeatable");
    CHECK(h == BT_VECTOR_HASH, "vector hash %08lx matches tests/vectors.h %08lx", (unsigned long)h,
          (unsigned long)BT_VECTOR_HASH);
}

/* --hash B S seed len [T<b>+ T<b>- F<k> U ...]: the state the game's
 * scramble and move hooks should reach (tests/e2e_devbench.py asks) */
static int hash_cli(int argc, char **argv)
{
    BtPuzzle p;
    int i;
    if (argc < 6 || !bt_init(&p, atoi(argv[2]), atoi(argv[3]))) return 2;
    bt_scramble(&p, (uint32_t)strtoul(argv[4], 0, 10), atoi(argv[5]));
    for (i = 6; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == 'T') bt_do(&p, BT_TWIST(atoi(a + 1), strchr(a, '-') != 0));
        else if (a[0] == 'F') bt_do(&p, BT_FLIP(atoi(a + 1)));
        else if (a[0] == 'U') bt_undo(&p);
    }
    printf("%08lx %d\n", (unsigned long)bt_hash(&p), bt_solved(&p));
    return 0;
}

/* --undo B S seed len: the moves that solve that scramble, as the move hook takes them */
static int undo_cli(int argc, char **argv)
{
    BtPuzzle p;
    int i;
    if (argc < 6 || !bt_init(&p, atoi(argv[2]), atoi(argv[3]))) return 2;
    bt_scramble(&p, (uint32_t)strtoul(argv[4], 0, 10), atoi(argv[5]));
    for (i = p.nhist - 1; i >= 0; i--) {
        int m = bt_inverse(p.hist[i]);
        if (BT_IS_FLIP(m)) printf("F%d\n", BT_HALF(m));
        else printf("T%d%c\n", BT_BAND(m), BT_WEST(m) ? '-' : '+');
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--hash") == 0) return hash_cli(argc, argv);
    if (argc > 1 && strcmp(argv[1], "--undo") == 0) return undo_cli(argc, argv);
    if (argc > 1 && strcmp(argv[1], "--vectors") == 0) {
        printf("/* made by make vectors: bt_vector_hash() on the host; the Amiga self-test must match */\n");
        printf("#define BT_VECTOR_HASH 0x%08lxUL\n", (unsigned long)bt_vector_hash());
        return 0;
    }
    test_sizes();
    test_cycles();
    test_inverses();
    test_permutation();
    test_scramble_solvable();
    test_scramble_deterministic();
    test_undo();
    test_solved();
    test_vector();
    printf("%d passed, %d failed (vector hash %08lx)\n", passes, fails, (unsigned long)bt_vector_hash());
    return fails ? 1 : 0;
}
