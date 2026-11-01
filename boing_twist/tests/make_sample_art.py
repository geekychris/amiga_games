#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""make_sample_art.py OUT_DIR [--many N] - stand-in "ChatGPT" art for testing
tools/import_art.py: the sizes ChatGPT gives (1536 x 1024, 1024 x 1024),
named the way the import expects, plus one with ChatGPT's own sort of name
and a manifest for it. --many N adds N more small ball pictures (for the
select list's scrolling)."""
import argparse
import math
import os
import random

from PIL import Image, ImageDraw


def bold_ball(w, h, seed):
    """big bold shapes: an Easy picture. Left and right edges differ, so the seam shows if not blended."""
    rnd = random.Random(seed)
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    cols = [(230, 60, 50), (250, 200, 40), (40, 150, 230), (60, 190, 90), (170, 70, 200), (240, 130, 30)]
    for i in range(6):
        d.rectangle((i * w // 6, 0, (i + 1) * w // 6, h), fill=cols[(i + seed) % 6])
    for _ in range(14):
        x, y, r = rnd.randrange(w), rnd.randrange(h // 6, h * 5 // 6), rnd.randrange(h // 14, h // 6)
        d.ellipse((x - r, y - r, x + r, y + r), fill=rnd.choice(cols), outline=(255, 255, 255), width=8)
    d.text((w // 2 - 40, h // 2), f"SAMPLE {seed}", fill=(0, 0, 0))
    return im


def fine_ball(w, h, seed):
    """fine detail: a Hard picture"""
    rnd = random.Random(seed)
    im = Image.new("RGB", (w, h), (20, 30, 60))
    d = ImageDraw.Draw(im)
    for _ in range(4000):
        x, y, r = rnd.randrange(w), rnd.randrange(h), rnd.randrange(2, 9)
        c = (rnd.randrange(80, 256), rnd.randrange(80, 256), rnd.randrange(80, 256))
        d.ellipse((x - r, y - r, x + r, y + r), fill=c)
    return im


def pastel(w, h):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = (int(200 + 50 * math.sin(x / 90.0)), int(170 + 60 * math.sin(y / 70.0 + 1)), int(210 + 40 * math.cos((x + y) / 130.0)))
    return im


def neon_back(w, h):
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        d.line((0, y, w, y), fill=(10 + y * 30 // h, 0, 30 + y * 60 // h))
    for i in range(7):
        x = 80 + i * (w - 160) // 6
        d.rectangle((x - 40, h // 4, x + 40, h * 3 // 4), outline=(255, 40, 200) if i % 2 else (40, 240, 255), width=10)
    return im


def neon_floor(s):
    im = Image.new("RGB", (s, s))
    d = ImageDraw.Draw(im)
    for y in range(s):                           # a gradient both ways: the edges don't match yet
        for x in range(0, s, 8):
            d.rectangle((x, y, x + 7, y), fill=(10 + 60 * x // s, 4, 20 + 70 * y // s))
    for i in range(0, s, s // 8):
        d.line((i + s // 16, 0, i + s // 16, s), fill=(255, 40, 200), width=6)   # off the edges on purpose
        d.line((0, i + s // 16, s, i + s // 16), fill=(40, 240, 255), width=6)
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--many", type=int, default=0)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    bold_ball(1536, 1024, 1).save(os.path.join(a.out, "ball_easy_rainbow_blocks.png"))
    fine_ball(1024, 1024, 2).save(os.path.join(a.out, "ball_hard_confetti.png"))
    neon_back(1536, 1024).save(os.path.join(a.out, "room_neon_back.png"))
    neon_floor(1024).save(os.path.join(a.out, "room_neon_floor.png"))
    pastel(1536, 1024).save(os.path.join(a.out, "DALL-E 2026-10-31 12.00.00 - a pastel planet.png"))
    with open(os.path.join(a.out, "manifest.csv"), "w") as f:
        f.write("file,kind,name,level,title,room\n")
        f.write('"DALL-E 2026-10-31 12.00.00 - a pastel planet.png",ball,pastel,medium,Pastel Planet,neon\n')
        f.write("ball_easy_rainbow_blocks.png,ball,rainbow_blocks,easy,Rainbow Blocks,neon\n")
        f.write("ball_hard_confetti.png,ball,confetti,hard,Confetti,\n")
        f.write("room_neon_back.png,back,neon,,Neon Arcade,\n")
        f.write("room_neon_floor.png,floor,neon,,,\n")
    for i in range(a.many):
        bold_ball(768, 512, 10 + i).save(os.path.join(a.out, f"ball_{['easy', 'medium', 'hard', 'expert'][i % 4]}_sample_{i:02d}.png"))
    print(f"sample art in {a.out}")


if __name__ == "__main__":
    main()
