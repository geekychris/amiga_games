#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Tank Duel on a live Amiga, driven through devbench MCP.

Launch; the in-game self-test (incl. the long autopilot fight, which must
match the host's hash); the title (start round with injected keys);
driving, turning and firing with injected keys, checked through the bridge
variables; a scout destroyed and a shot glancing off a bulwark's shield
(practice targets from the spawn hook); a round cleared by the autopilot;
being destroyed; the help pages; timings; stopping.

  python3 tests/e2e_devbench.py --url http://localhost:3001/mcp                 (classic Amiga: builds and deploys)
  python3 tests/e2e_devbench.py --os4 local --url http://localhost:3000/mcp --launch RAM:tank_duel
  python3 tests/e2e_devbench.py --url http://localhost:13001/mcp --remote chris@spark.local --no-build
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "amiga3d"))
from devbench_e2e import E2E  # noqa: E402

NAME = "tank_duel"
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
                                for h in ("start", "autoplay", "spawn", "clear", "selftest", "pause")), 15)),
        "hooks registered")

t.step("in-game self-test")
t0 = time.time()
print("  " + t.hook("selftest"))
res = t.wait_log(t0, "SELFTEST", 300)
print("  " + (res[-1] if res else "no SELFTEST result"))
t.check(bool(res) and " 0 failed" in res[-1], "the tests pass inside the game (the long fight matches the host's hash)")

t.step("title: choose a round with keys")
t.hook("title")
t.check(t.wait(lambda: t.var("state") == 0), "title")
time.sleep(3)
shot("00_title")
t.key(t.UP)
t.key(t.UP)
t.check(t.wait(lambda: t.var("start_round") == 3), "UP twice: round 3")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: fighting")
t.check(t.var("round") == 3 and t.var("enemies") == 3, "round 3: three enemies")
time.sleep(1)
shot("01_round3")

t.step("driving, turning, firing with keys")
t.hook("start", "1")
t.hook("spawn", "scout 9000")          # a target far out of the way: nothing shoots back
time.sleep(0.3)
z0 = t.var("z")
t.key(t.UP, 1.5)
t.check(t.wait(lambda: (t.var("z") or -9999) > z0 + 40), "UP drives forward")
y0 = t.var("yaw")
t.key(t.RIGHT, 0.4)
t.check(t.wait(lambda: t.var("yaw") not in (None, y0)), "RIGHT turns")
s0 = t.var("shots")
t.key(t.SPACE)
t.check(t.wait(lambda: (t.var("shots") or 0) > s0), "SPACE fires")

t.step("practice targets")
t.hook("spawn", "scout 300")
time.sleep(1.2)
t.key(t.SPACE)
t.check(t.wait(lambda: (t.var("kills") or 0) >= 1, 5), "a scout ahead: destroyed")
h0 = t.var("hits")
t.hook("spawn", "bulwark 300")
time.sleep(1.2)
t.key(t.SPACE)
time.sleep(0.4)
shot("02_bulwark")
time.sleep(1.5)
t.check(t.var("hits") == h0 and t.var("enemies") == 1, "a shot at a bulwark's shield glances off")

t.step("the autopilot clears a round")
t.hook("start", "1")
t.hook("autoplay", "on")
time.sleep(4)
shot("03_autopilot")
t.check(t.wait(lambda: t.var("round") == 2, 90), "round 1 cleared")
t.check((t.var("kills") or 0) >= 2, "both scouts destroyed")

t.step("destroyed")
t.hook("autoplay", "off")
t.hook("start", "2")
t.hook("hp", "1")
t.check(t.wait(lambda: t.var("state") == 2, 60), "one hit with 1 armour left: game over")
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
time.sleep(8)
print(f"  fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, polys {t.var('polys')}")
t.check((t.var("fps") or 0) >= 20, "20+ fps")

t.step("stop")
print("  " + t.tool("amiga_stop_client", name=NAME))
t.check(t.wait(lambda: NAME not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
