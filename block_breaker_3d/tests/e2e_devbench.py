#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Block Breaker 3D on a live Amiga, driven through devbench MCP.

Launch; the in-game self-test (incl. the long autopilot game, which must
match the host's hash); the title (start level with injected keys); the
paddle moved with keys and a serve with SPACE, checked through the bridge
variables; a rally on the autopilot; a capsule caught; a level cleared; the
cells memory region; game over; the help pages; timings; stopping.

  python3 tests/e2e_devbench.py --url http://localhost:13001/mcp --remote chris@spark.local --no-build
        (the binary already in the devbench's Dropbox:Dev/)
  python3 tests/e2e_devbench.py --os4 chris@spark.local --url http://localhost:13002/mcp
        (AmigaOS 4: the binary in RAM:block_breaker_3d/ already; screenshots from QEMU)
  python3 tests/e2e_devbench.py --os4 local --url http://localhost:3000/mcp
        (AmigaOS 4 in QEMU on this machine; screenshots from its monitor on :2348)
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "amiga3d"))
from devbench_e2e import E2E  # noqa: E402

NAME = "block_breaker_3d"
ap = argparse.ArgumentParser()
ap.add_argument("--url", default=os.environ.get("DEVBENCH_MCP", "http://localhost:3001/mcp"))
ap.add_argument("--no-build", action="store_true")
ap.add_argument("--os4", default="", help="the OS4 emulator's host (screenshots via ssh + QEMU's monitor)")
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
                                for h in ("start", "autoplay", "serve", "clear", "selftest", "pause")), 15)),
        "hooks registered")

t.step("in-game self-test")
t0 = time.time()
print("  " + t.hook("selftest"))
res = t.wait_log(t0, "SELFTEST", 180)
print("  " + (res[-1] if res else "no SELFTEST result"))
t.check(bool(res) and " 0 failed" in res[-1], "the tests pass inside the game (the long game matches the host's hash)")

t.step("title: choose a level with keys")
t.hook("title")
t.check(t.wait(lambda: t.var("state") == 0), "title")
time.sleep(2)
shot("00_title")
t.key(t.UP)
t.key(t.UP)
t.check(t.wait(lambda: t.var("start_level") == 3), "UP twice: start at level 3")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: playing")
t.check(t.var("level") == 3 and t.var("stuck") == 1, "level 3, the ball on the paddle")
shot("01_level3_serve")

t.step("the paddle and the serve with keys")
t.hook("start", "1")
time.sleep(0.5)
x0 = t.var("paddle_x")
t.key(t.RIGHT, 0.4)
t.check(t.wait(lambda: (t.var("paddle_x") or 0) > x0), "RIGHT moves the paddle right")
t.check(t.var("ball_x") == t.var("paddle_x"), "the ball rides on the paddle")
y0 = t.var("paddle_y")
t.key(t.UP, 0.4)
t.check(t.wait(lambda: (t.var("paddle_y") or 0) > y0), "UP moves it up")
t.check(t.key_until(t.SPACE, lambda: t.var("stuck") == 0), "SPACE serves")
t.check(t.wait(lambda: (t.var("ball_z") or 0) > 300, 5), "the ball flies down the tunnel")
time.sleep(0.4)
shot("02_in_flight")

t.step("a rally on the autopilot")
t.hook("autoplay", "on")
t.check(t.wait(lambda: (t.var("broken") or 0) >= 3, 60), "bricks broken")
t.check((t.var("paddle_hits") or 0) >= 1, "the paddle returned the ball")
shot("03_rally")

t.step("a capsule caught")
s0 = t.var("score")
t.hook("capsule", "wide")
t.check(t.wait(lambda: (t.var("wide") or 0) > 0, 6), "WIDE capsule caught: the paddle is wider")
t.check((t.var("score") or 0) >= s0 + 100, "a capsule scores 100")

t.step("clearing a level")
lv = t.var("level")
t.hook("clear")
t.check(t.wait(lambda: t.var("level") == lv + 1, 8), "the level clears and the next one loads")
time.sleep(1)
shot("04_next_level")

t.step("the cells memory region")
out = t.tool("amiga_read_memregion", client=NAME, region="cells")
t.check(len(out) > 40, "cells memory region readable (72 bytes)")

t.step("game over")
t.hook("autoplay", "off")
t.hook("start", "1")
t.hook("lives", "1")
t.hook("paddle", "-120 -90")
t.hook("serve")
t.check(t.wait(lambda: t.var("state") == 2, 20), "a missed ball with one life: game over")
time.sleep(1)
shot("05_game_over")

t.step("help pages")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 1), "HELP: page 1")
shot("06_help")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 2), "HELP again: controls")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 0), "and closed")

t.step("timings")
t.hook("start", "8")
t.hook("autoplay", "on")
time.sleep(4)
print(f"  fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, polys {t.var('polys')}")
t.check((t.var("fps") or 0) >= 20, "20+ fps on the busiest level")

t.step("stop")
print("  " + t.tool("amiga_stop_client", name=NAME))
t.check(t.wait(lambda: NAME not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
