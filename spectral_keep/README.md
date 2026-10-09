# Spectral Keep (AmigaOS 4.1)

An isometric flip-screen adventure in the style of Knight Lore and Head
over Heels. There are three keeps and 24 rooms. Find the relics and carry
them to the throne. Keys open the red gates and potions are extra lives.
Guards, hounds, ghosts, bouncers and spikes are deadly.

Ported from the 3DO version (`3do-dev/projects/spectral_keep`), which was
rebuilt from the Unity game
[geekychris/spectral-keep](https://github.com/geekychris/spectral-keep).
AmigaOS 4.1 PPC only. It runs at a steady 50 fps on QEMU sam460ex.

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
spectral_keep [SCALE=n]     window n x 320x240 (default 2: 640x480)
```

## Files

| File | |
|---|---|
| `game.c`, `room.c`, `world.c`, `levels.c`, `vox.c`, `font.c`, `textures.c`, `keep.h` | the 3DO code, unchanged: rules; rooms and actors; physics; the 24 rooms; the software voxel renderer that draws every model into sprites; the game's 8x8 font; the surface textures |
| `scene.c` | the 3DO room composer with its output changed. The dependency sort over boxes is as before. The cels became a software compositor: the room's walls and floor are composed once per room, copied each frame, and the sprites blitted over them in the same order. Shadows, the panel's shade and the flash are per-pixel versions of the 3DO pixel-processor modes |
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

## Note: newlib memcpy on this guest

The per-frame background copy (300 KB) is a plain loop, not `memcpy()`.
On the QEMU OS4 guest's newlib.library, a `memcpy` of 300 KB or more
silently leaves the last 256 KB uncopied for some buffer alignments.
Copies of 200 KB and less were all correct.
