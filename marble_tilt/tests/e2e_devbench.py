#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Marble Tilt on a live Amiga, driven through devbench MCP.

Launch; the in-game self-test (incl. the whole autopilot game, which must
match the host's hash); the title (level chosen with injected keys);
tilting with injected keys, checked through the bridge variables (the
tilt, the marble rolling); a fall through a hole and back; the exit
reached; a level rolled by the autopilot; the board memory region;
running out of time; the help pages; timings; stopping.

  python3 tests/e2e_devbench.py --url http://localhost:3001/mcp                 (classic Amiga: builds and deploys)
  python3 tests/e2e_devbench.py --os4 local --url http://localhost:3000/mcp --launch RAM:marble_tilt
  python3 tests/e2e_devbench.py --url http://localhost:13001/mcp --remote chris@spark.local --no-build
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "amiga3d"))
from devbench_e2e import E2E  # noqa: E402

NAME = "marble_tilt"
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
                                for h in ("start", "autoplay", "put", "time", "selftest", "pause")), 15)),
        "hooks registered")

t.step("in-game self-test")
t0 = time.time()
print("  " + t.hook("selftest"))
res = t.wait_log(t0, "SELFTEST", 300)
print("  " + (res[-1] if res else "no SELFTEST result"))
t.check(bool(res) and " 0 failed" in res[-1], "the tests pass inside the game (the whole game matches the host's hash)")

t.step("title: choose a level with keys")
t.hook("title")
t.check(t.wait(lambda: t.var("state") == 0), "title")
time.sleep(3)
shot("00_title")
t.key(t.UP)
t.key(t.UP)
t.check(t.wait(lambda: t.var("start_level") == 3), "UP twice: level 3")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: playing")
t.check(t.var("level") == 3 and t.var("row") == 1 and t.var("col") == 1, "level 3, the marble on the start")
time.sleep(1)
shot("01_level3")

t.step("tilting with keys")
t.hook("start", "1")
time.sleep(0.5)
x0 = t.var("x")
t.key(t.RIGHT, 0.8)
t.check(t.wait(lambda: (t.var("x") or -999) > x0 + 10, 4), "RIGHT: the marble rolls right")
t.check(t.wait(lambda: t.var("roll") == 0, 3), "let go, the board levels")
t.hook("put", "1 1")                     # the start: open floor below it
time.sleep(0.5)
z0 = t.var("z")
t.key(t.DOWN, 0.6)
t.check(t.wait(lambda: (t.var("z") or 999) < z0 - 10, 4), "DOWN: it rolls towards you")

t.step("a hole, and the exit")
t.hook("start", "2")
time.sleep(0.3)
f0 = t.var("falls")
t.hook("put", "2 3")                     # a hole
t.check(t.wait(lambda: (t.var("falls") or 0) == f0 + 1, 3), "on a hole: it falls")
time.sleep(0.3)
shot("02_falling")
t.check(t.wait(lambda: t.var("falling") == 0 and t.var("row") == 1 and t.var("col") == 1, 4), "and comes back at the start")
t.hook("put", "5 5")                     # the exit
t.check(t.wait(lambda: t.var("level") == 3, 6), "on the exit: through to the next level")

t.step("the autopilot rolls a level")
t.hook("start", "4")
t.hook("autoplay", "on")
time.sleep(4)
shot("03_autopilot")
t.check(t.wait(lambda: t.var("level") == 5, 60), "the autopilot gets through the gatehouse")
t.check(t.var("falls") == 0, "without a fall")
out = t.tool("amiga_read_memregion", client=NAME, region="board")
t.check(len(out) > 40, "board memory region readable (144 bytes)")

t.step("out of time")
t.hook("autoplay", "off")
t.hook("start", "1")
t.hook("time", "1")
t.check(t.wait(lambda: t.var("state") == 2, 6), "the clock runs out: game over")
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
t.hook("start", "8")
t.hook("autoplay", "on")
time.sleep(6)
print(f"  fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, polys {t.var('polys')}")
t.check((t.var("fps") or 0) >= 20, "20+ fps")

t.step("stop")
print("  " + t.tool("amiga_stop_client", name=NAME))
t.check(t.wait(lambda: NAME not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
