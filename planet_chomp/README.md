# Planet Chomp (AmigaOS 4.1, OpenGL)

A Pac-Man homage on a tiny planet: the maze wraps all the way round a
sphere. Eat every crumb, dodge the four spooks, and grab a golden key to
turn the tables. Ported from the 3DO version
(`3do_dev/projects/planet_chomp`), which was itself rebuilt from the Unity
game [geekychris/planet-chomp](https://github.com/geekychris/planet-chomp).

AmigaOS 4.1 PPC only (QEMU sam460ex or real hardware). The 3D goes
through **Mesa 7.8.2's software rasteriser (OSMesa)**, built for OS4 by
`third_party/mesa-os4/`, so it needs no Warp3D driver or 3D card.

## Keys

| Key | |
|---|---|
| Arrows, WASD or keypad 8/4/6/2 | steer, relative to the screen; a turn waits for the next junction |
| Q / E (or Z / X) | spin the view |
| C | whole-planet view |
| P | pause |
| Space / Return | start |
| Esc or the close gadget | quit |

The attract mode starts by itself after 15 s on the title. The high
score is kept in `PROGDIR:planetchomp.hi`.

```
planet_chomp [SCALE=n] [HIRES] [FULLSCREEN]
  SCALE=n     window is n x 320x256 (default 2: 640x512)
  HIRES       render the 3D at 640x512 instead of doubling 320x256 (~15 fps on QEMU)
  FULLSCREEN  a screen of its own (800x600 on QEMU, the picture at 2x)
```

**Full screen:** add `FULLSCREEN` to open a screen of the game's own. F or
F10 switches between window and full screen while playing. The screen is
the smallest RTG mode that shows the frame at 2x (32-bit, else 16-bit),
with the picture centred and the mouse pointer hidden. This is
`os4_display.c`, shared by all three OS4 ports.

## Files

| File | |
|---|---|
| `game.c`, `game.h`, `maze.c`, `pc.h`, `sprites.c`, `sprites.h` | the 3DO code, unchanged: rules, spook AI, autopilot, sphere maze, sprite textures |
| `gl_render.c` | replaces the 3DO cel renderer (`render.c` + `cels.c`): same camera, OpenGL through OSMesa |
| `os4_display.c` | window or full screen, integer scaling, the blit (the same file in all three OS4 ports) |
| `main_os4.c` | window, keyboard as a pad, 50 Hz logic clock, HUD drawn into the GL frame, blit with `WritePixelArray` |
| `sfx_ahi.c` | the 3DO's synthesised sounds (`sfx.c`), played through ahi.device instead of Paula |
| `amiga3do.h` | the pad bits `game.c` expects from the 3DO compatibility layer |

## Build and run

```
third_party/mesa-os4/build.sh                 # once: libOSMesa.a (~10 s)
scripts/build-example-ppc.sh planet_chomp
scripts/deploy-os4.sh examples/planet_chomp/planet_chomp planet_chomp   # with QEMU stopped
```

Then `DH1:planet_chomp` from a Shell, or push it into a running OS4
with devbench's `amiga_push_file` and `amiga_launch`. The bridge client
is `PLANET`:

- vars: `score hiscore level lives state crumbs demo fps10 walls prims`
- hooks: `press <UP|DOWN|LEFT|RIGHT|A|C|P|L|R>` (holds a pad button for a few frames), `quit`
- every 5 s it logs fps and milliseconds per frame for logic, each GL stage, HUD and blit

## Performance notes (QEMU sam460ex)

About 35-40 fps at 320x256 (the 3DO version manages about 14). Two
things made the difference:

- **QEMU emulates the PPC FPU in software**, so every vertex and
  triangle set-up in Mesa is expensive. The planet is a 49-vertex
  silhouette fan, not a sphere mesh. Walls share their corners through
  `glDrawElements` (6 vertices for the two faces you can see), and their
  float copies are made once per maze. Crumbs are points, sorted into
  bins by size.
- **`glDepthFunc(GL_LESS)`, not `GL_LEQUAL`.** OSMesa has its own fast
  flat and smooth z-buffered triangle functions, but it only uses them
  with GL_LESS, 16 depth bits and nothing else enabled. With GL_LEQUAL
  every triangle goes through the generic span path, which made the
  walls 20 times slower.

Mesa's span functions keep big arrays on the stack (`_swrast_texture_span`
alone takes about 390 KB), so the binary asks for a 2 MB stack with a
`$STACK` cookie.

## Sound

The sounds use the default AHI unit. sam460ex has no sound hardware of
its own, and AHI's default SM502 mode doesn't work in QEMU. To get sound:

- start QEMU with `AUDIO=1 scripts/start-qemu-os4.sh`, which adds an
  ES1370 (Sound Blaster PCI 128), or `AUDIO=wav:file.wav` to record it;
- set AHI's units to an **SB128** mode in `Prefs/AHI`.

Without a working unit, the game runs silently.
