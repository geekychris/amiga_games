#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

"""
Bake examples/emberkeep/art/tilesheet.png -> tilesheet_data.{h,cpp}.

Reads the source PNG (any size, any RGB), downscales it to 320x128
(20x8 grid of 16x16 tiles) with nearest-neighbour, snaps every pixel
to the game palette, and emits palette-index C arrays.

If art/tilesheet.png is missing, writes a fallback sheet of solid
palette blocks so the game still builds + plays (very ugly, but
functional) — replace with real art and re-run.
"""

from pathlib import Path
import sys

HERE     = Path(__file__).resolve().parent
GAME_DIR = HERE.parent
SRC_PNG  = GAME_DIR / "art" / "tilesheet.png"
OUT_H    = GAME_DIR / "tilesheet_data.h"
OUT_C    = GAME_DIR / "tilesheet_data.cpp"

TILE_PX = 16
COLS    = 20
ROWS    = 8
SHEET_W = COLS * TILE_PX
SHEET_H = ROWS * TILE_PX

# Must match render.cpp's PALETTE.
PALETTE = [
    (  0,   0,   0), (240, 240, 240), (168,  48,  48), (128,  88,  40),
    ( 64, 208, 224), (224,  80, 192), (240, 224,  64), ( 64, 200,  64),
    ( 72,  72,  80), (240,  64,  64), (240, 160,  64), ( 80, 128, 240),
    (128, 128, 128), (168, 224, 128), (240, 168, 200), ( 40,  40,  40),
]


def nearest_palette_index(r: int, g: int, b: int) -> int:
    best_i, best_d = 0, 1 << 30
    for i, (pr, pg, pb) in enumerate(PALETTE):
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if d < best_d:
            best_d, best_i = d, i
    return best_i


def fallback_grid():
    """[ROWS][COLS][TILE_PX][TILE_PX] palette indices. Solid coloured
    blocks per tile position that map to the SR_/SC_ enums in
    render.cpp so the game is visually readable even before real art
    lands."""
    # (row, col) -> palette index for that tile
    tile_pen = {}
    for r in range(ROWS):
        for c in range(COLS):
            tile_pen[(r, c)] = 0
    # Terrain (row 0)
    tile_pen[(0, 0)] = 0   # floor -> black (looks pitch-dark, works with the mask)
    tile_pen[(0, 1)] = 2   # wall variant A -> brick
    tile_pen[(0, 2)] = 3   # wall variant B -> brown
    tile_pen[(0, 3)] = 6   # rune -> yellow
    tile_pen[(0, 4)] = 10  # oil -> orange
    tile_pen[(0, 5)] = 8   # hazard -> dark grey
    # Decor (row 1)
    tile_pen[(1, 0)] = 15  # portal locked -> dim
    tile_pen[(1, 1)] = 7   # portal open -> green
    # Player poses (row 4)
    for c in range(4): tile_pen[(4, c)] = 9   # bright red
    # Enemies rows 5/6/7
    for c in range(8):
        tile_pen[(5, c)] = 4  # cyan
        tile_pen[(6, c)] = 5  # magenta
        tile_pen[(7, c)] = 6  # yellow
    grid = [[[[tile_pen[(r, c)]] * TILE_PX for _ in range(TILE_PX)]
             for c in range(COLS)] for r in range(ROWS)]
    return grid


def png_grid():
    from PIL import Image
    img = Image.open(SRC_PNG).convert("RGB")
    print(f"source: {img.size[0]}x{img.size[1]} -> resample to {SHEET_W}x{SHEET_H}")
    small = img.resize((SHEET_W, SHEET_H), Image.NEAREST)
    px = small.load()
    grid = []
    for r in range(ROWS):
        row_tiles = []
        for c in range(COLS):
            tile = []
            for py in range(TILE_PX):
                line = []
                for pxi in range(TILE_PX):
                    rr, gg, bb = px[c * TILE_PX + pxi, r * TILE_PX + py]
                    line.append(nearest_palette_index(rr, gg, bb))
                tile.append(line)
            row_tiles.append(tile)
        grid.append(row_tiles)
    return grid


def emit(grid):
    h = []
    h.append("// SPDX-License-Identifier: MIT")
    h.append("// Copyright (c) 2026 Chris Collins <chris@hitorro.com>")
    h.append("//")
    h.append("// Generated from art/tilesheet.png by tools/bake_tilesheet.py.")
    h.append("// Do not hand-edit — regenerate via `python3 tools/bake_tilesheet.py`.")
    h.append("")
    h.append("#ifndef EMBERKEEP_TILESHEET_DATA_H")
    h.append("#define EMBERKEEP_TILESHEET_DATA_H")
    h.append("")
    h.append("#include <exec/types.h>")
    h.append("")
    h.append(f"#define TSHEET_TILES_W {COLS}")
    h.append(f"#define TSHEET_TILES_H {ROWS}")
    h.append(f"#define TSHEET_TILE_PX {TILE_PX}")
    h.append("")
    h.append("extern const UBYTE tilesheet_data"
             "[TSHEET_TILES_H][TSHEET_TILES_W]"
             "[TSHEET_TILE_PX][TSHEET_TILE_PX];")
    h.append("")
    h.append("#endif")
    OUT_H.write_text("\n".join(h) + "\n")
    print(f"wrote header: {OUT_H}")

    c = []
    c.append("// SPDX-License-Identifier: MIT")
    c.append("// Copyright (c) 2026 Chris Collins <chris@hitorro.com>")
    c.append("//")
    c.append("// Generated from art/tilesheet.png by tools/bake_tilesheet.py.")
    c.append("// Do not hand-edit — regenerate via `python3 tools/bake_tilesheet.py`.")
    c.append("")
    c.append("#include \"tilesheet_data.h\"")
    c.append("")
    c.append("const UBYTE tilesheet_data"
             "[TSHEET_TILES_H][TSHEET_TILES_W]"
             "[TSHEET_TILE_PX][TSHEET_TILE_PX] = {")
    for r in range(ROWS):
        c.append(f"    /* --- tile-sheet row {r} --- */")
        c.append("    {")
        for col in range(COLS):
            c.append(f"        /* col {col} */ {{")
            for py in range(TILE_PX):
                bytes_line = [f"{grid[r][col][py][pxi]:2d}" for pxi in range(TILE_PX)]
                c.append("            { " + ", ".join(bytes_line) + " },")
            c.append("        },")
        c.append("    },")
    c.append("};")
    OUT_C.write_text("\n".join(c) + "\n")
    print(f"wrote data:   {OUT_C}")


def main() -> int:
    if SRC_PNG.exists():
        try:
            grid = png_grid()
        except Exception as exc:
            print(f"warning: PNG bake failed ({exc}); falling back to solid palette blocks", file=sys.stderr)
            grid = fallback_grid()
    else:
        print(f"note: {SRC_PNG} not found — generating fallback solid-colour sheet")
        grid = fallback_grid()
    emit(grid)
    return 0


if __name__ == "__main__":
    sys.exit(main())
