# Terra Strike

A Zarch / Carrier Command style landscape shooter for the classic 68k Amiga
(68020+, OS 3.0+) and AmigaOS 4.1 (PowerPC), written with Claude Code against
Amiga DevBench.

Lift off from the landing pad, fly over a wrapping fractal island, and
destroy the tanks, buildings and ships. Tanks and ships shoot back. Land on
the pad again to refuel and repair. Clear every target to reach the next
level.

| Key | |
|---|---|
| Arrows / joystick | turn, climb / dive (the craft hovers when you let go) |
| A / Z | throttle up / down |
| Space / fire | cannon (aims itself at a target within ~22° of the nose) |
| P | pause |
| Return / F1 | start |
| Esc | title / quit |
| F / F10 | window ↔ full screen (AmigaOS 4) |

## Main screen

The title artwork was generated in ChatGPT from the prompt in
`../../demo_recording/stage2/chatgpt_prompt.md` (`assets/title_source.png`)
and converted by `tools/make_title.py` (`make title`) into `title_image.c`:
320×256, 32 colours chosen by k-means in the 12-bit OCS/ECS gamut, five
bitplanes. The title alternates between the artwork (10 s) and the 3D
flyover (15 s), each with its own palette. `tests/test_make_title.py`
round-trips the converter.

## How it works

- **Terrain**: 64×64 diamond-square heightmap on a torus (the world wraps),
  rescaled so ~30% is sea; a 2×2-tile pad is flattened near the middle.
- **Renderer**: the 15×15 tiles around the view and every model are
  transformed with 16.16 fixed point, clipped against the near plane and
  the screen, and sorted into 256 depth buckets. Models are keyed to the
  depth of the tile they stand on so they draw right after it. Polygons are
  filled by a CPU span filler that writes the five bitplanes directly; this
  was eight times faster than `graphics.library` area fills here.
- **Display**: 320×256 PAL lores, 32 colours, double-buffered with
  `ChangeScreenBuffer`, locked to 25 fps; the simulation runs at a fixed
  25 Hz.
- **Audio**: ptplayer plays `terra_strike.mod` on two channels, with
  synthesised effects on the other two.

## AmigaOS 4.1

The same game on PowerPC (`terra_strike_os4`, deployed as
`DH1:terra_strike/terra_strike` next to `terra_strike.mod`). The
simulation, renderer, models and the whole bridge interface are shared
with the classic version; only the platform layer differs:

| | Classic Amiga | AmigaOS 4 |
|---|---|---|
| Frame | five bitplanes, spans written by the CPU | a 320×256 buffer of 8-bit pens, turned into RGB through the palette |
| Display | 320×256 PAL screen, `ChangeScreenBuffer` | `os4_display.c`: a window (`SCALE=n`, default 2) or a screen of its own (`FULLSCREEN`), scaled up; F / F10 switches |
| Text | `Text()` with topaz 8 | the same topaz 8 glyphs, read out of the ROM font |
| Timing | 25 fps, every other vertical blank | 25 fps on a fixed 40 ms schedule, sleeping on `timer.device` in between (`WaitTOF()` returns hundreds of times a second under QEMU, so waiting on it keeps the CPU busy) |
| Music | ptplayer on Paula | `modplay.c`, a small Protracker player that mixes in software, streamed to `ahi.device` |
| Effects | ptplayer `mt_playfx` | the same synthesised samples, sent to AHI at their Paula rate |
| Joystick | port 2 | keyboard only |

```
DH1:terra_strike/terra_strike FULLSCREEN
```

## Files

| | |
|---|---|
| `ts.h`, `ts_math.c`, `terrain.c`, `world.c` | portable core: maths, clipping, spans, terrain, simulation (no Amiga headers) |
| `models.c/h` | polygon models, palette layout |
| `render.c/h` | camera, renderer, HUD, radar |
| `game.c/h` | the shared side of the frame loop: input, fixed-rate simulation, what to draw, AmigaBridge vars and hooks |
| `main.c` | classic Amiga: screen, joystick, ptplayer, timers |
| `main_os4.c` | AmigaOS 4: pen buffer → RGB, window or screen, keyboard, timers |
| `sound.c/h`, `ptplayer.*` | effects and Protracker replay (from ace_pilot) |
| `sound_ahi.c`, `modplay.c/h` | AmigaOS 4 sound: AHI streaming and the software MOD player |
| `os4_display.c/h` | AmigaOS 4 window / full screen, shared with planet_chomp, rolling_steel and spectral_keep |
| `tests/test_cases.c` | 67 unit tests, shared by all three runners |
| `tests/host_main.c` | runs them natively on the host |
| `tests/amiga_test_main.c` | `terra_strike_test`, runs them on the Amiga |
| `tests/e2e_devbench.py` | end-to-end test of the live game through devbench MCP |

## Build and test

```bash
make host-test                         # unit tests on the Mac (cc)
make                                   # terra_strike + terra_strike_test (Docker m68k)
scripts/build-example-ppc.sh terra_strike   # terra_strike_os4 (from the amiga_mcp root)
```

Through devbench (classic 68k profile on :3001):

```
amiga_run(project="examples/terra_strike")              # build, deploy, launch
amiga_run_tests(project="examples/terra_strike", command="terra_strike_test")
amiga_call_hook(client="terra_strike", hook="selftest")  # same tests, inside the game
python3 tests/e2e_devbench.py                            # 32 checks against the live game
```

Copy `terra_strike.mod` next to the binary (`PROGDIR:`) for music.

## Bridge interface

| | |
|---|---|
| vars (monitor) | `score lives level targets state pstate fuel shield speed yaw alt px pz fps polys frame title_phase` and `us_update us_build us_draw us_hud` (stage timings in µs); on AmigaOS 4 also `us_convert us_present` |
| vars (tunables) | `god_mode inf_fuel enemy_fire enemy_range turn_rate lift gravity max_speed` |
| hooks | `title [image\|attract]`, `scene <sea\|tanks\|city\|landing\|lowfuel\|pad>`, `selftest`, `status`, `spawn <tank\|ship\|building> [dist]`, `start`, `clear`, `regen [seed]` |
| memregion | `heightmap` (64×64 big-endian WORD heights) |
| log | `TAKEOFF LANDED KILL HIT CRASH RESPAWN LEVEL n CLEAR GAME OVER SCENE SELFTEST STATE`, plus `fps=` debug every 10 s |

Bridge variable names are global (`GETVAR`/`SETVAR` carry no client name).
Stop other clients that register `score`/`lives`/`level` before testing;
`tests/e2e_devbench.py` does this. It also quits `examples/launcher` if
that's running, because the launcher's input handler sees injected keys too.
