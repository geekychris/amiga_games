<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# warden_keep

A clean-room **isometric Filmation-style** room adventure, inspired by
the 8-bit classics of that lineage (Fairlight, Knight Lore, Head over
Heels). You walk around a single 8×8 tile stone-floor room in isometric
projection, push barrels into place, and collect the jewels scattered
about. Yellow monochrome palette; two back walls forming the classic
Filmation cutaway view so you can see inside.

**No copyrighted asset or code** from the games it draws inspiration
from. Mechanics only — level layout, palette, sprite geometry are all
original.

## Controls

| Key | Action |
|---|---|
| `CURSORS` | Walk one tile per keypress (N / E / S / W in world) |
| `SPACE` | Begin / return |
| `ESC` | Quit |

Movement is **tile-locked** and edge-triggered — one press = one tile
step. Walk into a barrel to push it (the tile behind it must be free).
Walk over a jewel to pick it up. Collect all three to win the room.

## Build (68k)

From the repo root:

```sh
docker run --rm -v $(pwd):/work -w /work \
  amigadev/crosstools:m68k-amigaos make -C examples/warden_keep
```

Deploy `examples/warden_keep/warden_keep` to your AmiKit / FS-UAE
shared folder (e.g. `DH2:Dev/`) and run from a shell.

## Renderer

Everything is drawn with plain `graphics.library` primitives — no
sprite sheet, no chunky-to-planar conversion:

- **Isometric projection.** World `(wx, wy, wz)` → screen
  `(sx, sy)` via `sx = (wx-wy)*16 + org_x`,
  `sy = (wx+wy)*8 - wz*16 + org_y`. Standard 2:1 iso tiles.
- **Cubes.** Three quads: top diamond, left face parallelogram,
  right face parallelogram. Diamond fill is scanline `RectFill`s
  expanding from centre outward. Face fills are per-pixel `WritePixel`.
- **Painter's algorithm.** Draw floor tiles first, then sort props +
  player by `(wx + wy)` ascending and draw back-to-front.
- **Walls.** Stacked cubes 3 units high so they read as room walls,
  not floor bumps. Only the north + west edges — Filmation cutaway.
- **Double-buffered.** Two `ScreenBuffer`s ping-ponged via
  `ChangeScreenBuffer` (proven from miner_meteor / emberkeep).

## Bridge instrumentation

Live variables via amiga-devbench:

- `mode` — GM_TITLE / GM_PLAYING / GM_WIN
- `score` — 100 per jewel
- `jewels_left` — decrements as you pick them up

## Follow-up work

1. **Sprite art pass.** Replace the geometric cubes with proper
   isometric sprites from a tile sheet (same bake pipeline as
   miner_meteor / emberkeep). Player wants recognisable poses per
   facing direction; barrels want a barrel shape not a plain cube.
2. **Multi-room** — one room today. Room graph + door transitions
   à la Knight Lore / Fairlight.
3. **Stairs / lift blocks** — vertical movement was the mechanic
   that made Knight Lore so different. Requires `wz` on the player
   + push-object-up mechanics.
4. **Enemies / guards** — patrolling entities, kill on contact,
   maybe pattern-based like the Fairlight sword-guards.
5. **Real audio.** No audio yet — see miner_meteor / void_trader for
   the classic Paula MOD + SFX pattern.
6. **PPC OS4 build.** Scaffolded (`ARCH=ppc`); untested.

## License

MIT. See `LICENSE` at the repo root.
