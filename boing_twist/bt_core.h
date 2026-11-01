// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * bt_core.h - Boing Twist's puzzle, with no Amiga in it (host-tested).
 *
 * The ball is cut into B latitude bands (0 = the north cap) by S longitude
 * segments (0 starts at longitude 0 and they go east). Slot (b, s) holds a
 * piece: the tile of the picture that belongs at home (hb, hs), the right
 * way up (orient 0) or turned half round (orient 1).
 *
 * Two kinds of move:
 *   TWIST band b by one segment east (+) or west (-): the band spins about
 *         the ball's vertical axis. S twists of a band are no move at all.
 *   FLIP  half k: the S/2 segments k .. k+S/2-1 (a hemisphere, cut along
 *         two meridians) turn 180 degrees about the horizontal axis through
 *         the middle of that half. A piece in band b goes to band B-1-b,
 *         segment s to 2k+S/2-1-s, and ends up upside down (orient ^= 1).
 *         Two flips of the same half are no move.
 *
 * Every scramble is a sequence of these moves from the solved ball, so it
 * can always be undone. The ball is solved when every slot shows the
 * picture that belongs there, allowing for the whole ball having been spun
 * round (the same twist on every band): pieces whose tiles look identical
 * (a checkerboard) count as the same, through their signatures.
 */
#ifndef BT_CORE_H
#define BT_CORE_H

#include <stdint.h>

#define BT_MAX_B   8
#define BT_MAX_S   16
#define BT_MAX_T   (BT_MAX_B * BT_MAX_S)
#define BT_HIST    1024

/* a move: bit 8 set = flip of half (m & 0xFF); else twist of band
 * (m >> 1 & 0x7F), west if bit 0 */
#define BT_TWIST(b, west) ((int)(((b) << 1) | ((west) ? 1 : 0)))
#define BT_FLIP(k)        ((int)(0x100 | (k)))
#define BT_IS_FLIP(m)     (((m) & 0x100) != 0)
#define BT_BAND(m)        (((m) >> 1) & 0x7F)
#define BT_WEST(m)        ((m) & 1)
#define BT_HALF(m)        ((m) & 0xFF)

typedef struct {
    uint8_t home;          /* hb * S + hs */
    uint8_t orient;        /* 0 upright, 1 turned half round */
} BtPiece;

typedef struct {
    int bands, segs;
    BtPiece slot[BT_MAX_T];
    uint32_t sig[BT_MAX_T][2];   /* what home tile h looks like upright / turned */
    int16_t hist[BT_HIST];       /* the moves from the solved ball: scramble, then the player's */
    int nhist, nscramble;
    int moves;                   /* the player's moves (an undo takes one back) */
} BtPuzzle;

/* a solved ball of bands x segs (bands 2..8, segs 4..16 and a multiple of 4);
 * every tile looks different until bt_set_sig says otherwise. 0: bad size */
int  bt_init(BtPuzzle *p, int bands, int segs);
void bt_reset(BtPuzzle *p);                       /* solved again, same size and signatures */
void bt_set_sig(BtPuzzle *p, int home, uint32_t upright, uint32_t turned);
void bt_unique_sig(BtPuzzle *p);

int  bt_valid(const BtPuzzle *p, int m);          /* a legal move for this size? */
int  bt_inverse(int m);
void bt_apply(BtPuzzle *p, int m);                /* just the state (no history) */
int  bt_do(BtPuzzle *p, int m);                   /* a player's move: 0 if illegal */
int  bt_undo(BtPuzzle *p);                        /* the move undone (to animate), -1 none */
int  bt_last_move(const BtPuzzle *p);             /* the player's last move, -1 none */

/* which pieces a move carries: out[] gets slot numbers, returns how many */
int  bt_moved_slots(const BtPuzzle *p, int m, int *out);

/* n random moves from the solved ball (never one that undoes the one before) */
void bt_scramble(BtPuzzle *p, uint32_t seed, int n);
int  bt_random_move(const BtPuzzle *p, uint32_t *rng);

int      bt_solved(const BtPuzzle *p);
int      bt_identity(const BtPuzzle *p);          /* every piece home and upright */
uint32_t bt_hash(const BtPuzzle *p);
uint32_t bt_rng(uint32_t *s);                     /* xorshift32 */

/* the long deterministic run the host tests and the Amiga self-test share */
uint32_t bt_vector_hash(void);

#endif
