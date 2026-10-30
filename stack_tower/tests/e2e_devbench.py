#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Stack Tower on a live Amiga, driven through devbench MCP.

Build, deploy and launch; the in-game self-test; the title (mode and level
chosen with injected keys); playing with injected keys (turning the tower,
rotating, hard drops) checked through the bridge variables; hooks to set
up a ring clear, rubble and a game over; the board memory region; the
help pages; screenshots; stopping the client.

  python3 tests/e2e_devbench.py [--url http://localhost:3001/mcp] [--no-build]
  python3 tests/e2e_devbench.py --os4 spark.local --url http://localhost:13002/mcp
        (AmigaOS 4: the binary must be in RAM:stack_tower/ already; screenshots from QEMU)
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
ap.add_argument("--launch", default="", help="the Amiga command that starts the game (default Dropbox:Dev/stack_tower)")
ap.add_argument("--shots", default="e2e_shots")
ap.add_argument("--remote", default="", help="ssh host of a devbench on another machine (to fetch its screenshots)")
opt = ap.parse_args()

t = E2E("stack_tower", opt.url, opt.shots, opt.remote)
shot = (lambda n: t.qemu_shot(n, opt.os4)) if opt.os4 else t.shot

t.step("build, deploy, launch")
if not opt.os4 and not opt.no_build:
    out = t.tool("amiga_run", project="examples/stack_tower")
    print("  " + out.replace("\n", "\n  "))
    t.check("Build: OK" in out and "Deploy: OK" in out, "cross-compiled and deployed")
if "stack_tower" not in t.tool("amiga_list_clients"):
    t.tool("amiga_launch", command=opt.launch or ("RAM:stack_tower/stack_tower" if opt.os4 else "Dropbox:Dev/stack_tower"))
t.check(bool(t.wait(lambda: "stack_tower" in t.tool("amiga_list_clients"), 15)), "registered with the bridge")
t.check(all(f"  {h}:" in t.tool("amiga_list_hooks", client="stack_tower")
            for h in ("start", "move", "drop", "selftest", "pause")), "hooks registered")

t.step("in-game self-test")
t0 = time.time()
print("  " + t.hook("selftest"))
res = t.wait_log(t0, "SELFTEST", 60)
print("  " + (res[-1] if res else "no SELFTEST result"))
t.check(bool(res) and " 0 failed" in res[-1], "the tests (and the browser's games) pass inside the game")

t.step("title: choose a mode and level with keys")
t.hook("title")
t.check(t.wait(lambda: t.var("state") == 0), "title")
time.sleep(1.5)
t.check((t.var("pieces") or 0) > 0, "the demo game is playing behind the title")
shot("00_title")
t.key(t.RIGHT)
t.key(t.UP)
t.key(t.UP)
t.check(t.wait(lambda: t.var("start_level") == 3), "UP twice: start at level 3")
t.check(t.key_until(t.SPACE, lambda: t.var("state") == 1), "fire: playing")
t.check(t.var("mode") == 1 and t.var("level") == 3, "rising mode (RIGHT once), level 3")

t.step("playing with keys")
t.hook("start", "marathon 1")
time.sleep(0.5)
c0 = t.var("col")
t.key(t.RIGHT)
# a remote devbench can hold the key past the 280 ms auto-repeat delay, so
# any turn to the right counts here; the single step is checked by the hook
t.check(t.wait(lambda: t.var("col") is not None and 1 <= (t.var("col") - c0) % 16 <= 8), "RIGHT turns the tower right")
c1 = t.var("col")
t.hook("move", "1")
t.check(t.wait(lambda: t.var("col") == (c1 + 1) % 16), "one move is one column")
t.key(t.LEFT, 0.6)
t.check(t.wait(lambda: t.var("col") not in (None, (c0 + 1) % 16)), "holding LEFT repeats")
p0 = t.var("pieces")
s0 = t.var("score")
t.key(t.SPACE)
t.check(t.wait(lambda: t.var("pieces") == p0 + 1), "SPACE: hard drop locks the piece")
t.check(t.var("score") > s0, "a hard drop scores 2 a row")
r0 = t.var("row")
time.sleep(2.0)
t.check((t.var("row") or 99) < r0, "gravity: the next piece falls")
shot("01_playing")

t.step("the board memory region")
out = t.tool("amiga_read_memregion", client="stack_tower", region="board")
t.check(len(out) > 40, "board memory region readable (16 x 24 bytes)")

t.step("rubble and game over")
t.hook("start", "marathon 1")
for _ in range(25):
    t.hook("garbage", "1")
    if t.var("over"):
        break
t.check(t.wait(lambda: t.var("state") == 2, 5), "pushed up past the top: game over")
time.sleep(1.0)
shot("02_game_over")

t.step("help pages")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 1), "HELP: rules page 1")
shot("03_help")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 2), "HELP again: controls")
t.key(t.HELP)
t.check(t.wait(lambda: t.var("help") == 0), "and closed")

t.step("timings")
t.hook("start", "marathon 1")
time.sleep(2)
print(f"  fps {t.var('fps')}, draw {t.var('us_draw')} us, update {t.var('us_update')} us, polys {t.var('polys')}")
t.check((t.var("fps") or 0) >= 20, "20+ fps")

t.step("stop")
print("  " + t.tool("amiga_stop_client", name="stack_tower"))
t.check(t.wait(lambda: "stack_tower" not in t.tool("amiga_list_clients"), 10), "stopped and unregistered")
sys.exit(t.done())
