#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Canyon Rail on a live Amiga, driven through devbench MCP.

Launch; the in-game self-test (incl. the whole autopilot game, which must
match the host's hash); the title (stage chosen with injected keys); the
sight and shooting with injected keys (a hit on a target the aim hook
puts the sight on, and a miss), checked through the bridge variables; the
autopilot riding a stage; the guardian shot down; the shield running
out; the help pages; timings; stopping.

  python3 tests/e2e_devbench.py --url http://localhost:3001/mcp                 (classic Amiga: builds and deploys)
  python3 tests/e2e_devbench.py --os4 local --url http://localhost:3000/mcp --launch RAM:canyon_rail
  python3 tests/e2e_devbench.py --url http://localhost:13001/mcp --remote chris@spark.local --no-build
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "amiga3d"))
from devbench_e2e import E2E  # noqa: E402

NAME = "canyon_rail"
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
                                for h in ("start", "autoplay", "guardian", "aim", "selftest", "pause")), 15)),
        "hooks registered")

t.step("in-game self-test")
t0 = time.time()
print("  " + t.hook("selftest"))
res = t.wait_log(t0, "SELFTEST", 300)
print("  " + (res[-1] if res else "no SELFTEST result"))
t.check(bool(res) and " 0 failed" in res[-1], "the tests pass inside the game (the whole game matches the host's hash)")

t.step("title: choose a stage with keys")
t.hook("title")
t.check(t.wait(lambda: t.var("state") == 0), "title")
time.sleep(3)
shot("00_title")
t.key(t.UP)
t.key(t.UP)
t.check(t.wait(lambda: t.var("start_stage") == 3), "UP twice: stage 3")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: riding")
t.check(t.var("stage") == 3 and (t.var("ride") or 0) > 0, "stage 3, the ride under way")

t.step("the sight and shooting with keys")
t.hook("start", "1")
time.sleep(0.5)
x0 = t.var("sight_x")
t.key(t.RIGHT, 0.5)
t.check(t.wait(lambda: (t.var("sight_x") or 0) > x0 + 10), "RIGHT moves the sight")
y0 = t.var("sight_y")
t.key(t.UP, 0.5)
t.check(t.wait(lambda: (t.var("sight_y") or 999) < y0 - 10), "UP moves it up")
t.check(t.wait(lambda: t.hook("aim").startswith("[ok] on target"), 20, 0.3), "a target comes into view (the aim hook puts the sight on it)")
h0 = t.var("hits")
hit = False
for _ in range(5):                       # targets move: aim, fire, and try again if it got away
    t.hook("aim")
    t.key(t.SPACE)
    if t.wait(lambda: (t.var("hits") or 0) > h0, 1.5):
        hit = True
        break
t.check(hit, "SPACE with the sight on it: a hit")
shot("01_hit")
t.key(t.UP, 2.5)                        # the sight up into the open sky, about the middle
t.key(t.LEFT, 2.5)
t.key(t.RIGHT, 0.85)
s1, h1 = t.var("shots"), t.var("hits")
t.key(t.SPACE)
t.check(t.wait(lambda: (t.var("shots") or 0) > s1, 3) and t.var("hits") == h1 and t.var("combo") == 0, "at the bare rock: a miss, the chain broken")

t.step("the autopilot rides a stage")
t.hook("start", "1")
t.hook("autoplay", "on")
time.sleep(8)
shot("02_autopilot")
t.check(t.wait(lambda: t.var("boss") == 1, 90), "down the canyon to the guardian")
time.sleep(2)
shot("03_guardian")
t.check(t.wait(lambda: t.var("stage") == 2, 60), "the guardian shot down: stage 2")
t.check((t.var("shield") or 0) >= 50, "with the shield mostly whole")

t.step("the shield runs out")
t.hook("autoplay", "off")
t.hook("start", "1")
t.hook("shield", "1")
t.check(t.wait(lambda: t.var("state") == 2, 60), "the first bolt to land: game over")
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
t.hook("start", "3")
t.hook("autoplay", "on")
time.sleep(8)
print(f"  fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, polys {t.var('polys')} (view {t.var('view_ahead')})")
t.check((t.var("fps") or 0) >= 20, "20+ fps")

t.step("stop")
print("  " + t.tool("amiga_stop_client", name=NAME))
t.check(t.wait(lambda: NAME not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
