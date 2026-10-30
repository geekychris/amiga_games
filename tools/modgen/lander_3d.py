#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Lander 3D: "Quiet Descent" - an original, slow piece in D dorian:
arpeggios drifting over a drone, like the browser game's music.

Two channels are the game's (the effects take the others): the arpeggio
on channel 0, the drone on 1; a high glass pad and a soft pulse on 2 and 3.
Every note fades and rests cut, so nothing hangs on a looped cycle.
"""
from modlib import Song, m, pad, pluck, kick, takeme

song = Song("quiet descent", speed=6, bpm=84)
arp = song.instrument("glass arp", pluck("bell", 32, cycles=60, sustain=0.08, amp=100), 40)
drone = song.instrument("drone", pad("organ", 64, beats=8, detune=1, amp=70), 34)
air = song.instrument("air pad", pad("sine", 32, beats=16, detune=1, amp=50), 16)
pulse = song.instrument("pulse", kick(ms=200, f0=70.0, f1=40.0, amp=90), 24)

ARPS = {
    "Dm": "D-2 F-2 A-2 C-3 E-3 C-3 A-2 F-2",
    "G":  "G-2 B-2 D-3 F-3 A-3 F-3 D-3 B-2",
    "C":  "C-2 E-2 G-2 B-2 D-3 B-2 G-2 E-2",
    "Am": "A-1 C-2 E-2 G-2 B-2 G-2 E-2 C-2",
}
DRONES = {"Dm": "D-1", "G": "G-1", "C": "C-2", "Am": "A-1"}


def bar(p, b, chord, high=None):
    p.melody(0, arp, m(" ".join(n + ":2" for n in ARPS[chord].split())), start=b * 16, slide=0x02)
    p.melody(1, drone, [(DRONES[chord], 14), ("^", 2)], start=b * 16)
    if high:
        p.melody(2, air, [(high, 12), ("^", 4)], start=b * 16)
    p.drums(3, "k...............", {"k": pulse}, start=b * 16)


def pattern(chords, highs=None):
    p = song.pattern()
    for b, c in enumerate(chords):
        bar(p, b, c, highs[b] if highs else None)
    return p


pattern(["Dm", "Dm", "G", "G"])                                   # 0
pattern(["Dm", "C", "G", "Am"], ["A-3", "G-3", "B-3", "E-3"])     # 1
pattern(["C", "G", "Dm", "Dm"], ["E-3", "D-3", "F-3", "A-3"])     # 2
song.order([0, 1, 2, 1, 0, 2])
song.save(takeme("lander_3d", "lander_3d.mod"))
