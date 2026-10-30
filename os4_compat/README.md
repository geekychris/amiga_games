# os4_compat

A shared layer that runs the classic Amiga games in this directory on
AmigaOS 4.1 (PowerPC), mostly without changes to their code. The games
draw with graphics.library on a custom screen, poke Paula's registers or
call ptplayer for sound, and read the joystick from the custom chips. On
AmigaOS 4 (no chipset, no chip RAM) the layer gives them a stand-in
for each of these.

Used by `uranus_lander`, `nova_defense`, `frank_the_frog`, `rock_blaster`,
`orbital_patrol`, `jump_quest`, `stakattack`, `dot_chase`, `lunar_rider`,
`pea_shooter_blast`, `canyon_rescue`, `void_trader`, `sky_knights`,
`orb_hunter`, `ace_pilot`, `bullion_dash`, `rj_birthday`, `miner_meteor`
(C++), `boing_ball`, `plasma` and `starfield`.

```sh
scripts/build-example-ppc.sh <game>      # <game>_os4 (make ARCH=ppc)
scripts/build-example-68k.sh <game>      # the 68k build, unchanged
```

A game's Makefile, under `ARCH=ppc`, sets `NAME`, `SRCS` (and `CXXSRCS`
for C++), optionally `EXTRA_CFLAGS`, and includes `os4c.mk`. Every game
source is compiled with `-include os4_compat.h`. The layer itself and the
bridge client are compiled without it. The 68k branch of each Makefile is
the game's original, and its binaries are byte-identical to the ones
built before the layer was added.

## What the game sees

| | |
|---|---|
| `OpenScreenTags` / `OpenScreenTagList` / `OpenScreen` | A stand-in `struct Screen` with planar bitmaps in RAM: graphics.library draws into them in software, and the game can write into `Planes[]` itself. The screen is shown in a window on the Workbench, scaled up by a whole number (2 when it fits; `OS4C_SCALE` env var 1..4). A screen wider than the display (for scrolling) shows its mode's standard PAL size, or its `SA_DClip`. `SA_BitMap`, `SA_Colors`, `SA_Colors32` work. |
| `OpenWindow*` on that screen | The Workbench window itself, with the game's IDCMP flags. Its close gadget reads as Esc. `screen->FirstWindow` is set. |
| `AllocScreenBuffer` / `ChangeScreenBuffer` | Double buffering. The new bitmap is shown at the next 50 Hz tick, and the safe and display messages are replied then, so games that pace themselves on those run at 50 Hz. |
| `SetRGB4`, `SetRGB32`, `LoadRGB4`, `LoadRGB32`, `GetRGB4` | The picture's palette. |
| `ScrollVPort`, `RasInfo->RxOffset/RyOffset` | Read each time the picture is shown. |
| `CINIT` / `CWAIT` / `CMOVE` / `CEND` on `vp->UCopIns` | A user copper list: the layer keeps the waits and colour moves and applies them line by line when it makes the picture (Boing Ball's sky and rainbow, Starfield's gradient). |
| `WaitTOF`, `WaitBOVP` | Wait for the next tick of a 50 Hz clock on timer.device (on QEMU the real `WaitTOF` returns ~345 times a second). A game without double buffering is shown here. |
| `Delay` | Ticks of the same clock (dos.library's `Delay` is unsafe here, see CLAUDE.md). |
| `custom`, `ciaa` (and `OC_CUSTOM` / `OC_CIAA` for code that used `0xDFF000` / `0xBFE001`) | Structures in RAM with the real register layout (`include/hardware/custom.h`). The joystick reads as centred with the fire button up; `joy0dat` counts mouse movement in the window as the mouse port would; `vhposr` changes, for random seeds. |
| `custom.aud[n]`, `custom.dmacon = v` | The Paula emulation (`os4c_paula.c`), mixed in a task of its own into 16-bit stereo and played through ahi.device. `os4_compat.h` turns each `x.dmacon = v` into a store into the next slot of a queue (`#define dmacon dmacon_q[oc_dmacon_slot()]`); taking the slot also notes each channel's `ac_ptr`/`ac_len`, which is what Paula latches when DMA starts. The mixer works through the queue in order before each 23 ms block, so stop/set/start sequences and one-shot samples followed by a loop behave as on the chip. Channels 0+3 left, 1+2 right, with a little crossfeed. The mixer's waits end on Ctrl-C from the exit code, so a game still ends if ahi.device holds its requests back (all its channels in use by other programs). |
| `mt_install_cia`, `mt_init`, `mt_playfx`, ... | `ptplayer.c` from the Atari ST ports (`atari_st_games/st_port`), a C ProTracker player with the C interface of Frank Wille's ptplayer, ticked by the mixer. The games' `ptplayer.h` (68k register arguments) is skipped through its include guard. |
| `Open("DH2:Dev/x", ...)` | The games load their music from the AmiKit folder they were written in. `PROGDIR:x` is tried first, and a file being written goes there; then the original name with the "insert volume" requester turned off. |
| `Seek` | dos.library's `Seek` is `OBSOLETESeek` on AmigaOS 4. |
| `SysBase`, `GfxBase`, `IntuitionBase` the game declares | Compiled with `__USE_BASETYPE__`, so the games' own `struct GfxBase *GfxBase` declarations match the SDK's. |
| `sprintf`, `memset`, ... | `<stdio.h>`, `<string.h>` and `<stdlib.h>` are included (amiga.lib used to provide them). |

AmigaOS 4 header names that collide with the games' own (`CUSTOM` from
intuition/preferences.h, `LEVELS` from graphics/rastport.h) are
`#undef`ed; the blitter's minterm names (`ABC`, `ABNC`, ...) are enabled.

## Files

| | |
|---|---|
| `include/os4_compat.h` | The forced include: system headers first, then the macros above |
| `include/hardware/custom.h` | The custom chip registers, plus the dmacon queue after them |
| `include/os4c_ptplayer.h` | ptplayer's C interface |
| `os4c_screen.c` | The stand-in screen, the window, the picture (planes to pens to the Workbench's pixel format, scaled, into a bitmap in RAM like the Workbench's, then blitted), double buffering, palette, copper lists, the 50 Hz clock, mouse, frame statistics |
| `os4c_paula.c`, `paula.h` | Paula on AHI, and the dmacon queue |
| `ptplayer.c` | The MOD player |
| `os4c_dos.c` | `Open` redirection, `Seek` |
| `os4c.mk` | Build rules |

## Showing the picture

Each frame the front bitmap's planes are turned into pens 8 pixels at a
time (a table spreads a plane byte into 8 bytes), the pens into the
Workbench's pixel format through the palette (32-bit A R G B, B G R A,
R G B A, or 16-bit R5G6B5 either way round), written at 2x into a bitmap
in RAM made like the window's, and blitted. That takes 1 to 3 ms a frame
for 320 x 256 at 2x under QEMU. Drawing into video memory directly is
slow under QEMU (see `amiga3d/os4_display.c`).

`OS4C_CHUNKY=1` in a game's Makefile makes the screen's bitmaps 8-bit
chunky ones instead. graphics.library drew into them no faster under QEMU
(Jump Quest, Boing Ball), so planar is the default: it keeps `Planes[]`
and the depth's semantics exact.

## Frame rate

Every 10 seconds the layer logs a line through the bridge (and sets the
global env var `OS4C_FPS`, for games without the bridge):

```
os4c: 49.1 fps, 1012 us a frame to show (x2), 12171 us of work,
      1.0 waits of 6921 us a frame, 1455 us late (3 missed); sound: 433 blocks, peak 6289
```

the frames shown a second, the cost of showing one, the game's own work
between waits, the time spent waiting for the clock, how late timer.device
woke it, and the sound blocks played (437 is real time) with their peak.
The numbers vary with the host's load: QEMU's single emulated CPU slows
when the Mac is busy.

## Not covered

Code that drives the chips itself (its own copper list in `cop1lc`, the
blitter registers, hardware sprites, the keyboard from the CIA) needs a C
path of its own: `starfield/main_os4.c` is the demo of `starfield.asm`
written against the layer. The joystick isn't read (the games all take
the keyboard too). Sound through audio.device (Boing Ball) uses AmigaOS
4's own audio.device.
