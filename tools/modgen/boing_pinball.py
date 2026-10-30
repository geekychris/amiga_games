#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Boing Pinball: "Silver Ball Shuffle" - an original, bouncy arcade tune
in C major.

The game keeps two channels for music (the effects take the others), so
the melody is on channel 0 and the bass on 1; an organ and the drums on 2
and 3 fill in when no effect is playing. Notes fade and rests cut them, so
nothing drones on a looped cycle.
"""
from modlib import Song, m, pad, pluck, kick, snare, hat, takeme

song = Song("silver ball shuffle", speed=5, bpm=136)
lead = song.instrument("square lead", pluck("square", 32, cycles=60, sustain=0.12, amp=100), 42)
bass = song.instrument("slap bass", pluck("saw", 64, cycles=12, sustain=0.08, amp=115), 54)
organ = song.instrument("organ", pad("organ", 32, beats=12, detune=1, amp=55), 14)
bd = song.instrument("kick", kick(ms=130, f0=120.0, amp=110), 46)
sd = song.instrument("snare", snare(ms=110, tone=220.0, amp=90), 30)
hh = song.instrument("hat", hat(ms=22, amp=60), 18)

C, Am, F, G, Dm, E = "C-2", "A-1", "F-1", "G-1", "D-2", "E-1"

MEL_A = m("C-2:2 E-2:2 G-2:2 E-2:2 A-2:2 G-2:2 E-2:4 "
          "F-2:2 A-2:2 C-3:2 A-2:2 G-2:4 ^:4 "
          "E-2:2 G-2:2 C-3:2 G-2:2 F-2:2 E-2:2 D-2:2 E-2:2 "
          "C-2:4 D-2:2 E-2:2 G-2:4 ^:4")
MEL_B = m("A-2:2 ^:2 A-2:2 G-2:2 A-2:4 C-3:4 "
          "B-2:2 ^:2 B-2:2 A-2:2 G-2:8 "
          "F-2:2 A-2:2 C-3:2 A-2:2 D-3:4 C-3:4 "
          "B-2:4 G-2:4 B-2:4 ^:4")


def bassline(p, roots):
    for bar, r in enumerate(roots):
        hi = r[:2] + str(int(r[2]) + 1)
        p.melody(1, bass, m("%s:2 ^:2 %s:2 %s:2 %s:2 ^:2 %s:2 %s:2" % (r, hi, r, hi, r, hi)),
                 start=bar * 16, slide=0x02)


def chords(p, roots):
    for bar, r in enumerate(roots):
        p.melody(2, organ, [(r[:2] + str(int(r[2]) + 1), 6), ("^", 2), (r[:2] + str(int(r[2]) + 1), 6), ("^", 2)], start=bar * 16)


def build(mel, roots, busy=True):
    p = song.pattern()
    if mel:
        p.melody(0, lead, mel, slide=0x01)
    bassline(p, roots)
    chords(p, roots)
    p.drums(3, "k.h.s.hkk.h.s.hh" if busy else "k...s...k...s...", {"k": bd, "s": sd, "h": hh})
    return p


build(None, [C, Am, F, G], busy=False)          # 0 intro
build(MEL_A, [C, Am, F, G])                     # 1 A
build(MEL_B, [F, G, Dm, G])                     # 2 B
build(MEL_A, [Am, F, C, E])                     # 3 A, darker
song.order([0, 1, 2, 1, 3, 2])
song.save(takeme("boing_pinball", "boing_pinball.mod"))
