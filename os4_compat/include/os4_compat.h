// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * os4_compat.h - classic Amiga games on AmigaOS 4 (PowerPC), unchanged
 * where possible.  os4c.mk puts this header in front of every game source
 * (-include), so it is seen before the game's own #includes.
 *
 * It includes the system headers first, so the game's later #includes of
 * them do nothing, and then replaces a few of the OS4 inline macros with
 * the layer's functions (os4c_screen.c):
 *
 * - The game's custom screen is not opened.  OpenScreenTags() returns a
 *   stand-in Screen whose bitmaps are planar bitmaps in RAM (so drawing
 *   with graphics.library and poking Planes[] both work; OS4C_CHUNKY
 *   makes them 8-bit chunky), shown scaled up in a window on the
 *   Workbench.  A window the game opens on that screen
 *   is that Workbench window.  The palette calls (SetRGB4, SetRGB32,
 *   LoadRGB4, LoadRGB32) set the picture's colours; ChangeScreenBuffer,
 *   ScrollVPort and the double-buffer messages work as on a real screen.
 * - A user copper list (CINIT, CWAIT, CMOVE, CEND on vp->UCopIns) is
 *   kept by the layer, and its colour moves are applied line by line
 *   when the picture is made, as the copper would.
 * - WaitTOF() waits for the next tick of a 50 Hz clock on timer.device
 *   (on the QEMU guest the real WaitTOF returns ~345 times a second), and
 *   Delay() waits ticks of the same clock (Delay is unsafe here, see
 *   CLAUDE.md).
 * - `custom` and `ciaa` are structures in RAM.  The joystick reads as
 *   centred with no button pressed, and joy0dat counts mouse movement in
 *   the window, as on the real mouse port.  The audio registers drive a
 *   Paula emulation mixed through AHI (os4c_paula.c): every write to
 *   dmacon goes, in order, into a queue the mixer works through.
 * - Open() looks for "DH2:Dev/x" (the AmiKit folder the games load their
 *   music from) as PROGDIR:x first (os4c_dos.c).
 * - ptplayer's C interface (mt_init, mt_playfx, ...) is a C ProTracker
 *   player on that Paula (ptplayer.c, from the Atari ST ports).
 */
#ifndef OS4_COMPAT_H
#define OS4_COMPAT_H

#ifdef __amigaos4__

/* the blitter's minterm names (ABC, ABNC, ...) the games use */
#define __USE_CLASSIC_MINTERM__

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <graphics/modeid.h>
#include <graphics/copper.h>
#include <graphics/minterm.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <hardware/blit.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the ptplayer interface (the games' ptplayer.h is for the 68k assembly,
 * with register arguments; its include guard keeps it out) */
#include "os4c_ptplayer.h"

/* stand-ins, os4c_screen.c */
struct Screen *oc_OpenScreenTagList(const struct NewScreen *ns, const struct TagItem *tags);
struct Screen *oc_OpenScreenTags(const struct NewScreen *ns, ...);
struct Screen *oc_OpenScreen(const struct NewScreen *ns);
BOOL oc_CloseScreen(struct Screen *s);
struct Window *oc_OpenWindowTagList(const struct NewWindow *nw, const struct TagItem *tags);
struct Window *oc_OpenWindowTags(const struct NewWindow *nw, ...);
struct Window *oc_OpenWindow(const struct NewWindow *nw);
void oc_CloseWindow(struct Window *w);
struct ScreenBuffer *oc_AllocScreenBuffer(struct Screen *s, struct BitMap *bm, ULONG flags);
void oc_FreeScreenBuffer(struct Screen *s, struct ScreenBuffer *sb);
ULONG oc_ChangeScreenBuffer(struct Screen *s, struct ScreenBuffer *sb);
void oc_SetRGB4(struct ViewPort *vp, LONG n, ULONG r, ULONG g, ULONG b);
void oc_SetRGB32(struct ViewPort *vp, ULONG n, ULONG r, ULONG g, ULONG b);
void oc_LoadRGB4(struct ViewPort *vp, const UWORD *colors, LONG count);
void oc_LoadRGB32(struct ViewPort *vp, const ULONG *table);
ULONG oc_GetRGB4(struct ColorMap *cm, LONG n);
void oc_ScrollVPort(struct ViewPort *vp);
void oc_ScreenToFront(struct Screen *s);
void oc_ScreenToBack(struct Screen *s);
void oc_MakeScreen(struct Screen *s);
void oc_WaitTOF(void);
void oc_WaitBOVP(struct ViewPort *vp);
void oc_Delay(LONG ticks);
void oc_InitRastPort(struct RastPort *rp);
struct Message *oc_GetMsg(struct MsgPort *port);
BOOL oc_ModifyIDCMP(struct Window *w, ULONG flags);
void oc_ActivateWindow(struct Window *w);
LONG oc_Seek(BPTR file, LONG pos, LONG mode);
BPTR oc_Open(CONST_STRPTR name, LONG mode);
/* show the stand-in screen now (for a game that draws without waiting) */
void oc_present(void);

/* the register blocks, for code that uses their fixed addresses
 * (0xDFF000, 0xBFE001): another name for the same objects, so they don't
 * clash with the game's own `extern ... custom;` */
extern volatile struct Custom oc_hw_custom __asm__("custom");
extern volatile struct CIA oc_hw_ciaa __asm__("ciaa");
#define OC_CUSTOM (&oc_hw_custom)
#define OC_CIAA   (&oc_hw_ciaa)

/* user copper lists (os4c_screen.c): the colour moves are applied line by
 * line when the stand-in screen is shown */
void oc_CInit(struct UCopList *c, LONG n);
void oc_CMove(struct UCopList *c, volatile void *reg, LONG val);
void oc_CWait(struct UCopList *c, LONG v, LONG h);
void oc_FreeCopList(struct CopList *cl);

/* the Paula emulation, os4c_paula.c */
int oc_dmacon_slot(void);
void paula_dmacon(UWORD v);

#ifdef __cplusplus
}
#endif

#undef OpenScreenTagList
#undef OpenScreenTags
#undef OpenScreen
#undef CloseScreen
#undef OpenWindowTagList
#undef OpenWindowTags
#undef OpenWindow
#undef CloseWindow
#undef AllocScreenBuffer
#undef FreeScreenBuffer
#undef ChangeScreenBuffer
#undef SetRGB4
#undef SetRGB32
#undef LoadRGB4
#undef LoadRGB32
#undef GetRGB4
#undef ScrollVPort
#undef ScreenToFront
#undef ScreenToBack
#undef MakeScreen
#undef WaitTOF
#undef WaitBOVP
#undef Delay
#undef InitRastPort
#undef GetMsg
#undef ModifyIDCMP
#undef ActivateWindow
#undef CINIT
#undef CMOVE
#undef CWAIT
#undef CEND
#undef FreeCopList
#undef Seek
#undef Open

#define OpenScreenTagList(ns, tags)       oc_OpenScreenTagList((ns), (tags))
#define OpenScreenTags(...)               oc_OpenScreenTags(__VA_ARGS__)
#define OpenScreen(ns)                    oc_OpenScreen(ns)
#define CloseScreen(s)                    oc_CloseScreen(s)
#define OpenWindowTagList(nw, tags)       oc_OpenWindowTagList((nw), (tags))
#define OpenWindowTags(...)               oc_OpenWindowTags(__VA_ARGS__)
#define OpenWindow(nw)                    oc_OpenWindow(nw)
#define CloseWindow(w)                    oc_CloseWindow(w)
#define AllocScreenBuffer(s, bm, f)       oc_AllocScreenBuffer((s), (bm), (f))
#define FreeScreenBuffer(s, sb)           oc_FreeScreenBuffer((s), (sb))
#define ChangeScreenBuffer(s, sb)         oc_ChangeScreenBuffer((s), (sb))
#define SetRGB4(vp, n, r, g, b)           oc_SetRGB4((vp), (n), (r), (g), (b))
#define SetRGB32(vp, n, r, g, b)          oc_SetRGB32((vp), (n), (r), (g), (b))
#define LoadRGB4(vp, c, n)                oc_LoadRGB4((vp), (const UWORD *)(c), (n))
#define LoadRGB32(vp, t)                  oc_LoadRGB32((vp), (const ULONG *)(t))
#define GetRGB4(cm, n)                    oc_GetRGB4((cm), (n))
#define ScrollVPort(vp)                   oc_ScrollVPort(vp)
#define ScreenToFront(s)                  oc_ScreenToFront(s)
#define ScreenToBack(s)                   oc_ScreenToBack(s)
#define MakeScreen(s)                     oc_MakeScreen(s)
#define WaitTOF()                         oc_WaitTOF()
#define WaitBOVP(vp)                      oc_WaitBOVP(vp)
#define Delay(t)                          oc_Delay(t)
#define InitRastPort(rp)                  oc_InitRastPort(rp)
#define GetMsg(p)                         oc_GetMsg(p)
#define ModifyIDCMP(w, f)                 oc_ModifyIDCMP((w), (f))
#define ActivateWindow(w)                 oc_ActivateWindow(w)
/* dos.library's Seek is OBSOLETESeek on AmigaOS 4 */
#define Seek(f, p, m)                     oc_Seek((f), (p), (m))
/* "DH2:Dev/x" (the AmiKit folder) is looked for as PROGDIR:x first */
#define Open(n, m)                        oc_Open((CONST_STRPTR)(n), (m))

/* names from AmigaOS 4's headers the games use for their own things:
 * intuition/preferences.h's CUSTOM, graphics/rastport.h's LEVELS */
#undef CUSTOM
#undef LEVELS

/* a user copper list: waits and colour moves, kept by the layer */
#define CINIT(c, n)                       oc_CInit((c), (n))
#define CMOVE(c, a, b)                    oc_CMove((c), (volatile void *)&(a), (b))
#define CWAIT(c, a, b)                    oc_CWait((c), (a), (b))
#define CEND(c)                           oc_CWait((c), 10000, 255)
#define FreeCopList(cl)                   oc_FreeCopList(cl)

/* every `custom.dmacon = v` (or `p->dmacon = v`) becomes a write to the
 * next slot of the dmacon queue (hardware/custom.h, os4c_paula.c) */
#define dmacon dmacon_q[oc_dmacon_slot()]

/* 68k compiler keywords the games use */
#ifndef __chip
#define __chip
#endif

#endif /* __amigaos4__ */
#endif /* OS4_COMPAT_H */
