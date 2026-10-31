#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Install every AmigaOS 4 game into the OS4 dev HDF and its launcher menu.

Reads dist/release/ (made by tools/package_release.py): unpacks each
<id>-amigaos4.zip drawer (program + data) to DH1:Games/<id>/, writes
launcher/launcher.list from the manifest and copies both it and
launcher/launcher_os4 (as DH1:launcher) to the HDF.

QEMU must not be running: the HDF is written directly with xdftool.

    python3 tools/package_release.py && python3 tools/install_os4_games.py
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
EXAMPLES = os.path.dirname(HERE)

# games that take FULLSCREEN (a screen of their own; F/F10 toggles a window)
FULLSCREEN = {"planet_chomp", "rolling_steel", "spectral_keep", "terra_strike",
              "stack_tower", "lander_3d", "boing_pinball", "scairy_fun",
              "block_breaker_3d", "harbour_crane", "ridge_runner", "tank_duel",
              "marble_tilt", "orbit_docking", "drone_courier", "canyon_rail"}
DEMOS = {"boing_ball", "starfield", "plasma"}
# programs on DH1: that aren't games but belong in the menu
TOOLS = [("Masterwork", "DH1:masterwork"), ("Terrain Test", "DH1:terrain_test"),
         ("Organizer", "DH1:organizer"), ("TrapezePDF", "DH1:TrapezePDF"),
         ("NetSurf", "DH1:NetSurf/NetSurf")]
MAX_APPS, LIST_BYTES = 64, 4095        # launcher/main.c: MAX_APPS, load_list's buffer


def launcher_list(games):
    def line(g):
        full = " FULLSCREEN" if g["id"] in FULLSCREEN else ""
        return f"{g['title']}|DH1:Games/{g['id']}/{g['id']}{full}"
    by_title = lambda g: g["title"].lower()
    groups = [("3D games", [g for g in games if g["id"] in FULLSCREEN]),
              ("Arcade and classic games", [g for g in games if g["id"] not in FULLSCREEN | DEMOS]),
              ("Demos", [g for g in games if g["id"] in DEMOS])]
    out = ["# AmigaOS 4 launcher list: Title|program. Programs that aren't",
           "# installed are left out of the menu. Deploy beside DH1:launcher.",
           "# Arguments may follow the program: FULLSCREEN gives a game a screen of",
           "# its own (F or F10 switches back to a window while playing).",
           "# Games live in DH1:Games/<id>/ (the release zips' drawers, with their data).",
           "# Written by tools/install_os4_games.py from dist/release/manifest.json."]
    for head, gs in groups:
        out.append("# " + head)
        out += [line(g) for g in sorted(gs, key=by_title)]
    out.append("# Tools")
    out += [f"{t}|{p}" for t, p in TOOLS]
    text = "\n".join(out) + "\n"
    entries = sum(1 for l in out if not l.startswith("#"))
    if entries > MAX_APPS or len(text) > LIST_BYTES:
        sys.exit(f"launcher.list too big for the launcher: {entries} entries, {len(text)} bytes")
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hdf", default=os.path.expanduser(os.environ.get("OS4_DEV_HDF", "~/AmigaOS4/amigaos4-dev.hdf")))
    ap.add_argument("--release", default=os.path.join(EXAMPLES, "dist", "release"))
    ap.add_argument("--list-only", action="store_true", help="only write launcher/launcher.list")
    opt = ap.parse_args()

    m = json.load(open(os.path.join(opt.release, "manifest.json")))
    games = [g for g in m["games"] if "amigaos4" in g["packages"]]
    list_path = os.path.join(EXAMPLES, "launcher", "launcher.list")
    with open(list_path, "w") as fh:
        fh.write(launcher_list(games))
    print(f"{len(games)} games -> {list_path}")
    if opt.list_only:
        return
    if subprocess.run(["pgrep", "-f", f"qemu-system-ppc.*{opt.hdf}"], capture_output=True).returncode == 0:
        sys.exit("QEMU has the HDF open; stop the emulator first")

    stage = tempfile.mkdtemp(prefix="os4games")
    try:
        for g in games:
            with zipfile.ZipFile(os.path.join(opt.release, f"{g['id']}-amigaos4.zip")) as z:
                z.extractall(stage)
        listing = subprocess.run(["xdftool", "-r", opt.hdf, "open", "part=0", "+", "list"],
                                 capture_output=True, text=True).stdout
        have = {l.split()[0] for l in listing.splitlines()[1:] if l.startswith("  ") and not l.startswith("   ")}
        cmd = ["xdftool", "-f", opt.hdf, "open", "part=0"]
        if "Games" in have:
            cmd += ["+", "delete", "Games", "all"]     # a clean drawer: no files left from old builds
        cmd += ["+", "makedir", "Games"]
        for g in sorted(os.listdir(stage)):
            for root, _dirs, files in os.walk(os.path.join(stage, g)):
                rel = "Games/" + os.path.relpath(root, stage)
                cmd += ["+", "makedir", rel]
                for f in sorted(files):
                    cmd += ["+", "write", os.path.join(root, f), f"{rel}/{f}"]
        for name in ("launcher.list", "launcher"):
            if name in have:
                cmd += ["+", "delete", name]       # xdftool won't overwrite, even with -f
        cmd += ["+", "write", list_path, "launcher.list",
                "+", "write", os.path.join(EXAMPLES, "launcher", "launcher_os4"), "launcher"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            sys.exit((r.stdout + r.stderr)[-2000:])
    finally:
        shutil.rmtree(stage)
    print(f"installed {len(games)} games in DH1:Games/ and the launcher on {opt.hdf}")


if __name__ == "__main__":
    main()
