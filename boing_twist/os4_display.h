/*
 * os4_display.c: the game's picture on AmigaOS 4, in a window on the
 * Workbench or full screen on a screen of its own, scaled up by a whole
 * number and centred. From planet_chomp's, with the mouse added for
 * Boing Twist (mouse buttons and moves, a pointer on the full screen too).
 */
#ifndef OS4_DISPLAY_H
#define OS4_DISPLAY_H

#include <intuition/intuition.h>

/* w x h: the frame the game draws (A R G B, rows top to bottom).
 * scale: window size in frames (fullscreen picks the biggest that fits). */
int  disp_open(const char *title, int w, int h, int scale, int fullscreen);
void disp_close(void);
int  disp_toggle(void);                /* window <-> full screen; 0 if it failed */
int  disp_fullscreen(void);
struct Window *disp_window(void);      /* for its UserPort */
void disp_present(const void *argb);   /* the w x h frame */
void disp_mouse(int *x, int *y);       /* the pointer, in the frame's pixels */

/* IDCMP every game window asks for */
#define DISP_IDCMP (IDCMP_RAWKEY | IDCMP_CLOSEWINDOW | IDCMP_INACTIVEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE)
/* raw key codes that toggle full screen: F10 and F */
#define DISP_KEY_F10 0x59
#define DISP_KEY_F   0x23

#endif
