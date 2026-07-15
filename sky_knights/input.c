/*
 * SKY KNIGHTS - Input: Keyboard (P1) + Joystick port 2 (P2)
 */
#include <hardware/custom.h>
#include <hardware/cia.h>
#include "input.h"

extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

static UWORD key_state = 0;
static UWORD sys_state = 0;

void input_key_down(UWORD code)
{
    switch (code) {
        case 0x4F: key_state |= INP_LEFT;   break;  /* cursor left */
        case 0x4E: key_state |= INP_RIGHT;  break;  /* cursor right */
        case 0x4C: key_state |= INP_FLAP;   break;  /* cursor up */
        case 0x20: key_state |= INP_LEFT;   break;  /* A */
        case 0x22: key_state |= INP_RIGHT;  break;  /* D */
        case 0x11: key_state |= INP_FLAP;   break;  /* W */
        case 0x40: key_state |= INP_FLAP;   break;  /* space */
        case 0x45: sys_state |= INP_ESC;    break;  /* escape */
        case 0x50: sys_state |= INP_START1; break;  /* F1 */
        case 0x51: sys_state |= INP_START2; break;  /* F2 */
        case 0x01: sys_state |= INP_START1; break;  /* 1 */
        case 0x02: sys_state |= INP_START2; break;  /* 2 */
    }
}

void input_key_up(UWORD code)
{
    switch (code) {
        case 0x4F: key_state &= ~INP_LEFT;   break;
        case 0x4E: key_state &= ~INP_RIGHT;  break;
        case 0x4C: key_state &= ~INP_FLAP;   break;
        case 0x20: key_state &= ~INP_LEFT;   break;
        case 0x22: key_state &= ~INP_RIGHT;  break;
        case 0x11: key_state &= ~INP_FLAP;   break;
        case 0x40: key_state &= ~INP_FLAP;   break;
        case 0x45: sys_state &= ~INP_ESC;    break;
        case 0x50: sys_state &= ~INP_START1; break;
        case 0x51: sys_state &= ~INP_START2; break;
        case 0x01: sys_state &= ~INP_START1; break;
        case 0x02: sys_state &= ~INP_START2; break;
    }
}

void input_reset(void)
{
    key_state = 0;
    sys_state = 0;
}

void input_read(InputState *input)
{
    UWORD joy;

    /* Player 1: keyboard */
    input->p1 = key_state & (INP_LEFT | INP_RIGHT | INP_FLAP);

    /* System keys */
    input->sys = sys_state;

    /* Player 2: Joystick port 2 (JOY1DAT).
     * Amiga joystick decoding uses direct bit tests:
     *   RIGHT = bit 1
     *   LEFT  = bit 9
     *   UP    = bit 9 XOR bit 8   (also mapped to FLAP)
     * (DOWN  = bit 1 XOR bit 0, unused here)
     */
    input->p2 = 0;
    joy = custom.joy1dat;

    if (joy & 0x0002) input->p2 |= INP_RIGHT;         /* bit 1 */
    if (joy & 0x0200) input->p2 |= INP_LEFT;          /* bit 9 */
    if (((joy >> 9) ^ (joy >> 8)) & 1) input->p2 |= INP_FLAP; /* up = flap */

    /* Fire button: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        input->p2 |= INP_FLAP;  /* fire also flaps */
}
