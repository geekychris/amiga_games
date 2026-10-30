#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""make_image.py - a picture for the AmigaOS 4 builds (a3d_draw.h's
g_image_load): scaled to fill W x H pixels of the drawing (640 x 512 is the
whole screen) and cropped to it, then written as

    "A3IM", width, height (16-bit big-endian), then R G B bytes, rows top
    to bottom

  python3 tools/make_image.py in.png out.img [W H] [options]

  --align top|centre|bottom   which part a crop keeps (default centre)
  --stretch                   resize to exactly W x H without keeping the
                              shape (a top-down playfield painting,
                              squashed into the view's tilt)
  --rows A B                  keep only the source's rows from A to B (fractions
                              of its height) first: a horizon line in the
                              painting moved to its bottom edge
  --vscale F                  squash the height by F (after --rows): more
                              of a panorama's sky in a short strip
  --wrap N                    blend the last N columns into the first, so a
                              panorama joins without a seam
  --fade-top N                the top N rows fade to each row's average
                              colour, so the top row carried on above the
                              picture (g_image_wrap) is a plain sky
  --over FILE X Y W           a picture on a black background laid on top,
                              W wide at X, Y (its black is see-through)
  --shade-bottom N            darken the bottom N rows (under text)
"""
import argparse
from PIL import Image, ImageChops

ap = argparse.ArgumentParser()
ap.add_argument("src")
ap.add_argument("dst")
ap.add_argument("size", nargs="*", type=int)
ap.add_argument("--align", default="centre", choices=["top", "centre", "center", "bottom"])
ap.add_argument("--stretch", action="store_true")
ap.add_argument("--rows", nargs=2, type=float)
ap.add_argument("--vscale", type=float, default=1.0)
ap.add_argument("--wrap", type=int, default=0)
ap.add_argument("--fade-top", type=int, default=0)
ap.add_argument("--over", nargs=4, action="append", default=[])
ap.add_argument("--shade-bottom", type=int, default=0)
a = ap.parse_args()
W, H = (a.size[0], a.size[1]) if len(a.size) >= 2 else (640, 512)

im = Image.open(a.src).convert("RGB")
if a.rows:
    im = im.crop((0, round(a.rows[0] * im.height), im.width, round(a.rows[1] * im.height)))
if a.vscale != 1.0:
    im = im.resize((im.width, round(im.height * a.vscale)), Image.LANCZOS)
if a.wrap:
    # cross-fade the right edge into the left, then drop the right edge
    n = a.wrap * im.width // W
    left = im.crop((0, 0, n, im.height))
    right = im.crop((im.width - n, 0, im.width, im.height))
    ramp = Image.linear_gradient("L").rotate(90, expand=True).resize((n, im.height))
    im.paste(Image.composite(left, right, ramp), (0, 0))
    im = im.crop((0, 0, im.width - n, im.height))
if a.stretch:
    im = im.resize((W, H), Image.LANCZOS)
scale = max(W / im.width, H / im.height)
im = im.resize((max(W, round(im.width * scale)), max(H, round(im.height * scale))), Image.LANCZOS)
x0 = (im.width - W) // 2
y0 = {"top": 0, "bottom": im.height - H}.get(a.align, (im.height - H) // 2)
im = im.crop((x0, y0, x0 + W, y0 + H))

if a.fade_top:
    px = im.load()
    for y in range(min(a.fade_top, H)):
        row = [px[x, y] for x in range(W)]
        avg = tuple(sum(c[i] for c in row) // W for i in range(3))
        t = y / a.fade_top                      # 0 at the top: all average
        for x in range(W):
            px[x, y] = tuple(round(avg[i] * (1 - t) + row[x][i] * t) for i in range(3))

for f, x, y, w in a.over:
    x, y, w = int(x), int(y), int(w)
    ov = Image.open(f).convert("RGB")
    ov = ov.resize((w, round(ov.height * w / ov.width)), Image.LANCZOS)
    # see-through where it is black: alpha from the brightest channel
    alpha = ov.split()[0]
    for c in ov.split()[1:]:
        alpha = ImageChops.lighter(alpha, c)
    alpha = alpha.point(lambda v: min(255, v * 5))
    im.paste(ov, (x, y), alpha)

if a.shade_bottom:
    px = im.load()
    for y in range(H - a.shade_bottom, H):
        k = 1 - 0.7 * (y - (H - a.shade_bottom) + 1) / a.shade_bottom
        for x in range(W):
            px[x, y] = tuple(round(v * k) for v in px[x, y])

with open(a.dst, "wb") as fo:
    fo.write(b"A3IM" + W.to_bytes(2, "big") + H.to_bytes(2, "big") + im.tobytes())
print(f"{a.dst}: {W} x {H} from {a.src}")
