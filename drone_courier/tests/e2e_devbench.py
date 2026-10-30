#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Drone Courier on a live Amiga, driven through devbench MCP.

Launch; the in-game self-test (incl. the long autopilot game, which must
match the host's hash); the title (day chosen with injected keys); taking
off, flying and turning with injected keys, checked through the bridge
variables; a delivery and a collection landed on the pads (hopped over
them by a hook, then settled with keys); the autopilot delivering; the
clock running out three times; the help pages; timings; stopping.

  python3 tests/e2e_devbench.py --url http://localhost:3001/mcp                 (classic Amiga: builds and deploys)
  python3 tests/e2e_devbench.py --os4 local --url http://localhost:3000/mcp --launch RAM:drone_courier
  python3 tests/e2e_devbench.py --url http://localhost:13001/mcp --remote chris@spark.local --no-build
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "amiga3d"))
from devbench_e2e import E2E  # noqa: E402

NAME = "drone_courier"
ap = argparse.ArgumentParser()
ap.add_argument("--url", default=os.environ.get("DEVBENCH_MCP", "http://localhost:3001/mcp"))
ap.add_argument("--no-build", action="store_true")
ap.add_argument("--os4", default="", help="the OS4 emulator's host (ssh + QEMU's monitor), or 'local'")
ap.add_argument("--launch", default="", help="the Amiga command that starts the game")
ap.add_argument("--shots", default="e2e_shots")
ap.add_argument("--remote", default="", help="ssh host of a devbench on another machine (to fetch its screenshots)")
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

t.step("build, deploy, launch")
if not opt.os4 and not opt.no_build:
    out = t.tool("amiga_run", project=f"examples/{NAME}")
    print("  " + out.replace("\n", "\n  "))
    t.check("Build: OK" in out and "Deploy: OK" in out, "cross-compiled and deployed")
if NAME not in t.tool("amiga_list_clients"):
    t.tool("amiga_launch", command=opt.launch or (f"RAM:{NAME}/{NAME}" if opt.os4 else f"Dropbox:Dev/{NAME}"))
t.check(bool(t.wait(lambda: NAME in t.tool("amiga_list_clients"), 20)), "registered with the bridge")
t.check(bool(t.wait(lambda: all(f"  {h}:" in t.tool("amiga_list_hooks", client=NAME)
                                for h in ("start", "autoplay", "hop", "job", "selftest", "pause")), 15)),
        "hooks registered")

t.step("in-game self-test")
t0 = time.time()
print("  " + t.hook("selftest"))
res = t.wait_log(t0, "SELFTEST", 300)
print("  " + (res[-1] if res else "no SELFTEST result"))
t.check(bool(res) and " 0 failed" in res[-1], "the tests pass inside the game (the long game matches the host's hash)")

t.step("title: choose a day with keys")
t.hook("title")
t.check(t.wait(lambda: t.var("state") == 0), "title")
time.sleep(3)
shot("00_title")
t.key(t.UP)
t.key(t.UP)
t.check(t.wait(lambda: t.var("start_day") == 3), "UP twice: day 3")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: flying")
t.check(t.var("day") == 3 and t.var("carrying") == 1 and t.var("landed") == 1, "day 3, on the depot with a parcel")

t.step("flying with keys")
t.hook("start", "1")
time.sleep(0.5)
y0 = t.var("y")
t.key(t.SPACE, 1.5)
t.check(t.wait(lambda: (t.var("y") or 0) > y0 + 20, 3), "SPACE: it climbs off the roof")
t.check(t.var("landed") == 0, "airborne")
x0, z0 = t.var("x"), t.var("z")
t.key(t.UP, 1.5)
t.check(t.wait(lambda: abs((t.var("x") or 0) - x0) + abs((t.var("z") or 0) - z0) > 20, 3), "UP: forward")
time.sleep(0.5)
shot("01_flying")

t.step("landing on the pads")
t.hook("start", "1")
t.hook("hop", "30")
t.check(t.wait(lambda: (t.var("jobs_done") or 0) == 1, 15), "settled onto the customer's pad: delivered")
t.check(t.var("carrying") == 0, "the parcel handed over")
t.hook("hop", "30")
t.check(t.wait(lambda: t.var("carrying") == 1, 15), "settled onto the depot: the next parcel")
time.sleep(0.5)
shot("02_depot")

t.step("the autopilot delivers")
t.hook("start", "1")
t.hook("autoplay", "on")
time.sleep(10)
shot("03_autopilot")
t.check(t.wait(lambda: (t.var("jobs_done") or 0) >= 1, 90), "a parcel delivered")
t.check(t.var("bumps") == 0 and t.var("fails") == 0, "without a bump")

t.step("too late, three times")
t.hook("autoplay", "off")
t.hook("start", "1")
for k in range(3):
    t.hook("job", "1")
    time.sleep(1.5)
t.check(t.wait(lambda: t.var("state") == 2, 6), "three jobs late: game over")
time.sleep(1)
shot("04_game_over")

t.step("help pages")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 1), "HELP: page 1")
shot("05_help")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 2), "HELP again: controls")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 0), "and closed")

t.step("timings")
t.hook("start", "5")
t.hook("autoplay", "on")
time.sleep(12)
print(f"  fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, polys {t.var('polys')}, towers {t.var('towers')} (range {t.var('view_range')})")
t.check((t.var("fps") or 0) >= 20, "20+ fps")

t.step("stop")
print("  " + t.tool("amiga_stop_client", name=NAME))
t.check(t.wait(lambda: NAME not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
