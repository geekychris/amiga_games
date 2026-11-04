#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Sphere Siege on a live Amiga, driven through devbench MCP.

Launch; the in-game self-test (incl. a long autopilot game, which must
match the host's hash); the title (wave chosen with injected keys); the
ship moved with injected keys; a hit with SPACE on an enemy the aim hook
holds on the line of fire; the autopilot clearing a wave; a siege wave;
the last ship lost and the high score; the help pages; timings; stopping.
Test state is read through the game's own `state` hook (the bridge looks
variables up by name only, and the user may be running other games).

  python3 tests/e2e_devbench.py --os4 local --url http://localhost:3010/mcp --launch "RAM:sphere_siege/sphere_siege FULLSCREEN"
  python3 tests/e2e_devbench.py --url http://localhost:3001/mcp                 (classic Amiga: builds and deploys)

With --os4 local the run holds the emulator lock and shows the "CLAUDE IS
TESTING" banner (scripts/claude_busy.py).
"""
import argparse
import contextlib
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "amiga3d"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "scripts"))
from devbench_e2e import E2E  # noqa: E402

NAME = "sphere_siege"
ap = argparse.ArgumentParser()
ap.add_argument("--url", default=os.environ.get("DEVBENCH_MCP", "http://localhost:3001/mcp"))
ap.add_argument("--no-build", action="store_true")
ap.add_argument("--os4", default="", help="the OS4 emulator's host (ssh + QEMU's monitor), or 'local'")
ap.add_argument("--launch", default="", help="the Amiga command that starts the game")
ap.add_argument("--shots", default="e2e_shots")
ap.add_argument("--remote", default="", help="ssh host of a devbench on another machine (to fetch its screenshots)")
opt = ap.parse_args()

t = E2E(NAME, opt.url, opt.shots, opt.remote)


def st():
    """the game's state hook as a dict of ints"""
    out = t.hook("state")
    d = {}
    for kv in out.replace("[ok]", "").split():
        if "=" in kv:
            k, v = kv.split("=", 1)
            try:
                d[k] = int(v)
            except ValueError:
                pass
    return d


def sv(name, default=None):
    return st().get(name, default)


def local_qemu_shot(name, port=2348):
    """an OS4 screenshot from the QEMU monitor on this machine"""
    import socket
    t.hook("pause", "on")
    ppm = os.path.abspath(os.path.join(opt.shots, f"{name}.ppm"))
    os.makedirs(opt.shots, exist_ok=True)
    with socket.create_connection(("127.0.0.1", port), 5) as s:
        s.sendall(f"screendump {ppm}\n".encode())
        time.sleep(1.5)
    t.hook("pause", "off")
    dst = os.path.join(opt.shots, f"{name}.png")
    if os.path.exists(ppm):
        try:
            from PIL import Image
            Image.open(ppm).save(dst)
        except ImportError:
            os.system(f"sips -s format png '{ppm}' --out '{dst}' >/dev/null")
        os.remove(ppm)
        print(f"  shot  {dst}")
    return dst


if opt.os4 == "local":
    t.activate = True
    shot = local_qemu_shot
    from claude_busy import claude_testing
    lock = claude_testing(opt.url, "Sphere Siege tests")
else:
    shot = (lambda n: t.qemu_shot(n, opt.os4)) if opt.os4 else t.shot
    lock = contextlib.nullcontext()

with lock:
    t.step("build, deploy, launch")
    if not opt.os4 and not opt.no_build:
        out = t.tool("amiga_run", project=f"examples/{NAME}")
        print("  " + out.replace("\n", "\n  "))
        t.check("Build: OK" in out and "Deploy: OK" in out, "cross-compiled and deployed")
    if NAME not in t.tool("amiga_list_clients"):
        t.tool("amiga_launch", command=opt.launch or (f"RAM:{NAME}/{NAME} FULLSCREEN" if opt.os4 else f"Dropbox:Dev/{NAME}"))
    t.check(bool(t.wait(lambda: NAME in t.tool("amiga_list_clients"), 20)), "registered with the bridge")
    t.check(bool(t.wait(lambda: all(f"  {h}:" in t.tool("amiga_list_hooks", client=NAME)
                                    for h in ("state", "start", "autoplay", "aim", "selftest", "pause")), 15)),
            "hooks registered")

    t.step("in-game self-test")
    t0 = time.time()
    print("  " + t.hook("selftest"))
    res = t.wait_log(t0, "SELFTEST", 300)
    print("  " + (res[-1] if res else "no SELFTEST result"))
    t.check(bool(res) and " 0 failed" in res[-1], "the tests pass inside the game (the long game matches the host's hash)")

    t.step("title: choose a wave with keys")
    t.hook("title")
    t.check(t.wait(lambda: sv("state") == 0), "title")
    time.sleep(3)
    shot("00_title")
    t.key(t.UP)
    t.key(t.UP)
    t.check(t.wait(lambda: sv("start_wave") == 3), "UP twice: wave 3")
    t.check(t.key_until(t.SPACE, lambda: sv("state") == 1), "fire: playing")
    s = st()
    t.check(s.get("wave") == 3 and s.get("lives") == 3, "wave 3, three ships")

    t.step("moving and shooting with keys")
    t.hook("start", "1")
    time.sleep(0.5)
    t.key(t.RIGHT, 0.6)
    t.check(t.wait(lambda: sv("fz", 65536) < 65000), "RIGHT slides the ship over the shell (the frame turns)")
    t.check(t.wait(lambda: t.hook("aim", "hold").startswith("[ok] on enemy"), 25, 0.5), "an enemy comes out of the centre (aim holds it on the line)")
    s0 = st()
    hit = False
    for _ in range(4):
        t.hook("aim", "hold")
        t.key(t.SPACE, 0.15)
        if t.wait(lambda: sv("hits", 0) > s0["hits"], 3):
            hit = True
            break
    t.check(hit, "SPACE with an enemy on the line of fire: a hit")
    t.check(sv("shots", 0) > s0["shots"], "shots counted")
    time.sleep(0.3)
    shot("01_play")

    t.step("the autopilot clears a wave")
    t.hook("start", "1")
    t.hook("autoplay", "on")
    time.sleep(8)
    shot("02_autopilot")
    t.check(t.wait(lambda: sv("wave") == 2, 90), "wave 1 cleared: wave 2")
    s = st()
    t.check(s.get("lives", 0) >= 2, "with ships to spare")

    t.step("a siege wave")
    t.hook("start", "4")
    t.hook("autoplay", "on")
    time.sleep(10)
    s = st()
    t.check(s.get("siege") == 1 and s.get("live", 0) > 0, "siege wave: wardens out")
    shot("03_siege")
    t.check(t.wait(lambda: sv("wave") == 5, 150), "the siege wave cleared")

    t.step("the last ship lost: game over and the high score")
    t.hook("autoplay", "off")
    t.hook("start", "3")
    time.sleep(1)
    t.hook("lives", "1")
    t.check(t.wait(lambda: sv("state") == 2, 120), "hit with one ship left: game over")
    s = st()
    t.check(s.get("best", 0) >= s.get("score", 0), "the best score kept")
    time.sleep(1)
    shot("04_game_over")

    t.step("help pages")
    t.key(t.HELP)
    t.check(t.wait(lambda: sv("help") == 1), "HELP: page 1")
    shot("05_help")
    t.key(t.HELP)
    t.check(t.wait(lambda: sv("help") == 2), "HELP again: controls")
    t.key(t.HELP)
    t.check(t.wait(lambda: sv("help") == 0), "and closed")

    t.step("timings")
    t.hook("start", "6")
    t.hook("autoplay", "on")
    time.sleep(10)
    s = st()
    print(f"  fps {s.get('fps')}, draw {s.get('us_draw')} us, update {s.get('us_update')} us, polys {s.get('polys')}")
    t.check((s.get("fps") or 0) >= 24, "25 fps")
    t.check((s.get("us_draw") or 99999) < 20000, "drawn in under 20 ms")

    t.step("stop")
    t.hook("title")
    print("  " + t.tool("amiga_stop_client", name=NAME))
    t.check(t.wait(lambda: NAME not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
