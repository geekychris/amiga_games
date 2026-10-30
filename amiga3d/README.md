# amiga3d

A small shared engine for games that build, from the same source, for the
classic Amiga (68020 to 68060, OS 3.0+, a 32-colour PAL screen) and for
AmigaOS 4.1 on PowerPC, where the same games are drawn at twice the
resolution in full colour. Used by:

- [`stack_tower`](../stack_tower/README.md): the falling-block puzzle round a tower (3D models)
- [`lander_3d`](../lander_3d/README.md): landing on generated terrain (3D terrain, a model, the HUD)
- [`boing_pinball`](../boing_pinball/README.md): three pinball tables (a pre-drawn scrolling picture, 2D on top)
- [`scairy_fun`](../scairy_fun/README.md): the self-driving car game (its own road renderer and models, the engine's drawing, platform and sound)
- [`block_breaker_3d`](../block_breaker_3d/README.md): breakout down a tunnel (a fixed camera, bricks as visible faces only)
- [`harbour_crane`](../harbour_crane/README.md): a pendulum crane loading a rolling ship (three frames: world, ship, swinging load)
- [`ridge_runner`](../ridge_runner/README.md): hover-bike racing over generated terrain (a height-grid window, a track ribbon)
- [`tank_duel`](../tank_duel/README.md): a tank fight in a walled arena, from the turret (models, a radar)
- [`marble_tilt`](../marble_tilt/README.md): a tilting maze board (floor and walls drawn in runs)
- [`orbit_docking`](../orbit_docking/README.md): docking with a spinning station (a camera with roll, see below)
- [`drone_courier`](../drone_courier/README.md): deliveries across a city of towers (culling by grid cell, see below)
- [`canyon_rail`](../canyon_rail/README.md): a rail shooter (hits found with the engine's own projection)

Two additions are proposed for the engine from these, each so far in its
game's own `render.c`: **camera roll** (`cam_roll` in orbit_docking: a turn
of camera space's x and y about the line of sight, after `cam_xform`) and
**culling by grid cell** (`city` in drone_courier: visit only the grid
cells round the camera out to a range, and draw only those ahead of it).

A limit to keep in mind: the bridge takes 32 variables and 16 hooks a
client (`AB_MAX_VARS`, `AB_MAX_HOOKS` in `amiga-bridge/include/bridge_ipc.h`);
more are dropped without an error.

## What a game writes

Its pure simulation (no Amiga headers, so it is tested on the host too),
its drawing and its shell. The shell supplies what `a3d_plat.h` asks for:
the title, the bridge client's name, the music and voice files, a raw key
and joystick mapping, and `game_init`, `game_sound_init`, `game_input`,
`game_update`, `game_draw` and `game_after_frame`. The mains call them 25
times a second.

A game's Makefile sets `NAME`, `CORE` (the pure sources), `GAME` (the
rest), `TESTS` and `HEADERS`, then includes `a3d.mk`:

```sh
scripts/build-example-68k.sh <game>     # NAME and NAME_test (make ARCH=m68k)
scripts/build-example-ppc.sh <game>     # NAME_os4 (make ARCH=ppc)
make -C examples/<game> host-test       # the tests, natively
```

## The files

| | |
|---|---|
| `a3d.h`, `a3d_math.c` | 16-bit angles and 16.16 trig, `a3_isqrt`, `a3_hypot`, `a3_atan2`, a seeded random generator; the camera (`cam_view`, `cam_setup`, `cam_xform`, `cam_project`), near-plane and screen clipping, the span filler |
| `a3d_fix.h`, `a3d_fix.c` | 16.16 fixed point for simulations: `FMUL`, `FDIV`, `fsqrt`, `fhypot`, `fsin`, `fcos`, `fatan2`, `fasin`, `fexp_neg` |
| `a3d_draw.h`, `a3d_draw.c` | 2D (spans, rectangles, the blitter's fill, polygons, circles, text, big text, boxes), clip rows, the tall picture (`g_pic`), full-colour pictures (`g_image`, AmigaOS 4), the 3D draw list (depth buckets, far to near), models (boxes, pyramids, prisms, shadows) at any rotation (a game with its own defines `A3D_NO_MODELS`) |
| `a3d_font16.h`, `tools/make_font.py` | AmigaOS 4's text: DejaVu Sans Mono Bold in 16 x 16 cells (the topaz 8 cell at twice the resolution) |
| `tools/make_image.py` | a PNG made into a picture for `g_image_load` (`A3IM`: width, height, RGB) |
| `a3d_sound.h`, `a3d_sound.c` | effects synthesised at start-up (`a3_tone`, `a3_noise`) and voice clips |
| `a3d_plat.h` | the game / platform interface |
| `main_68k.c`, `ptplayer.asm` | the classic Amiga: a double-buffered 320 x 256 screen, the copper gradient (`plat_sky`), keyboard and joystick, ptplayer for the music and effects (never on the music's two channels) |
| `main_os4.c`, `os4_display.c`, `sound_ahi.c`, `modplay.c` | AmigaOS 4: a 640 x 512 window or screen drawn straight in the screen's pixel format and blitted, AHI and a module player, a fixed 40 ms frame schedule on timer.device |
| `test_host.c`, `test_amiga.c` | run a game's `run_tests(check)` on the host, or on the Amiga with the results over the bridge |
| `devbench_e2e.py` | the end-to-end tests' helper: devbench MCP calls, variables, hooks, injected keys, logs, screenshots (FS-UAE through the bridge, QEMU's monitor for AmigaOS 4) |

## AmigaOS 4: twice the resolution, full colour

The games keep their 320 x 256 coordinates and their 32 pens. On AmigaOS 4
(`A3_SUB` 2) the engine draws into a 640 x 512 frame. Rectangles and lines
are doubled, while polygons, circles and the 3D draw list are filled at
the finer resolution (`g_poly_sub` takes the finer coordinates directly).
Text uses the 16 x 16 font. The pens are looked up in the palette as the
pixels are written, and the sky pen is a smooth gradient rather than the
copper's 16 steps.

The frame is drawn straight in the screen's own pixel format (32-bit A R G
B, B G R A or R G B A, or 16-bit R5G6B5 either way round; a 16-bit store
writes two pixels), into a bitmap in ordinary memory, then blitted to the
window. Converting a full-colour frame to a 16-bit Workbench with
WritePixelArray took 20 to 30 ms of the 40 ms frame under QEMU. Drawing
into video memory is no better, because every store into it is slow
under QEMU. Fills go two pixels at a time through 8-byte floating-point
stores (`stfd`), since GCC splits a double store into integer ones.
Polygons fill each row in place, because a call per row costs too much
under emulation. A 64-bit divide in the projection is a slow library
call, so the extra precision comes from the remainder instead.

## Pictures

`g_image_load` reads a picture made by `tools/make_image.py`; `g_image_draw`
copies it into the frame, keeping a copy converted to the screen's format.
A 640 x 512 picture fills the screen. `g_image_row` copies part of one row
(a picture cut to a shape), and `g_image_wrap` draws a panorama that wraps
round, starting at any column, its top and bottom rows carried on above
and below it (a sky that turns with the camera). `g_fill_grad` shades rows
from one colour to another.

`make_image.py` scales and crops a PNG to the size asked for, and can
cross-fade a panorama's ends so they join (`--wrap`), keep a band of the
source's rows (`--rows`) and squash it (`--vscale`), fade its top rows to
a plain colour (`--fade-top`), lay a picture on a black background over it
(`--over`, a logo on a backglass: blending at run time costs too much
under emulation) and darken its bottom rows for text (`--shade-bottom`).
Each game's `make art` makes its pictures from its `assets/`. On the classic Amiga there are no
full-colour pictures: `g_image_load` returns NULL and the game draws its
32-colour look instead.

## The tall picture

A game with a fixed scene (a pinball table) draws it once with the 2D
calls into `g_pic_alloc(h)` (set `g_clip_y1` to `h - 1` first), then each
frame `g_pic_blit` copies a window of it into the canvas and the game
draws what moves on top. On the classic Amiga the picture is a five-plane
bitmap in chip memory and the blitter does the copy; on AmigaOS 4 it is a
full-colour buffer at twice the resolution, copied a row at a time.

## Notes for the 68k

- Everything builds with `-m68020-60`. Plain `-m68020` uses the 64-bit
  multiply and divide that the 68060 doesn't have: 68060.library emulates
  them, thousands of times slower, and the fixed-point simulations use
  them a great deal.
- GCC 6.5 for m68k miscompiled 64-bit square roots at `-O2` (they came
  out 0). `fsqrt` and `fhypot` work in 32 bits.
- No floating point at run time: the 68k build links no software floating
  point (`__adddf3` and the rest come out undefined). Constants written
  with `FX(1.5)` are folded at compile time; test vectors from the browser
  games are generated as 16.16 integers.
- amiga.lib's `sprintf` reads `%d` as 16 bits: `%ld` and a `(long)` cast.
