// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Orb Hunter - Input: Joystick port 2 + keyboard
 */
#include <hardware/custom.h>
#include <hardware/cia.h>
#include "input.h"

/* Hardware registers */
extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

/* Per-physical-key state, so releasing one alternate for an action
   (e.g. A vs. cursor-left, alt vs. space) doesn't clear the action while
   the other alternate is still held. */
#define KEY_MAX 128
static UBYTE key_down[KEY_MAX];

/* Map raw code -> action bit, or 0 for unmapped. */
static UWORD code_to_action(UWORD code)
{
    switch (code) {
        case 0x4F: return INPUT_LEFT;   /* cursor left */
        case 0x4E: return INPUT_RIGHT;  /* cursor right */
        case 0x4C: return INPUT_UP;     /* cursor up */
        case 0x4D: return INPUT_DOWN;   /* cursor down */
        case 0x20: return INPUT_LEFT;   /* A */
        case 0x22: return INPUT_RIGHT;  /* D */
        case 0x11: return INPUT_UP;     /* W */
        case 0x31: return INPUT_DOWN;   /* Z, reused for down */
        case 0x60: return INPUT_FIRE;   /* left alt */
        case 0x64: return INPUT_FIRE;   /* right alt */
        case 0x40: return INPUT_FIRE;   /* space */
        case 0x45: return INPUT_ESC;    /* escape */
        case 0x44: return INPUT_START;  /* return */
    }
    return 0;
}

void input_key_down(UWORD code)
{
    if (code < KEY_MAX) key_down[code] = 1;
}

void input_key_up(UWORD code)
{
    if (code < KEY_MAX) key_down[code] = 0;
}

void input_reset(void)
{
    WORD i;
    for (i = 0; i < KEY_MAX; i++) key_down[i] = 0;
}

/* Aggregate per-key state into action bits. */
static UWORD keyboard_state(void)
{
    UWORD state = 0;
    WORD i;
    for (i = 0; i < KEY_MAX; i++) {
        if (key_down[i]) state |= code_to_action((UWORD)i);
    }
    return state;
}

UWORD input_read(void)
{
    UWORD result = keyboard_state();
    UWORD joy;

    /* Read joystick port 2 (JOY1DAT register) */
    joy = custom.joy1dat;

    /* Joystick: the standard JOY1DAT decoding (Hardware Reference Manual):
     *   right = bit 1, left = bit 9, down = bit 1 ^ bit 0, up = bit 9 ^ bit 8
     * fix: it compared the low/high bit pairs with 2 and 1, but right alone
     * reads 3 (so did left), down alone read as LEFT and left+up as DOWN */
    if (joy & 0x0002) result |= INPUT_RIGHT;
    if (joy & 0x0200) result |= INPUT_LEFT;
    if ((joy ^ (joy >> 1)) & 0x0001) result |= INPUT_DOWN;
    if ((joy ^ (joy >> 1)) & 0x0100) result |= INPUT_UP;

    /* Fire button: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        result |= INPUT_FIRE;

    return result;
}
