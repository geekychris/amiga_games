#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of Terra Strike on a live Amiga, driven through devbench MCP.

Covers the whole loop the way a player (and a developer) would: build,
deploy, launch, the in-game self-test, key injection to start the game and
lift off, scene hooks to set up fights, bridge variables to check state and
to retune the game live, the heightmap memory region, the remote log, the
Protracker channels, screenshots, level clear, game over and stopping the
client.

  python3 tests/e2e_devbench.py [--url http://localhost:3001/mcp] [--no-build]

Exit status is the number of failed checks.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

ap = argparse.ArgumentParser()
ap.add_argument("--url", default=os.environ.get("DEVBENCH_MCP", "http://localhost:3001/mcp"))
ap.add_argument("--no-build", action="store_true")
ap.add_argument("--shots", default="e2e_shots", help="directory for screenshots")
ap.add_argument("--mark", default="", help="timeline script to call with each step (demo recording)")
opt = ap.parse_args()

CLIENT = "terra_strike"
_sid = None
failures = 0
log_cursor = 0.0


def _post(body):
    global _sid
    hdr = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
    if _sid:
        hdr["mcp-session-id"] = _sid
    req = urllib.request.Request(opt.url, json.dumps(body).encode(), hdr)
    with urllib.request.urlopen(req, timeout=180) as r:
        raw = r.read().decode()
        _sid = r.headers.get("mcp-session-id") or _sid
    msgs = [json.loads(l[5:]) for l in raw.splitlines() if l.startswith("data:")]
    return msgs or ([json.loads(raw)] if raw.strip() else [])


def tool(_tool, **args):
    if _sid is None:
        _post({"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
            "protocolVersion": "2025-03-26", "capabilities": {},
            "clientInfo": {"name": "terra-strike-e2e", "version": "1"}}})
        _post({"jsonrpc": "2.0", "method": "notifications/initialized"})
    res = _post({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                 "params": {"name": _tool, "arguments": args}})[-1].get("result", {})
    return "\n".join(c.get("text", "") for c in res.get("content", []) if c.get("type") == "text")


def var(name):
    out = tool("amiga_get_var", name=name)          # "score (i32) = 100"
    try:
        return int(out.split("=")[1].split()[0])
    except (IndexError, ValueError):
        return None


def key(code, hold=0.1):
    tool("amiga_input_key", rawkey=code, direction="down")
    time.sleep(hold)
    tool("amiga_input_key", rawkey=code, direction="up")


def logs_since(t0):
    base = opt.url.rsplit("/mcp", 1)[0]
    with urllib.request.urlopen(f"{base}/api/logs?limit=300", timeout=10) as r:
        d = json.load(r)
    out = []
    for l in (d.get("logs", d) if isinstance(d, dict) else d):
        ts = l.get("timestamp")
        if isinstance(ts, str):
            from datetime import datetime
            ts = datetime.fromisoformat(ts).timestamp()
        if l.get("client") == CLIENT and ts and ts >= t0:
            out.append(l["message"])
    return out


def wait_for(fn, timeout=8.0, every=0.4):
    end = time.time() + timeout
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(every)
    return fn()


def check(cond, what):
    global failures
    print(f"  {'PASS' if cond else 'FAIL'}  {what}", flush=True)
    if not cond:
        failures += 1


def step(title):
    print(f"\n== {title}", flush=True)
    if opt.mark:
        os.system(f'{opt.mark} e2e "{title}" >/dev/null')


def shot(name):
    out = tool("amiga_screenshot")
    path = out.split("saved: ")[-1].split(" (")[0].strip() if "saved:" in out else ""
    if path and os.path.exists(path):
        os.makedirs(opt.shots, exist_ok=True)
        dst = os.path.join(opt.shots, f"{name}.png")
        os.replace(path, dst)
        print(f"  shot  {dst}")


# ---------------------------------------------------------------------------
step("clean slate: stop other bridge clients")
# GETVAR/SETVAR carry no client name, so another client that registered
# "score" or "lives" (a game started by the Amiga's launcher, say) would
# answer for us. Stop everything that isn't Terra Strike first.
for line in tool("amiga_list_clients").split(":", 1)[-1].split(","):
    name = line.strip().split("(")[0]
    if name and name not in (CLIENT, "none"):
        print(f"  stopping {name}: " + tool("amiga_stop_client", name=name))
# The demo launcher (started from S:User-Startup) has a global input
# handler: it sees every key we inject and runs its selected game later.
if "launcher" in tool("amiga_dos_command", command="Status"):
    if CLIENT in tool("amiga_list_clients"):
        tool("amiga_stop_client", name=CLIENT)
        time.sleep(1.5)
    key("45")                                        # Esc quits the launcher
    time.sleep(1.5)
check("launcher" not in tool("amiga_dos_command", command="Status"), "launcher is not intercepting injected keys")
check(all(n.strip().split("(")[0] in (CLIENT, "none", "")
          for n in tool("amiga_list_clients").split(":", 1)[-1].split(",")), "no foreign bridge clients")

step("build, deploy, launch")
if not opt.no_build:
    out = tool("amiga_run", project="examples/terra_strike")
    print("  " + out.replace("\n", "\n  "))
    check("Build: OK" in out, "cross-compiled and deployed")
if CLIENT not in tool("amiga_list_clients"):
    tool("amiga_launch", command="Dropbox:Dev/terra_strike")
check(bool(wait_for(lambda: CLIENT in tool("amiga_list_clients"), 15)), "game registered with the bridge")
t_start = time.time() - 2
check(bool(wait_for(lambda: var("frame") and var("frame") > 10)), "main loop is running (frame counter advances)")

step("in-game self-test hook")
out = tool("amiga_call_hook", client=CLIENT, hook="selftest")
print("  " + out)
res = wait_for(lambda: [m for m in logs_since(t_start) if "SELFTEST" in m], 40, 1.0)
print("  " + (res[-1] if res else "no SELFTEST result"))
check(bool(res) and " 0 failed" in res[-1], "unit tests pass inside the running game")

step("main screen (ChatGPT artwork) and music")
check(var("state") == 0, "game starts on the title screen")
print("  " + tool("amiga_call_hook", client=CLIENT, hook="title", args="image"))
check(var("title_phase") == 0, "main screen artwork showing")
check("$017" in tool("amiga_get_palette").split("\n")[2], "title palette loaded (pen 1 = $017)")
shot("00_main_screen")
try:
    from PIL import Image
    im = Image.open(os.path.join(opt.shots, "00_main_screen.png")).convert("RGB")
    lit = sum(1 for y in range(238, 246) for x in range(80, 240)
              if im.getpixel((x, y)) in ((255, 255, 0), (238, 238, 238)))
    check(lit > 100, f"PRESS FIRE TO START drawn over the artwork ({lit} px)")
except ImportError:
    pass
audio = tool("amiga_audio_channels")
check("DMA" in audio or "ch" in audio.lower(), "Paula channels readable (Protracker)")
shot("01_title")

step("key injection: start and lift off")
key("44")                                            # Return
check(wait_for(lambda: var("state") == 1), "Return starts the game")
check("$FFF" in tool("amiga_get_palette").split("\n")[2], "game palette restored (pen 1 = $FFF)")
check(var("pstate") == 0, "craft waits on the landing pad")
key("4C", 1.2)                                       # hold cursor up
check(wait_for(lambda: var("pstate") == 1), "holding UP lifts off")
check(var("alt") > 60, f"climbing (alt {var('alt')})")
check(any("TAKEOFF" in m for m in wait_for(lambda: logs_since(t_start))), "TAKEOFF in the remote log")
shot("02_airborne")

step("scene hook: tank battle, fire with aim assist")
print("  " + tool("amiga_call_hook", client=CLIENT, hook="scene", args="tanks"))
s0, t0 = var("score"), var("targets")
tool("amiga_input_key", rawkey="40", direction="down")      # hold space
time.sleep(1.0)
shot("03_firing")
time.sleep(1.5)
tool("amiga_input_key", rawkey="40", direction="up")
check(wait_for(lambda: var("score") >= s0 + 100), f"tank destroyed (score {var('score')})")
check(var("targets") < t0, f"targets down to {var('targets')}")
check(any("KILL tank" in m for m in logs_since(t_start)), "KILL tank in the remote log")

step("scene hook: ships at sea")
print("  " + tool("amiga_call_hook", client=CLIENT, hook="scene", args="sea"))
s0 = var("score")
tool("amiga_input_key", rawkey="40", direction="down")
time.sleep(2.5)
tool("amiga_input_key", rawkey="40", direction="up")
check(wait_for(lambda: var("score") >= s0 + 150), f"ship sunk (score {var('score')})")
shot("04_sea")

step("landing on the pad")
print("  " + tool("amiga_call_hook", client=CLIENT, hook="scene", args="landing"))
key("4D", 4.0)                                       # hold cursor down
check(wait_for(lambda: var("pstate") == 0), "craft landed")
check(any("LANDED" in m for m in logs_since(t_start)), "LANDED in the remote log")
f0 = var("fuel")
tool("amiga_set_var", name="fuel", value="300")
check(wait_for(lambda: (var("fuel") or 0) > 300), "pad refuels the craft")
shot("05_landed")

step("live tuning through bridge variables")
tool("amiga_set_var", name="turn_rate", value="5")
check(var("turn_rate") == 5, "turn_rate set to 5 while running")
y0 = var("yaw")
key("4C", 0.6)                                       # lift off again
key("4E", 0.5)                                       # turn right
dy = ((var("yaw") or 0) - (y0 or 0)) & 255
check(dy > 20, f"turns faster after retune (yaw moved {dy})")
tool("amiga_set_var", name="turn_rate", value="2")

step("memory region: heightmap")
out = tool("amiga_read_memregion", client=CLIENT, region="heightmap")
if out.startswith("Error"):          # a stale VAR error can land on this call's err stream
    print("  retry after: " + out)
    out = tool("amiga_read_memregion", client=CLIENT, region="heightmap")
check(not out.startswith("Error") and len(out) > 200, "heightmap region readable (bridge returns the first 256 bytes)")
print("  " + out[:160].replace("\n", " ") + " ...")

step("level clear")
lvl = var("level")
print("  " + tool("amiga_call_hook", client=CLIENT, hook="clear"))
check(wait_for(lambda: var("state") == 2, 5), "level-clear state")
check(wait_for(lambda: var("level") == lvl + 1, 10), f"advances to level {lvl + 1}")
check(any("CLEAR" in m for m in logs_since(t_start)), "LEVEL CLEAR in the remote log")
shot("06_level2")

step("game over")
tool("amiga_set_var", name="lives", value="1")
tool("amiga_set_var", name="god_mode", value="0")
print("  " + tool("amiga_call_hook", client=CLIENT, hook="scene", args="tanks"))
key("4D", 5.0)                                       # dive into the ground
check(wait_for(lambda: var("state") == 3, 10), "crash with the last life ends the game")
check(any("GAME OVER" in m for m in logs_since(t_start)), "GAME OVER in the remote log")
shot("07_gameover")

step("status hook, process list, stop")
print("  " + tool("amiga_call_hook", client=CLIENT, hook="status"))
print("  " + tool("amiga_proc_list").replace("\n", "\n  "))
print("  " + tool("amiga_stop_client", name=CLIENT))
check(wait_for(lambda: CLIENT not in tool("amiga_list_clients"), 8), "game stopped cleanly")

print(f"\n{'ALL PASSED' if failures == 0 else f'{failures} FAILED'}")
sys.exit(failures)
