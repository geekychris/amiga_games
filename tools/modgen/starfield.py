#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Starfield: "Star Cruise" - an original slow, heroic space theme in Bb major.

The demo owns all four channels: lead on 0, bass on 1, pad on 2, a soft
pulse of drums on 3.
"""
from modlib import Song, m, pluck, pad, kick, snare, hat, takeme, MAJOR, MINOR

song = Song("star cruise", speed=6, bpm=100)
lead = song.instrument("horn", pluck("saw", 32, cycles=80, sustain=0.8, amp=95), 44)
bass = song.instrument("bass", pluck("triangle", 64, cycles=40, sustain=0.7, amp=120), 56)
strings = song.instrument("pad", pad("saw", 32, beats=16, amp=60), 26)
bd = song.instrument("kick", kick(ms=240), 50)
hh = song.instrument("hat", hat(), 22)

Bb, Gm, Eb, F, Cm, Dm = (("A#1", "A#2", MAJOR), ("G-1", "G-2", MINOR), ("D#1", "D#2", MAJOR),
                         ("F-1", "F-2", MAJOR), ("C-1", "C-2", MINOR), ("D-1", "D-2", MINOR))
MEL = [
    m("F-2:8 A#2:4 C-3:4 D-3:12 C-3:4 "
      "D#3:6 D-3:2 C-3:4 A#2:4 C-3:16"),
    m("F-2:8 A#2:4 D-3:4 F-3:12 D#3:4 "
      "D-3:6 C-3:2 A#2:4 G-2:4 A#2:16"),
    m("G-2:6 A#2:2 D-3:8 C-3:6 A#2:2 G-2:8 "
      "D#2:6 G-2:2 A#2:8 A-2:6 C-3:2 F-3:8"),
]
CHORDS = [[Bb, Gm, Eb, F], [Bb, Dm, Eb, Bb], [Gm, Cm, Eb, F]]


def build(i):
    p = song.pattern()
    p.melody(0, lead, MEL[i])
    for bar, ch in enumerate(CHORDS[i]):
        p.melody(1, bass, m("%s:6 %s:2 %s:8" % (ch[0], ch[0], ch[0][:2] + "2")), start=bar * 16)
        p.melody(2, strings, [(ch[1], 16)], start=bar * 16, arp=ch[2])
    p.drums(3, "k.......h...k.h." * 4, {"k": bd, "h": hh})
    return p


build(0)
build(1)
build(2)
song.order([0, 1, 2, 1])
song.save(takeme("starfield", "starfield.mod"))
