#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""ScAIry Fun: "Autopilot Lounge" - an original easy-listening bossa in F major.

The music the car plays while it drives itself: soft organ chords, a lazy
vibraphone melody, walking bass. Every note fades (a volume slide) and
rests cut the note, so nothing is left droning on a looped cycle; the
chords are held organ notes rather than ProTracker arpeggios, which buzz. The game keeps two channels for music
(the effects and the AI's voice take the others), so the melody is on
channel 0 and the bass on 1; the chords and the brushed drums on 2 and 3
fill in when nothing else is playing.
"""
from modlib import (Song, m, pad, pluck, kick, snare, hat, takeme,
                    MAJOR, MINOR)

song = Song("autopilot lounge", speed=6, bpm=104)
vibes = song.instrument("vibraphone", pluck("sine", 32, cycles=90, sustain=0.12, amp=110), 46)
bass = song.instrument("upright bass", pluck("triangle", 64, cycles=20, sustain=0.15, amp=120), 56)
keys = song.instrument("lounge organ", pad("organ", 32, beats=12, detune=1, amp=60), 16)
bd = song.instrument("soft kick", kick(ms=160, f0=110.0, amp=100), 40)
rim = song.instrument("rim", snare(ms=60, tone=420.0, amp=80), 28)
brush = song.instrument("brush", hat(ms=60, amp=50, seed=5), 18)
flute = song.instrument("synth flute", pluck("sine", 32, cycles=120, sustain=0.25, amp=100), 40)

F, Dm, Gm, C, Bb, Am = (("F-1", "F-2", MAJOR), ("D-1", "D-2", MINOR), ("G-1", "G-2", MINOR),
                        ("C-2", "C-2", MAJOR), ("A#1", "A#2", MAJOR), ("A-1", "A-2", MINOR))

PROG_A = [F, Dm, Gm, C]
PROG_B = [Bb, Am, Gm, C]

MEL_A = m("A-2:6 C-3:2 F-3:4 E-3:4 "
          "D-3:6 C-3:2 A-2:8 "
          "A#2:4 D-3:4 G-3:4 F-3:2 E-3:2 "
          "E-3:12 ^:4")
MEL_B = m("D-3:4 F-3:4 A-3:6 G-3:2 "
          "E-3:4 C-3:4 E-3:8 "
          "D-3:2 E-3:2 F-3:4 A#2:4 D-3:4 "
          "C-3:8 G-2:4 A-2:4")
# "please keep your hands on the wheel": a little two-note chime phrase
MEL_CHIME = m("C-3:2 F-3:6 ^:8 "
              "C-3:2 G-3:6 ^:8 "
              "C-3:2 A-3:6 ^:4 G-3:4 "
              "F-3:12 ^:4")


def walking_bass(p, bar, root):
    fifth = {"F": "C-2", "D": "A-1", "G": "D-2", "C": "G-1", "A#": "F-1", "A": "E-2"}[root[:-1].rstrip("-")]
    p.melody(1, bass, m("%s:6 %s:2 %s:4 %s:3 ^:1" % (root, fifth, root, fifth)), start=bar * 16, slide=0x02)


def chords(p, prog):
    for bar, ch in enumerate(prog):
        p.melody(2, keys, [(ch[1], 12), ("^", 4)], start=bar * 16)


def bossa(p):
    p.drums(3, "k..rk.r.k..rk.r.", {"k": bd, "r": rim})


def brushes(p):
    p.drums(3, "b.b.b.b.b.b.b.b.", {"b": brush})


def build(mel, prog, drums=bossa, inst=None):
    p = song.pattern()
    if mel:
        p.melody(0, inst or vibes, mel, slide=0x01)
    for bar, ch in enumerate(prog):
        walking_bass(p, bar, ch[0])
    chords(p, prog)
    drums(p)
    return p


build(None, PROG_A, brushes)              # 0 intro: bass, keys, brushes
build(MEL_A, PROG_A)                      # 1 A
build(MEL_B, PROG_B)                      # 2 B
build(MEL_CHIME, PROG_A, inst=flute)      # 3 the chime break
build(MEL_A, PROG_A, inst=flute)          # 4 A again, flute
song.order([0, 1, 2, 1, 3, 2, 4, 2])
song.save(takeme("scairy_fun", "scairy.mod"))
