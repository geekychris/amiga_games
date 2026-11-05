#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Make the OS4 launcher's picture view files: one picture and one
description per game, named after the program (DH1:LauncherPics/<name>.png
and <name>.txt; tools/install_os4_games.py copies them to the HDF).

The picture is a screenshot from the game's screenshots/ folder (an OS4
one, gameplay before title screens), or a PICK override below, letterboxed
into PIC_W x PIC_H on black. The description is the manifest's.

    python3 tools/launcher_pictures.py [--out dist/launcher_pics] [--sheet contact.png]
"""
import argparse
import glob
import json
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
EXAMPLES = os.path.dirname(HERE)
PIC_W, PIC_H = 320, 240                 # launcher/main.c: PIC_W, PIC_H

# a chosen picture, relative to examples/, where the automatic pick isn't the best
PICK = {
    # frames of the full-screen gameplay videos (the other OS4 shots are of a window on the desktop)
    **{g: g + "/screenshots/os4-fullscreen.png" for g in (
        "block_breaker_3d", "canyon_rail", "drone_courier", "harbour_crane", "marble_tilt",
        "orbit_docking", "planet_chomp", "ridge_runner", "spectral_keep", "tank_duel")},
    "ace_pilot2": "ace_pilot2/screenshots/dogfight_os4.png",
    "boing_twist": "boing_twist/screenshots/mid_move.png",
    "warden_keep": "warden_keep/docs/screen.png",
    "sphere_siege": "sphere_siege/screenshots/play_os4.png",
    "terra_strike": "terra_strike/screenshots/os4-gameplay.png",
}
# words in a file name that mark a title, menu or other non-gameplay screen
NOT_PLAY = ("title", "menu", "window", "briefing", "result", "import", "select", "demo")
SITE_THUMBS = os.path.join(EXAMPLES, "..", "..", "geekychris-site", "site", "static", "images", "games")


def pick(gid):
    if gid in PICK:
        return os.path.join(EXAMPLES, PICK[gid])
    shots = sorted(glob.glob(os.path.join(EXAMPLES, gid, "screenshots", "*.png")))
    os4 = [s for s in shots if "os4" in os.path.basename(s)]
    for group in (os4, shots):
        play = [s for s in group if not any(w in os.path.basename(s) for w in NOT_PLAY)]
        if play or group:
            return (play or group)[0]
    thumb = os.path.join(SITE_THUMBS, gid + ".png")
    return thumb if os.path.exists(thumb) else None


def fit(path):
    im = Image.open(path).convert("RGB")
    s = min(PIC_W / im.width, PIC_H / im.height)
    w, h = max(1, round(im.width * s)), max(1, round(im.height * s))
    im = im.resize((w, h), Image.LANCZOS if s < 1 else Image.NEAREST if s >= 2 else Image.BICUBIC)
    out = Image.new("RGB", (PIC_W, PIC_H))
    out.paste(im, ((PIC_W - w) // 2, (PIC_H - h) // 2))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", default=os.path.join(EXAMPLES, "dist", "release"))
    ap.add_argument("--out", default=os.path.join(EXAMPLES, "dist", "launcher_pics"))
    ap.add_argument("--sheet", help="also write a contact sheet of every picture")
    opt = ap.parse_args()

    games = [g for g in json.load(open(os.path.join(opt.release, "manifest.json")))["games"]
             if "amigaos4" in g["packages"]]
    os.makedirs(opt.out, exist_ok=True)
    made, missing = [], []
    for g in sorted(games, key=lambda g: g["title"].lower()):
        src = pick(g["id"])
        # Latin-1: the Amiga's text is ISO 8859-1
        desc = g.get("description", "").replace("—", "-").replace("–", "-")
        with open(os.path.join(opt.out, g["id"] + ".txt"), "w", encoding="latin-1", errors="replace") as fh:
            fh.write(desc + "\n")
        if not src:
            missing.append(g["id"])
            continue
        pic = fit(src)
        pic.save(os.path.join(opt.out, g["id"] + ".png"), optimize=True)
        made.append((g["id"], pic, os.path.relpath(src, EXAMPLES)))
        print(f"{g['id']:<18} {os.path.relpath(src, EXAMPLES)}")
    if missing:
        print("no picture:", " ".join(missing), file=sys.stderr)
    if opt.sheet and made:
        cols = 6
        rows = (len(made) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * (PIC_W // 2 + 4), rows * (PIC_H // 2 + 4)), (40, 40, 40))
        for i, (_gid, pic, _src) in enumerate(made):
            sheet.paste(pic.resize((PIC_W // 2, PIC_H // 2)),
                        ((i % cols) * (PIC_W // 2 + 4), (i // cols) * (PIC_H // 2 + 4)))
        sheet.save(opt.sheet)
    print(f"{len(made)} pictures, {len(games)} descriptions -> {opt.out}")


if __name__ == "__main__":
    main()
