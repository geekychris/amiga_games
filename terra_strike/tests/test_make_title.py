#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Round-trip test for tools/make_title.py: convert a synthetic image, decode
the generated bitplanes back to pixels and compare with the source colours.

  python3 tests/test_make_title.py
"""
import os, re, subprocess, sys, tempfile
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "tools", "make_title.py")
fails = 0

def check(cond, name):
    global fails
    print(f"  {'PASS' if cond else 'FAIL'}  {name}")
    fails += not cond

with tempfile.TemporaryDirectory() as d:
    # 4 flat colour bands (already 12-bit exact) on black, at 2x the target size
    src = Image.new("RGB", (640, 512), (0, 0, 0))
    bands = [(255, 238, 0), (0, 68, 170), (51, 153, 34), (238, 238, 238)]
    for i, c in enumerate(bands):
        src.paste(c, (0, 128 + i * 96, 640, 128 + (i + 1) * 96))
    sp = os.path.join(d, "src.png"); src.save(sp)
    out = os.path.join(d, "title_image.c")
    subprocess.run([sys.executable, TOOL, sp, "--out", out], check=True, capture_output=True)
    text = open(out).read()
    hdr = open(os.path.join(d, "title_image.h")).read()

    body = lambda name: text.split(name)[1].split("{", 1)[1].split("};", 1)[0]
    pal = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{3})\b", body("title_palette"))]
    data = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})\b", body("title_planes"))]
    check(len(pal) == 32, "32-entry 12-bit palette")
    check(pal[0] == 0x000, "pen 0 is black (border and sky)")
    check(len(data) == 5 * 40 * 256, "five 320x256 bitplanes")

    def pixel(x, y):
        v = 0
        for p in range(5):
            if data[p * 10240 + y * 40 + x // 8] & (0x80 >> (x & 7)):
                v |= 1 << p
        return v

    def rgb(pen):
        c = pal[pen]
        return ((c >> 8) * 17, (c >> 4 & 15) * 17, (c & 15) * 17)

    check(rgb(pixel(10, 10)) == (0, 0, 0), "black sky decodes as black")
    ok = all(rgb(pixel(160, 64 + i * 48 + 24)) == c for i, c in enumerate(bands))
    check(ok, "each colour band decodes to its exact colour")
    m = re.search(r"TITLE_PEN_TEXT (\d+)", hdr)
    check(m and rgb(int(m.group(1))) == (255, 238, 0), "text pen is the yellow")

print("ALL PASSED" if not fails else f"{fails} FAILED")
sys.exit(fails)
