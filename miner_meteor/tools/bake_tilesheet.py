#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

"""
Bake examples/miner_meteor/art/tilesheet.png -> tilesheet_data.h.

Reads the source PNG (any size, any RGB), downscales it to the
expected 320x128 = 20x8 grid of 16x16 tiles using nearest-neighbour
(preserves pixel-art edges), then snaps every pixel to the closest
match in the game's 16-colour palette. Emits a C header with:

  #define TSHEET_TILES_W 20
  #define TSHEET_TILES_H 8
  #define TSHEET_TILE_PX 16
  extern const UBYTE tilesheet_data[TSHEET_TILES_H][TSHEET_TILES_W]
                                   [TSHEET_TILE_PX][TSHEET_TILE_PX];

Run: python3 tools/bake_tilesheet.py
"""

from PIL import Image
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
GAME_DIR = HERE.parent
SRC_PNG  = GAME_DIR / "art" / "tilesheet.png"
OUT_H    = GAME_DIR / "tilesheet_data.h"

TILE_PX  = 16
COLS     = 20
ROWS     = 8
SHEET_W  = COLS * TILE_PX      # 320
SHEET_H  = ROWS * TILE_PX      # 128

# Must match render.cpp's PALETTE table.
PALETTE = [
    (  0,   0,   0),   # 0  black bg
    (240, 240, 240),   # 1  white
    (168,  48,  48),   # 2  brick red
    (128,  88,  40),   # 3  brown
    ( 64, 208, 224),   # 4  cyan
    (224,  80, 192),   # 5  magenta
    (240, 224,  64),   # 6  yellow
    ( 64, 200,  64),   # 7  green
    ( 72,  72,  80),   # 8  dark grey
    (240,  64,  64),   # 9  bright red
    (240, 160,  64),   # 10 orange
    ( 80, 128, 240),   # 11 blue
    (128, 128, 128),   # 12 grey
    (168, 224, 128),   # 13 light green
    (240, 168, 200),   # 14 pink
    ( 40,  40,  40),   # 15 dim
]


def nearest_palette_index(r: int, g: int, b: int) -> int:
    best_i, best_d = 0, 1 << 30
    for i, (pr, pg, pb) in enumerate(PALETTE):
        # Squared Euclidean in RGB — fine for a 16-colour target.
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if d < best_d:
            best_d, best_i = d, i
    return best_i


def main() -> int:
    if not SRC_PNG.exists():
        print(f"error: {SRC_PNG} not found", file=sys.stderr)
        return 1

    img = Image.open(SRC_PNG).convert("RGB")
    print(f"source: {img.size[0]}x{img.size[1]} -> resample to {SHEET_W}x{SHEET_H}")
    small = img.resize((SHEET_W, SHEET_H), Image.NEAREST)
    px = small.load()

    # Emit as [ROWS][COLS][TILE_PX][TILE_PX] UBYTE.
    lines = []
    lines.append("// SPDX-License-Identifier: MIT")
    lines.append("// Copyright (c) 2026 Chris Collins <chris@hitorro.com>")
    lines.append("//")
    lines.append("// Generated from art/tilesheet.png by tools/bake_tilesheet.py.")
    lines.append("// Do not hand-edit — regenerate via `python3 tools/bake_tilesheet.py`.")
    lines.append("")
    lines.append("#ifndef MINER_METEOR_TILESHEET_DATA_H")
    lines.append("#define MINER_METEOR_TILESHEET_DATA_H")
    lines.append("")
    lines.append("#include <exec/types.h>")
    lines.append("")
    lines.append(f"#define TSHEET_TILES_W {COLS}")
    lines.append(f"#define TSHEET_TILES_H {ROWS}")
    lines.append(f"#define TSHEET_TILE_PX {TILE_PX}")
    lines.append("")
    lines.append("extern const UBYTE tilesheet_data"
                 "[TSHEET_TILES_H][TSHEET_TILES_W]"
                 "[TSHEET_TILE_PX][TSHEET_TILE_PX];")
    lines.append("")
    lines.append("#endif")

    OUT_H.write_text("\n".join(lines) + "\n")
    print(f"wrote header: {OUT_H}")

    # Emit the data as a separate .cpp so it lives in its own translation unit.
    OUT_C = GAME_DIR / "tilesheet_data.cpp"
    cparts = []
    cparts.append("// SPDX-License-Identifier: MIT")
    cparts.append("// Copyright (c) 2026 Chris Collins <chris@hitorro.com>")
    cparts.append("//")
    cparts.append("// Generated from art/tilesheet.png by tools/bake_tilesheet.py.")
    cparts.append("// Do not hand-edit — regenerate via `python3 tools/bake_tilesheet.py`.")
    cparts.append("")
    cparts.append("#include \"tilesheet_data.h\"")
    cparts.append("")
    cparts.append("const UBYTE tilesheet_data"
                  "[TSHEET_TILES_H][TSHEET_TILES_W]"
                  "[TSHEET_TILE_PX][TSHEET_TILE_PX] = {")

    for row in range(ROWS):
        cparts.append(f"    /* --- tile-sheet row {row} --- */")
        cparts.append("    {")
        for col in range(COLS):
            cparts.append(f"        /* col {col} */ {{")
            for py in range(TILE_PX):
                bytes_line = []
                for pxi in range(TILE_PX):
                    r, g, b = px[col * TILE_PX + pxi, row * TILE_PX + py]
                    bytes_line.append(f"{nearest_palette_index(r, g, b):2d}")
                cparts.append("            { " + ", ".join(bytes_line) + " },")
            cparts.append("        },")
        cparts.append("    },")

    cparts.append("};")
    OUT_C.write_text("\n".join(cparts) + "\n")
    print(f"wrote data:   {OUT_C}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
