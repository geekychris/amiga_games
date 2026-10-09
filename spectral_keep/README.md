# Spectral Keep (AmigaOS 4.1)

An isometric flip-screen adventure in the style of Knight Lore and Head
over Heels. There are three keeps and 24 rooms. Find the relics and carry
them to the throne. Keys open the red gates and potions are extra lives.
Guards, hounds, ghosts, bouncers and spikes are deadly.

Ported from the 3DO version (`3do-dev/projects/spectral_keep`), which was
rebuilt from the Unity game
[geekychris/spectral-keep](https://github.com/geekychris/spectral-keep).
AmigaOS 4.1 PPC, and classic 68k AmigaOS 3.x (see below). It runs at a steady 50 fps on QEMU sam460ex.

## Keys

| Key | |
|---|---|
| arrows, WASD or keypad 8/4/6/2 | walk; relative to the screen (C on the title switches to grid directions) |
| Space / Return | jump; on the title, start |
| P | pause |
| Esc or the close gadget | quit |
| Title: up/down | choose a keep |
| Title: M | music on / off |
| Title: Q + E together | a tour of every room of every keep |

```
spectral_keep [SCALE=n] [FULLSCREEN]
  SCALE=n     window is n x 320x240 (default 2: 640x480)
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
| `game.c`, `room.c`, `world.c`, `levels.c`, `vox.c`, `font.c`, `textures.c`, `keep.h` | the 3DO code, unchanged: rules; rooms and actors; physics; the 24 rooms; the software voxel renderer that draws every model into sprites; the game's 8x8 font; the surface textures |
| `scene.c` | the 3DO room composer with its output changed. The dependency sort over boxes is as before. The cels became a software compositor: the room's walls and floor are composed once per room, copied each frame, and the sprites blitted over them in the same order. Shadows, the panel's shade and the flash are per-pixel versions of the 3DO pixel-processor modes |
| `os4_display.c` | window or full screen, integer scaling, the blit (the same file in all three OS4 ports) |
| `main_os4.c` | window, keyboard as the pad, 50 Hz loop, the HUD (Hud.cs) in the game's font, data loading |
| `sound_ahi.c` | the four music loops and the effects through ahi.device. Music streams in one-second linked chunks, so it loops without a gap and can duck while someone talks |
| `data/` | the music and effects rendered by the 3DO version's `tools/assets.py` (signed 8-bit, 11050 Hz) |

No OpenGL here, unlike `planet_chomp` and `rolling_steel`. Like the
8-bit originals, the game draws pre-rendered sprites at whole pixels,
so plain blitting is both exact and far faster than textured quads
through Mesa's software rasteriser.

## Build and run

```
scripts/build-example-ppc.sh spectral_keep
```

The program needs `data/` beside it, e.g. `DH1:spectral_keep/spectral_keep`
and `DH1:spectral_keep/data/` (see `examples/rolling_steel/README.md` for an
`xdftool` recipe).

The bridge client is `KEEP`:

- vars: `state lives relics keys room px py pz fps10 cels`
- hooks: `press <UP|DOWN|LEFT|RIGHT|A|B|C|P|L|R|L+R>`, `hold <BUTTON> <frames>` (walk), `quit`

## Classic 68k version (AmigaOS 3.x)

`make ARCH=m68k` (or `scripts/build-example-68k.sh spectral_keep`) builds
`spectral_keep_68k` for a 68020 or better with AGA or an RTG card. No 3D
library or FPU is needed. It runs the 3DO version's own code, which was
written for a CPU with no FPU and uses integer maths throughout:
`scene.c` composes the room in 15-bit RGB (`SCENE_RGB15`) and `sound_paula.c` is the 3DO sound code.

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
spectral_keep_68k [SCALE=n] [FULLSCREEN] [AGA]
```

Copy `spectral_keep_68k` and `data/` somewhere and run it, e.g. into the folder FS-UAE
mounts as `DH2:` (`deploy_dir` in devbench.toml).

Input: the keyboard as on OS4, and the joystick in port 1 (fire = start /
jump). FS-UAE puts that joystick on the cursor keys unless told otherwise,
which is why the arrows work through it.

On FS-UAE's 68060 (no JIT) through AGA, it runs at about 32 fps.

The 68k build doesn't link amiga.lib. Its `sprintf` would replace
libnix's, and amiga.lib's `%d` reads a 16-bit WORD, which the 3DO
code's `%d` would trip over. Paula's registers come from `$DFF000`
directly.

## Note: newlib memcpy on this guest

The per-frame background copy (300 KB) is a plain loop, not `memcpy()`.
On the QEMU OS4 guest's newlib.library, a `memcpy` of 300 KB or more
silently leaves the last 256 KB uncopied for some buffer alignments.
Copies of 200 KB and less were all correct.
