#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""import_art.py - bring generated art (ChatGPT's 1536 x 1024 or 1024 x 1024
PNGs, or anything else) into Boing Twist: ball pictures and rooms.

  python3 tools/import_art.py IN_DIR [--manifest art.csv] [--seam 3] [--poles 0]
          [--sides crop|colour|off] [--repeat 4] [--contact contact.png]
          [--game DIR] [--no-make] [--dry-run]

Files in IN_DIR are recognised by name:

  ball_<level>_<name>.png      a ball picture; level: easy medium hard expert
                               (the lowest level it suits)
  room_<name>_back.png         a room's back wall (landscape, 3:2 is fine)
  room_<name>_floor.png        its floor (square; made to tile)

or by a manifest (CSV with a header row; any file name, e.g. ChatGPT's own):

  file,kind,name,level,title,room
  "DALL-E 2026-10-31 a pastel planet.png",ball,pastel,medium,Pastel Planet,neon
  neon wall.png,back,neon,,Neon Arcade,
  neon floor.png,floor,neon,,,

  kind: ball, back or floor. title: shown in the game (default: the name).
  room (ball pictures): the room chosen with it (default: a room of the same
  name, if this import has one).

A ball picture is cut to 2:1 keeping the middle (a 3:2 picture loses a
sixth at the top and at the bottom; a square one a quarter), its left and
right edges cross-faded over --seam percent of the width so the join at the
back of the ball doesn't show, optionally softened towards the poles
(--poles: the fraction of the height at each end that fades to its row's
average colour), and written to images/<n>_<name>.jpg (2048 x 1024) with
its line in images/titles.txt. Importing the same name again replaces it.

A room's back wall and floor go to themes/src/<name>_back.png and
<name>_floor.png (the floor's edges cross-faded so it tiles), the side
walls (--sides crop: the back wall's outer thirds, darkened; colour: its
average colour, darkened; off: none), and themes/<name>.theme naming them,
the floor tiled --repeat times across.

Then `make data` (unless --no-make) turns everything into the .btx files
the game reads, and a contact sheet of the results (the ball pictures flat
and on a ball, front and back, so the seam can be checked; the rooms) is
written to --contact (default import_contact.png in IN_DIR).
"""
import argparse
import csv
import math
import os
import re
import subprocess
import sys

from PIL import Image, ImageChops, ImageDraw, ImageOps, ImageStat

HERE = os.path.dirname(os.path.abspath(__file__))
LEVELS = ["easy", "medium", "hard", "expert"]
EXTS = (".png", ".jpg", ".jpeg", ".webp", ".tif", ".tiff")
BALL_SIZE = (2048, 1024)
ROOM_SIZE = 1024


def slug(s):
    s = re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")
    return s[:24] or "art"


def title_of(name):
    return name.replace("_", " ").title()


# ---------------- ball pictures ----------------

def to_2_1(im):
    """the middle 2:1 of the picture"""
    w, h = im.size
    if w >= 2 * h:
        nw = 2 * h
        x = (w - nw) // 2
        return im.crop((x, 0, x + nw, h))
    nh = w // 2
    y = (h - nh) // 2
    return im.crop((0, y, w, y + nh))


def wrap_seam(im, pct):
    """cross-fade so the right edge runs on into the left: the picture is
    made pct percent wider, and the extra strip (what lies past the right
    edge) is faded into the left edge"""
    W, H = BALL_SIZE
    n = max(2, int(W * pct / 100.0))
    wide = im.resize((W + n, H), Image.LANCZOS)
    extra = wide.crop((W, 0, W + n, H))             # continues the right edge
    left = wide.crop((0, 0, n, H))
    ramp = Image.linear_gradient("L").rotate(90, expand=True).resize((n, H))   # 0 at the left .. 255
    ramp = ImageOps.invert(ramp)                     # 255 at the left: all extra there
    out = wide.crop((0, 0, W, H))
    out.paste(Image.composite(extra, left, ramp), (0, 0))
    return out


def soften_poles(im, frac):
    if frac <= 0:
        return im
    W, H = im.size
    n = max(1, int(H * frac))
    px = im.load()
    for rows in (range(n), range(H - 1, H - 1 - n, -1)):
        for k, y in enumerate(rows):
            row = [px[x, y] for x in range(W)]
            avg = tuple(sum(c[i] for c in row) // W for i in range(3))
            t = k / n                                    # 0 at the pole: all average
            for x in range(W):
                px[x, y] = tuple(round(avg[i] * (1 - t) + row[x][i] * t) for i in range(3))
    return im


def ball_picture(src, seam, poles):
    im = Image.open(src).convert("RGB")
    im = to_2_1(im)
    im = wrap_seam(im, seam) if seam > 0 else im.resize(BALL_SIZE, Image.LANCZOS)
    return soften_poles(im, poles)


def seam_error(im):
    """mean difference between the right and left edge columns (0..255)"""
    W, H = im.size
    a = im.crop((W - 1, 0, W, H))
    b = im.crop((0, 0, 1, H))
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3


# ---------------- rooms ----------------

def tileable(im, pct=8):
    """cross-fade opposite edges (both ways) so copies side by side join"""
    S = ROOM_SIZE
    n = max(2, int(S * pct / 100.0))
    big = im.resize((S + n, S + n), Image.LANCZOS)
    ramp_x = ImageOps.invert(Image.linear_gradient("L").rotate(90, expand=True).resize((n, S + n)))
    out = big.crop((0, 0, S, S + n))
    out.paste(Image.composite(big.crop((S, 0, S + n, S + n)), big.crop((0, 0, n, S + n)), ramp_x), (0, 0))
    ramp_y = ImageOps.invert(Image.linear_gradient("L").resize((S, n)))
    top = out.crop((0, 0, S, n))
    extra = out.crop((0, S, S, S + n))
    out2 = out.crop((0, 0, S, S))
    out2.paste(Image.composite(extra, top, ramp_y), (0, 0))
    return out2


def darken(im, k):
    return Image.eval(im, lambda v: int(v * k))


def avg_colour(im, k=1.0):
    r, g, b = (int(v * k) for v in ImageStat.Stat(im.convert("RGB")).mean[:3])
    return f"{r:02X}{g:02X}{b:02X}"


def room(name, title, back_src, floor_src, sides, repeat, game, dry):
    src_dir = os.path.join(game, "themes", "src")
    os.makedirs(src_dir, exist_ok=True)
    out = {"name": name, "title": title}
    back = Image.open(back_src).convert("RGB") if back_src else None
    floor = Image.open(floor_src).convert("RGB") if floor_src else None
    lines = [f"# made by tools/import_art.py from {os.path.basename(back_src or '')} {os.path.basename(floor_src or '')}".rstrip(),
             f"name = {title}"]
    base = back or floor
    lines.append(f"background = {avg_colour(base, 0.35) if base else '101018'}")
    lines.append("shadow = floor 50")
    if back:
        back = back.resize((ROOM_SIZE, ROOM_SIZE * 2 // 3), Image.LANCZOS)
        if not dry:
            back.save(os.path.join(src_dir, f"{name}_back.png"))
        lines.append(f"back = {avg_colour(back)} none 0 {name}_back.btx")
        out["back"] = back
    else:
        lines.append("back = 303040 none 0")
    if floor:
        w, h = floor.size
        s = min(w, h)
        floor = tileable(floor.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s)))
        if not dry:
            floor.save(os.path.join(src_dir, f"{name}_floor.png"))
        lines.append(f"floor = {avg_colour(floor)} none 0 {name}_floor.btx {repeat}")
        out["floor"] = floor
    else:
        lines.append("floor = 202028 none 0")
    if back and sides == "crop":
        third = back.width // 3
        for side, box in (("left", (0, 0, third, back.height)), ("right", (back.width - third, 0, back.width, back.height))):
            wall = darken(back.crop(box), 0.55)
            if not dry:
                wall.save(os.path.join(src_dir, f"{name}_{side}.png"))
            lines.append(f"{side} = {avg_colour(wall)} none 0 {name}_{side}.btx")
            out[side] = wall
        lines.append(f"ceiling = {avg_colour(back.crop((0, 0, back.width, back.height // 6)), 0.4)} none 0")
    elif base and sides == "colour":
        c = avg_colour(base, 0.5)
        lines += [f"left = {c} none 0", f"right = {c} none 0", f"ceiling = {avg_colour(base, 0.3)} none 0"]
    else:
        lines += ["left = off", "right = off", "ceiling = off"]
    if not dry:
        with open(os.path.join(game, "themes", f"{name}.theme"), "w") as f:
            f.write("\n".join(lines) + "\n")
    return out


# ---------------- titles.txt ----------------

def next_number(images):
    n = 0
    for f in os.listdir(images):
        m = re.match(r"(\d+)_", f)
        if m:
            n = max(n, int(m.group(1)))
    return n + 1


def existing_stem(images, name):
    for f in sorted(os.listdir(images)):
        stem, ext = os.path.splitext(f)
        if ext.lower() in EXTS and re.fullmatch(r"\d+_" + re.escape(name), stem):
            return stem
    return None


def set_title(images, stem, title, level, room_name):
    path = os.path.join(images, "titles.txt")
    lines = open(path, encoding="utf-8").read().splitlines() if os.path.exists(path) else []
    entry = f"{stem} = {title} | {level}" + (f" | {room_name}" if room_name else "")
    for i, l in enumerate(lines):
        if l.split("=", 1)[0].strip() == stem:
            lines[i] = entry
            break
    else:
        lines.append(entry)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


# ---------------- what to import ----------------

def collect(in_dir, manifest):
    balls, rooms = [], {}
    if manifest:
        with open(manifest, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                path = os.path.join(in_dir, r["file"].strip())
                kind = (r.get("kind") or "").strip().lower()
                name = slug(r.get("name") or os.path.splitext(r["file"])[0])
                title = (r.get("title") or "").strip()
                if kind == "ball":
                    lv = (r.get("level") or "easy").strip().lower()
                    balls.append({"src": path, "name": name, "level": lv if lv in LEVELS else "easy",
                                  "title": title or title_of(name), "room": slug(r["room"]) if (r.get("room") or "").strip() else ""})
                elif kind in ("back", "floor"):
                    rm = rooms.setdefault(name, {"title": title_of(name)})
                    rm[kind] = path
                    if title:
                        rm["title"] = title
        return balls, rooms
    for f in sorted(os.listdir(in_dir)):
        stem, ext = os.path.splitext(f)
        if ext.lower() not in EXTS:
            continue
        m = re.fullmatch(r"ball_(easy|medium|hard|expert)_(.+)", stem, re.I)
        if m:
            name = slug(m.group(2))
            balls.append({"src": os.path.join(in_dir, f), "name": name, "level": m.group(1).lower(),
                          "title": title_of(name), "room": ""})
            continue
        m = re.fullmatch(r"room_(.+)_(back|floor)", stem, re.I)
        if m:
            name = slug(m.group(1))
            rooms.setdefault(name, {"title": title_of(name)})[m.group(2).lower()] = os.path.join(in_dir, f)
            continue
        print(f"  skipped {f}: not ball_<level>_<name> or room_<name>_back|floor (use a manifest)")
    return balls, rooms


# ---------------- contact sheet ----------------

def sphere_view(im, lon0, size=200):
    """the picture on a ball, centred on longitude lon0 (degrees), shaded a little"""
    W, H = im.size
    src = im.load()
    out = Image.new("RGB", (size, size), (40, 40, 48))
    px = out.load()
    r = size / 2 - 2
    for y in range(size):
        for x in range(size):
            u, v = (x - size / 2) / r, -(y - size / 2) / r
            d = u * u + v * v
            if d > 1:
                continue
            z = math.sqrt(1 - d)
            lat = math.asin(v)
            lon = math.radians(lon0) + math.atan2(u, z)
            tx = int((lon / (2 * math.pi)) % 1.0 * W) % W
            ty = min(H - 1, int((0.5 - lat / math.pi) * H))
            k = 0.45 + 0.55 * max(0.0, -0.4 * u + 0.5 * v + 0.75 * z)
            c = src[tx, ty]
            px[x, y] = (int(c[0] * k), int(c[1] * k), int(c[2] * k))
    return out


def contact(balls, rooms, path):
    rows = []
    for b in balls:
        im = b["image"]
        tile = Image.new("RGB", (1000, 230), (24, 24, 32))
        tile.paste(im.resize((440, 220)), (5, 5))
        tile.paste(sphere_view(im, 180, 210), (455, 10))         # the picture's middle
        tile.paste(sphere_view(im, 0, 210), (680, 10))           # the seam, front and centre
        d = ImageDraw.Draw(tile)
        d.text((900, 12), b["level"].upper(), fill=(255, 216, 56))
        d.text((900, 30), f"seam {b['seam']:.1f}", fill=(180, 180, 200))
        d.text((10, 210), f"{b['stem']}: {b['title']}" + (f" (room {b['room']})" if b["room"] else ""), fill=(255, 255, 255))
        d.text((680, 214), "<- middle    seam ->", fill=(150, 150, 170))
        rows.append(tile)
    for name, r in rooms.items():
        tile = Image.new("RGB", (1000, 230), (24, 24, 32))
        x = 5
        for k in ("left", "back", "right"):
            if k in r:
                w = 300 if k == "back" else 100
                tile.paste(r[k].resize((w, 150)), (x, 5))
                x += w + 5
        if "floor" in r:
            fl = r["floor"].resize((110, 110))
            for i in range(2):                                    # two side by side: does it tile?
                for j in range(2):
                    tile.paste(fl, (x + 10 + i * 110, 5 + j * 110))
        d = ImageDraw.Draw(tile)
        d.text((10, 210), f"room {name}: {r['title']}", fill=(255, 255, 255))
        rows.append(tile)
    if not rows:
        return None
    sheet = Image.new("RGB", (1000, 235 * len(rows)), (0, 0, 0))
    for i, t in enumerate(rows):
        sheet.paste(t, (0, i * 235))
    sheet.save(path)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("in_dir")
    ap.add_argument("--manifest")
    ap.add_argument("--seam", type=float, default=3.0, help="cross-fade width, percent of the picture (0: none)")
    ap.add_argument("--poles", type=float, default=0.0, help="fraction of the height at each pole softened (e.g. 0.05)")
    ap.add_argument("--sides", choices=["crop", "colour", "off"], default="crop")
    ap.add_argument("--repeat", type=float, default=4, help="floor tiles across the room")
    ap.add_argument("--contact")
    ap.add_argument("--game", default=os.path.dirname(HERE))
    ap.add_argument("--no-make", action="store_true")
    ap.add_argument("--dry-run", action="store_true", help="convert and make the contact sheet, write nothing else")
    a = ap.parse_args()

    images = os.path.join(a.game, "images")
    os.makedirs(images, exist_ok=True)
    balls, rooms = collect(a.in_dir, a.manifest)
    if not balls and not rooms:
        sys.exit("nothing to import")

    made_rooms = {}
    for name, r in rooms.items():
        made_rooms[name] = room(name, r["title"], r.get("back"), r.get("floor"), a.sides, a.repeat, a.game, a.dry_run)
        print(f"room  {name}: themes/{name}.theme")
    n = next_number(images)
    for b in balls:
        im = ball_picture(b["src"], a.seam, a.poles)
        stem = existing_stem(images, b["name"])
        if not stem:
            stem = f"{n}_{b['name']}"
            n += 1
        room_name = b["room"] or (b["name"] if b["name"] in rooms else "")
        b.update(image=im, stem=stem, room=room_name, seam=seam_error(im))
        if not a.dry_run:
            im.save(os.path.join(images, stem + ".jpg"), quality=90)
            set_title(images, stem, b["title"], b["level"], room_name)
        print(f"ball  {stem}: '{b['title']}' {b['level']}" + (f" room {room_name}" if room_name else "")
              + f" (seam difference {b['seam']:.1f})")
    sheet = contact(balls, made_rooms, a.contact or os.path.join(a.in_dir, "import_contact.png"))
    if sheet:
        print(f"contact sheet: {sheet}")
    if not a.dry_run and not a.no_make:
        subprocess.run([sys.executable, os.path.join(HERE, "make_texture.py"), "--all", "--game", a.game], check=True)


if __name__ == "__main__":
    main()
