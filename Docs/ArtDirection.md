# Art direction: Monument Valley × Splatoon (branch `paint`)

Reference images are in `SourceArt/Reference/` (gitignored; not in the repo and not in the report):
- `MonumentValley1/`, `MonumentValley2/`, `MonumentValley3/`: Steam store screenshots (1920×1080). The ones closest to the target look: `mv_2_09` (a floating castle on a peach background), `mv_2_06` (orange arches on white), `mv_2_03` (colourful floating islands on cream), `mv_1_03` (a pink tower in fog), `mv_3_02` (pink and teal towers on light teal).
- `Splatoon/`: Splatoon 3 promotional screenshots. For the ink, see `sp3_10`, `sp3_16` (purple and yellow ink on vehicles and the ground), `sp3_05` (a red ink surface seen from above), `sp3_03` (yellow ink blobs in the air).

## Monument Valley: observations and colours picked from the references

**Geometry**
- Orthographic, true isometric view (35.26° downward pitch, 45° yaw).
- Blocky buildings with details: battlements, steps, arched door and window openings, round portholes, spires.
- The main structure floats on a flat or gradient background, with a base under it.

**Face shading**
- No real-time lighting. Each facing has one fixed colour (in effect a 6-direction "ambient cube").
- From face to face the hue changes, not just the brightness. In `mv_2_09` the left side is dark, the right side light, and the top lightest.

| Material | Top | Light side | Dark side | Picked from |
|---|---|---|---|---|
| Beige tower body | `#e2edd2` | `#cad2bb` | `#b3b488` | mv_2_09 |
| Pink base | (light pink) | `#ff88a8` | `#fb565d` | mv_2_09 |
| Teal hedge | `#4fcec1` | `#24abbb` | `#0c8d92` | mv_2_09 |
| Orange arch | `#ffd251` | `#ff8853` | `#e65961`, shading up to purple `#aa73b7` | mv_2_06 |
| Spire | — | `#2f3b46` | `#22354e` | mv_2_09 |

**Other traits**
- Soft, tinted AO in corners and gaps.
- Thin bright lines on top edges, e.g. `#f5eb91` on top of the battlements.
- Doorways are a dark brown gradient (`#917e60`).
- Backgrounds are soft, low-contrast gradients: peach `#ffe9dc` → `#f6ba95` (a warm glow in the bottom-left corner). There is also plain white (`mv_2_06`) and a light teal fog (`#c2dfe5` in `mv_1_03`).
- Floating objects have a soft, warm shadow under them (`#f9cbae`).
- There is no HUD on screen.

## Splatoon ink

- Saturated colours that contrast with the scene's main colours, e.g. purple with yellow, orange with green.
- Ink follows the surface. Its boundary is made of irregular blobs, with small satellite dots near the edge.
- The surface is glossy: highlights plus a slight sense of thickness.
- Ink in the air is a thick, sticky blob trailing a tail.

## How this project does it

- An orthographic isometric camera. The view turns in 90° steps.
- Box geometry is generated in C++. Each exposed face gets a region in the "paint atlas".
- Atlas channels: R holds paint, G baked AO, B decoration such as doors and windows, A the bright edge lines.
- The material is Unlit. Side tones are computed from the camera's left and right, so after the view turns it is still "dark left, light right".
- A vertical gradient, tinted AO and a fade at the bottom are layered on top. No real-time lights, shadows or Lumen.
- Colours are picked from the references (sRGB hex). The tone curve is off (Tone Curve Amount 0, Expand Gamut 0). A colour chart checks that the colours on screen match the palette.

## Iteration log

- **r1**: The colours are accurate (chart `#ff8853` renders as `#ff8754`), but the forms look like a "toy castle". Review: 5/10.
- **r2**, changes from the review:
  - Forms: the base is smaller and taller, with a plinth and a string course; the tower is now tall and thin (about 6:1), with an overhanging top; the main building gets a corbelled-arch doorway; columns and a bridge are added; a second small floating island is added.
  - Details: the battlements are flatter and wider, and half as many; the spire is now teardrop-shaped with a small flag; doors and windows are pointed arches with frames; the portholes are concentric rings in groups of three.
  - Shading and background: the AO reaches further, at about 10% strength; the top of the background is now one flat colour; a shadow is added under the floating island.

- **r3** (6/10 → 6.5/10):
  - New, real round-arch and pointed-arch geometry: the front and back walls are cut open, and the inner curve is built from flat strips that can take colour;
  - Portholes are "light outer ring + tube wall + deep magenta hole"; doors and windows get a bright edge on their side walls;
  - AO is split into a very faint wide term and a contact term of about 15%;
  - The tower gets a balcony halfway up and a moulding below the top; the small tower gets a crown and a dome; the second floating island is raised and joined by a bridge;
  - Neighbouring faces of the spire alternate light and dark; the flag becomes a wavy ribbon.
  - Also fixed the orthographic framing: under the engine's default `MaintainYFOV`, `OrthoWidth` is actually the frame's height.
- **r4** (finishing the r3 review notes; not reviewed again):
  - The base is lower, and shades to a light warm yellow `#e4e0a0` higher up;
  - The pink ground gets a faint 30 cm tile grid (matching the game grid); window openings have a bright edge on one side only;
  - The teal door is wider and thicker, the spire and ribbon are bigger, and the bridge is thicker;
  - The small tower gets an arched opening at the top and a door at the bottom.
- **Raised in review, not done yet**:
  - The beige mass sticks out past the edge of the base, held up by a quarter-round arch below (a cantilever);
  - Round corbels under the balcony;
  - A wood-plank pattern on the doors; a vertical gradient inside the doorways;
  - The teal blockers get a hedge texture;
  - Spiral faces on the spire, and a diamond-shaped finial on its tip.

## Performance (M2 Pro, standalone window 1920×1080, 100% resolution, no particles)

| Scene | Frame time | Notes |
|---|---|---|
| Old realistic scene (L_RainShield) | 26.5 ms | The baseline in the report |
| New scene + TSR | 14.8 ms | ProfileGPU: the TSR pass alone is 12.7 ms |
| New scene + FXAA | ≤ 8.3 ms | Capped by the 120 Hz refresh rate. Estimated from the passes, the real render work is about 2–3 ms (Metal's ProfileGPU counts about 5 ms of waiting in "Scene"; not verified separately) |

The game uses FXAA. Flat colours with hard edges don't need temporal super resolution, and TSR leaves ghost trails behind moving drops.

## Paint: puddles, not Splatoon ink (decided 2026-10-27)

- Reference: a generated image, `SourceArt/Reference/User/paint_rain_reference.png`.
  - The paint is flat, matte patches. Their edges are chains of many arcs, the tiles show through a little, and there are faint ripple rings inside.
  - The rain is thin coloured lines, each with a teardrop-shaped bead at the end.
  - Only one colour for now: the blue `#6ab4ec` from the reference image.
- Method: each drop of paint that lands makes a small circle that spreads (to 9 cm in about 1 s). The circles are metaballs with the kernel (1 − (d/R)²)³. Colour shows where the field reaches a threshold, so neighbouring circles merge before they touch, like puddles joining.
- Shading: flat paint colour, slightly lighter on top faces and slightly darker on dark sides, 8% transparency, a bright rim around the edge. The Splatoon-style bright lip and dark edge are gone.
- Drops (following the reference image): thin coloured lines with a highlighted bead at the front, and a tail that tapers and fades. On landing, a drop stays in place for 0.5 s and becomes a ripple ring that spreads along the surface and fades.
- Two kinds of drops (decided):
  - A big drop makes a puddle where it lands;
  - A small drop does not paint directly. When it lands on an existing puddle it feeds it: each drop adds 12 cm² of area, applied in batches every 0.15 s, so small drops make the patch grow outwards;
  - `rain.paint.SmallPaints 1` instead makes every drop create a small 4 cm puddle directly.
- The blockers are now Monument Valley 2's lilac (the `#bd9ad8` family), to set them apart from the blue paint.
- Iteration 2 (feedback: "the drops look bad, the small drops are too thin, the puddles look like footprints"):
  - Drops: now short capsules rounded at both ends, fading from head to tail, with no pointed tail; small drops are 2.6 cm wide at opacity 0.38, big drops 4.2 cm wide.
  - Ripples: only on upward-facing faces.
  - Puddles: now with a watercolour feel.
    - The field mapping is now "0.25 at the edge, 1 at four times the threshold", so the more paint piles up, the darker the colour;
    - The edge has a thin dark line of settled pigment;
    - Soft mottling at three scales (70 cm / 23 cm / 9 cm), about ±12% in density, with a little hue drift;
    - A little fine noise on the boundary, acting only near the edge.
- Bug fixed: the paint's shading by facing used to branch on the sign of the normal's z. The screen-derivative normal has z≈0 on walls and flips per pixel there, which left a layer of scratch marks. It is now a continuous blend.
- Iteration 3 (feedback 2026-10-28):
  - Rule: a small drop that lands where there is no paint yet goes straight back to the cloud. Only drops that land on paint are used up, and they feed that puddle. The "seeping" tried before (faint paint building up on bare surface) is removed.
    - The check runs on the CPU: drops that land on the target are exported as usual, and the CPU checks whether the paint field at the texel nearest the landing point reaches the colour threshold. So places where puddles have joined also count as paint.
    - Particles on the GPU no longer die: after landing they all go back to the cloud, and as each one leaves the cloud, its own index and the "share left" decide whether it still falls. The rain thins as it is used up, and you can't tell which drop was used.
    - Big drops are now 3% of the drops (the first example given).
  - Freeze: the rain streaks shorten as the freeze sets in, and end up as beads. A bead is 1.6 times the original width; the small drops' opacity rises to 0.7; the beads have a dark rim, light focused at the bottom and a highlight at the top left, and slowly "breathe".
  - Trackball: a translucent cream glass ball with latitude and longitude lines every 30°. Its poles are the rain's direction axis, and the arrow passes through it.
    - Dragging works like a globe: while the button is held, the grabbed point on the sphere stays under the cursor; dragged outside the ball, it turns about the view direction.
    - The input mode no longer hides or locks the cursor while dragging. Before, the engine switched to raw mouse input while the button was held, so the cursor position did not change and the ball could not be dragged.
  - The panel: shrunk to a narrow column in the top left with only the round time, the coverage bar (with a 90% mark), the bar for the rain left, and stats. Control hints moved to the bottom left.
- Iteration 4 (feedback 2026-10-28):
  - Rule: paint that is joined together counts as one body; a body does not cross faces. Whichever body a small drop lands in, that whole body's outline moves out by the same distance (fed area ÷ perimeter), so holes inside it and small gaps far away fill in too. Implementation: grouping by flood fill on each face's texel grid, plus an 8SSEDT distance transform; the outline can sit between two texels. Paint that could not grow out, held back by another body or the face's edge, is given back at the next grouping.
  - Freezing time: while the trackball is held, the round clock and the paint's spreading stop together.
  - Frozen drops: the rim, highlight and bottom glow are gone; they are now even, translucent round drops.
  - Film-look drops (toggle with F or the button before a round): each drop in the air is a ball lens that uses Scene Color to show an upside-down image of the scene behind it, with a dark rim, a highlight and a little paint colour; a falling drop shows the image stretched out, with a bright core. Landing is the same as now.
  - System 2: bouncy CPU paint balls. They bounce, stretch in flight and squash on impact; a ball that hits the target bursts and paints, and is then used up. Balls collide with each other through a neighbour query.
  - Four new levels: Canopy, Courtyard (rain); Nook, Pinball (paint balls).
- Iteration 5 (feedback 2026-10-28): Question: why do the puddles look two-coloured again? Cause: the watercolour shading made paint darker or lighter by its thickness. A big drop's puddle is thick and dark at the centre, paint grown by a whole body's spread is just over the threshold and light, and a thin dark line is also drawn near the threshold. Decision: no watercolour feel; a whole body is one colour, and no effect is tied to thickness.
  - Material: the thickness-based shading and the settled-pigment line are removed. There is one paint colour, shaded by the facing of the face it is on, with the tiles showing through slightly; over large areas only a ±3% low-frequency brightness drift remains. The fine jitter at the edge stays.
  - Growth: texels inside a body that are surrounded by paint hold at least 2 times the threshold amount of paint, so the edge noise cannot cut small holes in the middle of a body; while growing, paint becomes solid within 2 cm (was 6 cm) behind the outer edge.
