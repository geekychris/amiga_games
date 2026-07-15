#include <exec/types.h>
#include <intuition/intuition.h>
#include <hardware/cia.h>
#include "input.h"

/* Amiga raw key codes */
#define RAW_ESC    0x45
#define RAW_SPACE  0x40
#define RAW_LEFT   0x4F
#define RAW_RIGHT  0x4E
#define RAW_A      0x20
#define RAW_D      0x22
#define RAW_LALT   0x64
#define RAW_RALT   0x65

static WORD last_mouse_x;
static BOOL prev_fire;      /* union of kb+mouse from prior frame */
static BOOL kb_fire;        /* current keyboard fire state */
static BOOL mouse_fire;     /* current mouse fire state */

void input_init(void)
{
    volatile UWORD *joy0dat = (volatile UWORD *)0xDFF00A;
    last_mouse_x = (WORD)(*joy0dat & 0xFF);
    prev_fire = FALSE;
    kb_fire = FALSE;
    mouse_fire = FALSE;
}

void input_read(InputState *input, struct IntuiMessage *imsg)
{
    if (!imsg) return;

    UWORD code = imsg->Code;
    BOOL up = (code & 0x80) ? TRUE : FALSE;
    code &= 0x7F;

    switch (code) {
        case RAW_LEFT:
        case RAW_A:
            input->left = !up;
            break;
        case RAW_RIGHT:
        case RAW_D:
            input->right = !up;
            break;
        case RAW_SPACE:
        case RAW_LALT:
        case RAW_RALT:
            kb_fire = !up;
            break;
        case RAW_ESC:
            if (!up) input->quit = TRUE;
            break;
    }
}

void input_read_mouse(InputState *input)
{
    volatile UWORD *joy0dat = (volatile UWORD *)0xDFF00A;
    volatile UBYTE *ciaa_pra = (volatile UBYTE *)0xBFE001;

    /* Mouse X delta (bits 7-0 of JOY0DAT) */
    WORD mouse_x = (WORD)(*joy0dat & 0xFF);
    input->mouse_dx = (BYTE)(mouse_x - last_mouse_x);
    last_mouse_x = mouse_x;

    /* Left mouse button = CIA-A PRA bit 6, active low.
     * Track mouse state independently so mouse release clears it even when
     * a keyboard fire key is still held. */
    mouse_fire = (*ciaa_pra & (1 << 6)) ? FALSE : TRUE;

    /* Combined fire state = OR of keyboard and mouse sources */
    BOOL any_fire = (kb_fire || mouse_fire);
    input->fire = any_fire;
    input->fire_pressed = (any_fire && !prev_fire);
    prev_fire = any_fire;
}
