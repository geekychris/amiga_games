<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# modgen: the games' music and voices

Every MOD and voice sample the games load from disk is generated here, so
every note and sample is ours: the songs are written in Python, the
instruments are synthesised (no recorded samples), and the voices are
synthetic speech. The same scripts (with the 3DO disc paths) live in
[3do-dev](https://github.com/geekychris/3do-dev) `tools/modgen/`.

| Script | Writes | What it is |
|---|---|---|
| `rock_blaster.py` | `rock_blaster/rock_blaster.mod` | "Rock Storm": original driving synth tune, E minor (Orb Hunter plays it too) |
| `pea_shooter_blast.py` | `pea_shooter_blast/music.mod` | "Pea Patrol": original upbeat march, D major |
| `starfield.py` | `starfield/starfield.mod` | "Star Cruise": original slow space theme, Bb major |
| `stakattack.py` | `stakattack/stakattack.mod` | "Stack Bounce": original bouncy puzzle tune, G major |
| `uranus.py` | `uranus_lander/uranus.mod` | "Ring Drift": original slow space-funk, D dorian |
| `orbital_patrol.py` | `orbital_patrol/orbital_patrol.mod` | "Patrol Drive": original driving techno, A minor |
| `rj_party.py` | `rj_birthday/party.mod` | "Party Time": original upbeat party tune, C major |
| `rj_birthday.py` | `rj_birthday/birthday.mod` | "Happy Birthday to You" (Mildred and Patty Hill, public domain), our own waltz arrangement |
| `arcade_voices.py` | `rj_birthday/snd_arcade1..4.raw` | "Game on!", "Ready, player!", "High score!", "Level up!" spoken by Kokoro TTS (Apache-2.0), made to sound like a robot |

Ace Pilot's `blue_danube.mod` (Strauss, public domain) comes from
`ace_pilot/gen_mod.py`; Sky Knights' samples from `sky_knights/gen_sounds.py`.

## Regenerating

```sh
cd tools/modgen
python3 rock_blaster.py          # any song script; writes straight into the game's directory
python3 render.py ../../rock_blaster/rock_blaster.mod /tmp/rock.wav   # listen on the Mac
python3 arcade_voices.py         # needs kokoro-server running on 127.0.0.1:8770 (KOKORO_URL)
```

`modlib.py` is the writer: `Song`, `Pattern.melody()` / `Pattern.drums()`,
`m("C-2:4 E-2:2 .:2")` for compact note lists, and the synthesised
instruments (`cycle`, `pluck`, `pad`, `kick`, `snare`, `hat`, `clap`, `mix`).
`render.py` is a preview player for the effects the songs use, not a full
ProTracker player.

The games' sound effects take channels from 3 downwards and
`mt_MusicChannels` says how many the music keeps, so the songs put the
melody on channel 0 and the bass on 1, drums and chords on 2 and 3.
