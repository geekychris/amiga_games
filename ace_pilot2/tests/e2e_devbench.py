#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Ace Pilot 2 on a live Amiga, driven through devbench MCP.

Launch; the in-game self-test (incl. the long autopilot campaign, which
must match the host's hash); the title (the mission chosen with injected
keys) and the briefing; the throttle, the stick, the bank and the guns
with injected keys, checked through the bridge variables; the cockpit
view; a practice Wasp shot down; a dogfight; the autopilot flying the
dawn patrol to its end and the result; a crash into the ground losing an
aeroplane; the help pages; timings; Esc to the title; stopping.

  python3 tests/e2e_devbench.py --url http://localhost:3001/mcp --no-build     (classic Amiga, deployed already)
  python3 tests/e2e_devbench.py --os4 local --url http://localhost:3010/mcp --launch RAM:ace_pilot2
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "amiga3d"))
from devbench_e2e import E2E  # noqa: E402

NAME = "ace_pilot2"
A, Z, V = "20", "31", "34"              # throttle up, throttle down, view
ap = argparse.ArgumentParser()
ap.add_argument("--url", default=os.environ.get("DEVBENCH_MCP", "http://localhost:3001/mcp"))
ap.add_argument("--no-build", action="store_true")
ap.add_argument("--os4", default="", help="the OS4 emulator's host (ssh + QEMU's monitor), or 'local'")
ap.add_argument("--launch", default="", help="the Amiga command that starts the game")
ap.add_argument("--shots", default="e2e_shots")
ap.add_argument("--remote", default="", help="ssh host of a devbench on another machine (to fetch its screenshots)")
ap.add_argument("--skip-selftest", action="store_true")
opt = ap.parse_args()

t = E2E(NAME, opt.url, opt.shots, opt.remote)


def local_qemu_shot(name, port=2348):
    """an OS4 screenshot from the QEMU monitor on this machine"""
    import socket
    from PIL import Image
    t.hook("pause", "on")
    ppm = os.path.abspath(os.path.join(opt.shots, f"{name}.ppm"))
    os.makedirs(opt.shots, exist_ok=True)
    with socket.create_connection(("127.0.0.1", port), 5) as s:
        for cmd in ("mouse_move 2 2", "mouse_move -2 -2", f"screendump {ppm}"):
            s.sendall((cmd + "\n").encode())
            time.sleep(0.6)
    time.sleep(1)
    t.hook("pause", "off")
    dst = os.path.join(opt.shots, f"{name}.png")
    if os.path.exists(ppm):
        Image.open(ppm).save(dst)
        os.remove(ppm)
        print(f"  shot  {dst}")
    return dst


if opt.os4 == "local":
    t.activate = True
    shot = local_qemu_shot
else:
    shot = (lambda n: t.qemu_shot(n, opt.os4)) if opt.os4 else t.shot


def num(v, default=0):
    return default if v is None else v


t.step("build, deploy, launch")
if not opt.os4 and not opt.no_build:
    out = t.tool("amiga_run", project=f"examples/{NAME}")
    print("  " + out.replace("\n", "\n  "))
    t.check("Build: OK" in out and "Deploy: OK" in out, "cross-compiled and deployed")
if NAME not in t.tool("amiga_list_clients"):
    t.tool("amiga_launch", command=opt.launch or (f"RAM:{NAME}/{NAME}" if opt.os4 else f"Dropbox:Dev/{NAME}"))
t.check(bool(t.wait(lambda: NAME in t.tool("amiga_list_clients"), 20)), "registered with the bridge")
t.check(bool(t.wait(lambda: all(f"  {h}:" in t.tool("amiga_list_hooks", client=NAME)
                                for h in ("start", "autoplay", "spawn", "view", "next", "selftest", "pause")), 15)),
        "hooks registered")

if not opt.skip_selftest:
    t.step("in-game self-test")
    t0 = time.time()
    print("  " + t.hook("selftest"))
    res = t.wait_log(t0, "SELFTEST", 900)
    print("  " + (res[-1] if res else "no SELFTEST result"))
    t.check(bool(res) and " 0 failed" in res[-1], "the tests pass inside the game (the long campaign matches the host's hash)")

t.step("title: choose a mission with keys, the briefing")
t.hook("title")
t.check(t.wait(lambda: t.var("state") == 0), "title")
time.sleep(3)
shot("00_title")
t.key(t.UP)
t.key(t.UP)
t.check(t.wait(lambda: t.var("start_mission") == 3), "UP twice: mission 3")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: the briefing")
t.check(t.var("mission") == 3, "for mission 3, the escort")
time.sleep(1)
shot("01_briefing")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 2), "fire again: flying")
time.sleep(2)
shot("02_escort_chase")

t.step("throttle, stick, bank, guns with keys")
t.hook("start", "1")
time.sleep(0.5)
th0 = num(t.var("throttle"), 100)
t.key(Z, 1.0)
t.check(t.wait(lambda: num(t.var("throttle"), 100) < th0 - 30), "Z: throttle back")
t.key(A, 1.5)
t.check(t.wait(lambda: num(t.var("throttle")) == 100), "A: throttle open")
p0 = num(t.var("pitch"))
t.key(t.DOWN, 0.5)
t.check(t.wait(lambda: num(t.var("pitch")) > p0 + 800), "DOWN (stick back): the nose comes up")
p1 = num(t.var("pitch"))
t.key(t.UP, 0.9)
t.check(t.wait(lambda: num(t.var("pitch")) < p1 - 800), "UP (stick forward): the nose goes down")
y0 = t.var("yaw")
t.key(t.RIGHT, 0.5)
t.check(t.wait(lambda: num(t.var("roll")) > 1500 or t.var("yaw") != y0, 3), "RIGHT: banks right and turns")
time.sleep(1.5)
s0 = num(t.var("shots"))
t.key(t.SPACE, 0.5)
t.check(t.wait(lambda: num(t.var("shots")) > s0), "SPACE: the guns fire")
shot("03_chase_view")
t.key(V)
t.check(t.wait(lambda: t.var("view") == 1), "V: the cockpit")
time.sleep(1)
shot("04_cockpit_view")
t.key(V)
t.check(t.wait(lambda: t.var("view") == 0), "V again: the chase view")

t.step("a practice Wasp")
t.hook("start", "1")
time.sleep(0.5)
t.hook("spawn", "wasp 150")
time.sleep(0.3)
t.key(t.SPACE, 2.5)
t.check(t.wait(lambda: num(t.var("kills")) >= 1, 6), "a Wasp dead ahead: shot down")
time.sleep(0.5)
shot("05_wasp_down")

t.step("a dogfight")
t.hook("start", "8")
t.hook("autoplay", "on")
t.check(t.wait(lambda: num(t.var("hits")) > 0 or num(t.var("hp"), 30) < 30, 90), "the Grey Falcon engaged")
shot("06_dogfight")
t.hook("view", "cockpit")
time.sleep(1.5)
shot("07_dogfight_cockpit")
t.hook("view", "chase")

t.step("the autopilot flies the dawn patrol")
t.hook("start", "1")
t.hook("autoplay", "on")
t.check(t.wait(lambda: num(t.var("phase")) == 2, 300), "three markers and home: mission complete")
t.check(t.wait(lambda: t.var("state") == 3, 20), "the result")
time.sleep(1)
shot("08_mission_result")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: the next briefing")
t.check(t.var("mission") == 2, "mission 2 next")

t.step("into the ground")
t.hook("start", "2")
lives = t.var("lives")
t.key(t.UP, 6)
t.check(t.wait(lambda: num(t.var("phase")) == 3, 30), "stick forward to the ground: mission failed")
t.check(t.wait(lambda: t.var("state") == 3, 20), "the result")
shot("09_crashed")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: the briefing again")
t.check(t.var("lives") == num(lives) - 1 and t.var("mission") == 2, "an aeroplane lost, the same mission")

t.step("help pages")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 1), "HELP: page 1")
shot("10_help")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 2), "HELP again: controls")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 0), "and closed")

t.step("timings")
t.hook("start", "7")
t.hook("autoplay", "on")
time.sleep(10)
print(f"  mission 7 (Kessin): fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, polys {t.var('polys')}")
f7 = num(t.var("fps"))
t.hook("start", "8")
t.hook("autoplay", "on")
time.sleep(10)
print(f"  mission 8 (dogfight): fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, polys {t.var('polys')}")
t.check(min(f7, num(t.var("fps"))) >= 20, "20+ fps")

t.step("Esc to the title, stop")
t.key(t.ESC)
t.check(t.wait(lambda: t.var("state") == 0), "Esc: the title")
print("  " + t.tool("amiga_stop_client", name=NAME))
t.check(t.wait(lambda: NAME not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
