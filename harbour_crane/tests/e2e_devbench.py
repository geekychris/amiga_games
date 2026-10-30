#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Harbour Crane on a live Amiga, driven through devbench MCP.

Launch; the in-game self-test (incl. the long autopilot game, which must
match the host's hash); the title (start level with injected keys); the
trolley, winch and grab with injected keys, checked through the bridge
variables; landing, denting and losing containers through hooks; the
ship's list; a level loaded by the autopilot; capsizing; the heights
memory region; the help pages; timings; stopping.

  python3 tests/e2e_devbench.py --url http://localhost:3001/mcp                 (classic Amiga: builds and deploys)
  python3 tests/e2e_devbench.py --os4 local --url http://localhost:3000/mcp --launch RAM:harbour_crane
  python3 tests/e2e_devbench.py --url http://localhost:13001/mcp --remote chris@spark.local --no-build
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "amiga3d"))
from devbench_e2e import E2E  # noqa: E402

NAME = "harbour_crane"
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
                                for h in ("start", "autoplay", "grab", "place", "selftest", "pause")), 15)),
        "hooks registered")

t.step("in-game self-test")
t0 = time.time()
print("  " + t.hook("selftest"))
res = t.wait_log(t0, "SELFTEST", 300)
print("  " + (res[-1] if res else "no SELFTEST result"))
t.check(bool(res) and " 0 failed" in res[-1], "the tests pass inside the game (the long game matches the host's hash)")

t.step("title: choose a level with keys")
t.hook("title")
t.check(t.wait(lambda: t.var("state") == 0), "title")
time.sleep(3)
shot("00_title")
t.key(t.UP)
t.key(t.UP)
t.check(t.wait(lambda: t.var("start_level") == 3), "UP twice: start at level 3")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: working")
t.check(t.var("level") == 3 and t.var("carrying") == -1, "level 3, nothing on the hook yet")

t.step("the trolley, the winch and grabbing with keys")
t.hook("start", "1")
time.sleep(0.5)
x0 = t.var("trolley_x")
t.key(t.RIGHT, 1.0)
t.check(t.wait(lambda: (t.var("trolley_x") or -999) > x0 + 3), "RIGHT moves the trolley out")
swing = 0
for _ in range(10):
    swing = max(swing, abs(t.var("swing_mrad") or 0))
    time.sleep(0.4)
t.check(swing > 3, f"starting and stopping set the hook swinging ({swing} mrad)")
t.hook("start", "1")
time.sleep(0.5)
L0 = t.var("cable")
t.key(t.DOWN, 3.0)
t.check(t.wait(lambda: (t.var("cable") or 0) > L0 + 20), "DOWN lets the cable out")
t.key(t.DOWN, 4.0)
t.check(t.wait(lambda: abs((t.var("hook_y") or 0) - 34) <= 3, 6), "the hook comes to rest on the waiting container")
t.check(t.key_until(t.SPACE, lambda: t.var("carrying") == 0), "SPACE grabs it")
t.key(t.UP, 2.0)
t.check(t.wait(lambda: (t.var("hook_y") or 0) > 60), "UP lifts it off the quay")
shot("01_lifted")

t.step("landing, denting and losing containers")
t.hook("start", "1")
t.hook("swell", "0")
t.hook("box", "1 3")
t.hook("grab")
t.hook("place", "5")
t.check(t.wait(lambda: t.var("loaded") == 1, 5), "landed in slot 5")
t.check(t.var("score") == 150 and t.var("dents") == 0, "a 3 t container scores 150")
t.check(t.wait(lambda: (t.var("list_mrad") or 0) > 40, 3), "she lists to the right")
t.hook("box", "1 3")
t.hook("grab")
t.hook("place", "0")
t.check(t.wait(lambda: t.var("loaded") == 2 and t.var("list_mrad") is not None and abs(t.var("list_mrad")) < 5, 5),
        "the same on the left trims her")
t.hook("grab")
t.hook("place", "2 120")
t.check(t.wait(lambda: t.var("dents") == 1, 5), "let go from high up: dented")
t.hook("grab")
t.hook("place", "6")
t.check(t.wait(lambda: t.var("lost") == 1, 6), "let go off the end of the deck: lost")
time.sleep(0.3)
shot("02_loading")
out = t.tool("amiga_read_memregion", client=NAME, region="heights")
t.check(len(out) > 10, "heights memory region readable (6 bytes)")

t.step("the autopilot loads a ship")
t.hook("start", "1")
t.hook("autoplay", "on")
t.check(t.wait(lambda: (t.var("loaded") or 0) >= 2, 90), "the autopilot loads containers")
shot("03_autopilot")
t.check(t.wait(lambda: t.var("level") == 2, 240), "and finishes level 1")
t.check((t.var("total_loaded") or 0) >= 6, "all six loaded")

t.step("capsizing")
t.hook("autoplay", "off")
t.hook("start", "1")
t.hook("swell", "0")
for k in range(4):
    t.hook("box", "2 4")
    t.hook("grab")
    t.hook("place", "4")
    t.wait(lambda: t.var("falling") == -1, 4)
    if t.var("state") == 2:
        break
t.check(t.wait(lambda: t.var("capsized") == 1 and t.var("state") == 2, 5), "four heavy ones at one end: she capsizes")
time.sleep(1)
shot("04_capsized")

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
time.sleep(10)
print(f"  fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, polys {t.var('polys')}")
t.check((t.var("fps") or 0) >= 20, "20+ fps")

t.step("stop")
print("  " + t.tool("amiga_stop_client", name=NAME))
t.check(t.wait(lambda: NAME not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
