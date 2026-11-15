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

## Sound

Sound effects, after the Atari Lynx port's (which plays them on Mikey):

| Effect | When | Sound |
|---|---|---|
| jump | a jump starts | rising G4 C5 E5 square blip |
| land | landing after a fall of 8 px or more (not in the Lynx port) | short low noise thud |
| key | a key picked up | E6 A6 chirp |
| death | a life lost (spikes, guardian, air, falling out) | decaying noise burst |
| cavern cleared | the exit reached, more caverns to go | C5 E5 G5 C6 arpeggio |
| game over | the last life lost (after the death noise) | falling G4 F4 Eb4 C4 |
| win | the last cavern cleared | C5 E5 G5 C6, a rest, G5 C6 fanfare |

They are 8-bit samples (15977 Hz, one Paula period) made by
`tools/gen_sfx.py`, which renders the Lynx note lists at Mikey's quantised
pitches, and its LFSR noise, into `sfx_data.{h,cpp}`. The output is checked
in; after changing the script, run `make sfx` (Python 3, no packages).

- **Classic 68k:** audio.device, one channel allocated per voice, samples
  copied to chip RAM.
- **AmigaOS 4:** ahi.device (the sam460ex has no Paula).

There are three voices, as on the Lynx: jump and land share one, the
jingles another, and the death noise has its own. If the device can't be
opened, or no channel is free, the game runs silent. Every effect is also
logged to the bridge as `sfx: <name>`.

## Bridge instrumentation

The game registers these variables with the amiga-devbench bridge so you can
watch state live from the web UI (or `amiga_get_var`):

- `mode` — GM_TITLE / GM_PLAYING / GM_LEVELWIN / GM_WIN / GM_LOSE
- `level` — current cavern index (0-based)
- `lives`, `score`, `air`, `keys_remaining`

And two hooks for tests (`amiga_call_hook`, client `miner-meteor`):

- `state` returns `mode=.. level=.. lives=.. keys=.. air=.. x=.. y=..`
- `sfx` plays an effect by name (`jump`, `land`, `key`, `death`, `levelwin`,
  `gameover`, `win`)

## Tile sheet

Sprite + tile art lives in `art/tilesheet.png` — a 320×128 sheet
laid out as a 20 × 8 grid of 16×16 tiles (rows: terrain / decor /
player-right / player-left / player-pose / guardian-A / guardian-B /
guardian-C). Regenerate the C-baked data from the PNG with:

```sh
make tilesheet            # python3 tools/bake_tilesheet.py; needs PIL
```

A plain `make` never regenerates it (the cross-compiler images have no PIL,
and a fresh clone's file times are arbitrary).

The script downsamples the PNG to 320×128 with nearest-neighbour, snaps
every pixel to the 16-colour palette, and emits `tilesheet_data.{h,cpp}`
(checked in so builds work without Python). `render.cpp` blits each
tile to the screen with `WritePixelArray8`, using a small 320×2 scratch
bitmap for the graphics.library chunky-to-planar conversion.

To swap in updated art: replace the PNG, re-run the bake, rebuild.

## Follow-up work

Deliberately out of scope for the initial cut, all tracked in code comments:

1. **Music.** The sound effects are in (see Sound); a MOD tune is not.
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
- **Cavern 2 (Hazard Bay):** no jump clears the four floor spikes in front of
  the exit. One of them is now floor: row 12 `#..P.....^^^^....E.#` →
  `#..P.....^.^^....E.#`.
- **Cavern 6 (Full Ensemble):** the full-width conveyors in rows 3 and 10
  sealed the keys away and everything above the floor was out of jump reach.
  Now a step block in row 11 (`#....#.............#`) leads up through a gap
  in the row-10 conveyor (column 6) onto the conveyor and the ledges; a gap in
  the row-3 conveyor (column 5, `#>>>>.>>>>>>>>>>>>>#`) reaches the top key;
  and a second row-10 gap (column 14, `#<<<<<.<<<<<<<.<<<<#`) drops you past
  the spikes to the exit. Guardians, spikes, conveyors, collapsing floors and
  the three keys are unchanged.
- **Captions** on the title and end screens were drawn in pen 0 under the
  default JAM2 draw mode, so they showed as solid black boxes. `render_open()`
  now sets JAM1 on both buffers.

All six caverns are now winnable: a search over the real game state
(guardians moving, floors collapsing, air draining; the collision snaps
below included) finds a run through each, checked by replaying it with the
game code (3.0, 6.1, 17.1, 4.2, 5.3 and 6.0 s). Each changed row is marked
"bug in the original: unwinnable" in `levels.cpp`.

Known, not fixed: the collision code snaps. A side hit moves the player to the
tile edge (up to 14 px in one tick), and landing with an unaligned y can put
them inside the block above. A landing at an unaligned y snaps the player
16 px above the floor, where they drop and snap back every few frames until
they jump or walk off (the land sound ignores these short drops). Check any
level change with a reachability search, not by eye.

## License

MIT. See `LICENSE` at the repo root.
