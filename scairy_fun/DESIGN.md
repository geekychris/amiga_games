# ScAIry Fun: design

You ride in a self-driving car. Points come from distance driven **on
autopilot**, multiplied by how long it has stayed on. The autopilot is
good, but it has blind spots, and the game is learning them: take the wheel
just before it gets something wrong, then hand it back.

## Core loop

- **Autopilot on**: the car follows its own planned path (the cyan ribbon on
  the road) at the speed limit, keeps its distance, yields, brakes for deer.
  Every metre scores `1 x multiplier`; the multiplier goes up by one every
  8 s of unbroken autopilot, to x8.
- **Taking over**: steering or braking disengages it, like a real car.
  Driving yourself scores nothing and the multiplier drops back to x1.
- **Handing back**: Fire re-engages it, but only with the car near a lane
  centre and on the road ("AUTOPILOT UNAVAILABLE" otherwise); it can pull
  away from a standstill, e.g. after the tow truck.
- **Reading the AI**: no warnings. You see the road (flood water, cones,
  two sets of lane lines, an overpass shadow, the low sun), the AI's
  planned path on the road, brackets round what it has detected, and a
  top-down "what the AI sees" display. When the path heads for the river,
  or a box appears where nothing is, it's wrong.
- **Judged takeovers**: disengaging while a hazard the AI would fail is
  ahead, and getting through it, is a **save** (+500, "Good catch").
  Disengaging when nothing was wrong is **needless** ("I had that") and
  costs the multiplier for nothing.

## Damage: insurance cover

Cover starts at 100. Scraping a guardrail or barrier costs 5-8, hitting cones
3 each, debris 10, a rear-end 15, a collision 25, the river or the embankment
40 (the tow truck puts you back on the road with the autopilot off). An
ambulance you don't let past is a 10-point ticket. At 0 the policy is
cancelled: game over. Each arrival restores 30.

## Routes and software updates

| # | Route | AI | Road | AI fails at | Bait (AI is right) |
|---|---|---|---|---|---|
| 1 | Levee Road | v1.0 | two-way levee, river left, drop right | flooded road, old lane lines, slow tractor (it just follows) | cyclist, deer, oncoming traffic |
| 2 | Rush Hour | v2.0 "now detects water" | 3-lane freeway, median barrier | overpass shadows (phantom braking with a truck behind), cones, ambulance, debris | merging truck |
| 3 | Mountain Pass | v3.0 "phantom braking fixed" | two-way, rock wall and cliff | low sun on bends (camera blinded), fallen rocks, old lane lines | goats |
| 4 | Flooded Town | v4.0 "now confident" | town streets, canal | flooded streets, detour cones, ambulance, phantom shadows | pedestrians |

Between routes, a software update screen lists the patch notes.

## Scoring at each destination

Distance, autopilot %, saves, needless takeovers and damage; bonus =
autopilot % x 50 + saves x 500.

## Presentation

- Chase view and cockpit view (V switches); the cockpit draws a dashboard
  and steering wheel.
- 320x256 PAL, 32 colours, 25 fps, a renderer written for roads: segments
  drawn far to near with a cross-section of 8 points (road, shoulders,
  levee slopes, water, drops, walls), lane markings, guardrails and models.
- The AI speaks: short lines in a British female voice (Kokoro bf_emma),
  8-bit samples played on Paula, and shown as text.
- Title artwork made in ChatGPT and converted with tools/make_title.py.

## Controls

| Key | |
|---|---|
| Left / right | steer (takes over) |
| Down | brake (takes over) |
| Up | accelerate (manual) |
| Space / fire | autopilot on / off |
| V | chase / cockpit view |
| P | pause |
| Return / F1 | start |
| Esc | title / quit |
