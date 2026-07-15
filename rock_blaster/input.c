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

    /* Joystick decoding for digital joystick:
     * JOY1DAT low byte = X counter, high byte = Y counter.
     * For a digital joystick (as emulated by FS-UAE):
     *   Right: bit1=1, bit0=0  (low byte = 0x02)
     *   Left:  bit1=0, bit0=1  (low byte = 0x01)
     *   Down:  bit9=1, bit8=0  (high byte = 0x02)
     *   Up:    bit9=0, bit8=1  (high byte = 0x01)
     */
    {
        UWORD h = joy & 3;        /* horizontal bits */
        UWORD v = (joy >> 8) & 3; /* vertical bits */

        if (h == 2) result |= INPUT_RIGHT;
        if (h == 1) result |= INPUT_LEFT;
        if (v == 1) result |= INPUT_UP;
        /* v == 2 would be down, not used in this game */
    }

    /* Fire button: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        result |= INPUT_FIRE;

    return result;
}
