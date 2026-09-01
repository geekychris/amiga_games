// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins

/*
 * Rock Blaster - Input: Joystick port 2 + keyboard
 */
#include <hardware/custom.h>
#include <hardware/cia.h>
#include "input.h"

/* Hardware registers */
extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

/*
 * Track each physical key independently, so releasing one key doesn't
 * clear an action bit still required by another held key (e.g. A and Z
 * both map to INPUT_LEFT — releasing A must not cancel Z's INPUT_LEFT).
 */
#define NUM_MAPPED_KEYS 12
static UBYTE key_down[NUM_MAPPED_KEYS];  /* 1 = held, 0 = up */

/* Parallel table of raw key codes for each slot. */
static const UBYTE key_codes[NUM_MAPPED_KEYS] = {
    0x4F, /*  0: cursor left  -> LEFT  */
    0x4E, /*  1: cursor right -> RIGHT */
    0x4C, /*  2: cursor up    -> UP    */
    0x20, /*  3: A            -> LEFT  */
    0x22, /*  4: D            -> RIGHT */
    0x11, /*  5: W            -> UP    */
    0x31, /*  6: Z            -> LEFT  */
    0x33, /*  7: C            -> RIGHT */
    0x60, /*  8: left alt     -> FIRE  */
    0x64, /*  9: right alt    -> FIRE  */
    0x40, /* 10: space        -> FIRE  */
    0x45  /* 11: escape       -> ESC   */
};

/* Parallel table of INPUT_ action bits produced by each slot. */
static const UWORD key_actions[NUM_MAPPED_KEYS] = {
    INPUT_LEFT,  INPUT_RIGHT, INPUT_UP,
    INPUT_LEFT,  INPUT_RIGHT, INPUT_UP,
    INPUT_LEFT,  INPUT_RIGHT,
    INPUT_FIRE,  INPUT_FIRE,  INPUT_FIRE,
    INPUT_ESC
};

/* Derive the current keyboard action mask from all held keys. */
static UWORD derive_key_state(void)
{
    UWORD state = 0;
    WORD i;
    for (i = 0; i < NUM_MAPPED_KEYS; i++) {
        if (key_down[i])
            state |= key_actions[i];
    }
    return state;
}

void input_key_down(UWORD code)
{
    WORD i;
    for (i = 0; i < NUM_MAPPED_KEYS; i++) {
        if (key_codes[i] == (UBYTE)code) {
            key_down[i] = 1;
        }
    }
}

void input_key_up(UWORD code)
{
    WORD i;
    for (i = 0; i < NUM_MAPPED_KEYS; i++) {
        if (key_codes[i] == (UBYTE)code) {
            key_down[i] = 0;
        }
    }
}

void input_reset(void)
{
    WORD i;
    for (i = 0; i < NUM_MAPPED_KEYS; i++)
        key_down[i] = 0;
}

UWORD input_read(void)
{
    UWORD result = derive_key_state();
    UWORD joy;

    /* Read joystick port 2 (JOY1DAT register) */
    joy = custom.joy1dat;

    /* Joystick decoding using Amiga quadrature semantics.
     * JOY1DAT bits: 9 = Y2, 8 = Y1, 1 = X2, 0 = X1.
     *   Right = bit 1
     *   Left  = bit 1 XOR bit 0
     *   Down  = bit 9
     *   Up    = bit 9 XOR bit 8
     * Down is unused in this game.
     */
    {
        UWORD b0 = (joy >> 0) & 1;
        UWORD b1 = (joy >> 1) & 1;
        UWORD b8 = (joy >> 8) & 1;
        UWORD b9 = (joy >> 9) & 1;

        if (b1)         result |= INPUT_RIGHT;
        if (b1 ^ b0)    result |= INPUT_LEFT;
        if (b9 ^ b8)    result |= INPUT_UP;
        /* down (b9) omitted — not used in this game */
    }

    /* Fire button: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        result |= INPUT_FIRE;

    return result;
}
