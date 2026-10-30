#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Ridge Runner on a live Amiga, driven through devbench MCP.

Launch; the in-game self-test (incl. the long autopilot race, which must
match the host's hash); the title (track chosen with injected keys); the
countdown; the throttle, steering off the track and the boost with
injected keys, checked through the bridge variables; gates and jumps on
the autopilot; finishing and qualifying; running out of time; the help
pages; timings; stopping.

  python3 tests/e2e_devbench.py --url http://localhost:3001/mcp                 (classic Amiga: builds and deploys)
  python3 tests/e2e_devbench.py --os4 local --url http://localhost:3000/mcp --launch RAM:ridge_runner
  python3 tests/e2e_devbench.py --url http://localhost:13001/mcp --remote chris@spark.local --no-build
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "amiga3d"))
from devbench_e2e import E2E  # noqa: E402

NAME = "ridge_runner"
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
                                for h in ("start", "autoplay", "warp", "skip", "selftest", "pause")), 15)),
        "hooks registered")

t.step("in-game self-test")
t0 = time.time()
print("  " + t.hook("selftest"))
res = t.wait_log(t0, "SELFTEST", 300)
print("  " + (res[-1] if res else "no SELFTEST result"))
t.check(bool(res) and " 0 failed" in res[-1], "the tests pass inside the game (the long race matches the host's hash)")

t.step("title: choose a track with keys")
t.hook("title")
t.check(t.wait(lambda: t.var("state") == 0), "title")
time.sleep(3)
shot("00_title")
t.key(t.RIGHT)
t.key(t.RIGHT)
t.check(t.wait(lambda: t.var("start_track") == 3), "RIGHT twice: track 3")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: racing")
t.check(t.var("track") == 3 and (t.var("countdown") or 0) > 0, "track 3, the countdown running")
time.sleep(0.5)
shot("01_countdown")
t.check(t.wait(lambda: t.var("countdown") == 0, 6), "3, 2, 1, go")

t.step("throttle, steering, boost with keys")
t.hook("start", "1 now")
time.sleep(0.5)
z0 = t.var("z")
t.key(t.UP, 2.5)
t.check(t.wait(lambda: (t.var("z") or 0) > z0 + 200), "UP: the bike races off")
t.check((t.var("speed") or 0) > 40, "at speed")
t.key(t.RIGHT, 1.2)
off = False
for _ in range(3):                      # turned right: throttle out until it leaves the track
    t.key(t.UP, 1.5)
    off = off or t.var("offtrack") == 1 or "offtrack" in t.hook("status")
    if off:
        break
t.check(off, "RIGHT steers it off the track")
t.hook("start", "1 now")
time.sleep(0.5)
t.key(t.UP, 1.0)
e0 = t.var("energy")
t.key("60", 1.5)                        # left shift: boost
t.check(t.wait(lambda: (t.var("energy") or 100) < e0 - 10, 4), "SHIFT boosts (the energy drains)")

t.step("gates and jumps on the autopilot")
t.hook("start", "2 now")
t.hook("autoplay", "on")
t.check(t.wait(lambda: (t.var("gates_hit") or 0) >= 2, 40), "gates passed between the pylons")
time.sleep(1)
shot("02_racing")
t.check(t.wait(lambda: (t.var("jumps") or 0) >= 1, 40), "and air over the crests")
t.check(t.var("missed") == 0, "no gates missed")

t.step("finishing and qualifying")
t.hook("skip")
t.check(t.wait(lambda: t.var("finished") == 1, 10), "over the line")
t.check(t.var("qualified") == 1 and 1 <= (t.var("place") or 9) <= 3, f"qualified ({t.var('place')})")
time.sleep(0.5)
shot("03_finish")
t.check(t.wait(lambda: t.var("track") == 3, 10), "on to the next track")

t.step("out of time")
t.hook("autoplay", "off")
t.hook("start", "1 now")
t.hook("time", "2")
t.check(t.wait(lambda: t.var("state") == 2, 8), "the clock runs out: game over")
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
t.hook("start", "6 now")
t.hook("autoplay", "on")
time.sleep(10)
print(f"  fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, polys {t.var('polys')}, window {t.var('ground_win')}")
t.check((t.var("fps") or 0) >= 20, "20+ fps")

t.step("stop")
print("  " + t.tool("amiga_stop_client", name=NAME))
t.check(t.wait(lambda: NAME not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
