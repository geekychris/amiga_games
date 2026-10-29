#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""ScAIry Fun: the autopilot's voice.

Each line the AI says (msg_text in world.c, in the same order) is spoken by
Kokoro TTS (Kokoro-82M, Apache-2.0) through the local kokoro_runtime server
in a calm British voice (bf_emma), given a light in-car-speaker treatment,
and packed into scairy_fun/scairy.voc as signed 8-bit mono at 11025 Hz:

  "SCVO", UWORD count, UWORD rate, count x (ULONG offset, ULONG length),
  then the samples (big-endian, as the Amiga reads it). Clip n is MSG n.

Needs the server: kokoro_runtime/target/release/kokoro-server
(127.0.0.1:8770, override with KOKORO_URL).
"""
import io
import json
import os
import struct
import urllib.request
import wave

from modlib import takeme

URL = os.environ.get("KOKORO_URL", "http://127.0.0.1:8770")
RATE = 11025
VOICE = "bf_emma"

# index = MSG_* in sc.h; spoken text (spelt for the voice, not the screen)
LINES = [
    "",
    "Autopilot engaged.",
    "Autopilot disengaged.",
    "I had that.",
    "Good catch.",
    "Recalculating.",
    "That is a road. Probably.",
    "Obstacle detected!",
    "Please keep your hands on the wheel.",
    "Oops.",
    "Update installed. I am better now.",
    "Autopilot unavailable.",
    "Welcome to Scary Fun.",
    "Water detected. Proceeding anyway.",
    "You have arrived.",
    "Camera blinded.",
    "I am very patient.",
    "You failed to yield. That's a ticket.",
    "A tow truck is on its way.",
    "Your policy has been cancelled.",
]


def speak(text):
    req = urllib.request.Request(
        URL + "/tts",
        data=json.dumps({"text": text, "voice": VOICE, "lang": "en-gb", "speed": 1.0,
                         "format": "wav"}).encode(),
        headers={"Content-Type": "application/json"})
    data = urllib.request.urlopen(req, timeout=60).read()
    w = wave.open(io.BytesIO(data))
    n, ch, sw, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    if sw != 2:
        raise ValueError("expected 16-bit wav")
    vals = struct.unpack("<%dh" % (n * ch), w.readframes(n))[::ch]
    return [v / 32768.0 for v in vals], rate


def car_speaker(vals, rate):
    """low-pass to the new rate, resample, trim, normalise, soft-limit, 8-bit"""
    win = max(1, round(rate / RATE))
    acc, lp = 0.0, []
    for k, v in enumerate(vals):
        acc += v
        if k >= win:
            acc -= vals[k - win]
        lp.append(acc / win)
    res, pos, ratio = [], 0.0, rate / RATE
    while pos < len(lp) - 1:
        i = int(pos)
        f = pos - i
        res.append(lp[i] * (1 - f) + lp[i + 1] * f)
        pos += ratio
    thr = 0.02 * max(abs(v) for v in res)
    a = next(i for i, v in enumerate(res) if abs(v) > thr)
    b = len(res) - next(i for i, v in enumerate(reversed(res)) if abs(v) > thr)
    res = res[max(0, a - 80):b + 300]
    peak = max(abs(v) for v in res) or 1.0
    out = []
    for v in res:
        x = v / peak * 1.4                  # a little louder, softly limited
        x = x / (1 + abs(x) * 0.4)
        out.append(max(-127, min(127, int(x * 127))))
    if len(out) & 1:
        out.append(0)
    return bytes(c & 0xFF for c in out)


def main():
    clips = []
    for i, text in enumerate(LINES):
        if not text:
            clips.append(b"")
            continue
        vals, rate = speak(text)
        clips.append(car_speaker(vals, rate))
        print("%2d %-40r %6d bytes %.2f s" % (i, text, len(clips[-1]), len(clips[-1]) / RATE))
    head = b"SCVO" + struct.pack(">HH", len(clips), RATE)
    off = len(head) + 8 * len(clips)
    table, body = b"", b""
    for c in clips:
        table += struct.pack(">II", off + len(body) if c else 0, len(c))
        body += c
    path = takeme("scairy_fun", "scairy.voc")
    with open(path, "wb") as f:
        f.write(head + table + body)
    print("%s: %d clips, %d bytes" % (path, len(clips), len(head + table + body)))


if __name__ == "__main__":
    main()
