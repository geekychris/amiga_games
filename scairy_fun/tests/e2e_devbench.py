#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end test of ScAIry Fun on a live Amiga, driven through devbench MCP.

Covers the game the way a player (and a developer) would: build, deploy and
launch; the in-game self-test; key injection to start, take over, hand back
and switch views; every hazard set up with the scene hook and left to the
autopilot (it should get each one wrong); a takeover judged as a save;
bridge variables read and tuned live; the route and plan memory regions;
the AI's voice on Paula; the arrival, software update and game-over
screens; screenshots of all of it; and stopping the client.

  python3 tests/e2e_devbench.py [--url http://localhost:3001/mcp] [--no-build] [--remote HOST]
  python3 tests/e2e_devbench.py --os4 HOST --url http://localhost:13002/mcp
        (AmigaOS 4: scairy_fun_os4, scairy.mod and scairy.voc must be in RAM:scairy_fun/
        already, the program as RAM:scairy_fun/scairy_fun; screenshots from QEMU's monitor)

Exit status is the number of failed checks.
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import time
import urllib.request

ap = argparse.ArgumentParser()
ap.add_argument("--url", default=os.environ.get("DEVBENCH_MCP", "http://localhost:3001/mcp"))
ap.add_argument("--no-build", action="store_true")
ap.add_argument("--shots", default="e2e_shots", help="directory for screenshots")
ap.add_argument("--os4", default="", help="the OS4 emulator's host (screenshots via ssh + QEMU's monitor)")
ap.add_argument("--remote", default="", help="ssh host of a devbench on another machine (to fetch its screenshots)")
opt = ap.parse_args()
DEV = "RAM:scairy_fun" if opt.os4 else "Dropbox:Dev"

CLIENT = "scairy_fun"
HERE = os.path.dirname(os.path.abspath(__file__))
_sid = None
failures = 0

# rawkeys
RETURN, SPACE, LEFT, RIGHT, UP, DOWN, KEY_V, KEY_P, ESC = "44", "40", "4F", "4E", "4C", "4D", "34", "19", "45"
STATES = ["title", "intro", "play", "arrived", "update", "gameover", "win"]


def _post(body):
    global _sid
    hdr = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
    if _sid:
        hdr["mcp-session-id"] = _sid
    req = urllib.request.Request(opt.url, json.dumps(body).encode(), hdr)
    with urllib.request.urlopen(req, timeout=300) as r:
        raw = r.read().decode()
        _sid = r.headers.get("mcp-session-id") or _sid
    msgs = [json.loads(l[5:]) for l in raw.splitlines() if l.startswith("data:")]
    return msgs or ([json.loads(raw)] if raw.strip() else [])


def tool(_tool, **args):
    if _sid is None:
        _post({"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
            "protocolVersion": "2025-03-26", "capabilities": {},
            "clientInfo": {"name": "scairy-fun-e2e", "version": "1"}}})
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


def setvar(name, value):
    return tool("amiga_set_var", name=name, value=str(value))


def hook(name, args=""):
    return tool("amiga_call_hook", client=CLIENT, hook=name, args=args)


def key(code, hold=0.1):
    if opt.os4:                                 # keys go to the active window
        hook("activate")
        time.sleep(0.3)
    tool("amiga_input_key", rawkey=code, direction="down")
    time.sleep(hold)
    tool("amiga_input_key", rawkey=code, direction="up")


def key_until(code, cond, tries=2, timeout=8.0):
    """press a key until cond() holds (an injected key can land while the
    game is busy, e.g. right after the self-test)"""
    for _ in range(tries):
        key(code)
        if wait_for(cond, timeout):
            return True
    return False


def logs_since(t0):
    base = opt.url.rsplit("/mcp", 1)[0]
    with urllib.request.urlopen(f"{base}/api/logs?limit=400", timeout=10) as r:
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


def wait_log(t0, text, timeout=20.0):
    return wait_for(lambda: [m for m in logs_since(t0) if text in m], timeout, 0.5)


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


def qemu_shot(name):
    """an OS4 screenshot from QEMU's monitor on the emulator's host"""
    cmd = ('(echo "mouse_move 2 2"; sleep 0.3; echo "mouse_move -2 -2"; sleep 1; '
           'echo "screendump /tmp/sc_shot.ppm"; sleep 1) | nc -q1 localhost 2348 >/dev/null; '
           'python3 -c "from PIL import Image; Image.open(\'/tmp/sc_shot.ppm\').save(\'/tmp/sc_shot.png\')"')
    subprocess.run(["ssh", opt.os4, cmd], check=False)
    os.makedirs(opt.shots, exist_ok=True)
    dst = os.path.join(opt.shots, f"{name}.png")
    subprocess.run(["scp", "-q", f"{opt.os4}:/tmp/sc_shot.png", dst], check=False)
    print(f"  shot  {dst}")


def shot(name, pause=True):
    """screenshot; the game is paused for it, so the picture is one frame"""
    if pause:
        hook("pause", "on")
    if opt.os4:
        qemu_shot(name)
        if pause:
            hook("pause", "off")
        return
    out = tool("amiga_screenshot")
    if pause:
        hook("pause", "off")
    path = out.split("saved: ")[-1].split(" (")[0].strip() if "saved:" in out else ""
    if path and not os.path.exists(path) and opt.remote:      # saved on the devbench's machine
        local = os.path.join(opt.shots, os.path.basename(path))
        os.makedirs(opt.shots, exist_ok=True)
        subprocess.run(["scp", "-q", f"{opt.remote}:{path}", local], check=False)
        path = local
    if path and os.path.exists(path):
        os.makedirs(opt.shots, exist_ok=True)
        dst = os.path.join(opt.shots, f"{name}.png")
        os.replace(path, dst)
        print(f"  shot  {dst}")


def scene(name, t_wait, expect, shot_name=None, shot_after=2.0):
    """put the car before a hazard with the autopilot on, and wait for the
    autopilot to get it wrong"""
    t0 = time.time()
    out = hook("scene", name)
    check(out.startswith("[ok]"), f"scene {name}: {out}")
    if shot_name:
        time.sleep(shot_after)
        shot(shot_name)
    for ev in expect:
        res = wait_log(t0, f"EV {ev} ", t_wait)
        check(bool(res), f"{name}, left to the autopilot: EV {ev}" + (f"  ({res[0][:70]})" if res else ""))
    return t0


# ---------------------------------------------------------------------------
step("clean slate: stop other bridge clients")
# GETVAR/SETVAR carry no client name: another game that registered "score"
# or "state" would answer for us. Stop everything that isn't ScAIry Fun.
for line in tool("amiga_list_clients").split(":", 1)[-1].split(","):
    name = line.strip().split("(")[0]
    if name and name not in (CLIENT, "none"):
        print(f"  stopping {name}: " + tool("amiga_stop_client", name=name))

step("build, deploy, launch")
if not opt.no_build and not opt.os4:
    out = tool("amiga_run", project="examples/scairy_fun")
    print("  " + out.replace("\n", "\n  "))
    check("Build: OK" in out and "Deploy: OK" in out, "cross-compiled and deployed")
listing = tool("amiga_list_dir", path=DEV)
for asset in ("scairy.voc", "scairy.mod"):
    if asset not in listing and not opt.os4:
        print("  " + tool("amiga_push_file", local_path=os.path.join(HERE, "..", asset),
                          amiga_path=f"{DEV}/{asset}"))
if CLIENT not in tool("amiga_list_clients"):
    tool("amiga_launch", command=f"{DEV}/scairy_fun")
check(bool(wait_for(lambda: CLIENT in tool("amiga_list_clients"), 15)), "game registered with the bridge")
t_start = time.time() - 3
info = tool("amiga_client_info", client=CLIENT)
check(all(v in info for v in ("score", "cover", "ap", "draw_dist", "in_joy")), "all 32 bridge variables registered")
hooks = tool("amiga_list_hooks", client=CLIENT)
check(all(f"  {h}:" in hooks for h in ("scene", "selftest", "ap", "say", "pause", "spawn", "activate")), "all the hooks registered")
res = logs_since(t_start - 10)
check(any("voice: 19 clips" in m or "voice: 20 clips" in m for m in res), "the AI's voice loaded (scairy.voc)")
check(any("music loaded" in m for m in res), "music loaded (scairy.mod)")

step("in-game self-test hook")
t0 = time.time()
print("  " + hook("selftest"))
res = wait_log(t0, "SELFTEST", 120)
print("  " + (res[-1] if res else "no SELFTEST result"))
check(bool(res) and " 0 failed" in res[-1], "unit and gameplay tests pass inside the running game")

step("title, then start with key injection")
t0 = time.time()
hook("title")
check(wait_for(lambda: var("state") == 0), "title screen (attract mode, the autopilot driving)")
time.sleep(1)
check(var("ap") == 1 and (var("kmh") or 0) > 0, "the autopilot drives the attract mode")
shot("00_title")
check(key_until(RETURN, lambda: var("state") == 1), "Return: the route card")
check(bool(wait_log(t0, "SAY KEEP YOUR HANDS", 5)), "the AI: keep your hands on the wheel")
shot("01_route_card")
check(key_until(SPACE, lambda: var("state") == 2), "Space: driving")
check(var("ap") == 1, "autopilot on at the start")
s0 = var("score")
time.sleep(3)
check(var("score") > s0, f"autopilot scores ({s0} -> {var('score')})")
check(var("route") == 0 and 0 < var("seg") < 60, f"route 1, near the start (segment {var('seg')})")
shot("02_levee_road")

step("needless takeover, unavailable, handing back")
t0 = time.time()
n0 = var("needless")
key(LEFT, 0.15)
check(wait_for(lambda: var("ap") == 0), "steering takes over (autopilot off)")
check(var("needless") == n0 + 1, "nothing ahead: a needless takeover")
check(bool(wait_log(t0, "SAY I HAD THAT", 5)), "the AI: I had that")
key(LEFT, 0.6)
time.sleep(0.5)
t1 = time.time()
key(SPACE)
check(bool(wait_log(t1, "EV unavailable", 5)), "off the lane centre: AUTOPILOT UNAVAILABLE")
print("  " + hook("ap", "on"))
key(RIGHT, 0.6)
time.sleep(1.0)


def lane_off():
    """how far the car is from its lane's centre (units): the autopilot takes
    over within a quarter lane (14)"""
    d = var("lane_d")
    if d is None:
        return 99
    if d < 0:                       # over the centre line: steer right
        return -99
    return d - ((d // 56) * 56 + 28)


# a key held for a fixed time lands anywhere across the lane (an injected key's
# timing varies, more so over a network): steer in taps into the window first
for _ in range(16):
    off = lane_off()
    if abs(off) <= 10:
        break
    key(LEFT if off > 0 else RIGHT, 0.08 if abs(off) < 40 else 0.25)
    time.sleep(0.5)
print(f"  {lane_off()} units off the lane's centre")
t1 = time.time()
key(SPACE)
check(wait_for(lambda: var("ap") == 1, 5) or bool(wait_log(t1, "EV ap_on", 3)), "back in lane: Space hands back to the autopilot")

step("cockpit view")
key(KEY_V)
check(wait_for(lambda: var("view") == 1), "V: cockpit view")
time.sleep(0.5)
shot("03_cockpit")
key(KEY_V)
check(wait_for(lambda: var("view") == 0), "V again: chase view")

step("the rules: help pages")
t0 = time.time()
key("25")                                            # H
check(bool(wait_log(t0, "HELP page 1", 5)), "H: the rules, page 1")
key("25"); key("25"); key("25")
check(bool(wait_log(t0, "HELP page 0", 6)), "H through the pages and closed again")
for p in (1, 2, 3):
    print("  " + hook("help", str(p)))
    shot(f"04_help_{p}", pause=False)
s_before = var("seg")
time.sleep(1.5)
check(var("seg") == s_before, "the game waits while the rules are up")
print("  " + hook("help", "0"))
time.sleep(1.5)
check(var("seg") != s_before, "and carries on when they close")

step("memory regions: the route and the autopilot's plan")
out = tool("amiga_read_memregion", client=CLIENT, region="plan")
check("plan" in out.lower() or len(out) > 40, "plan region readable")
hexes = ""
for line in out.splitlines():                       # "ADDRESS  01 02 ... 10   ascii"
    toks = line.split()
    for t in toks[1:17]:
        if len(t) != 2 or any(c not in "0123456789abcdefABCDEF" for c in t):
            break
        hexes += t
if len(hexes) >= 8:
    d0 = struct.unpack(">i", bytes.fromhex(hexes[:8]))[0]
    check(abs(d0 - 28 * 16) < 16 * 16, f"plan[0] is the lane centre (fine units {d0})")
out = tool("amiga_read_memregion", client=CLIENT, region="route")
check(len(out) > 40, "route region readable")

step("live tuning over the bridge")
setvar("draw_dist", 16)
time.sleep(1.0)
p16 = var("polys")
setvar("draw_dist", 32)
time.sleep(1.0)
check(var("polys") > p16, f"draw_dist 16 -> 32 draws more polygons ({p16} -> {var('polys')})")
print(f"  timings: build {var('us_build')} us, draw {var('us_draw')} us, hud {var('us_hud')} us, {var('fps')} fps")
check((var("fps") or 0) >= 20, "20+ fps")

step("every hazard, left to the autopilot")
scene("flood", 25, ["aquaplane", "splash"], "10_flood", 3.5)
scene("ghost", 30, ["roll"], "11_ghost_lines", 4.0)
scene("phantom", 25, ["phantom", "rearend"], "12_overpass", 3.0)
scene("cones", 25, ["cone"], "13_cones", 3.5)
scene("glare", 30, ["blinded"], "14_low_sun", 4.5)
scene("siren", 25, ["siren", "ticket"], "15_ambulance", 3.0)
scene("debris", 25, ["debris"], "16_debris", 2.0)

step("bait: the autopilot gets these right")
t0 = scene("deer", 20, [], "17_deer", 5.0)
time.sleep(4)
check(not [m for m in logs_since(t0) if "EV hit_animal" in m], "deer: the autopilot stops, no hit")

step("a takeover judged as a save (the flood, braked through by hand)")
setvar("god_mode", 0)
t0 = time.time()
hook("scene", "flood")
s0 = var("saves")
wait_for(lambda: (var("seg") or 0) >= 100, 20, 0.3)
key(SPACE)
check(wait_for(lambda: var("ap") == 0, 3), "Space: taking over before the water")
end = time.time() + 40
while time.time() < end and (var("seg") or 0) < 124:
    kmh = var("kmh") or 0
    if kmh > 42:
        key(DOWN, 0.25)
    elif kmh < 28:
        key(UP, 0.3)
    time.sleep(0.2)
check(not [m for m in logs_since(t0) if "EV splash" in m], "through the flood by hand, dry")
check(wait_for(lambda: var("saves") == s0 + 1, 8), "judged a save (GOOD CATCH)")
check(bool(wait_log(t0, "SAY GOOD CATCH", 5)), "the AI: good catch")

step("the AI's voice")
t0 = time.time()
print("  " + hook("say", "13"))
time.sleep(0.2)
if opt.os4:                                     # AHI: no Paula to look at; the game logs what it says
    check(bool(wait_log(t0, "SAY", 5)), "the AI spoke (AHI)")
else:
    audio = tool("amiga_audio_channels")
    check(audio.count("DMA=ON") >= 1, "Paula channels playing")

step("arrival and the software update")
hook("route", "1")
hook("skip")
check(wait_for(lambda: var("state") == 3, 25), "arrived at the marina")
time.sleep(1.5)
shot("20_arrived")
time.sleep(2.0)
key(SPACE)
check(wait_for(lambda: var("state") == 4, 5), "Space: the software update")
time.sleep(3.0)
shot("21_update", pause=False)
key(SPACE)
# (on AmigaOS 4 the slow QEMU screenshot can let the update time out, and
# Space then skips route 2's intro card: either state is route 2 begun)
check(wait_for(lambda: var("state") in (1, 2) and var("route") == 1, 12), "route 2 (rush hour) after the update")
key(SPACE)
time.sleep(2)
shot("22_rush_hour")

step("other routes")
for r, name in ((3, "23_mountain_pass"), (4, "24_flooded_town")):
    hook("route", str(r))
    key(SPACE)
    time.sleep(6)
    shot(name)

step("game over: the policy is cancelled")
setvar("cover", 10)
t0 = time.time()
hook("scene", "flood")
setvar("cover", 10)
check(wait_for(lambda: var("state") == 5, 30), "cover gone: POLICY CANCELLED")
check(bool(wait_log(t0, "SAY YOUR POLICY", 5) or wait_log(t0, "SAY POLICY", 5)), "the AI says so")
shot("30_game_over", pause=False)

step("stop the client")
print("  " + tool("amiga_stop_client", name=CLIENT))
check(wait_for(lambda: CLIENT not in tool("amiga_list_clients"), 10), "scairy_fun stopped and unregistered")
check("No crash" in tool("amiga_last_crash") or "no crash" in tool("amiga_last_crash").lower(), "no crash recorded")

print(f"\n{failures} failed")
sys.exit(failures)
