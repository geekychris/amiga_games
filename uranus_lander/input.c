/*
 * Uranus Lander - Input: Joystick port 2 + keyboard
 *
 * We track each physical key independently and OR their contributions
 * into the action bits. This means holding one alias (e.g. cursor-left)
 * while releasing another (e.g. A) leaves INPUT_LEFT asserted, and
 * vice versa — no shared-bit stomping on release.
 */
#include <hardware/custom.h>
#include <hardware/cia.h>
#include "input.h"

extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

/* Bit set indexed by rawkey code (0x00 - 0x7F). Only the keys we care
 * about are ever set, but sizing at 128 keeps indexing trivial. */
#define KEY_BITMAP_WORDS  (128 / 16)
static UWORD key_bits[KEY_BITMAP_WORDS];

/* Map a rawkey code to the action bit it contributes (0 = no mapping). */
static UWORD key_to_action(UWORD code)
{
    switch (code) {
        case 0x4F: return INPUT_LEFT;    /* cursor left */
        case 0x4E: return INPUT_RIGHT;   /* cursor right */
        case 0x4C: return INPUT_UP;      /* cursor up */
        case 0x4D: return INPUT_DOWN;    /* cursor down */
        case 0x40: return INPUT_THRUST;  /* space */
        case 0x45: return INPUT_ESC;     /* escape */
        /* WASD alternatives */
        case 0x20: return INPUT_LEFT;    /* A */
        case 0x22: return INPUT_RIGHT;   /* D */
        case 0x11: return INPUT_THRUST;  /* W */
        case 0x21: return INPUT_DOWN;    /* S (for menus) */
        case 0x37: return INPUT_MUSIC;   /* M = toggle music */
        default:   return 0;
    }
}

static void key_set(UWORD code, WORD down)
{
    if (code >= 128) return;
    if (down)
        key_bits[code >> 4] |=  (UWORD)(1 << (code & 15));
    else
        key_bits[code >> 4] &= (UWORD)~(1 << (code & 15));
}

static WORD key_is_down(UWORD code)
{
    if (code >= 128) return 0;
    return (key_bits[code >> 4] & (UWORD)(1 << (code & 15))) ? 1 : 0;
}

/* Derive the composite action bitmask from per-key state. */
static UWORD derive_actions(void)
{
    UWORD result = 0;
    /* Cursor keys */
    if (key_is_down(0x4F)) result |= INPUT_LEFT;
    if (key_is_down(0x4E)) result |= INPUT_RIGHT;
    if (key_is_down(0x4C)) result |= INPUT_UP;
    if (key_is_down(0x4D)) result |= INPUT_DOWN;
    /* Space / escape */
    if (key_is_down(0x40)) result |= INPUT_THRUST;
    if (key_is_down(0x45)) result |= INPUT_ESC;
    /* WASD */
    if (key_is_down(0x20)) result |= INPUT_LEFT;
    if (key_is_down(0x22)) result |= INPUT_RIGHT;
    if (key_is_down(0x11)) result |= INPUT_THRUST;
    if (key_is_down(0x21)) result |= INPUT_DOWN;
    /* Music toggle */
    if (key_is_down(0x37)) result |= INPUT_MUSIC;
    return result;
}

void input_key_down(UWORD code)
{
    /* Ignore keys we don't map so we don't waste bitmap space
     * on unrelated presses (though we track them all the same). */
    if (key_to_action(code) == 0) return;
    key_set(code, 1);
}

void input_key_up(UWORD code)
{
    if (key_to_action(code) == 0) return;
    key_set(code, 0);
}

void input_reset(void)
{
    WORD i;
    for (i = 0; i < KEY_BITMAP_WORDS; i++) key_bits[i] = 0;
}

UWORD input_read(void)
{
    UWORD result = derive_actions();
    UWORD joy;

    /* Read joystick port 2 (JOY1DAT register) */
    joy = custom.joy1dat;

    {
        UWORD h = joy & 3;
        UWORD v = (joy >> 8) & 3;

        if (h == 2) result |= INPUT_RIGHT;
        if (h == 1) result |= INPUT_LEFT;
        if (v == 1) result |= INPUT_UP;
        if (v == 2) result |= INPUT_DOWN;
    }

    /* Fire button: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        result |= INPUT_THRUST;

    return result;
}
