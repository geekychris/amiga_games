#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Stack Tower: "Tower Climb" - an original, brisk puzzle tune in A minor.

The game keeps two channels for music (the effects take the others), so
the melody is on channel 0 and the bass on 1; a soft organ and the drums
on 2 and 3 fill in when no effect is playing. Notes fade (volume slide)
and rests cut them, so nothing drones on a looped cycle.
"""
from modlib import Song, m, pad, pluck, kick, snare, hat, takeme

song = Song("tower climb", speed=5, bpm=128)
lead = song.instrument("bell lead", pluck("bell", 32, cycles=70, sustain=0.1, amp=110), 46)
bass = song.instrument("pluck bass", pluck("saw", 64, cycles=14, sustain=0.1, amp=115), 54)
organ = song.instrument("organ", pad("organ", 32, beats=12, detune=1, amp=55), 14)
bd = song.instrument("kick", kick(ms=140, f0=130.0, amp=110), 46)
sd = song.instrument("snare", snare(ms=120, tone=210.0, amp=90), 32)
hh = song.instrument("hat", hat(ms=25, amp=60), 18)

Am, F, C, G, Dm, E = "A-1", "F-1", "C-2", "G-1", "D-2", "E-1"

MEL_A = m("A-2:2 C-3:2 E-3:2 A-3:2 G-3:2 E-3:2 C-3:2 E-3:2 "
          "F-3:2 E-3:2 D-3:2 C-3:2 D-3:4 ^:4 "
          "C-3:2 E-3:2 G-3:2 B-3:2 A-3:2 G-3:2 E-3:2 G-3:2 "
          "A-3:4 G-3:2 E-3:2 D-3:4 ^:4")
MEL_B = m("E-3:4 A-3:4 G-3:2 F-3:2 E-3:4 "
          "D-3:4 F-3:4 E-3:2 D-3:2 C-3:4 "
          "B-2:4 E-3:4 G#3:4 B-3:4 "
          "A-3:8 E-3:4 ^:4")


def bassline(p, roots):
    for bar, r in enumerate(roots):
        hi = r[:2] + str(int(r[2]) + 1)
        p.melody(1, bass, m("%s:2 %s:2 %s:2 %s:2 %s:2 %s:2 %s:2 ^:2" % (r, hi, r, hi, r, hi, r)),
                 start=bar * 16, slide=0x02)


def chords(p, roots):
    for bar, r in enumerate(roots):
        p.melody(2, organ, [(r[:2] + str(int(r[2]) + 1), 12), ("^", 4)], start=bar * 16)


def drums(p, busy=True):
    p.drums(3, "k.h.s.h.k.h.s.hh" if busy else "k.......s.......", {"k": bd, "s": sd, "h": hh})


def build(mel, roots, busy=True):
    p = song.pattern()
    if mel:
        p.melody(0, lead, mel, slide=0x01)
    bassline(p, roots)
    chords(p, roots)
    drums(p, busy)
    return p


build(None, [Am, F, C, G], busy=False)          # 0 intro
build(MEL_A, [Am, F, C, G])                     # 1 A
build(MEL_B, [Dm, Am, E, Am])                   # 2 B
song.order([0, 1, 2, 1, 2])
song.save(takeme("stack_tower", "stack_tower.mod"))
