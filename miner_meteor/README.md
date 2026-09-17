<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# miner-meteor

A clean-room single-screen tile platformer, inspired by 8-bit classics of the
Manic Miner / Jet Set Willy lineage. Every level is a fixed 20×15-tile room
with keys to collect, an exit that unlocks once the last key is grabbed, plus
a menagerie of hazards: stationary spikes, patrolling guardians, conveyor
belts, and collapsing floors that evaporate a few frames after you step on
them. Air drains regardless of what you do.

**No copyrighted asset or code** from the games it's inspired by. Mechanics
only — level layouts, tile art, sprite art, palette are all original.

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
| 0 | CENTRAL CAVERN   | walking, jumping, key collection |
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

## Follow-up work

Deliberately out of scope for the initial cut, all tracked in code comments:

1. **Real audio.** `audio.cpp` is a stub that logs SFX triggers to the bridge.
   Wiring Paula 4-channel MOD playback + chip-tune SFX (jump / land / key /
   death / levelwin) matches the pattern from `void_trader/{modplay,sfx}.c`.
2. **20+ cavern pack.** Six starter levels ship here; authoring the rest is
   pure content work.
3. **Sprite art.** Player + guardians + tiles are drawn as coloured
   rectangles. Swap in 16×16 sprite blits (e.g. from a ChatGPT-generated
   tile sheet, then hand-touched) for real 8-bit charm.
4. **AGA palette variant.** Palette is 16 entries at the top of the 8bpp
   AGA slot table; changing palette per cavern (`palette_variant` in
   `LevelSpec`) is defined in data but not yet honoured at draw time.
5. **Frame-rate cap.** Currently `WaitTOF()` — matches classic PAL @ 50Hz
   fine; on OS4 RTG a timer.device-based cap is needed (same lesson as
   fractalus).
6. **PPC OS4 build.** Scaffolded (`__PPC__` guards, input via bridge hook)
   but not tested. Building against `walkero/amigagccondocker:os4-gcc11`
   should Just Work; input needs a full input-hook rewrite for OS4.

## License

MIT. See `LICENSE` at the repo root.
