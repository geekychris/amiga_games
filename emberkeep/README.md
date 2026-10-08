<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# emberkeep

A clean-room top-down cavern explorer, inspired by the atmospheric
"lantern in the deep dark" mechanic of 8-bit classics. You wander a
grid of stone chambers carrying a small burning lantern that lights
only a tight square-ish cone around you. Everything else — enemies,
runes, oil flasks, hazards — is pitch black until it enters your
light. The lantern burns oil; run dry and the light collapses. Fill
up from flasks scattered through each keep, collect the runes to
unlock the exit portal, and don't blunder into the things that stalk
in the dark.

**No copyrighted asset or code** from the games it draws mood from.
Mechanics only — level layouts, tile art, sprite art, palette are all
original.

## Controls

| Key | Action |
|---|---|
| `CURSORS` / `WASD` | Walk (4-directional) |
| `SPACE` | Begin / return to title (on end screen) |
| `ESC` | Quit |

## Build (68k)

From the repo root:

```sh
docker run --rm -v $(pwd):/work -w /work \
  amigadev/crosstools:m68k-amigaos make -C examples/emberkeep
```

Deploy `examples/emberkeep/emberkeep` to your AmiKit / FS-UAE shared
folder (e.g. `DH2:Dev/`) and run from a shell. Or use MCP's
`amiga_build_deploy_run` with `example: "emberkeep"`.

## Chambers

Four starter caverns. The engine + level format handles 20+ chambers
— authoring more is pure content work.

| # | Name | Introduces |
|---|---|---|
| 0 | GUIDING HALL    | walk + runes + portal — the tutorial |
| 1 | OIL CELLAR      | oil flasks + lantern management |
| 2 | WATCHFUL HALLS  | patrolling enemies + spike hazards |
| 3 | FULL DEPTHS     | everything at once, longer path |

## Tile sheet

Sprite + tile art lives in `art/tilesheet.png` — a 320×128 sheet in
a 20×8 grid of 16×16 tiles. `render.cpp` blits each tile from the
baked `tilesheet_data.{h,cpp}` (checked in so builds don't need
Python). If `art/tilesheet.png` isn't present, `tools/bake_tilesheet.py`
falls back to a solid-colour sheet so the game is still playable
(ugly but functional).

To provide real art:

1. Feed the prompt below to ChatGPT / image generator of choice.
2. Save the output as `examples/emberkeep/art/tilesheet.png`.
3. Regenerate the baked data: `python3 tools/bake_tilesheet.py`
4. Rebuild.

### ChatGPT tile-sheet prompt

```
Generate a retro Amiga-style pixel-art SPRITE SHEET for a top-down
cavern explorer with a lantern-in-the-dark mechanic. Output as a
single PNG, 320 x 128 pixels, black background. NO text, NO
borders, NO drop shadows. Sprites laid out on a 16x16 grid.

STYLE: chunky 16x16 pixel art, dark stone-and-torchlight vibe.
Saturated colours where they appear (runes, flames, enemy eyes)
against black + deep stone tones. 1-2 pixel outlines for readability.
Nothing photoreal, nothing 3D-rendered, nothing anime.

PALETTE — restrict every pixel to one of these 16 colours:
   0  #000000  black background
   1  #F0F0F0  white
   2  #A83030  brick red      (walls)
   3  #805828  brown          (wall trim / stone)
   4  #40D0E0  cyan           (enemy A eyes / glow)
   5  #E050C0  magenta        (enemy B)
   6  #F0E040  yellow         (runes, oil highlights)
   7  #40C840  green          (open portal)
   8  #484850  dark grey      (spikes)
   9  #F04040  bright red     (player body)
   10 #F0A040  orange         (lantern flame)
   11 #5080F0  blue           (title decoration only)
   12 #808080  grey           (stone shadow)
   13 #A8E080  light green
   14 #F0A8C8  pink
   15 #282828  dim / deep shadow

LAYOUT — 20 tiles wide x 8 tiles tall (320 x 128). Each cell is
exactly 16 x 16 pixels.

ROW 0 — TERRAIN TILES (8 cells)
  (0,0) floor        — pitch black with 1-2 tiny grey pixels of stone
                       texture (looks near-black at rest)
  (0,1) wall A       — mossy stone brick, red/brown mix
  (0,2) wall B       — variant of wall A for chequering
  (0,3) rune         — glowing yellow sigil on floor, blue underglow
  (0,4) oil flask    — small orange glass flask with a wick
  (0,5) spike hazard — jagged grey spikes rising from floor
  (0,6) spare 1      — small torch sconce on wall
  (0,7) spare 2      — small chest / brazier

ROW 1 — DECOR + PORTAL (8 cells)
  (1,0) portal locked — dim grey stone arch, dark inside
  (1,1) portal open   — bright green stone arch, glowing inside
  (1,2..1,7) small props (crystals, cracks, dust motes,
              mushrooms, chains, water droplets) — decor for
              future levels

ROW 2 — PLAYER FACING RIGHT (8 cells) — 8-frame walk cycle
  Bright red tunic, dark hood, small yellow lantern in one hand
  glowing orange. Frames 0-7: standing / walk cycle.

ROW 3 — PLAYER FACING LEFT (8 cells) — mirror of row 2

ROW 4 — PLAYER SPECIAL POSES (4 cells + 4 empty)
  (4,0) facing UP     — back of hood, lantern held aloft
  (4,1) facing RIGHT  — profile, one frame
  (4,2) facing DOWN   — face + lantern held forward
  (4,3) facing LEFT   — mirror of (4,1)
  (4,4..4,7) empty

ROW 5 — ENEMY A CYAN (8 cells)
  Ghostly cyan creature, dark eye slits, faint tendrils.
  8-frame animation: subtle drift + eye pulse.

ROW 6 — ENEMY B MAGENTA (8 cells)
  Different silhouette (say a low crawling shape), magenta glow.

ROW 7 — ENEMY C YELLOW (8 cells)
  A bat-like or moth-like figure, yellow-glowing eyes.

Deliver at exactly 320 x 128 pixels. If your tool can't guarantee
exact-pixel output, produce it at 4x scale (1280 x 512) with crisp
pixel edges, no anti-aliasing, no blurring, no gradients.
```

Once you drop the PNG into `art/`, re-bake and rebuild — the light
cone with real sprite art gives the game much of its character.

## Bridge instrumentation

The game exports these variables live via the amiga-devbench bridge:

- `mode` — GM_TITLE / GM_PLAYING / GM_LEVELWIN / GM_WIN / GM_LOSE
- `level`, `lives`, `score`
- `lantern` — 0..1000 fuel scale
- `runes_remaining` — decrements as you grab runes; portal unlocks at 0

## Follow-up work

1. **Real audio.** `audio.cpp` is a bridge-log stub. Wire in Paula MOD
   ambience + torch-flicker SFX à la `void_trader/{modplay,sfx}.c`.
2. **20+ chambers** — engine's already sized for a big pack.
3. **Softer light-cone** — currently a hard-edge square with chopped
   corners. A per-pixel distance mask (or a pre-rendered radial gradient
   BLTed once per frame) would give proper torchlight falloff.
4. **Enemy vision cones** — currently all enemies walk their patrol
   blindly. Adding "sees player if in torch radius" behaviour would
   turn the dark from cover into a two-way tool.
5. **Real joystick support** — cursor keys work everywhere thanks to
   FS-UAE's default arrow→joystick map. Direction decode from raw
   `JOY1DAT` needs proper edge detect (see miner_meteor for the
   attempt that trip-wired at rest).
6. **PPC OS4 build.** Scaffolded in Makefile (`ARCH=ppc`); untested.

## License

MIT. See `LICENSE` at the repo root.
