#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""make_texture.py - pictures for Boing Twist's ball, and textures for its
rooms, in the .btx form the game reads at run time (BTX2):

    0  "BTX2"
    4  width, height        16-bit big-endian, powers of two
    8  title                32 bytes, ASCII, zero padded
    40 level                0 easy, 1 medium, 2 hard, 3 expert: the lowest it suits
    41 (3 bytes, zero)
    44 room                 24 bytes: the .theme (without .theme) it goes with, or empty
    68 thumb width, height  16-bit big-endian (128 x 64)
    72 the thumbnail's R G B bytes, then the picture's, rows top to bottom

(The game still reads the older BTX1: "BTX1", width, height, title, pixels.)
Width and height are powers of two (OSMesa's fast texture paths need them).

  python3 tools/make_texture.py --all
      every picture in images/ (PNG, JPEG, TIFF, WebP) -> data/<name>.btx,
      and every picture in themes/src/ -> themes/<name>.btx
  python3 tools/make_texture.py in.png out.btx [--size 1024x512] [--title "MY PICTURE"]
  python3 tools/make_texture.py --stars themes/stars.btx      a star field

A ball picture should be an equirectangular map (twice as wide as it is
high, longitude across, north at the top), the kind a globe is printed
from; anything else is stretched to 2:1. Its title comes from
images/titles.txt ("file-name-without-extension = Title | level | room";
level and room may be left out), or the file name. The game lists data/ in name order, after its own Boing ball.
"""
import argparse
import os
import random
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(HERE)
EXTS = (".png", ".jpg", ".jpeg", ".tif", ".tiff", ".webp")


def pot(v):
    return v >= 8 and v <= 4096 and (v & (v - 1)) == 0


LEVELS = ["easy", "medium", "hard", "expert"]
THUMB = (128, 64)


def level_of(s):
    s = (s or "").strip().lower()
    if s in LEVELS:
        return LEVELS.index(s)
    if s.isdigit() and int(s) < len(LEVELS):
        return int(s)
    return 0


def write_btx(im, dst, title, level=0, room=""):
    im = im.convert("RGB")
    w, h = im.size
    t = title.upper().encode("ascii", "replace")[:31].ljust(32, b"\0")
    r = room.encode("ascii", "replace")[:23].ljust(24, b"\0")
    th = im.resize(THUMB, Image.LANCZOS)
    with open(dst, "wb") as f:
        f.write(b"BTX2" + w.to_bytes(2, "big") + h.to_bytes(2, "big") + t + bytes([level, 0, 0, 0]) + r
                + THUMB[0].to_bytes(2, "big") + THUMB[1].to_bytes(2, "big") + th.tobytes() + im.tobytes())
    print(f"{dst}: {w} x {h} '{title.upper()[:31]}' {LEVELS[level]}" + (f" room {room}" if room else ""))


def convert(src, dst, size, title, level=0, room=""):
    w, h = size
    if not (pot(w) and pot(h)):
        sys.exit(f"{w} x {h}: width and height must be powers of two")
    im = Image.open(src).convert("RGB")
    im = im.resize((w, h), Image.LANCZOS)
    write_btx(im, dst, title, level, room)


def stars(dst, size=(512, 256), seed=1984):
    w, h = size
    rnd = random.Random(seed)
    im = Image.new("RGB", (w, h), (4, 6, 18))
    px = im.load()
    for y in range(h):                      # a faint nebula band
        for x in range(w):
            g = max(0.0, 1.0 - abs(y - h * 0.55 - 30 * (x / w - 0.5)) / (h * 0.3))
            px[x, y] = (4 + int(30 * g * g), 6 + int(12 * g * g), 18 + int(46 * g * g))
    for _ in range(w * h // 90):
        x, y, b = rnd.randrange(w), rnd.randrange(h), rnd.randrange(90, 256)
        tint = rnd.choice([(1, 1, 1), (1, 0.9, 0.7), (0.75, 0.85, 1)])
        px[x, y] = tuple(int(b * c) for c in tint)
    write_btx(im, dst, "STARS")


def titles(game=None):
    """images/titles.txt: stem -> (title, level, room)"""
    t = {}
    path = os.path.join(game or GAME, "images", "titles.txt")
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.split("#")[0].strip()
            if "=" in line:
                k, v = line.split("=", 1)
                f = [x.strip() for x in v.split("|")] + ["", ""]
                t[k.strip()] = (f[0], level_of(f[1]), f[2])
    return t


def up_to_date(dst, deps):
    if not os.path.exists(dst):
        return False
    t = os.path.getmtime(dst)
    return all(t > os.path.getmtime(d) for d in deps if os.path.exists(d))


def convert_all(size, game=None):
    game = game or GAME
    names = titles(game)
    os.makedirs(os.path.join(game, "data"), exist_ok=True)
    n = 0
    for folder, out, sz in (("images", "data", size), (os.path.join("themes", "src"), "themes", (512, 512))):
        src_dir = os.path.join(game, folder)
        if not os.path.isdir(src_dir):
            continue
        for f in sorted(os.listdir(src_dir)):
            stem, ext = os.path.splitext(f)
            if ext.lower() not in EXTS:
                continue
            dflt = stem.split("_", 1)[-1].replace("_", " ") if stem[:1].isdigit() else stem.replace("_", " ")
            title, level, room = names.get(stem, (dflt, 0, ""))
            dst = os.path.join(game, out, stem + ".btx")
            if up_to_date(dst, [os.path.join(src_dir, f), os.path.join(game, "images", "titles.txt"), __file__]):
                n += 1
                continue
            convert(os.path.join(src_dir, f), dst, sz, title or dflt, level, room)
            n += 1
    stars(os.path.join(game, "themes", "stars.btx"))
    print(f"{n} pictures and textures")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", nargs="?")
    ap.add_argument("dst", nargs="?")
    ap.add_argument("--size", default="1024x512", help="W x H, powers of two (default 1024x512)")
    ap.add_argument("--title")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--stars", metavar="DST")
    ap.add_argument("--level", default="easy", help="with SRC DST: the lowest level the picture suits")
    ap.add_argument("--room", default="", help="with SRC DST: the room (theme file stem) it goes with")
    ap.add_argument("--game", help="the game's folder (default: this one)")
    a = ap.parse_args()
    size = tuple(int(v) for v in a.size.lower().split("x"))
    if a.all:
        convert_all(size, a.game)
    elif a.stars:
        stars(a.stars)
    elif a.src and a.dst:
        convert(a.src, a.dst, size, a.title or os.path.splitext(os.path.basename(a.src))[0].replace("_", " "),
                level_of(a.level), a.room)
    else:
        ap.print_help()


if __name__ == "__main__":
    main()
