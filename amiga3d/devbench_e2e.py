# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""devbench_e2e.py - the shared engine's end-to-end test helpers: a game
driven on a live Amiga through devbench MCP (key injection, bridge
variables and hooks, the remote log, screenshots).

    from devbench_e2e import E2E
    t = E2E("stack_tower", url="http://localhost:3001/mcp", shots="e2e_shots")
    t.step("start"); t.key(t.RETURN); t.check(t.wait(lambda: t.var("state") == 1), "started")
    ...
    sys.exit(t.done())

An OS4 devbench shows QEMU's display, not just the game's window, so
screenshots there come from QEMU's monitor (qemu_shot).
"""
import json
import os
import subprocess
import time
import urllib.request
from datetime import datetime


class E2E:
    RETURN, SPACE, ESC, HELP = "44", "40", "45", "5F"
    LEFT, RIGHT, UP, DOWN = "4F", "4E", "4C", "4D"

    def __init__(self, client, url="http://localhost:3001/mcp", shots="e2e_shots", remote=""):
        self.client, self.url, self.shots = client, url, shots
        self.remote = remote            # ssh host of a devbench on another machine (for its screenshots)
        self.sid = None
        self.failures = 0
        self.passes = 0
        self.activate = False            # make the game's window active before each key

    # ---------- MCP ----------
    def _post(self, body):
        hdr = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.sid:
            hdr["mcp-session-id"] = self.sid
        req = urllib.request.Request(self.url, json.dumps(body).encode(), hdr)
        with urllib.request.urlopen(req, timeout=300) as r:
            raw = r.read().decode()
            self.sid = r.headers.get("mcp-session-id") or self.sid
        msgs = [json.loads(l[5:]) for l in raw.splitlines() if l.startswith("data:")]
        return msgs or ([json.loads(raw)] if raw.strip() else [])

    def tool(self, _tool, **args):
        if self.sid is None:
            self._post({"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
                "protocolVersion": "2025-03-26", "capabilities": {},
                "clientInfo": {"name": self.client + "-e2e", "version": "1"}}})
            self._post({"jsonrpc": "2.0", "method": "notifications/initialized"})
        res = self._post({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                          "params": {"name": _tool, "arguments": args}})[-1].get("result", {})
        return "\n".join(c.get("text", "") for c in res.get("content", []) if c.get("type") == "text")

    def var(self, name):
        out = self.tool("amiga_get_var", name=name)
        try:
            return int(out.split("=")[1].split()[0])
        except (IndexError, ValueError):
            return None

    def setvar(self, name, value):
        return self.tool("amiga_set_var", name=name, value=str(value))

    def hook(self, name, args=""):
        return self.tool("amiga_call_hook", client=self.client, hook=name, args=str(args))

    def key(self, code, hold=0.1):
        if self.activate:                  # AmigaOS 4: keys go to the active window
            self.hook("activate")
            time.sleep(0.3)                # Intuition activates it a moment later
        self.tool("amiga_input_key", rawkey=code, direction="down")
        time.sleep(hold)
        self.tool("amiga_input_key", rawkey=code, direction="up")

    def key_until(self, code, cond, tries=2, timeout=8.0):
        for _ in range(tries):
            self.key(code)
            if self.wait(cond, timeout):
                return True
        return False

    def logs_since(self, t0):
        base = self.url.rsplit("/mcp", 1)[0]
        with urllib.request.urlopen(f"{base}/api/logs?limit=400", timeout=10) as r:
            d = json.load(r)
        out = []
        for l in (d.get("logs", d) if isinstance(d, dict) else d):
            ts = l.get("timestamp")
            if isinstance(ts, str):
                ts = datetime.fromisoformat(ts).timestamp()
            if l.get("client") == self.client and ts and ts >= t0:
                out.append(l["message"])
        return out

    def wait_log(self, t0, text, timeout=20.0):
        return self.wait(lambda: [m for m in self.logs_since(t0) if text in m], timeout, 0.5)

    @staticmethod
    def wait(fn, timeout=8.0, every=0.4):
        end = time.time() + timeout
        while time.time() < end:
            v = fn()
            if v:
                return v
            time.sleep(every)
        return fn()

    # ---------- reporting ----------
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

    def shot(self, name, pause=True):
        """a screenshot through devbench, the game paused for it (one frame)"""
        if pause:
            self.hook("pause", "on")
        out = self.tool("amiga_screenshot")
        if pause:
            self.hook("pause", "off")
        path = out.split("saved: ")[-1].split(" (")[0].strip() if "saved:" in out else ""
        if path and not os.path.exists(path) and self.remote:      # saved on the devbench's machine
            local = os.path.join(self.shots, os.path.basename(path))
            os.makedirs(self.shots, exist_ok=True)
            subprocess.run(["scp", "-q", f"{self.remote}:{path}", local], check=False)
            path = local
        if path and os.path.exists(path):
            os.makedirs(self.shots, exist_ok=True)
            dst = os.path.join(self.shots, f"{name}.png")
            os.replace(path, dst)
            print(f"  shot  {dst}")
            return dst
        return None

    def qemu_shot(self, name, host, port=2348):
        self.activate = True
        """an OS4 screenshot from QEMU's monitor on the emulator's host (via ssh)"""
        self.hook("pause", "on")
        cmd = ('(echo "mouse_move 2 2"; sleep 0.3; echo "mouse_move -2 -2"; sleep 1; '
               f'echo "screendump /tmp/a3d_shot.ppm"; sleep 1) | nc -q1 localhost {port} >/dev/null; '
               'python3 -c "from PIL import Image; Image.open(\'/tmp/a3d_shot.ppm\').save(\'/tmp/a3d_shot.png\')"')
        subprocess.run(["ssh", host, cmd], check=False)
        os.makedirs(self.shots, exist_ok=True)
        dst = os.path.join(self.shots, f"{name}.png")
        subprocess.run(["scp", "-q", f"{host}:/tmp/a3d_shot.png", dst], check=False)
        self.hook("pause", "off")
        print(f"  shot  {dst}")
        return dst

    def done(self):
        print(f"\n{self.passes} passed, {self.failures} failed")
        return self.failures
