# Boing Twist

A twisty puzzle on the Boing ball, for AmigaOS 4.1 (PowerPC).

A picture is wrapped round the ball, the ball is cut into tiles along
lines of latitude and longitude, and the tiles are jumbled. Twist bands
of tiles round the ball and flip halves of it over until the picture is
whole again. The ball sits in a room: the first is the 1984 Boing demo's
grey room with its purple grid, the ball's shadow thrown on the back wall.
When you solve it the ball bounces round the room, spinning on its leaning
axis, as it did in the demo.

![Title](screenshots/title.png)

| | |
|---|---|
| ![Choosing a puzzle](screenshots/select.png) | ![Jupiter, scrambled, in the Boing room](screenshots/scrambled.png) |
| Choosing a puzzle: picture, grid, scramble and room | Jupiter on a 6 x 12 grid, scrambled. Yellow rings: the band a twist turns. Blue: the half a flip turns over |
| ![A flip half done](screenshots/mid_move.png) | ![Solved](screenshots/solved.png) |
| A flip, half done | Solved: the ball bounces |
| ![The Midnight Box room](screenshots/room_midnight.png) | ![The Deep Space room](screenshots/room_space.png) |
| The Midnight Box room | The Deep Space room (a texture on the back wall) |

## The puzzle

The ball is cut into **B bands** of latitude by **S segments** of
longitude: 4 x 8 (32 tiles), 6 x 12 (72) or 8 x 16 (128, the Boing ball's
own checkerboard). There are two moves:

- **Twist** a band one tile east or west round the ball's vertical axis.
  Pieces stay in their band. S twists of a band are no move at all.
- **Flip** a half of the ball: the S/2 segments either side of a meridian
  turn 180 degrees about the horizontal axis through the middle of that
  half. A piece in band b goes to band B-1-b, mirrored across the half,
  and ends up upside down. This is the move that takes pieces between the
  northern and southern bands. Two flips of the same half are no move.

A scramble is a run of random legal moves from the solved ball (never one
that just undoes the move before it), so every scramble can be solved.
The ball counts as solved when every tile shows the picture that belongs
there, allowing for the whole ball having been spun round; tiles that look
the same (the Boing checkerboard's) count as the same piece, so the easy
Boing ball puzzle is "make the checkerboard again". Upside-down pieces are
not solved.

Scrambles: Gentle (6 moves), Mixed (14), Tangled (30), Chaos (60). Your
best moves and time for each picture, grid and scramble are kept in
`boing_twist.best` beside the program.

## Controls

| | Keyboard | Mouse |
|---|---|---|
| Choose the band | Up / Down | click a tile |
| Twist it | Left / Right | drag it sideways (one twist each 18 pixels) |
| Choose the half | A / D (the ball turns to face it) | click a tile |
| Flip it | Space / Return | drag a tile up or down |
| Turn the ball to look | Q / E (round), W / S (tilt), C (face the half) | right button drag |
| Zoom | + / - | |
| Undo | U / Backspace | |
| Peek at the picture | H (hold) | |
| Pause | P | |
| Give up (it solves itself, move by move) | R | |
| New scramble | N | |
| Room | T | |
| Full screen / window | F / F10 | |
| Help | Help / F1 | |
| Back, then quit | Esc | |

On the title: Space or a click to choose a puzzle. On the select screen:
Up/Down to choose picture, grid, scramble or room, Left/Right to change it,
Return (or a click on the ball) to play.

```
boing_twist [RES=640|800|1024] [FULLSCREEN] [FLAT] [LINEAR]
```

`RES` is the frame: 640 x 480, 800 x 600 (the default) or 1024 x 768. A
frame too big for the Workbench opens on a screen of its own. `FLAT` turns
the ball's shading off; `LINEAR` smooths the textures (much slower, see
below).

## Pictures

| | Picture | Source | Licence |
|---|---|---|---|
| 1 | The Boing ball | made by the game, red and white, one square per tile | - |
| 2 | Earth - Blue Marble | NASA Visible Earth, *Blue Marble: Land Surface, Shallow Water, and Shaded Topography* (Reto Stöckli, NASA Goddard): <https://visibleearth.nasa.gov/images/57752>, file <https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57752/land_shallow_topo_2048.jpg> | Public domain (NASA) |
| 3 | The Moon | NASA Scientific Visualization Studio, *CGI Moon Kit* (LRO LROC colour mosaic): <https://svs.gsfc.nasa.gov/4720>, file `lroc_color_poles_2k.tif` | Public domain (NASA) |
| 4 | Mars | Viking MDIM 2.1 colour mosaic, NASA / USGS Astrogeology, via Wikimedia Commons: <https://commons.wikimedia.org/wiki/File:Mars_Viking_MDIM21_ClrMosaic_1km.jpg> | Public domain (NASA/USGS) |
| 5 | Jupiter | Cassini cylindrical map, December 2000, NASA/JPL/Space Science Institute (PIA07782): <https://photojournal.jpl.nasa.gov/catalog/PIA07782>, via <https://commons.wikimedia.org/wiki/File:Jupiter_Cylindrical_Map_-_Dec_2000_PIA07782.jpg> | Public domain (NASA) |

`images/` holds them at 2048 x 1024 (resized from the sources, JPEG).

### Adding your own

1. Make an **equirectangular** picture: twice as wide as it is high,
   longitude across (0 at the left edge, 360 at the right, the two edges
   meeting at the back of the ball), north at the top. Anything else is
   stretched to 2:1. Ask an image generator for "an equirectangular
   360-degree map / panorama, 2:1"; detail near the poles is squeezed into
   small tiles, so keep the interesting things nearer the equator. Pictures
   with lots of plain areas (oceans, sky) make harder puzzles.
2. Put it in `images/` as PNG, JPEG, TIFF or WebP. The game lists pictures
   in file-name order, so a number in front (`5_my_picture.png`) puts it in
   place; the number is left out of the title.
3. Give it a title in `images/titles.txt` (`5_my_picture = My Picture`,
   31 characters at most, shown in capitals), or the file name is used.
4. `make data` (or `python3 tools/make_texture.py --all`) writes
   `data/<name>.btx` for each. One picture by hand:
   `python3 tools/make_texture.py in.png data/name.btx --title "NAME" --size 1024x512`.
5. Copy `data/` beside the program (`boing_twist/data/`). The game finds
   whatever is there when it starts; no rebuild.

A `.btx` is `"BTX1"`, width and height (16-bit big-endian, powers of two),
a 32-byte title, then R G B bytes, rows top to bottom: 1024 x 512 is
1.5 MB. 512 x 256 works too (`--size 512x256`), and is what to use if
memory is short; the speed is much the same.

## Rooms (themes)

A room is a text file in `themes/` (`*.theme`), listed by the game at
start-up; T changes room. The room is a box round the ball: back wall,
floor, left and right walls and ceiling, each a colour with an optional
grid, an optional texture, or left out:

```
name = Boing Room
background = AAAAAA              # behind everything (RRGGBB)
shadow = wall 45                 # wall|floor <darkness 0-100> | off
back = AAAAAA A000A0 0.5         # <colour> <grid colour | none> <grid square> [texture.btx]
floor = AAAAAA A000A0 0.5
left = off
right = off
ceiling = off
```

The grid square is in ball radii. A texture is a `.btx` in `themes/`
(made by `tools/make_texture.py`; pictures in `themes/src/` are converted
by `make data`, at 512 x 512), stretched over the part of that face that
can be seen; a grid is drawn over it unless it is `none`. Three rooms come
with the game: **Boing Room** (the demo's), **Midnight Box** (all five
faces, a cyan grid, the shadow on the floor) and **Deep Space**
(`stars.btx`, made by `make_texture.py --stars`, on the back wall). To map
your own pictures onto the box, make one per face (the back wall is seen
square-on and about 4:3; the floor, walls and ceiling in perspective), put
them in `themes/src/`, `make data`, and name them in a new `.theme`.

The room is drawn once, when it is chosen, into a background picture; it
costs nothing while playing, so textures there can be any size.

## Building and running

```
../../third_party/mesa-os4/build.sh        # once, in amiga_mcp: libOSMesa
scripts/build-example-ppc.sh boing_twist   # in amiga_mcp
make data                                  # the pictures and textures (Pillow)
```

From a git worktree of `examples/` (Docker must see both), `./build.sh`
runs the same Docker build with `amiga_mcp` mounted beside it. The build
checks that the binary has no `dlmzb` instructions (it is built with
`-mcpu=powerpc`, never 440).

To run, copy `boing_twist`, `data/` and `themes/` into one drawer and start
`boing_twist` from a Shell there. The `$STACK` cookie asks for 2 MB of
stack (Mesa's rasteriser keeps whole spans on it).

## How it is drawn, and how fast

Software Mesa (OSMesa 7.8.2, `third_party/mesa-os4`), into a 32-bit frame
that one `WritePixelArray` puts on the window or screen. The camera never
moves, so the room is drawn into a background buffer when it is chosen;
each frame copies that, darkens the ball's shadow into it in C, draws only
the ball in GL, shades it, and draws the text in C.

The ball is a mesh per tile (about 32 quads round the equator), each tile
textured with the part of the picture its piece carries, turned round if
the piece is upside down. The grooves between tiles are drawn into a copy
of the picture (cut for the grid in use), not as geometry. Tiles facing
away are skipped before GL sees them.

What makes it fast enough: OSMesa has a fixed-point textured-triangle
function that it only uses with `GL_REPLACE`, `GL_NEAREST`, an RGB
power-of-two texture and `GL_FASTEST`. Shading the ball with `GL_MODULATE`
(colour per vertex) falls back to Mesa's general span code, whose
per-pixel floating point crawls on QEMU's emulated FPU: **1125 ms** a
frame against **26 ms** for the same ball. So the ball is drawn unshaded
and shaded afterwards in C: the light is fixed to the eye, so the shade of
a pixel of the ball depends only on where it is in the ball's outline (an
ellipse, in perspective), and a table gives it (diffuse light and a
highlight), for about 2-6 ms. And the ball needs no depth buffer: it is
convex and its back faces are culled, so the depth test is off (28% less
time), the shading finds the ball's pixels by what changed, and the
selection rings are clipped to the near side in C.

Measured on the QEMU sam460ex guest (AmigaOS 4.1 FE), the Earth on a
4 x 8 grid, the ball turning every frame (the `bench` hook), shaded:

| Frame | ms per frame | fps | of which: ball (GL + shading) | blit |
|---|---|---|---|---|
| 640 x 480 (window) | 32 | 31 | 26 (2) | 3 |
| 800 x 600 (window, the default) | 40 | 25 | 31 (4) | 5 |
| 1024 x 768 (full screen) | 57 | 17.5 | 44 (6) | 9 |

Triangle set-up costs about 29 us a triangle (the mesh detail, `detail`
16 / 24 / 32 / 48, gave 25 / 21 / 18 / 14 fps at 800 x 600 before the
depth test went), the rest is filling pixels. The frame rate only matters
while something moves: a ball standing still isn't redrawn, so the game
sleeps between moves. Frames are paced at 30 a second at most on
timer.device (never `WaitTOF`), with at least 3 ms of sleep each, so the
bridge and the rest of the system keep running. `LINEAR` (bilinear
filtering) takes the slow path again and is only there to compare.

## Tests

```
make host-test     # the puzzle core on the host (18,150 checks)
make ubsan         # the same under UBSan and ASan
make vectors       # regenerate tests/vectors.h
python3 tests/e2e_devbench.py --deploy [--readme] [--bench-res 640,1024]
```

The puzzle core (`bt_core.c`: state, moves, scrambles, undo, the solved
test, a xorshift RNG) has no Amiga headers. The host tests check that S
twists, four quarter turns and two flips are no move, that every move
followed by its inverse is none, that moves are permutations that turn the
right pieces, that scrambles replayed backwards solve the ball, the solved
test (a spun ball, a checkerboard, an upside-down ball), undo, and a long
deterministic run folded into one hash (`tests/vectors.h`). The game's
`selftest` hook runs the same run on the Amiga and must get the same hash.

`tests/e2e_devbench.py` drives the game on the OS4 guest through devbench
(it takes the emulator lock, pushes to `RAM:boing_twist/`, never the HDF):
the self-test; scrambles and moves through the hooks, the keys and undo,
each state's hash checked against the host core (`tests/host_tests --hash`);
peek, the three rooms, an animated and an instant solve, a solve by the
player's moves, a 6 x 12 ball; a frame-rate bench; and Esc quitting from
the title with the launcher left running. Screenshots come from QEMU's
monitor; `--readme` takes the ones in `screenshots/`.

Bridge (client `boing_twist`): hooks `selftest`, `scramble` (seed length
[grid]; a length that isn't one of the four is set at once), `move`
(`T<band>+`, `T<band>-`, `F<half>`, `!` for no animation), `hash`, `solve`
(`anim` or `now`), `undo`, `picture`, `theme`, `screen`, `view` (yaw pitch
distance band half), `peek`, `pause` (freezes a move half done, no
overlay), `bench` (frames), `activate`, `quit`; variables `screen`,
`moves`, `secs`, `solved`, `hash`, `anim`, `bands`, `pic`, `npics`,
`grid`, `scr`, `theme`, `nthemes`, `sel_band`, `sel_half`, `peeks`,
`paused`, `fps10`, `bench_fps10`, `bench_left`, `ms_bg`, `ms_ball`,
`ms_shade`, `ms_hud`, `ms_blit`, `tris`, `width`, `height`, and the
render options `lit`, `linear`, `detail` and `zbuf` (settable).

## Files

| | |
|---|---|
| `bt_core.c/h` | the puzzle (host-tested) |
| `render.c/h` | the ball and room in OSMesa, the shading, picking |
| `main_os4.c` | screens, input, pictures, themes, best scores, bridge |
| `hud.c/h` | text and panels in C over the frame |
| `os4_display.c/h` | window / full screen (planet_chomp's, with the mouse) |
| `sound_ahi.c/h` | the bounce, click, whoosh and chord, synthesised, through AHI |
| `tools/make_texture.py` | pictures and room textures to `.btx` |
| `images/`, `themes/` | the pictures' sources; the rooms |

MIT licence (the code). The pictures are NASA's / USGS's, public domain,
as above.
