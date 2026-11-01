#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""e2e_devbench.py - Boing Twist on the AmigaOS 4 guest (QEMU), driven
through devbench: the self-test against the host's vector hash, scrambles
and moves (hooks and keys) checked against the host's puzzle core, undo,
peek, themes, solving, a frame-rate bench, screenshots, and quitting with
Esc.

  python3 tests/e2e_devbench.py [--url http://localhost:3010/mcp] [--deploy]
          [--res 800] [--bench-res 640,800,1024] [--shots screenshots/e2e]

--deploy pushes the binary (stripped, if boing_twist.stripped is newer),
data/ and themes/ to RAM:boing_twist/ first (the HDF is never written while
QEMU runs). Screenshots come from QEMU's monitor (--monitor host:port).
The emulator lock (/tmp/amiga-emu.lock) is held throughout, with amiga_mcp's
"CLAUDE IS TESTING" banner on the Amiga's screen (scripts/claude_busy.py).
--art loads every picture and room, checks memory, and photographs the
pictures that come with a room in it.
"""
import argparse
import fcntl
import json
import os
import socket
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(HERE)
CLIENT = "boing_twist"
RIGHT, LEFT, UP, DOWN, SPACE, RETURN, ESC = "4E", "4F", "4C", "4D", "40", "44", "45"


class Bench:
    def __init__(self, url, monitor, shots):
        self.url, self.monitor, self.shots = url, monitor, shots
        self.sid = None
        self.passes = self.failures = 0

    # ---------- MCP ----------
    def _post(self, body):
        hdr = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.sid:
            hdr["mcp-session-id"] = self.sid
        with urllib.request.urlopen(urllib.request.Request(self.url, json.dumps(body).encode(), hdr), timeout=600) as r:
            raw = r.read().decode()
            self.sid = r.headers.get("mcp-session-id") or self.sid
        msgs = [json.loads(l[5:]) for l in raw.splitlines() if l.startswith("data:")]
        return msgs or ([json.loads(raw)] if raw.strip() else [])

    def tool(self, _name, **args):
        if self.sid is None:
            self._post({"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
                "protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "boing-twist-e2e", "version": "1"}}})
            self._post({"jsonrpc": "2.0", "method": "notifications/initialized"})
        res = self._post({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                          "params": {"name": _name, "arguments": args}})[-1].get("result", {})
        return "\n".join(c.get("text", "") for c in res.get("content", []) if c.get("type") == "text")

    def hook(self, name, args=""):
        return self.tool("amiga_call_hook", client=CLIENT, hook=name, args=str(args))

    def var(self, name):
        """one of the game's values, from its state hook (devbench's own
        variables are found by name alone, and another game may have one
        of the same name)"""
        out = self.hook("state")
        for kv in out.split():
            if kv.startswith(name + "="):
                try:
                    return int(kv.split("=", 1)[1])
                except ValueError:
                    return None
        return None

    def key(self, code, hold=0.08):
        self.hook("activate")
        time.sleep(0.3)
        self.tool("amiga_input_key", rawkey=code, direction="down")
        time.sleep(hold)
        self.tool("amiga_input_key", rawkey=code, direction="up")

    def clients(self):
        return self.tool("amiga_list_clients")

    def logs_since(self, t0):
        base = self.url.rsplit("/mcp", 1)[0]
        with urllib.request.urlopen(f"{base}/api/logs?limit=400", timeout=10) as r:
            d = json.load(r)
        out = []
        for l in (d.get("logs", d) if isinstance(d, dict) else d):
            ts = l.get("timestamp")
            if isinstance(ts, str):
                from datetime import datetime
                ts = datetime.fromisoformat(ts).timestamp()
            if l.get("client") == CLIENT and ts and ts >= t0 - 2:
                out.append(l.get("message", ""))
        return out

    @staticmethod
    def wait(fn, timeout=10.0, every=0.4):
        end = time.time() + timeout
        while time.time() < end:
            v = fn()
            if v:
                return v
            time.sleep(every)
        return fn()

    # ---------- results ----------
    def check(self, cond, what):
        print(f"  {'PASS' if cond else 'FAIL'}  {what}", flush=True)
        if cond:
            self.passes += 1
        else:
            self.failures += 1
        return cond

    @staticmethod
    def step(title):
        print(f"\n== {title}", flush=True)

    # ---------- screenshots (QEMU's monitor) ----------
    def shot(self, name, crop=True):
        host, port = self.monitor.split(":")
        ppm = os.path.join("/tmp", f"boing_twist_{os.getpid()}.ppm")
        try:
            s = socket.create_connection((host, int(port)), timeout=5)
            s.sendall(f"screendump {ppm}\n".encode())
            time.sleep(1.0)
            s.close()
        except OSError as e:
            print(f"  shot  failed: {e}")
            return None
        time.sleep(0.3)
        try:
            from PIL import Image
            im = Image.open(ppm).convert("RGB")
            if crop:
                box = window_box(im)
                if box:
                    im = im.crop(box)
            os.makedirs(self.shots, exist_ok=True)
            dst = os.path.join(self.shots, name + ".png")
            im.save(dst)
            os.remove(ppm)
            print(f"  shot  {dst}")
            return dst
        except Exception as e:      # no Pillow, or no file
            print(f"  shot  failed: {e}")
            return None


def window_box(im):
    """the game's window: it opens at the top left of the Workbench, its
    inner area 5 pixels in and 62 down (the OS4 default window border and
    title bar); full screen (a frame too big for the Workbench) isn't cropped"""
    w, h = FRAME
    if 5 + w > im.size[0] or 62 + h > im.size[1]:
        return None
    return (5, 62, 5 + w, 62 + h)


FRAME = (800, 600)


def host_hash(*args):
    exe = os.path.join(GAME, "tests", "host_tests")
    if not os.path.exists(exe):
        subprocess.run(["make", "-C", GAME, "-s", "host-test"], check=True, stdout=subprocess.DEVNULL)
    out = subprocess.run([exe, "--hash"] + [str(a) for a in args], capture_output=True, text=True, check=True).stdout.split()
    return out[0], int(out[1])


def game_hash(b):
    out = b.hook("hash")
    try:
        return out.split("hash=")[1].split()[0]
    except IndexError:
        return None


def deploy(b, files_too):
    b.step("deploy to RAM:boing_twist/")
    b.tool("amiga_run_script", script="makedir RAM:boing_twist RAM:boing_twist/data RAM:boing_twist/themes", timeout=30)
    exe = os.path.join(GAME, "boing_twist")
    stripped = exe + ".stripped"
    if os.path.exists(stripped) and os.path.getmtime(stripped) >= os.path.getmtime(exe):
        exe = stripped
    out = b.tool("amiga_push_file", local_path=exe, amiga_path="RAM:boing_twist/boing_twist")
    b.check("Transferred" in out, f"binary: {out[:70]}")
    if files_too:
        for sub, ext in (("data", ".btx"), ("themes", "")):
            for f in sorted(os.listdir(os.path.join(GAME, sub))):
                if f.endswith(ext) and not f.startswith(".") and os.path.isfile(os.path.join(GAME, sub, f)):
                    out = b.tool("amiga_push_file", local_path=os.path.join(GAME, sub, f), amiga_path=f"RAM:boing_twist/{sub}/{f}")
                    b.check("Transferred" in out, f"{sub}/{f}: {out[:60]}")


def launch(b, args):
    b.tool("amiga_launch", command=f"RAM:boing_twist/boing_twist {args}")
    ok = b.wait(lambda: CLIENT in b.clients(), 30, 1.0)
    time.sleep(2.0)
    return ok


def quit_game(b):
    b.hook("quit")
    return b.wait(lambda: CLIENT not in b.clients(), 15, 1.0)


def bench(b, frames=40):
    t0 = time.time()
    b.hook("bench", frames)
    line = b.wait(lambda: [m for m in b.logs_since(t0) if "bench " in m and "fps" in m], 60, 1.0)
    return line[-1] if line else None


def park_pointer(b):
    """the mouse pointer off to the right, out of the game's window"""
    b.tool("amiga_input_mouse_move", dx=3000, dy=3000)


def readme_shots(b, npics):
    """the README's pictures, in screenshots/"""
    keep = b.shots
    b.shots = os.path.join(GAME, "screenshots")
    b.step("README screenshots")
    park_pointer(b)
    b.hook("theme", 0)
    b.hook("screen", "title")
    time.sleep(1.5)
    b.shot("title")
    b.hook("picture", 1)
    b.hook("screen", "select")
    time.sleep(2.5)
    b.shot("select")
    b.hook("picture", min(4, npics - 1))
    b.hook("scramble", "21 25 1")
    b.hook("view", "-150 18 3.6 2 3")
    time.sleep(1.2)
    b.shot("scrambled")
    b.hook("move", "F3")
    time.sleep(0.15)
    b.hook("pause", "on")
    time.sleep(0.6)
    b.shot("mid_move")
    b.hook("pause", "off")
    b.wait(lambda: b.var("anim") == 0, 5)
    for i, name in ((1, "room_midnight"), (2, "room_space")):
        b.hook("theme", i)
        time.sleep(1.2)
        b.shot(name)
    b.hook("theme", 0)
    b.hook("picture", 1)
    b.hook("scramble", "4 3 0")
    exe = os.path.join(GAME, "tests", "host_tests")
    for mv in subprocess.run([exe, "--undo", "4", "8", "4", "3"], capture_output=True, text=True).stdout.split():
        b.hook("move", mv)                   # played back as the player's moves
    b.check(b.wait(lambda: b.var("screen") == 3, 15), "solved by the player's moves")
    local = os.path.join("/tmp", f"boing_twist_best_{os.getpid()}.txt")
    b.tool("amiga_pull_file", amiga_path="RAM:boing_twist/boing_twist.best", local_path=local)
    best = open(local).read() if os.path.exists(local) else ""
    b.check("1_earth.btx|EASY " in best, f"best scores kept per picture and level: {best.split()[:3]}")
    time.sleep(1.0)
    b.shot("solved")
    b.hook("screen", "title")
    b.shots = keep


def free_fast(b, newer_than=0.0, timeout=20):
    """the Amiga's free fast memory from the bridge's next heartbeat"""
    base = b.url.rsplit("/mcp", 1)[0]
    end = time.time() + timeout
    while time.time() < end:
        with urllib.request.urlopen(f"{base}/api/status", timeout=10) as r:
            s = json.load(r)
        if (s.get("lastHeartbeatAt") or 0) > newer_than:
            return s["lastHeartbeat"]["freeFast"]
        time.sleep(1)
    return None


def all_art(b, npics, nthemes):
    """every picture and every room loads"""
    b.step("every picture and room loads")
    t0 = time.time()
    names = {}
    for i in range(npics):
        names[i] = b.hook("picture", i)
        b.hook("screen", "select")
        time.sleep(0.6)                      # loaded once the choice has stayed put 0.25 s
    time.sleep(1.0)
    logs = b.logs_since(t0)
    decoded = [m for m in logs if "decoded in" in m]
    ms = [int(m.split("decoded in ")[1].split()[0]) for m in decoded]
    b.check(len(decoded) >= npics - 1, f"{len(decoded)} pictures decoded (of {npics - 1} files)"
            + (f": {min(ms)}-{max(ms)} ms, average {sum(ms) // len(ms)} ms" if ms else ""))
    b.check(not [m for m in logs if "can't load" in m or "not a picture" in m], "none failed to load")
    t1 = time.time()
    for i in range(nthemes):
        out = b.hook("theme", i)
        b.check(out.startswith("[ok]"), out.strip()[:60])
        time.sleep(0.8)
    missing = [m for m in b.logs_since(t1) if "missing" in m]
    b.check(not missing, "every room's textures found" + (f": {missing[:2]}" if missing else ""))
    b.hook("theme", 0)
    return names


def paired_shots(b, names, shots):
    keep = b.shots
    b.shots = shots
    b.step("pictures in their rooms")
    want = ["CORAL REEF", "STEAMPUNK", "OLD MAP", "ALIEN WORLD", "CIRCUIT BOARD", "FLOWER GARDEN"]
    park_pointer(b)
    for i, out in names.items():
        title = next((w for w in want if f" {w} level" in out), None)
        if not title:
            continue
        lv = {"EASY": 0, "MEDIUM": 1, "HARD": 2, "EXPERT": 3}[out.split(" level ")[1].split()[0]]
        b.hook("theme", 0)
        b.hook("picture", i)
        room = b.var("theme")
        b.check(room not in (None, 0), f"{title} brings its room (theme {room})")
        b.hook("scramble", f"{i} -1 {lv}")
        b.check(b.wait(lambda: b.var("anim") == 0, 60), f"{title} scrambled")
        b.hook("view", "-160 20 3.6 1 2")
        time.sleep(1.2)
        b.shot("art_" + title.lower().replace(" ", "_"))
    b.hook("theme", 0)
    b.shots = keep


def run(b, a):
    global FRAME
    res = int(a.res)
    FRAME = {640: (640, 480), 800: (800, 600), 1024: (1024, 768)}[res]
    if a.deploy:
        deploy(b, not a.no_data)

    b.step(f"launch RES={res}")
    if CLIENT in b.clients():
        quit_game(b)
    mem0 = free_fast(b, time.time()) if a.art else None
    b.check(launch(b, f"RES={res}"), "the game registers with the bridge")
    b.check(b.var("width") == FRAME[0] and b.var("height") == FRAME[1], f"frame {FRAME[0]} x {FRAME[1]}")
    npics, nthemes = b.var("npics"), b.var("nthemes")
    b.check(npics is not None and npics >= 2, f"pictures listed from data/: {npics}")
    b.check(nthemes is not None and nthemes >= 1, f"themes listed from themes/: {nthemes}")

    if a.art:
        mem1 = free_fast(b, time.time())
        names = all_art(b, npics, nthemes)
        mem2 = free_fast(b, time.time())
        all_art(b, npics, nthemes)                   # again: nothing should be left behind
        mem2b = free_fast(b, time.time())
        if mem0 and mem1 and mem2 and mem2b:
            print(f"  info  free fast memory: {mem0 // 1024} KB before, {mem1 // 1024} KB with the game started "
                  f"(it takes {(mem0 - mem1) // 1024} KB), {mem2 // 1024} KB after loading every picture and room "
                  f"({(mem1 - mem2) // 1024} KB more: the allocator keeps its peak), {mem2b // 1024} KB after doing it again")
        b.check(None not in (mem2, mem2b) and mem2 - mem2b < 1024 * 1024,
                "a second pass over every picture and room takes no more memory (no leak)")
        paired_shots(b, names, os.path.join(GAME, "screenshots"))

    b.step("self-test on the Amiga: the puzzle core's vector hash")
    out = b.hook("selftest")
    b.check("PASS" in out, out.strip())

    b.step("title screen")
    park_pointer(b)
    b.hook("theme", 0)
    b.hook("screen", "title")
    b.check(b.var("screen") == 0, "on the title")
    time.sleep(1.0)
    b.shot("title")

    b.step("picture select")
    b.hook("picture", 1)
    b.hook("screen", "select")
    time.sleep(2.5)
    b.check(b.var("screen") == 1, "on the select screen")
    b.shot("select")

    b.step("levels, the picture list and rooms that come with pictures")
    info = {}
    for i in range(npics):
        out = b.hook("picture", i)
        info[i] = out
    levels = {i: next((l for l in ("EASY", "MEDIUM", "HARD", "EXPERT") if f"level {l}" in s), None) for i, s in info.items()}
    b.check(all(levels.values()), f"every picture has a level: {sorted(set(levels.values()))}")
    paired = [i for i, s in info.items() if "room -" not in s and "room" in s]
    hard = [i for i, l in levels.items() if l in ("HARD", "EXPERT") and i > 0]
    if hard:
        b.hook("picture", hard[0])
        b.hook("scramble", "1 -1 0")                 # Easy: that picture isn't offered
        b.check(b.var("level") == 0 and b.var("pic") != hard[0], f"Easy swaps picture {hard[0]} ({levels[hard[0]]}) for one it offers")
        b.check(b.var("bands") == 4, "Easy is 4 x 8")
        b.hook("picture", hard[0])
        b.hook("scramble", "1 -1 3")
        b.check(b.var("level") == 3 and b.var("pic") == hard[0] and b.var("bands") == 8, "Expert: 8 x 16, the picture kept")
    if paired:
        b.hook("theme", 0)
        b.hook("picture", paired[0])
        b.check(b.var("theme") != 0, f"picture {paired[0]} brings its room (theme 0 -> {b.var('theme')})")
        b.hook("theme", 0)
    if npics > 9:
        b.hook("screen", "select")
        b.hook("picture", npics - 2)
        time.sleep(2.0)
        b.shot("select_list")
        b.check(b.var("screen") == 1, f"the select list scrolls to picture {npics - 2} of {npics}")

    b.step("scramble and moves, against the host's puzzle core")
    b.hook("picture", 1)
    b.hook("scramble", "7 25 0")
    want, _ = host_hash(4, 8, 7, 25)
    b.check(game_hash(b) == want, f"scramble seed 7, 25 moves, 4x8: {want}")
    b.hook("move", "T1+!")
    b.hook("move", "F2!")
    want, _ = host_hash(4, 8, 7, 25, "T1+", "F2")
    b.check(game_hash(b) == want, f"T1+ F2 (straight away): {want}")
    b.hook("move", "T0-")
    b.wait(lambda: b.var("anim") == 0, 5)
    want, _ = host_hash(4, 8, 7, 25, "T1+", "F2", "T0-")
    b.check(game_hash(b) == want, f"T0- (animated): {want}")
    b.check(b.var("moves") == 3, "three moves counted")
    b.hook("undo")
    b.wait(lambda: b.var("anim") == 0, 5)
    want, _ = host_hash(4, 8, 7, 25, "T1+", "F2", "T0-", "U")
    b.check(game_hash(b) == want and b.var("moves") == 2, f"undo: {want}, two moves")
    b.check(b.var("secs") is not None and b.var("secs") >= 0, "the clock runs")

    b.step("keys")
    band, half = b.var("sel_band"), b.var("sel_half")
    b.key(RIGHT)
    b.wait(lambda: b.var("anim") == 0, 5)
    want, _ = host_hash(4, 8, 7, 25, "T1+", "F2", f"T{band}+")
    b.check(game_hash(b) == want, f"Right twists band {band} east: {want}")
    b.key(SPACE)
    b.wait(lambda: b.var("anim") == 0, 5)
    want, _ = host_hash(4, 8, 7, 25, "T1+", "F2", f"T{band}+", f"F{half}")
    b.check(game_hash(b) == want, f"Space flips half {half}: {want}")
    b.key(UP)
    b.check(b.var("sel_band") == max(0, band - 1), "Up picks the band above")
    b.key("16")                                  # U: undo
    b.wait(lambda: b.var("anim") == 0, 5)
    want, _ = host_hash(4, 8, 7, 25, "T1+", "F2", f"T{band}+")
    b.check(game_hash(b) == want, "U undoes the flip")

    b.step("screenshots: scrambled, mid-move, peek")
    b.hook("view", "-160 22 3.6 1 2")
    time.sleep(1.0)
    b.shot("scrambled")
    b.hook("move", "F2")
    time.sleep(0.25)
    b.hook("pause", "on")
    time.sleep(0.6)
    b.shot("mid_move")
    b.hook("pause", "off")
    b.wait(lambda: b.var("anim") == 0, 5)
    b.hook("peek", "on")
    time.sleep(1.0)
    b.check(game_hash(b) is not None, "peek leaves the puzzle alone")
    b.shot("peek")
    b.hook("peek", "off")

    b.step("themes")
    for i in range(min(nthemes or 0, 3)):
        out = b.hook("theme", i)
        b.check(out.startswith("[ok]"), out.strip())
        time.sleep(1.0)
        b.shot(f"theme_{i}")
    b.hook("theme", 0)

    b.step("solve (animated, replaying the moves backwards)")
    b.hook("solve", "anim")
    b.check(b.wait(lambda: b.var("screen") == 3, 60, 1.0), "the solved screen")
    b.check(b.var("solved") == 1, "the puzzle says solved")
    time.sleep(1.2)
    b.shot("solved")

    b.step("R gives up: an 80-move history unwound move by move")
    b.hook("scramble", "9 80 0")
    b.check(b.var("solved") == 0, "80 moves scrambled")
    b.key("13")                                  # R
    b.check(b.wait(lambda: b.var("screen") == 3, 120, 1.0), "solved for you")
    b.check(b.var("solved") == 1, "the puzzle says solved")

    b.step("a bigger ball: 6x12, an animated scramble, then solved at once")
    b.hook("picture", min(2, npics - 1))
    b.hook("scramble", "11 14 1")
    b.check(b.wait(lambda: b.var("anim") == 0, 30), "the scramble plays out")
    b.check(b.var("bands") in (None, 6), "6 bands")
    b.check(b.var("solved") == 0, "scrambled")
    time.sleep(0.5)
    b.shot("scrambled_6x12")
    b.hook("solve", "now")
    b.check(b.wait(lambda: b.var("screen") == 3, 10), "solved")

    b.step("help")
    b.hook("screen", "help")
    time.sleep(1.0)
    b.shot("help")
    b.hook("screen", "title")

    b.step(f"frame rate at {FRAME[0]} x {FRAME[1]}")
    b.hook("scramble", "3 25 0")
    line = bench(b)
    b.check(line is not None, line or "no bench result")

    if a.readme:
        readme_shots(b, npics)

    b.step("Esc on the title quits (once)")
    b.hook("screen", "title")
    time.sleep(0.5)
    b.key(ESC)
    gone = b.wait(lambda: CLIENT not in b.clients(), 15, 1.0)
    b.check(gone, "the game quit")
    if not gone:
        quit_game(b)
    b.check("launcher" in b.clients(), "the launcher is still running")
    if a.art and mem0:
        time.sleep(3)
        mem3 = free_fast(b, time.time())
        b.check(mem3 is not None and mem3 >= mem0 - 2 * 1024 * 1024,
                f"memory back after quitting: {mem3 // 1024 if mem3 else '?'} KB (before {mem0 // 1024} KB)")

    for r in [int(x) for x in a.bench_res.split(",") if x] if a.bench_res else []:
        b.step(f"bench RES={r}")
        if not launch(b, f"RES={r}" + (" FULLSCREEN" if r >= 1024 else "")):
            b.check(False, f"launch RES={r}")
            continue
        b.hook("picture", 1)
        b.hook("scramble", "3 25 0")
        time.sleep(1)
        line = bench(b)
        b.check(line is not None, line or "no bench result")
        b.check(quit_game(b), "quit")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:3010/mcp")
    ap.add_argument("--monitor", default="127.0.0.1:2348")
    ap.add_argument("--deploy", action="store_true")
    ap.add_argument("--no-data", action="store_true", help="with --deploy: only the binary")
    ap.add_argument("--res", default="800")
    ap.add_argument("--bench-res", default="", help="e.g. 640,1024: relaunch at each and bench")
    ap.add_argument("--shots", default=os.path.join(GAME, "screenshots", "e2e"))
    ap.add_argument("--no-lock", action="store_true")
    ap.add_argument("--readme", action="store_true", help="also take the README's screenshots")
    ap.add_argument("--art", action="store_true", help="load every picture and room, memory, paired-room shots")
    a = ap.parse_args()
    b = Bench(a.url, a.monitor, a.shots)
    busy = None
    if not a.no_lock:
        # amiga_mcp's claude_busy: the emulator lock and a "CLAUDE IS TESTING"
        # banner on the Amiga's screen while this runs; or just the lock
        for root in (os.environ.get("AMIGA_MCP", ""), os.path.dirname(os.path.dirname(GAME)),
                     os.path.expanduser("~/code/claude_world/amiga_mcp")):
            if root and os.path.exists(os.path.join(root, "scripts", "claude_busy.py")):
                sys.path.insert(0, os.path.join(root, "scripts"))
                from claude_busy import claude_testing
                busy = claude_testing(a.url, "Boing Twist tests")
                break
        if busy is None:
            import contextlib

            @contextlib.contextmanager
            def lock_only():
                f = open("/tmp/amiga-emu.lock", "w")
                fcntl.flock(f, fcntl.LOCK_EX)
                try:
                    yield
                finally:
                    fcntl.flock(f, fcntl.LOCK_UN)
            busy = lock_only()
    else:
        import contextlib
        busy = contextlib.nullcontext()
    with busy:
        try:
            run(b, a)
        finally:
            if CLIENT in b.clients():
                quit_game(b)
            if "launcher" not in b.clients():
                b.tool("amiga_launch", command="DH1:launcher")
    print(f"\n{b.passes} passed, {b.failures} failed")
    return 1 if b.failures else 0


if __name__ == "__main__":
    sys.exit(main())
