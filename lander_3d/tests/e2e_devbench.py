#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Lander 3D on a live Amiga, driven through devbench MCP.

Build, deploy and launch; the in-game self-test; the title with the
autopilot flying the demo; flying with injected keys (thrust, tipping)
checked through the bridge variables; the autopilot landing on a pad and
a crash costing a ship (set up with the place hook); every world; the help
pages; screenshots; stopping the client.

  python3 tests/e2e_devbench.py [--url http://localhost:3001/mcp] [--no-build] [--dev DIR]
  python3 tests/e2e_devbench.py --os4 HOST --url http://localhost:13002/mcp
        (AmigaOS 4: the binary must be in RAM:lander_3d/ already; screenshots from QEMU)
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
ap.add_argument("--launch", default="", help="the Amiga command that starts the game (default Dropbox:Dev/lander_3d)")
ap.add_argument("--shots", default="e2e_shots")
ap.add_argument("--remote", default="", help="ssh host of a devbench on another machine (to fetch its screenshots)")
opt = ap.parse_args()

C = "lander_3d"
t = E2E(C, opt.url, opt.shots, opt.remote)
shot = (lambda n: t.qemu_shot(n, opt.os4)) if opt.os4 else t.shot
launch = opt.launch or ("RAM:lander_3d/lander_3d" if opt.os4 else "Dropbox:Dev/lander_3d")

t.step("build, deploy, launch")
if not opt.os4 and not opt.no_build:
    out = t.tool("amiga_run", project="examples/lander_3d")
    print("  " + out.replace("\n", "\n  "))
    t.check("Build: OK" in out and "Deploy: OK" in out, "cross-compiled and deployed")
if C not in t.tool("amiga_list_clients"):
    t.tool("amiga_launch", command=launch)
t.check(bool(t.wait(lambda: C in t.tool("amiga_list_clients"), 15)), "registered with the bridge")
t.check(all(f"  {h}:" in t.tool("amiga_list_hooks", client=C) for h in ("start", "place", "autopilot", "selftest")),
        "hooks registered")

t.step("in-game self-test")
t0 = time.time()
print("  " + t.hook("selftest"))
res = t.wait_log(t0, "SELFTEST", 120)
print("  " + (res[-1] if res else "no SELFTEST result"))
t.check(bool(res) and " 0 failed" in res[-1], "the tests pass inside the game (the autopilot lands every world)")

t.step("title: the autopilot flies the demo")
t.hook("title")
t.check(t.wait(lambda: t.var("state") == 0), "title")
t.check(t.var("autopilot") == 1, "the autopilot is flying")
time.sleep(1)
shot("00_title")
t.key(t.RIGHT)
t.check(t.wait(lambda: t.var("menu") == 1), "RIGHT: Mars")
t.key(t.LEFT)
t.check(t.wait(lambda: t.var("menu") == 0), "LEFT: the Moon")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: flying")
t.check(t.var("autopilot") == 0 and t.var("ships") == 3, "you are flying, three ships")

t.step("flying with keys")
vs0 = t.var("vs")
t.key(t.SPACE, 1.5)                                   # full thrust for a moment
t.check((t.var("vs") or 0) < vs0, f"thrust slows the descent ({vs0} -> {t.var('vs')} cm/s)")
t.key(t.UP, 0.6)
t.check(t.wait(lambda: (t.var("tilt") or 0) >= 5, 3), f"UP tips it forward ({t.var('tilt')} degrees)")
t.key(t.DOWN, 0.6)
shot("01_flying")

t.step("a landing: the autopilot puts it down on the x1 pad")
t.hook("place", "pad")
t.hook("autopilot", "on")
t0 = time.time()
t.check(t.wait(lambda: t.var("state") == 2, 60), "touched down")
res = t.wait_log(t0, "OUTCOME", 5)
print("  " + (res[-1] if res else "no OUTCOME"))
t.check(t.var("outcome") == 1 and (t.var("score") or 0) > 100, f"landed, scored ({t.var('score')})")
shot("02_landed")
t.hook("autopilot", "off")
t.check(t.wait(lambda: t.var("state") == 1 and t.var("level") == 2, 10), "on to level 2")

t.step("a crash costs a ship")
ships = t.var("ships")
t.hook("place", "high")
t.check(t.wait(lambda: t.var("state") == 2, 30), "fell 60 m with the engine off")
t.check(t.var("outcome") == 3 and t.var("ships") == ships - 1, "crashed: one ship fewer")
shot("03_crashed")

t.step("every world")
for w, name in (("mars", "04_mars"), ("asteroid", "05_asteroid")):
    print("  " + t.hook("start", w + " 3"))
    t.hook("autopilot", "on")
    time.sleep(6)
    t.check(t.var("world") == (1 if w == "mars" else 2), f"{w}: flying")
    shot(name)
t.hook("autopilot", "off")

t.step("help")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 1), "HELP: how to fly")
shot("06_help")
t.key(t.HELP)
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 0), "closed")

t.step("timings")
t.hook("start", "moon 1")
time.sleep(3)
print(f"  fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, polys {t.var('polys')}")
t.check((t.var("fps") or 0) >= 15, "15+ fps")

t.step("stop")
print("  " + t.tool("amiga_stop_client", name=C))
t.check(t.wait(lambda: C not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
