#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Boing Pinball on a live Amiga, driven through devbench MCP.

Build, deploy and launch; the in-game self-test; the title (choosing a
table with injected keys); a game started, the plunger pulled and the
flippers flipped with injected keys, checked through the bridge
variables; the top lanes, multiball and the tilt through the rules'
hooks; the bot playing; a game played out to game over; every table;
the help pages; screenshots; timings; stopping the client.

  python3 tests/e2e_devbench.py [--url http://localhost:3001/mcp] [--no-build] [--remote HOST]
  python3 tests/e2e_devbench.py --os4 HOST --url http://localhost:13002/mcp
        (AmigaOS 4: the binary must be in RAM:boing_pinball/ already; screenshots from QEMU)
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "amiga3d"))
from devbench_e2e import E2E  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--url", default=os.environ.get("DEVBENCH_MCP", "http://localhost:3001/mcp"))
ap.add_argument("--no-build", action="store_true")
ap.add_argument("--os4", default="", help="the OS4 emulator's host (screenshots via ssh + QEMU's monitor)")
ap.add_argument("--launch", default="", help="the Amiga command that starts the game (default Dropbox:Dev/boing_pinball)")
ap.add_argument("--shots", default="e2e_shots")
ap.add_argument("--remote", default="", help="ssh host of a devbench on another machine (to fetch its screenshots)")
opt = ap.parse_args()

C = "boing_pinball"
LSHIFT, RSHIFT = "60", "61"
t = E2E(C, opt.url, opt.shots, opt.remote)
shot = (lambda n: t.qemu_shot(n, opt.os4)) if opt.os4 else t.shot
launch = opt.launch or ("RAM:boing_pinball/boing_pinball" if opt.os4 else "Dropbox:Dev/boing_pinball")

t.step("build, deploy, launch")
if not opt.os4 and not opt.no_build:
    out = t.tool("amiga_run", project="examples/boing_pinball")
    print("  " + out.replace("\n", "\n  "))
    t.check("Build: OK" in out and "Deploy: OK" in out, "cross-compiled and deployed")
if C not in t.tool("amiga_list_clients"):
    t.tool("amiga_launch", command=launch)
t.check(bool(t.wait(lambda: C in t.tool("amiga_list_clients"), 20)), "registered with the bridge")
t.check(bool(t.wait(lambda: all(f"  {h}:" in t.tool("amiga_list_hooks", client=C) for h in ("start", "place", "launch", "event", "selftest")), 15)),
        "hooks registered")

t.step("in-game self-test")
t0 = time.time()
print("  " + t.hook("selftest"))
res = t.wait_log(t0, "SELFTEST", 180)
print("  " + (res[-1] if res else "no SELFTEST result"))
t.check(bool(res) and " 0 failed" in res[-1], "the tests pass inside the game (physics, rules, every table)")

t.step("the title: the bot plays the demo; choosing a table")
t.hook("title")
t.check(t.wait(lambda: t.var("state") == 0), "title")
t.check(t.wait(lambda: (t.var("pstate") or 0) in (1, 2), 5), "the demo game is running")
time.sleep(1)
shot("00_title")
table0 = t.var("table")
t.key(t.RIGHT)
t.check(t.wait(lambda: t.var("table") == (table0 + 1) % 3), "RIGHT: the next table")
t.key(t.LEFT)
t.check(t.wait(lambda: t.var("table") == table0), "LEFT: back")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: a game")
t.check(t.wait(lambda: t.var("ball") == 1 and t.var("ball_st") == 1), "ball 1 in the plunger lane")

t.step("the plunger and the flippers, with keys")
t.key(t.SPACE, 0.9)                                 # pull back, let go
t.check(t.wait(lambda: t.var("ball_st") == 0 and (t.var("ball_y") or 0) > 300, 4), f"launched up the lane (y {t.var('ball_y')} mm)")
time.sleep(1.2)
shot("01_launched")
for code, var, name in ((LSHIFT, "flip_l", "LEFT SHIFT"), (RSHIFT, "flip_r", "RIGHT SHIFT")):
    t.tool("amiga_input_key", rawkey=code, direction="down")
    t.check(t.wait(lambda: (t.var(var) or 0) > 45, 3), f"{name} raises the flipper ({t.var(var)} degrees)")
    t.tool("amiga_input_key", rawkey=code, direction="up")
    t.check(t.wait(lambda: (t.var(var) or 0) < 2, 3), f"and lets it fall back ({t.var(var)})")

t.step("the rules: lanes, multiball, the tilt")
for lane in ("laneA", "laneB", "laneC"):
    t.hook("event", lane)
t.check(t.wait(lambda: t.var("bonus_x") == 2, 3), "all three top lanes: bonus 2x")
s0 = t.var("score")
for _ in range(3):
    t.hook("event", "ramp")
t.check(t.wait(lambda: t.var("multiball") == 1, 3), "three ramps: multiball")
t.check(t.wait(lambda: (t.var("nballs") or 0) >= 3, 6), f"three balls on the table ({t.var('nballs')})")
t.check((t.var("score") or 0) > s0, "the ramps scored")
time.sleep(1)
shot("02_multiball")
for _ in range(6):
    t.key(t.UP, 0.05)
t.check(t.wait(lambda: (t.var("tilt_warn") or 0) >= 1, 3), f"quick nudges with UP: a tilt warning ({t.var('tilt_warn')})")

t.step("the bot plays, then the game is played out")
t.hook("bot", "on")
time.sleep(8)
t.check((t.var("score") or 0) > s0, f"the bot scores ({t.var('score')})")
t0 = time.time()
deadline = time.time() + 240
last = 0
while time.time() < deadline and t.var("state") != 2:
    time.sleep(3)
    if time.time() - last > 30:
        print("  " + t.hook("status"), "| ball_st", t.var("ball_st"), "y", t.var("ball_y"))
        last = time.time()
t.check(t.var("state") == 2, f"game over ({int(time.time() - t0)} s)")
res = t.wait_log(t0, "GAMEOVER", 3)
print("  " + (res[-1] if res else "no GAMEOVER"))
shot("03_game_over")

t.step("every table")
for name, idx, sn in (("deep", 1, "04_deep_dive"), ("foundry", 2, "05_robot_foundry"), ("comet", 0, "06_comet_tour")):
    print("  " + t.hook("start", name))
    t.hook("bot", "on")
    time.sleep(7)
    t.check(t.var("table") == idx and t.var("state") == 1, f"{name}: playing")
    shot(sn)
t.hook("bot", "off")

t.step("help")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 1), "HELP: the controls")
shot("07_help")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 2), "HELP again: the rules")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 0), "closed")

t.step("timings")
t.hook("bot", "on")
time.sleep(4)
print(f"  fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, substeps {t.var('substeps')}")
t.check((t.var("fps") or 0) >= 20, "20+ fps")

t.step("stop")
print("  " + t.tool("amiga_stop_client", name=C))
t.check(t.wait(lambda: C not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
