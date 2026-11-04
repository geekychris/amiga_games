# Sphere Siege

A shooter inside a hollow sphere, for AmigaOS 4.1 (and the classic Amiga),
on the shared [amiga3d](../amiga3d/README.md) engine.

Your ship rides the inside of the shell, always facing the centre, and
fires inward. A shot crosses the centre and carries on to the far wall, so
it hits whatever is on the line on either side of the centre. Enemies come
out of the vortex at the centre in spiralling trains, circle, swoop out at
you and shoot back; orbiters run along the shell itself. Every fourth wave
is a siege wave of armoured wardens (three hits). Waves get faster as you
go; an extra ship every 25,000.

The camera sits just behind and above the ship and looks through the
centre: the far wall (a dotted latitude/longitude grid with stars beyond)
shows the curve of the space, and the whole view turns with the ship.
The radar (bottom right) shows every enemy about you: red ones are on your
side of the centre, where the view doesn't reach; yellow ones are swooping.

## Controls

| | |
|---|---|
| Arrows / WASD / joystick | slide over the shell (camera-relative) |
| Space / Ctrl / fire | fire (hold) |
| Up/down on the title | the starting wave |
| P | pause |
| B | autopilot |
| Help / H | how to play, controls |
| Esc | title / quit |

Pressing left orbits you left round the centre: things on the far side of
the centre slide left towards your line of fire, things on your side slide
right. Lead the ones on the far side.

Run it as `sphere_siege_os4 FULLSCREEN` for a screen of its own (F / F10
toggles a window). The high score is kept in `PROGDIR:sphere_siege.hi`.

## How it works

- `ss.h`, `ss_core.c`: the simulation, no Amiga headers (tested on the
  host). The ship's place is a rotation frame (right, up, forward to the
  centre) in 16.16 fixed point, turned a little each tick and
  re-orthonormalised (a Newton step for unit length), so there is no pole
  and no singularity anywhere on the sphere. Enemies are a direction from
  the centre turning about an axis, plus a radius; a swoop turns the axis
  towards the ship. Shots are tested against enemies by their closest
  approach over the tick's step.
- `render.c`: the camera is worked out from the ship's frame (camera
  space in quarter units) and the engine projects and depth-sorts it. The
  grid (about 3000 dots) and stars are drawn straight into the frame
  first; enemies are spinning shapes facing the camera; the ship is a
  delta in its own frame.
- `game.c`: the shell (title with the autopilot playing behind it, game
  over, help, sound, bursts, the high score file) and the bridge.

## Building and testing

```sh
scripts/build-example-ppc.sh sphere_siege   # sphere_siege_os4
scripts/build-example-68k.sh sphere_siege   # sphere_siege, sphere_siege_test
make -C examples/sphere_siege host-test     # the tests on the host
make -C examples/sphere_siege ubsan         # under UBSan and ASan
make -C examples/sphere_siege vectors       # regenerate tests/vectors.h
make -C examples/sphere_siege sim           # the autopilot over 8 seeds
python3 examples/sphere_siege/tests/e2e_devbench.py --os4 local --url http://localhost:3010/mcp \
    --launch "RAM:sphere_siege/sphere_siege FULLSCREEN" --shots e2e_shots_os4
```

The tests check the frame (unit and square after 5000 random ticks and over
the poles), the controls' directions, the trains out of the centre, hits on
both sides of the centre, the warp-in immunity, wardens, bullets, lives,
wave flow and the siege wave, the autopilot clearing waves, and a
four-minute autopilot game whose state hash the Amiga must match
(`selftest` hook).

Bridge: 20 variables; hooks `state` (everything as name=value: tests read
it rather than variables, which the bridge looks up by name only), `status`,
`start [wave]`, `title`, `autoplay on|off`, `aim [hold]`, `lives n`,
`clear`, `grid 0|1`, `help n`, `pause on|off`, `selftest`, `activate`.
