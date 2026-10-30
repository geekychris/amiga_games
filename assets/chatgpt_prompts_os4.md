# Artwork for the AmigaOS 4 (PPC) versions: ChatGPT prompts

Generate each image with ChatGPT and save it as a PNG at the path given.
Any size is fine; the build scales and converts them. The classic 68k
versions keep their current look, so the art is for the PPC builds only.
All of them have been made and are in the games' `assets/` folders; `make
art` in each game's folder turns them into the pictures it loads.

Rules for every image: original artwork only; no text unless the prompt
asks for it; no logos, brands or real products; no people's faces except
where asked for.

## Boing Pinball

The game still draws the walls, bumpers, ramps, flippers, lamps and
targets in 3D. The playfield images are the printed art underneath, so
they must contain no pinball parts.

### 1. Playfield art, one for each table (portrait, 1:2, e.g. 1024 x 2048)

`boing_pinball/assets/playfield_comet.png`
> Flat printed artwork for the playfield of a pinball table, seen straight
> from above, portrait, twice as tall as it is wide. Theme: a comet tour of
> the solar system. Deep violet-black space background with soft nebula
> glows. Planets in a loose vertical line: a large ringed planet in the
> upper middle, a red rocky planet on the left, a small grey cratered
> planet on the right, a glowing comet with a long golden tail sweeping
> diagonally across the middle. Painted, slightly retro airbrushed style
> like 1990s pinball art. The bottom quarter is darker and plainer. No
> pinball machine parts, no flippers, no bumpers, no rails, no text.

`boing_pinball/assets/playfield_deep.png`
> Flat printed artwork for the playfield of a pinball table, seen straight
> from above, portrait, twice as tall as it is wide. Theme: a deep sea
> dive. Teal-to-navy ocean water with light rays from the top, a coral reef
> in the lower third, glowing jellyfish drifting in the upper half, a
> sunken wooden shipwreck in the middle, scattered pearls and bubbles.
> Painted, slightly retro airbrushed style like 1990s pinball art. The
> bottom quarter is darker and plainer. No pinball machine parts, no
> flippers, no bumpers, no rails, no text.

`boing_pinball/assets/playfield_foundry.png`
> Flat printed artwork for the playfield of a pinball table, seen straight
> from above, portrait, twice as tall as it is wide. Theme: a robot
> factory. Dark riveted steel plates, yellow-and-black hazard stripes along
> the edges, glowing orange molten metal in a channel down the middle,
> cogs, conveyor belts and a friendly boxy robot face in the upper half.
> Painted, slightly retro airbrushed style like 1990s pinball art. The
> bottom quarter is darker and plainer. No pinball machine parts, no
> flippers, no bumpers, no rails, no text.

### 2. Backglass art, one for each table (landscape, 4:3)

Shown on the title, behind the table chooser.

`boing_pinball/assets/backglass_comet.png`, `backglass_deep.png`, `backglass_foundry.png`:
> A pinball machine backglass painting, landscape 4:3, in the bold airbrushed
> style of late-1980s / early-1990s pinball art. Theme: [a comet tour of the
> solar system: an astronaut riding a comet past a ringed planet |
> a deep sea dive: a diver in a brass helmet among glowing jellyfish and a
> shipwreck | a robot foundry: a cheerful robot pouring molten metal among
> giant cogs]. A red-and-white checked bouncing ball is in the scene. Leave
> the top fifth plain and dark for a title. No text, no logos.

### 3. The logo (wide, transparent or black background)

`boing_pinball/assets/logo.png`
> A game logo reading "BOING PINBALL" in chunky chrome 1980s arcade
> lettering with a red-and-white checked ball as the O in BOING. Black
> background, no other text.

## Lander 3D

### 4. Title (landscape, 4:3)

`lander_3d/assets/title_source.png`
> A 4:3 video game title screen in the style of a late-1980s / early-1990s
> Amiga game box cover, painted. A small gold-foil lunar lander with four
> spindly legs fires its engine, hovering low over a grey cratered surface;
> a glowing green landing pad ring below it; Earth hangs in a black starry
> sky. Big chunky title lettering across the top reads "LANDER 3D" in
> brushed metal with an orange glow. Leave the bottom fifth dark and plain.
> No other text, no logos, no flags.

### 5. Sky panoramas, one for each world (very wide, 4:1, e.g. 2048 x 512)

Drawn as the sky behind the terrain, wrapping round as you turn, so the
left and right edges must join seamlessly. The bottom edge is the horizon.

`lander_3d/assets/sky_moon.png`
> A seamless 360-degree panoramic sky, very wide (4:1), for the Moon: pure
> black space with dense fine stars and a faint Milky Way band, a small
> blue-and-white Earth low on the left. The bottom edge is the horizon.
> The left and right edges must tile seamlessly. No ground, no text.

`lander_3d/assets/sky_mars.png`
> A seamless 360-degree panoramic sky, very wide (4:1), for Mars at
> midday: dusty butterscotch-orange sky, paler near the horizon, thin dust
> haze, a tiny pale sun, and two small moons faintly visible. The bottom
> edge is the horizon. The left and right edges must tile seamlessly. No
> ground, no text.

`lander_3d/assets/sky_asteroid.png`
> A seamless 360-degree panoramic sky, very wide (4:1), seen from a small
> asteroid: black space, a big purple-and-blue nebula, bright stars, and a
> few other tumbling rocky asteroids at different distances. The bottom
> edge is the horizon. The left and right edges must tile seamlessly. No
> ground, no text.

## Stack Tower

### 6. Title (landscape, 4:3)

`stack_tower/assets/title_source.png`
> A 4:3 video game title screen in the style of an early-1990s puzzle game,
> painted. A tall round stone tower wrapped in brightly coloured blocks
> (red, orange, yellow, green, cyan, blue, purple) like a puzzle game
> rising into a twilight sky, a few blocks falling towards it. Big chunky
> title lettering across the top reads "STACK TOWER". Leave the bottom
> fifth dark and plain. No other text, no logos.

### 7. Backdrop (portrait-ish, 4:5)

`stack_tower/assets/backdrop.png`
> A calm painted twilight landscape for behind a puzzle game, 4:5: a
> deep blue-to-violet sky, distant misty mountains, faint stars, soft and
> low in contrast so bright blocks stand out in front of it. No buildings,
> no text.

## ScAIry Fun

### 8. Title (landscape, 4:3)

The prompt already in `scairy_fun/assets/chatgpt_prompt.md`; save it as
`scairy_fun/assets/title_source.png`.

### 9. Distant scenery (very wide, 4:1)

`scairy_fun/assets/horizon.png`
> A seamless very wide (4:1) painted horizon strip for a driving game at
> dusk: rolling farmland hills, a distant small town with lit windows,
> power pylons, a river glinting, an orange-pink sunset sky fading to
> violet at the top. The bottom edge is the horizon line. The left and
> right edges must tile seamlessly. No cars, no people, no text.
