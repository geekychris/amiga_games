#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Package the games for download: one zip per game per Amiga, each a
drawer named after the game holding the program, the files it opens at run
time and a ReadMe; an LhA of them all per Amiga; and manifest.json, which
the website's /games/ page is made from.

  python3 tools/package_release.py [--out dist/release] [--tag games-YYYY-MM-DD]

Uses the binaries already built (scripts/build-example-68k.sh and
build-example-ppc.sh in amiga_mcp); a game with no binary for an Amiga is
listed in the manifest as not built for it.

Classic Amiga packages are <id>-amiga68k.zip, AmigaOS 4 ones
<id>-amigaos4.zip; the LhAs are amiga_games-68k.lha and amiga_games-os4.lha.
"""
import argparse
import configparser
import datetime
import json
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)                     # the examples directory
REPO = "geekychris/amiga_games"

NEEDS_68K = "An Amiga with a 68020 or better (an A1200, or an accelerated A500/A2000/A4000), AmigaOS 3.0 or later, fast RAM recommended."
NEEDS_68K_FAST = "An Amiga with a 68020 or better and AGA or an RTG card, AmigaOS 3.x. The 3D is drawn in software, so the faster the CPU the better."
NEEDS_OS4 = "AmigaOS 4.1 Final Edition on a PowerPC Amiga, or on QEMU's sam460ex."

# The games not in games.ini (the engine games and the 3D ports), in the
# order the page lists them after the catalogue's. kind: game or demo.
EXTRA = [
    ("stack_tower", "Stack Tower", "A falling-block puzzle wrapped round a tower of sixteen columns: no side walls, and moving a piece turns the tower under it. Marathon, Rising and Dig modes.",
     "Left/right: turn the tower. Up / X: rotate. Z: rotate back. Down: soft drop. Space / fire: hard drop. C: hold. Help: the rules.", NEEDS_68K),
    ("lander_3d", "Lander 3D", "Land on generated terrain on the Moon, on Mars (with wind) and on an asteroid: tip the lander to move, keep thrust on, and touch down gently on a pad.",
     "Space / fire: full thrust. Q / E: throttle down / up. Arrows / WASD / joystick: tip. Help: the rules.", NEEDS_68K),
    ("boing_pinball", "Boing Pinball", "Pinball with the Boing ball on three tables: Comet Tour, Deep Dive and Robot Foundry, seen from the player's end, scrolling with the ball.",
     "Left Shift / Z: left flippers. Right Shift / /: right flippers. Space / Return: plunger. Left, right, up: nudge.", NEEDS_68K),
    ("scairy_fun", "ScAIry Fun", "Ride in a car that drives itself: score for the distance it drives on autopilot, and take the wheel just before it gets something wrong.",
     "Left/right: steer (takes over). Down: brake. Up: accelerate. Space / fire: autopilot on/off. V: view. Help: the rules.", NEEDS_68K),
    ("terra_strike", "Terra Strike", "Lift off from the pad, fly over a wrapping island, and destroy the tanks, buildings and ships; land on the pad again to refuel and repair.",
     "Arrows / joystick: turn, climb, dive. A / Z: throttle. Space / fire: cannon.", NEEDS_68K),
    ("planet_chomp", "Planet Chomp", "A maze chase on a tiny planet: the maze wraps all the way round a sphere. Eat every crumb, dodge the four spooks, and grab a golden key to turn the tables.",
     "Arrows / WASD: steer. Q / E: spin the view. C: whole-planet view. Space / Return: start.", NEEDS_68K_FAST),
    ("rolling_steel", "Rolling Steel", "Roll a steel marble down six floating isometric courses, against one clock that carries over from course to course.",
     "Arrows / keypad / WASD: push the marble. Space / Return: start.", NEEDS_68K_FAST),
    ("spectral_keep", "Spectral Keep", "An isometric flip-screen adventure: three keeps and 24 rooms. Find the relics and carry them to the throne; keys open the red gates.",
     "Arrows / WASD: walk. Space / Return: jump.", NEEDS_68K_FAST),
    ("rotoball", "Rotoball", "A split-screen futuristic ball sport against the computer: steer your skimmer, grab the orb and carry it over the goal line.",
     "W / S: forward / reverse. A / D: strafe. Esc: quit.", NEEDS_68K),
    ("block_breaker_3d", "Block Breaker 3D", "Breakout down a tunnel: move the paddle across the near opening and send the ball to the bricks at the far end. Eight levels.",
     "Arrows / WASD / joystick: paddle. Space / fire: serve. B: autopilot. Help: rules.", NEEDS_68K),
    ("harbour_crane", "Harbour Crane", "Work a gantry crane: swing containers onto a ship that rolls in the swell, and keep her level. Eight levels.",
     "Left/right: trolley. Up/down: winch. Space / fire: grab, let go. B: autopilot.", NEEDS_68K),
    ("ridge_runner", "Ridge Runner", "Hover-bike racing down winding valleys against three rivals: gates, jumps and a boost. Six tracks.",
     "Left/right: steer. Up: throttle. Down: brake. Space / Shift: boost. B: autopilot.", NEEDS_68K),
    ("tank_duel", "Tank Duel", "A tank fight in a walled arena, seen from the turret: scouts, mortars and shielded bulwarks. Shells bounce off the walls. Eight rounds.",
     "Up/down: drive. Left/right: turn. Space / fire: fire. B: autopilot.", NEEDS_68K),
    ("marble_tilt", "Marble Tilt", "Tilt a floating maze to roll a marble to the exit, past holes, gates, bumpers and sand. Eight levels.",
     "Arrows / WASD / joystick: tilt. B: autopilot. Help: rules.", NEEDS_68K),
    ("orbit_docking", "Orbit Docking", "Dock with a spinning space station: match its roll, line up on its axis and come in gently. Six stations.",
     "Cursor keys: move across. W/S: in and out. A/D: roll. B: autopilot.", NEEDS_68K),
    ("drone_courier", "Drone Courier", "Deliver parcels by drone across a city of towers, against the clock, the battery and the wind. Five days.",
     "Up/down: forward/back. Left/right: turn. Space / fire: climb. B: autopilot.", NEEDS_68K),
    ("canyon_rail", "Canyon Rail", "A rail shooter down a canyon: turrets, drones and their bolts, and a guardian at the end of each stage.",
     "Arrows / joystick: sight. Space / fire: fire. B: autopilot.", NEEDS_68K),
]

# where each game's binaries are, if not <id>/<id> and <id>/<id>_os4 (the
# C++ games build both to <id>: keep the PowerPC one as <id>_os4)
BIN_68K = {"planet_chomp": "planet_chomp_68k", "rolling_steel": "rolling_steel_68k", "spectral_keep": "spectral_keep_68k"}
BIN_OS4 = {"planet_chomp": "planet_chomp", "spectral_keep": "spectral_keep", "rolling_steel": "rolling_steel"}
# what the games open at run time, besides what games.ini says (os4: AmigaOS 4 only)
DATA = {
    "stack_tower": (["stack_tower.mod"], ["stack_title.img", "stack_backdrop.img"]),
    "lander_3d": (["lander_3d.mod"], ["lander_title.img", "sky_moon.img", "sky_mars.img", "sky_asteroid.img"]),
    "boing_pinball": (["boing_pinball.mod"], ["playfield_comet.img", "playfield_deep.img", "playfield_foundry.img",
                                              "title_comet.img", "title_deep.img", "title_foundry.img"]),
    "scairy_fun": (["scairy.mod", "scairy.voc"], ["scairy_title.img", "scairy_horizon.img"]),
    "terra_strike": (["terra_strike.mod"], []),
    "ace_pilot": (["blue_danube.mod"], []),
    "orb_hunter": (["../rock_blaster/rock_blaster.mod"], []),
    "orbital_patrol": (["orbital_patrol.mod"], []),
    "pea_shooter_blast": (["music.mod"], []),
    "rolling_steel": (["data"], []),
    "spectral_keep": (["data"], []),
}


def is_elf(path):
    with open(path, "rb") as f:
        return f.read(4) == b"\x7fELF"


def is_hunk(path):
    with open(path, "rb") as f:
        return f.read(4) == b"\x00\x00\x03\xf3"


def catalogue():
    """games.ini, then the extras"""
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(os.path.join(ROOT, "games.ini"))
    games = []
    for gid in ini.sections():
        s = ini[gid]
        games.append({
            "id": gid,
            "title": s.get("title", gid),
            "description": " ".join(s.get("description", "").split()),
            "controls": "; ".join(l.strip() for l in s.get("controls", "").splitlines() if l.strip()),
            "files": s.get("files", "").split(),
            "needs68k": NEEDS_68K,
            "kind": "demo" if gid in ("boing_ball", "starfield", "plasma") else "game",
        })
    for gid, title, desc, controls, needs in EXTRA:
        games.append({"id": gid, "title": title, "description": desc, "controls": controls, "files": [],
                      "needs68k": needs, "kind": "game"})
    return games


def readme(g, arch, files):
    lines = [g["title"], "=" * len(g["title"]), "", g["description"], ""]
    if g["controls"]:
        lines += ["Controls: " + g["controls"], ""]
    if arch == "68k":
        lines += ["Needs: " + g["needs68k"], "",
                  "To run: copy the " + g["id"] + " drawer anywhere (a hard disk, or an",
                  "emulator's shared folder) and start it from a Shell in that drawer",
                  "(there is no Workbench icon):",
                  "  cd " + g["id"], "  " + g["id"], ""]
    else:
        lines += ["Needs: " + NEEDS_OS4, "",
                  "To run: copy the " + g["id"] + " drawer to a disk (for QEMU, into a hardfile",
                  "or a RAM: drawer) and run " + g["id"] + " from a Shell in that drawer:",
                  "  cd " + g["id"], "  " + g["id"], ""]
    if files:
        lines += ["Keep these beside the program: " + ", ".join(files), ""]
    lines += ["Written for Amiga DevBench: https://github.com/geekychris/amiga_mcp",
              "Source: https://github.com/" + REPO + "/tree/main/" + g["id"], "MIT licence."]
    return "\n".join(lines) + "\n"


def package(g, arch, out, stage_root):
    gid = g["id"]
    src = os.path.join(ROOT, gid)
    if arch == "68k":
        prog = os.path.join(src, BIN_68K.get(gid, gid))
        ok = os.path.exists(prog) and is_hunk(prog)
        files = list(g["files"]) + DATA.get(gid, ([], []))[0]
    else:
        prog = os.path.join(src, BIN_OS4.get(gid, gid + "_os4"))
        ok = os.path.exists(prog) and is_elf(prog)
        files = list(g["files"]) + DATA.get(gid, ([], []))[0] + DATA.get(gid, ([], []))[1]
    if not ok:
        return None
    missing = [f for f in files if not os.path.exists(os.path.join(src, f))]
    files = [f for f in files if f not in missing]
    drawer = os.path.join(stage_root, arch, gid)
    os.makedirs(drawer, exist_ok=True)
    shutil.copy2(prog, os.path.join(drawer, gid))
    os.chmod(os.path.join(drawer, gid), 0o755)
    for f in files:
        s = os.path.join(src, f)
        if os.path.isdir(s):
            shutil.copytree(s, os.path.join(drawer, f), dirs_exist_ok=True)
        else:
            shutil.copy2(s, os.path.join(drawer, os.path.basename(f)))
    files = [os.path.basename(f) for f in files]
    with open(os.path.join(drawer, "ReadMe.txt"), "w") as fh:
        fh.write(readme(g, arch, files))
    name = f"{gid}-{'amiga68k' if arch == '68k' else 'amigaos4'}.zip"
    with zipfile.ZipFile(os.path.join(out, name), "w", zipfile.ZIP_DEFLATED) as z:
        for base, _, names in os.walk(drawer):
            for n in sorted(names):
                p = os.path.join(base, n)
                z.write(p, os.path.relpath(p, os.path.dirname(drawer)))
    return {"file": name, "bytes": os.path.getsize(os.path.join(out, name)), "missing": missing}


def make_lha(src, dest):
    """an LhA of every drawer in src: with a local lha that can make one,
    else the cross-compiler's Docker image's (Lhasa, often the local one,
    only unpacks)"""
    drawers = sorted(os.listdir(src))
    local = shutil.which("lha")
    if local and "Lhasa" not in subprocess.run([local], capture_output=True, text=True).stdout:
        subprocess.run([local, "aq", dest] + drawers, cwd=src, check=True)
        return
    out = os.path.dirname(dest)
    subprocess.run(["docker", "run", "--rm", "-v", f"{src}:/src", "-v", f"{out}:/out", "-w", "/src",
                    "amigadev/crosstools:m68k-amigaos", "lha", "aq", "/out/" + os.path.basename(dest)] + drawers,
                   check=True, stdout=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "dist", "release"))
    ap.add_argument("--tag", default="games-" + datetime.date.today().isoformat())
    ap.add_argument("--skip-os4", default="", help="comma-separated games whose AmigaOS 4 build isn't ready to release")
    opt = ap.parse_args()
    skip_os4 = set(filter(None, opt.skip_os4.split(",")))
    if os.path.exists(opt.out):
        shutil.rmtree(opt.out)
    os.makedirs(opt.out)
    stage = os.path.join(opt.out, ".stage")
    games, built = catalogue(), {"68k": 0, "os4": 0}
    for g in games:
        g["packages"] = {}
        for arch in ("68k", "os4"):
            if arch == "os4" and g["id"] in skip_os4:
                continue
            p = package(g, arch, opt.out, stage)
            if p:
                p["url"] = f"https://github.com/{REPO}/releases/download/{opt.tag}/{p['file']}"
                g["packages"]["amiga68k" if arch == "68k" else "amigaos4"] = p
                built[arch] += 1
        g.pop("files")
    for arch, name in (("68k", "amiga_games-68k.lha"), ("os4", "amiga_games-os4.lha")):
        if os.path.isdir(os.path.join(stage, arch)):
            make_lha(os.path.join(stage, arch), os.path.join(os.path.abspath(opt.out), name))
    shutil.rmtree(stage)
    manifest = {"repo": REPO, "tag": opt.tag, "platforms": ["amiga68k", "amigaos4"],
                "bundles": {"amiga68k": f"https://github.com/{REPO}/releases/download/{opt.tag}/amiga_games-68k.lha",
                            "amigaos4": f"https://github.com/{REPO}/releases/download/{opt.tag}/amiga_games-os4.lha"},
                "games": games}
    with open(os.path.join(opt.out, "manifest.json"), "w") as fh:
        json.dump(manifest, fh, indent=1)
    print(f"{len(games)} games: {built['68k']} for the classic Amiga, {built['os4']} for AmigaOS 4 -> {opt.out}")
    for g in games:
        for k, p in g["packages"].items():
            if p["missing"]:
                print(f"  {g['id']} {k}: missing {', '.join(p['missing'])}", file=sys.stderr)
        if not g["packages"]:
            print(f"  {g['id']}: no binary built", file=sys.stderr)


if __name__ == "__main__":
    main()
