# Rolling Steel (AmigaOS 4.1, OpenGL)

An isometric roll-a-marble-downhill game in the spirit of the 1984 Atari
cabinet. There are six floating courses and one clock that carries over
between them, with no jump and no brakes. Ice slides, sand bites and acid
dissolves. Sweepers, crushers, fans, chasers and blobs get in the way.
Ported from the 3DO version (`3do-dev/projects/rolling_steel`), which was
rebuilt from the Unity game
[geekychris/rolling_steel](https://github.com/geekychris/rolling_steel)
(MIT, `LICENSE.original`).

AmigaOS 4.1 PPC, and classic 68k AmigaOS 3.x (see below). The 3D goes through **Mesa 7.8.2's software
rasteriser (OSMesa)**, built by `third_party/mesa-os4/`, so it needs no
3D card. It runs at about 40-50 fps on QEMU sam460ex (the 3DO manages
16-30).

## Keys

| | Player 1 | Player 2 |
|---|---|---|
| push the marble (relative to the view: up is away from you) | arrows, keypad 8/4/6/2 | W A S D |
| turn the view (tap: 45 degrees, hold: spin) | Z / X | Q / E |
| zoom in / out | = or keypad + / - or keypad - | Tab / 1 |
| tilt the view | C + up/down | |
| start (A) | Space, Return | Tab |
| pause | P | |

On the title, up/down picks the starting course (course 1 is the full
run), left/right picks one or two players (split screen), and C turns the
music on or off. Esc or the close gadget quits. Left alone, the title
plays a demo, one course at a time.

Best times per course and the best full run are kept in
`PROGDIR:rollingsteel.prog`.

```
rolling_steel [SCALE=n] [HIRES] [FULLSCREEN]
  SCALE=n     window is n x 320x240 (default 2: 640x480)
  HIRES       render at 640x480 instead of doubling 320x240
  FULLSCREEN  a screen of its own (640x480, the picture at 2x)
```

**Full screen:** add `FULLSCREEN` to open a screen of the game's own. F or
F10 switches between window and full screen while playing. The screen is
the smallest RTG mode that shows the frame at 2x (32-bit, else 16-bit),
with the picture centred and the mouse pointer hidden. This is
`os4_display.c`, shared by all three OS4 ports.

## Files

| File | |
|---|---|
| `game.c`, `game.h`, `phys.c`, `course.c`, `rs.h` | the 3DO code, unchanged: rules, clock, medals, ghost, autopilot; the marble's physics; the course loader |
| `render.c` | the 3DO renderer with its output changed. The orthographic camera, cell culling, per-frame vertex cache, lighting and depth fog are as before. Each corner now carries its depth, and the faces go to OpenGL instead of the cel engine |
| `glcels.c` | the cel engine's job in OpenGL: opaque faces batched into one `glDrawArrays`, sphere sprites as textured quads, and the 3DO pixel-processor modes (shadow, ghost, additive flash, HUD boxes) as blending |
| `os4_display.c` | window or full screen, integer scaling, the blit (the same file in all three OS4 ports) |
| `main_os4.c` | window, keyboard as two pads, 50 Hz logic clock, the HUD (drawn into the frame once GL has finished), data loading, best times |
| `sound_ahi.c` | the music themes, effects and speed-pitched rolling rumble through ahi.device; music and rumble loop gaplessly as linked request pairs |
| `game_os4.h` | force-included into `game.c`: renames its static `kill()`, which clashes with newlib's |
| `data/` | the six courses, the seven music themes and the effects, as built by the 3DO version's `tools/courses.py` and `tools/sounds.py` (big-endian, so loaded as they are) |

## Build and run

```
third_party/mesa-os4/build.sh                 # once: libOSMesa.a
scripts/build-example-ppc.sh rolling_steel
```

The program needs `data/` beside it, e.g. `DH1:rolling_steel/rolling_steel`
and `DH1:rolling_steel/data/`. With QEMU stopped:

```
HDF=~/AmigaOS4/amigaos4-dev.hdf
xdftool $HDF open part=0 + makedir rolling_steel + makedir rolling_steel/data \
  + write examples/rolling_steel/rolling_steel rolling_steel/rolling_steel
for f in examples/rolling_steel/data/*; do
  xdftool $HDF open part=0 + write $f rolling_steel/data/$(basename $f)
done
```

The bridge client is `ROLL`:

- vars: `state level time_left falls fps10 quads prims`
- hooks: `press <UP|DOWN|LEFT|RIGHT|A|B|C|P|L|R>` (pad 1), `press2` (pad 2), `quit`
- every 5 s it logs fps, faces drawn, and milliseconds per frame for logic, drawing, and HUD plus blit

## Classic 68k version (AmigaOS 3.x)

`make ARCH=m68k` (or `scripts/build-example-68k.sh rolling_steel`) builds
`rolling_steel_68k` for a 68020 or better with AGA or an RTG card. No 3D
library or FPU is needed. It runs the 3DO version's own code, which was
written for a CPU with no FPU and uses integer maths throughout:
it uses the same `render.c` as the OS4 build (the 3DO renderer, with each corner's depth), drawn by `glcels_soft.c` on `softcel.c`: a software rasteriser with a depth buffer, so track pieces hide each other per pixel (the 3DO's painter's sort let a side face show over the deck at some junctions). `sound_paula.c` is the 3DO sound code.

The frame is 15-bit RGB, the 3DO's own pixel format, so sprites and
textures need no conversion. `amiga68k.c` puts it on screen in one of
three ways:

- **RTG window** on an RTG Workbench, scaled up (Picasso96 `p96WritePixelArray`)
- **RTG screen** of its own, with `FULLSCREEN`; F or F10 switches between the two
- **AGA screen**, 320 x 256 with 8 bitplanes: a 256-colour palette fitted to
  the colours on screen (refreshed every few seconds, no dithering),
  converted chunky-to-planar and double buffered. This is the
  default when the Workbench isn't RTG; `AGA` forces it.

Sound is the 3DO code on the real Paula. Samples are in chip RAM, the
four channels are claimed from audio.device, and the 3DO's 50 Hz
audio tick runs from the main loop (`paula.h`, `paula68k.c`).

```
rolling_steel_68k [SCALE=n] [FULLSCREEN] [AGA]
```

Copy `rolling_steel_68k` and `data/` somewhere and run it, e.g. into the folder FS-UAE
mounts as `DH2:` (`deploy_dir` in devbench.toml).

Input: the keyboard as on OS4, and the joystick in port 1 (fire = start /
jump). FS-UAE puts that joystick on the cursor keys unless told otherwise,
which is why the arrows work through it.

On FS-UAE's 68060 (no JIT) through AGA, it runs at about 20-23 fps (the logic keeps full speed).

The 68k build doesn't link amiga.lib. Its `sprintf` would replace
libnix's, and amiga.lib's `%d` reads a 16-bit WORD, which the 3DO
code's `%d` would trip over. Paula's registers come from `$DFF000`
directly.

## How the drawing works on OS4

The 3DO version already projects every vertex itself with integer
maths: orthographic, so three dot products and no divide. That code is
kept, and OpenGL only rasterises. It gets screen positions plus each
corner's depth through an orthographic pixel matrix, so Mesa's
per-vertex work stays small. That matters because QEMU emulates the
PowerPC FPU in software.

The depth buffer replaces the 3DO's painter's sort for everything solid.
The sort is kept only to draw translucent things (blob shadows, the
ghost marble, sparks) back to front after the solid faces.

A sphere sprite is drawn at the depth of its front, so the deck it rolls
on never cuts it. Decals (acid, fans, boost strips) sit 0.05 units in
front of their deck.

Opaque faces use `glDepthFunc(GL_LESS)` with flat shading and nothing
else enabled. That is the combination for which OSMesa uses its fast
z-buffered triangle code.

Sound needs a working AHI unit; on QEMU, see `examples/planet_chomp/README.md`.
