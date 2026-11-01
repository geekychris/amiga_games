#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""test_import.py - tools/import_art.py and make_texture.py on stand-in art,
in a scratch copy of the game's folders (make test-tools)."""
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops, ImageStat

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(GAME, "tools"))
import import_art  # noqa: E402

fails = passes = 0


def check(cond, what):
    global fails, passes
    print(f"  {'PASS' if cond else 'FAIL'}  {what}")
    if cond:
        passes += 1
    else:
        fails += 1


def run(*args):
    return subprocess.run([sys.executable] + list(args), capture_output=True, text=True)


def edge_diff(im, axis):
    W, H = im.size
    if axis == "x":
        a, b = im.crop((W - 1, 0, W, H)), im.crop((0, 0, 1, H))
    else:
        a, b = im.crop((0, H - 1, W, H)), im.crop((0, 0, W, 1))
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3


def btx_head(path):
    with open(path, "rb") as f:
        d = f.read(72)
    return d[:4], int.from_bytes(d[4:6], "big"), int.from_bytes(d[6:8], "big"), d[8:40].rstrip(b"\0").decode(), d[40], d[44:68].rstrip(b"\0").decode()


def main():
    tmp = tempfile.mkdtemp(prefix="bt_import_")
    art, art2, game = os.path.join(tmp, "art"), os.path.join(tmp, "art2"), os.path.join(tmp, "game")
    os.makedirs(os.path.join(game, "images"))
    os.makedirs(os.path.join(game, "themes"))
    r = run(os.path.join(HERE, "make_sample_art.py"), art)
    check(r.returncode == 0, "sample art made")

    print("== by file name")
    r = run(os.path.join(GAME, "tools", "import_art.py"), art, "--game", game, "--no-make")
    check(r.returncode == 0, "import_art.py ran" + ("" if r.returncode == 0 else ": " + r.stderr[-300:]))
    imgs = sorted(f for f in os.listdir(os.path.join(game, "images")) if f.endswith(".jpg"))
    check(imgs == ["1_rainbow_blocks.jpg", "2_confetti.jpg"], f"pictures numbered in order: {imgs}")
    im = Image.open(os.path.join(game, "images", "1_rainbow_blocks.jpg")).convert("RGB")
    check(im.size == (2048, 1024), f"2:1 at 2048 x 1024: {im.size}")
    raw = import_art.to_2_1(Image.open(os.path.join(art, "ball_easy_rainbow_blocks.png")).convert("RGB")).resize((2048, 1024))
    check(raw.size == (2048, 1024), "a 3:2 picture is cropped (not squashed) to 2:1")
    before, after = edge_diff(raw, "x"), edge_diff(im, "x")
    check(after < before / 4 and after < 12, f"the wrap seam is blended: edge difference {before:.1f} -> {after:.1f}")
    sq = import_art.to_2_1(Image.new("RGB", (1024, 1024)))
    check(sq.size == (1024, 512), "a square picture keeps its middle half")
    titles = open(os.path.join(game, "images", "titles.txt")).read()
    check("1_rainbow_blocks = Rainbow Blocks | easy" in titles and "2_confetti = Confetti | hard" in titles, "titles.txt lines with levels")
    theme = open(os.path.join(game, "themes", "neon.theme")).read()
    check("name = Neon" in theme and "back = " in theme and "neon_back.btx" in theme and "neon_floor.btx 4" in theme,
          "neon.theme names its textures, the floor tiled 4 times")
    check("neon_left.btx" in theme and "neon_right.btx" in theme, "side walls cropped from the back wall")
    fl = Image.open(os.path.join(game, "themes", "src", "neon_floor.jpg")).convert("RGB")
    rawfl = Image.open(os.path.join(art, "room_neon_floor.png")).convert("RGB").resize(fl.size)
    check(edge_diff(fl, "x") < edge_diff(rawfl, "x") / 3 and edge_diff(fl, "y") < edge_diff(rawfl, "y") / 3,
          f"the floor tiles: edges {edge_diff(rawfl, 'x'):.1f}/{edge_diff(rawfl, 'y'):.1f} -> {edge_diff(fl, 'x'):.1f}/{edge_diff(fl, 'y'):.1f}")
    check(os.path.exists(os.path.join(art, "import_contact.png")), "contact sheet written")

    print("== again, by manifest (ChatGPT's own file names)")
    r = run(os.path.join(GAME, "tools", "import_art.py"), art, "--manifest", os.path.join(art, "manifest.csv"),
            "--game", game, "--sides", "colour", "--poles", "0.05", "--contact", os.path.join(tmp, "c2.png"), "--no-make")
    check(r.returncode == 0, "manifest import ran" + ("" if r.returncode == 0 else ": " + r.stderr[-300:]))
    imgs = sorted(f for f in os.listdir(os.path.join(game, "images")) if f.endswith(".jpg"))
    check(imgs == ["1_rainbow_blocks.jpg", "2_confetti.jpg", "3_pastel.jpg"], f"re-imported names keep their numbers: {imgs}")
    titles = open(os.path.join(game, "images", "titles.txt")).read().splitlines()
    check(sum(1 for l in titles if l.startswith("1_rainbow_blocks")) == 1, "no duplicate titles.txt lines")
    check("3_pastel = Pastel Planet | medium | neon" in titles, "the manifest's title, level and room")
    check("1_rainbow_blocks = Rainbow Blocks | easy | neon" in titles, "a ball paired with a room")
    theme = open(os.path.join(game, "themes", "neon.theme")).read()
    check("name = Neon Arcade" in theme and "left = " in theme and "neon_left.btx" not in theme, "--sides colour: plain side walls")
    pole = Image.open(os.path.join(game, "images", "3_pastel.jpg")).convert("RGB").crop((0, 0, 2048, 1))
    check(max(ImageStat.Stat(pole).stddev) < 6, "--poles: the top row is one colour")

    print("== make_texture.py --all")
    r = run(os.path.join(GAME, "tools", "make_texture.py"), "--all", "--game", game, "--size", "512x256")
    check(r.returncode == 0, "make_texture ran")
    h = btx_head(os.path.join(game, "data", "3_pastel.btx"))
    check(h[0] == b"BTX3" and h[1:3] == (512, 256), f"BTX3 512 x 256: {h[:3]}")
    check(h[3] == "PASTEL PLANET" and h[4] == 1 and h[5] == "neon", f"title, level, room in the header: {h[3:]}")
    path = os.path.join(game, "data", "3_pastel.btx")
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        d = f.read()
    at = 72 + 128 * 64 * 3
    jl = int.from_bytes(d[at:at + 4], "big")
    check(size == at + 4 + jl and d[at + 4:at + 6] == b"\xff\xd8", f"thumbnail, then a {jl}-byte JPEG: {size} bytes")
    import io
    pic = Image.open(io.BytesIO(d[at + 4:])).convert("RGB")
    check(pic.size == (512, 256), "the JPEG decodes to the picture's size")
    r3 = run(os.path.join(GAME, "tools", "make_texture.py"), os.path.join(game, "images", "3_pastel.jpg"),
             os.path.join(tmp, "raw.btx"), "--raw", "--size", "512x256")
    check(r3.returncode == 0 and os.path.getsize(os.path.join(tmp, "raw.btx")) == at + 512 * 256 * 3, "--raw writes BTX2")
    for t in ("neon_back", "neon_floor"):
        check(os.path.exists(os.path.join(game, "themes", t + ".btx")), f"themes/{t}.btx")
    r2 = run(os.path.join(GAME, "tools", "make_texture.py"), "--all", "--game", game, "--size", "512x256")
    check(r2.stdout.count(".btx:") <= 1, "a second run converts only what changed (the stars)")
    print(f"\n{passes} passed, {fails} failed   ({tmp})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
