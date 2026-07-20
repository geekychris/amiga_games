/*
 * input.c - Keyboard and joystick input for StakAttack
 * Simple IDCMP + joystick reading.
 */

#include <exec/types.h>
#include <hardware/cia.h>
#include <hardware/custom.h>

#include "input.h"

extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

static UWORD key_held = 0;
static UWORD prev_input = 0;

static UWORD keycode_to_bit(UBYTE raw)
{
    switch (raw) {
    case 0x4F: return INPUT_LEFT;    /* Left arrow */
    case 0x20: return INPUT_LEFT;    /* A */
    case 0x4E: return INPUT_RIGHT;   /* Right arrow */
    case 0x22: return INPUT_RIGHT;   /* D */
    case 0x4D: return INPUT_DOWN;    /* Down arrow */
    case 0x21: return INPUT_DOWN;    /* S */
    case 0x4C: return INPUT_UP;      /* Up arrow */
    case 0x11: return INPUT_UP;      /* W */
    case 0x40: return INPUT_FIRE;    /* Space */
    case 0x64: return INPUT_START;   /* Left Alt */
    case 0x65: return INPUT_START;   /* Right Alt */
    case 0x44: return INPUT_START;   /* Return/Enter */
    case 0x45: return INPUT_ESC;     /* ESC */
    case 0x19: return INPUT_PAUSE;   /* P */
    default:   return 0;
    }
}

void input_init(void) { /* nothing needed for IDCMP */ }
void input_cleanup(void) { /* nothing needed for IDCMP */ }

void input_handle_key(UWORD code, UWORD qualifier)
{
    UBYTE raw = (UBYTE)(code & 0x7F);
    BOOL released = (code & 0x80) != 0;
    UWORD bit = keycode_to_bit(raw);
    (void)qualifier;
    if (bit) {
        if (released) key_held &= ~bit;
        else key_held |= bit;
    }
}

UWORD input_read(void)
{
    UWORD result = key_held;
    UWORD joy;

    /* Read joystick port 2 (JOY1DAT) — digital decoding:
     *   Right: low byte = 0x02   Left: low byte = 0x01
     *   Down:  high byte = 0x02  Up:   high byte = 0x01
     */
    joy = custom.joy1dat;
    {
        UWORD h = joy & 3;
        UWORD v = (joy >> 8) & 3;
        if (h == 2) result |= INPUT_RIGHT;
        if (h == 1) result |= INPUT_LEFT;
        if (v == 2) result |= INPUT_DOWN;
        if (v == 1) result |= INPUT_UP;
    }

    /* Fire button */
    if (!(ciaa.ciapra & 0x80))
        result |= INPUT_FIRE | INPUT_START;

    return result;
}

UWORD input_edge(UWORD current)
{
    UWORD edges = current & ~prev_input;
    prev_input = current;
    return edges;
}
