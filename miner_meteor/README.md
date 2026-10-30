<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# miner-meteor

A clean-room single-screen tile platformer in the style of the 8-bit
single-screen platformers of the early 1980s. Every level is a fixed 20×15-tile room
with keys to collect, an exit that unlocks once the last key is grabbed, plus
a menagerie of hazards: stationary spikes, patrolling guardians, conveyor
belts, and collapsing floors that evaporate a few frames after you step on
them. Air drains regardless of what you do.

**No copyrighted asset or code** from the games it's inspired by. Mechanics
only — level names, level layouts, tile art, sprite art, palette are all original.

## Controls

| Key | Action |
|---|---|
| `LEFT` / `A` | Walk left |
| `RIGHT` / `D` | Walk right |
| `SPACE` / `UP` | Jump (from title / end screens: begin / return) |
| `ESC` | Quit |

## Build (68k)

From the repo root:

```sh
make miner_meteor            # Docker build via amigadev/crosstools
```

Deploy the binary from `examples/miner_meteor/miner_meteor` to your AmiKit /
FS-UAE shared folder (typically `DH2:Dev/`) and run from a shell.

Or use the MCP tool `amiga_build_deploy_run` with `example: "miner_meteor"`.

## Levels

Six starter caverns exercise every mechanic — the engine + level format are
already sized for the 20+ caverns the ticket calls for, but authoring the
remaining 14 is a follow-up:

| # | Name | Introduces |
|---|---|---|
| 0 | FIRST SHAFT      | walking, jumping, key collection |
| 1 | HAZARD BAY       | stationary hazards + air pressure |
| 2 | THE ESCALATORS   | conveyor belts (zig-zag descent through drop-gaps) |
| 3 | GUARDIAN ALLEY   | patrolling horizontal guardians |
| 4 | COLLAPSING ATTIC | collapsing floors |
| 5 | FULL ENSEMBLE    | every mechanic combined |

Levels are hand-authored ASCII grids in `levels.cpp`. The tile alphabet:

```
.   empty
#   solid wall / floor
K   key
E   exit (locked until keys_remaining == 0)
c   collapsing floor
<   conveyor pushing left
>   conveyor pushing right
^   hazard (spikes / lava)
P   player spawn
```

## Bridge instrumentation

The game registers these variables with the amiga-devbench bridge so you can
watch state live from the web UI (or `amiga_get_var`):

- `mode` — GM_TITLE / GM_PLAYING / GM_LEVELWIN / GM_WIN / GM_LOSE
- `level` — current cavern index (0-based)
- `lives`, `score`, `air`, `keys_remaining`

## Tile sheet

Sprite + tile art lives in `art/tilesheet.png` — a 320×128 sheet
laid out as a 20 × 8 grid of 16×16 tiles (rows: terrain / decor /
player-right / player-left / player-pose / guardian-A / guardian-B /
guardian-C). Regenerate the C-baked data from the PNG with:

```sh
python3 tools/bake_tilesheet.py
```

The script downsamples the PNG to 320×128 with nearest-neighbour, snaps
every pixel to the 16-colour palette, and emits `tilesheet_data.{h,cpp}`
(checked in so builds work without Python). `render.cpp` blits each
tile to the screen with `WritePixelArray8`, using a small 320×2 scratch
bitmap for the graphics.library chunky-to-planar conversion.

To swap in updated art: replace the PNG, re-run the bake, rebuild.

## Follow-up work

Deliberately out of scope for the initial cut, all tracked in code comments:

1. **Real audio.** `audio.cpp` is a stub that logs SFX triggers to the bridge.
   Wiring Paula 4-channel MOD playback + chip-tune SFX (jump / land / key /
   death / levelwin) matches the pattern from `void_trader/{modplay,sfx}.c`.
2. **20+ cavern pack.** Six starter levels ship here; authoring the rest is
   pure content work.
3. **AGA palette variant.** Palette is 16 entries at the top of the 8bpp
   AGA slot table; changing palette per cavern (`palette_variant` in
   `LevelSpec`) is defined in data but not yet honoured at draw time.
4. **Frame-rate cap.** Currently `WaitTOF()` — matches classic PAL @ 50Hz
   fine; on OS4 RTG a timer.device-based cap is needed (same lesson as
   canyon_rescue).
5. **PPC OS4 build.** Scaffolded (`__PPC__` guards, input via bridge hook)
   but not tested. Building against `walkero/amigagccondocker:os4-gcc11`
   should Just Work; input needs a full input-hook rewrite for OS4.
6. **Sprite art pass.** The bake script produces palette-mapped tiles
   from the source PNG — good baseline, but a hand-clean pass in
   Aseprite / Piskel snapping colours to the 16-slot palette is worth
   doing for full 8-bit crispness. Re-run the bake after.

## Fixes to the original (2026-10-30)

A search of every reachable position (done for the Atari Falcon and 3DO
ports) showed that no cavern could be finished as first released:

- **Jump:** `JUMP_IMPULSE` was -8, a 36 px rise, but the keys sit on
  platforms 64 px up. It is now -12 (`game.h`), which makes caverns 1, 3, 4
  and 5 winnable.
- **Captions** on the title and end screens were drawn in pen 0 under the
  default JAM2 draw mode, so they showed as solid black boxes. `render_open()`
  now sets JAM1 on both buffers.

Still unwinnable, waiting on a level-design decision:

- **Cavern 2 (Hazard Bay):** no jump clears the four floor spikes in front of
  the exit. Turning any one into floor fixes it, e.g. row 12
  `#..P.....^^^^....E.#` → `#..P.....^.^^....E.#`.
- **Cavern 6 (Full Ensemble):** everything above the floor is at least 80 px
  up, beyond the jump, and one key stands on the row-3 conveyor. It needs new
  platforms.

Known, not fixed: the collision code snaps. A side hit moves the player to the
tile edge (up to 14 px in one tick), and landing with an unaligned y can put
them inside the block above. Check any level change with a reachability
search, not by eye.

## License

MIT. See `LICENSE` at the repo root.
