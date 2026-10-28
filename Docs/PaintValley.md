# Paint Valley — design and implementation notes (branch `paint`)

Paint Valley is the redesign of the Rain Shield course project. The structure is blocks on a floating island in Monument Valley's style. You steer a rain of paint so that it covers the cream "target" blocks. The lilac blockers and the pink ground repel paint. A round ends at 90 % coverage (the clock is the score) or when the paint runs out. Art notes and the iteration log are in `Docs/ArtDirection.md` (Chinese).

## Run

- Build `ProjectRainEditor` (see the README), open **`/Game/RainMagic/Maps/L_PaintValley`** and press Play.
- The generated assets are committed. To regenerate them, run this in the editor's Python console:
  ```python
  import unreal
  L = unreal.RainBuilderLibrary
  L.build_paint_assets()              # MPC_Paint, M_PaintSurface, M_PaintBackdrop, M_PaintBall (the trackball)
  L.build_paint_rain_assets(100000)   # System 1: NS_PaintRain + M_PaintDrop
  L.build_paint_ball_assets(3500)     # System 2: NS_PaintBalls (CPU), NS_PaintBallsGPU (the port) + M_PaintBlob
  ```
- Standalone measurement (one run, one line in `Saved/Sweeps/PaintMeasure.txt`):
  ```sh
  Scripts/paint_sweep.sh rain | balls | ballsgpu | pause
  python3 Scripts/paint_perf.py       # -> Docs/Perf/paint_sweep.csv and the report's figure
  ```

## Controls

| Input | Action |
|---|---|
| Drag the ball (bottom right) | Turns the direction of the rain or the balls. It works like a globe: the point you grab stays under the cursor, and the arrow is the globe's axis. |
| Hold the ball still (0.25 s) | Stops the rain, the round's clock and the spreading paint. The drops hang in the air. |
| WASD / arrow keys | Turn the direction around the vertical; tilt it. |
| Enter / Space | Start the round (Enter after a win: next level). |
| R | Reset the round. |
| Tab or the panel's button (before a round) | Switch between System 1 (rain) and System 2 (paint balls). |
| F or the panel's button (before a round, rain only) | Drops in the air: paint look or film look. |
| Panel slider "drops" / "paint balls" (before a round) | The pool's size (rain 10k–400k, balls 100–5,000, on a log scale); the pool is rebuilt when you let go. |
| Panel slider "drop opacity" (any time, rain only) | How opaque the drops in the air are: 0–200 % of their own opacity. |
| Q / E, mouse wheel, PgUp / PgDn | Turn the view, zoom, change level. |

## Rules (as the user set them)

**One pool.** Each system's particles are spawned once per level. No particle is ever spawned after that, and none is killed.
- A small drop is **used up only where it lands on paint**: it feeds the paint there.
- A small drop that lands on bare target surface, the ground, a blocker or a full face goes back to the cloud.
- **Big drops** are 3 % of the pool. A big drop seeds a new puddle wherever it lands on the target, and is never used up.
- The rain thins as the pool empties: a drop leaves the cloud only while its rank is below the share of the pool left. Big drops stay 3 % of what falls.

**Paint is puddles that join.** A big drop's puddle spreads over about a second. Puddles are metaballs, so neighbouring puddles flow together.
- All paint joined together on one face is one **body**. A body never crosses an edge.
- A small drop that lands in a body makes the body's **whole outline** move out by the same distance: the fed area divided by the perimeter.
- So holes inside a body, and gaps far from where the drop fell, close up too.
- This is done on each face's texel grid:
  - The paint level is crossed between texels, so the outline can sit between them.
  - A two-pass distance transform (8SSEDT) gives every bare texel its distance to the nearest body's outline.
  - Bodies are found by flood fill.
  - Only fed bodies claim the bare texels around them.
  - Paint that an outline could not place (held back by another fed body or the face's edge) is given back at the next regrouping, as long as the outline still makes progress.

**Holding time.** A still press on the trackball freezes the rain: the drops round up into see-through drops and hang there. The round clock and the paint's spreading stop with them.

**End.** The round is won at 90 % of the target's visible area (downward faces don't count). It is lost when the pool is empty and everything already bought has finished spreading (at most 3 s after the pool runs dry).

## System 1 — paint rain (GPU sprites)

- **Asset:** `NS_PaintRain`, one GPU emitter.
- **HLSL:** `Niagara/HLSL/PaintRainSpawn.hlsl` and `PaintRainUpdate.hlsl`.
- **Count:** 100,000 (`rain.paint.Count`).

Per-drop rules:
1. **Steer.** The velocity relaxes towards the player's current direction (time constant 0.25 s), so the stream bends like a hose's when the direction turns.
2. **Collide.** Each step is swept against up to 32 level boxes (slab test, no tunnelling). The face entered gives the normal.
3. **Recycle.** A drop goes back to the cloud on landing, after a ripple (shown on floors only), or when it leaves the stream.

**GPU → game.** A drop landing on a target face that is not full raises `RainGameHit` for one frame. *Export Particle Data* reads back the hit point, the kind (big or small) and the velocity. The game paints with it and counts the pool.

**Game → GPU.** User parameters carry:
- the direction;
- the boxes;
- one bit per box face that is already full (re-uploaded every 0.2 s);
- the share of the pool left;
- a round epoch that sends every drop back to the cloud;
- the freeze.

**Film look** (`F`). In the air, the drops use the film version's look: each drop is a ball lens. `Materials/HLSL/PaintDropLens.hlsl` computes, from the sprite's UV derivatives, where each pixel looks. A Scene Color lookup then shows the scene behind the drop upside down and shrunk. On top of that image the drop gets a darker rim, a glint and a slight paint tint. Falling drops show the same image smeared along the streak, with a bright core. Landing works the same in both looks.

## System 2 — paint balls (CPU meshes)

- **Asset:** `NS_PaintBalls`, one CPU (VectorVM) emitter drawn as engine spheres.
- **HLSL:** `PaintBallSpawn.hlsl`, `PaintBallUpdate.hlsl` and `PaintBallNeighbors.hlsl`.
- **Count:** 3,500 (`rain.paint.Balls`).
- The CPU HLSL is branch-free (0/1 factors and `lerp`), because the VectorVM cannot flatten branches that declare variables.

**State machine:** cloud → flying → resting → burst → used.
- **Flying.** A ball is pulled along the player's direction with quadratic drag. It bounces off the ground and the blockers (restitution 0.72, friction 0.08), and squashes on impact and stretches with speed.
- **Resting.** A ball goes back to the cloud after 0.5 s at rest, after 10 bounces, or when it leaves the stream.
- **Burst.** On a target face that still takes paint, the ball bursts: one export event, and it flattens on the face.
  - On paint, the game feeds that body.
  - On bare surface, it splashes a puddle (radius at least 6 cm).
  - That ball is used up.

**Neighbour query** (`NM_PaintBallNeighbors`): an approximate 2-nearest-neighbour search on a Particle Attribute Reader bound to the emitter itself, which reads the previous frame's balls by index.
- Each ball keeps the indices of its two nearest balls.
- Every frame it re-measures them, the two each of them keeps (a neighbour's neighbour is likely near) and three random balls, and keeps the closest two.
- It then collides with them: it pushes them apart by half the overlap each, and exchanges their approaching speed with the same restitution.
- No grid is needed (NeighborGrid3D is GPU-only).
- **VectorVM pitfall.** Its `frac(x)` is x − trunc(x), so it is negative for x < 0. The hashes use x − floor(x) instead. With `frac`, half the balls were launched from the centre of the launch disc, because the launch radius is √(random) and the square root of a negative number came out as 0.
- **Wiring the reader in from C++.** A module graph reaches a data interface through a map-get node, and the engine does not export that node's class.
  - `RainAuthoring::CreateReaderModule` duplicates the engine's `CalculateNeighbors` module, which already has that chain.
  - It rewires the graph to InputMap → Custom HLSL (Map, Reader) → Output.
  - It binds the default reader to *Self*.

**Paused when idle.** Only one system plays at a time. The other is paused and hidden (`rain.paint.PauseIdle`).

## Levels

| # | Name | System | Idea |
|---|---|---|---|
| 0 | Blocks | rain | A gate and two steps under a floating plate, with a low wall. |
| 1 | Canopy | rain | A long low block under a wide roof: tilt the rain low to reach under it. |
| 2 | Courtyard | rain | A block with a small tower inside walls open on one side. The walls shade the lower faces. |
| 3 | Nook | balls | The target sits in a nook roofed and walled on three sides: skip the balls over the ground into it. |
| 4 | Pinball | balls | Four target pillars of different heights between floating bumpers. |
| 5 | Stairs | rain | Four steps rising to a plate that floats over the top two: straight-down rain misses them. |
| 6 | Table | balls | A bank shot: a thin slab (15 cm) under a floating table top that overhangs it by 30 cm, 30 cm above it. Its low sides (about 40 % of the target) take paint straight on. For its top, a ball fired straight in must skim under the edge; fired at the ground just outside, it bounces once and skips in. In 10 s with `BallWait 8`: 75–77 % aimed at the ground (tilt 55°), 42–43 % skimming straight in (tilt 80°). |
| 7 | Peach Keep | rain | The castle showcase from the art pass (not a game level). |

## Console variables

| Cvar | Default | Effect |
|---|---|---|
| `rain.paint.Count` | 100000 | System 1 pool (respawns the rain) |
| `rain.paint.CloudWait` | 8 | Longest wait of a recycled drop in the cloud (s): sets how many drops are in the air |
| `rain.paint.Budget` | 2.0 | Paint in the small drops' pool, as a multiple of what the goal needs |
| `rain.paint.BigSeed` | 60 | Paint a big drop seeds, in small drops' worth |
| `rain.paint.BigShare` | 0.03 | Share of big drops (refills the pool) |
| `rain.paint.Balls` | 3500 | System 2 pool |
| `rain.paint.BallsGPU` | 0 | 1 runs System 2 on the GPU (`NS_PaintBallsGPU`, the same HLSL): the CPU → GPU port measured in the report. Rebuilds the pool |
| `rain.paint.BallWait` | 30 | Longest wait of a paint ball in the cloud (s) |
| `rain.paint.BallBudget` | 1.5 | Paint in System 2's pool, as a multiple of what the goal needs |
| `rain.paint.PauseIdle` | 1 | 0 keeps the idle system simulating (to measure what pausing saves) |
| `rain.paint.DropOpacity` | 1.0 | Opacity of the drops in the air, times their own (0–2; the panel's slider sets it) |

Command-line options:
- `-PaintLevel=N` starts on level N.
- `-PaintBalls` starts in System 2.
- `-PaintBallsGPU` starts with System 2 on the GPU (`rain.paint.BallsGPU 1`).
- `-PaintMeasure` measures and quits (`-PaintMeasureSettle=S` sets the settle time; `-PaintMeasureNoNiagara` leaves out the per-system Niagara timers).
- `-PaintDemo` plays the scripted ~88 s tour of the video (`-PaintDemoQuit` quits at the end; `Scripts/paint_demo.sh` records it).

## Performance

The measurements, the method and the analysis are in **`Docs/PaintValleyReport.pdf`** (2 pages). The data are in `Docs/Perf/paint_sweep.csv`, and the profiler captures are `Docs/Perf/paint_*`. Headline numbers (Apple M2 Pro, standalone 1920×1080, FXAA; frames are capped at the display's 120 Hz, 8.33 ms):

- **System 1, rain.** The GPU sets the pace: each 100k drops cost about 2.9 ms of GPU, above about 2.5 ms for the scene.
  - The 16.6 ms budget breaks at about 460k drops.
  - Niagara's CPU time stays near 0.1 ms all the way to 1.6M drops.
- **System 2, paint balls on the CPU.** About 0.72 µs of Niagara CPU time per ball.
  - The 4 ms Niagara budget breaks at about 5.4k balls; the default 3,500 uses 2.6 ms.
- **The GPU port** (`rain.paint.BallsGPU 1`).
  - Niagara CPU time drops to about 0.05 ms at any count.
  - At 16k balls the frame drops from 15.2 to 9.0 ms. The port breaks the frame budget at about 39k balls.
- **Pausing the idle system** makes no measurable difference: the idle particles wait hidden in the cloud.

**Numbers taken before 2026-10-28 11:20 are wrong.** Changing a pool at runtime used to keep the old pool alive (500 balls requested, 4,000 live). `Activate(true)` re-fired the spawn burst on top of the old particles; `ReinitializeSystem()` now replaces the instance. The sweeps now set the pools at launch (`-PaintCount=`, `-PaintBallCount=`) and log the live count.

## Code map

| File | What |
|---|---|
| `Source/ProjectRain/*/RainPaintGame.*` | The game actor: levels, camera, trackball, both systems' parameters, readback, pools, rounds |
| `Source/ProjectRain/*/PaintSurface.*` | Box faces → texel grid, mesh, atlas; puddles (metaballs), bodies (flood fill, distance transform, outline growth), coverage |
| `Source/ProjectRain/*/PaintHUD.*` | The canvas panel and its buttons |
| `Source/ProjectRainEditor/Private/RainAssetBuilder.cpp` | Builds every material and Niagara asset from the HLSL files |
| `Source/ProjectRainEditor/Private/RainNiagaraAuthoring.cpp` | Niagara authoring from C++ (modules, emitters, the reader module) |
| `Niagara/HLSL/Paint*.hlsl`, `Materials/HLSL/Paint*.hlsl` | The simulation and material code |

## Known issues and open questions

- **Balance is not tuned.** It is left to play-testing, through the cvars above.
  - Paint balls make Nook easy. Once any paint is on a face, a ball that lands on it grows the whole body, including the parts under the roof.
  - The best rain density (`rain.paint.CloudWait`) is still open.
- **Trackball not tested with a real mouse.** The globe drag was tested through script calls only. Posting real mouse events needs a macOS Accessibility permission this setup does not have.
- **Peach Keep (showcase only).** On the arch faces, the full-face mask doesn't line up with the arch's collision proxies, so drops there are not read back. The level also has 35 collision boxes, of which 32 are tested.
- **The GPU port paints faster early on.** On Nook at 3,500 balls it painted 89 % in 13 s, against 55 % on the CPU (both reach about 92 % by 35 s). The same HLSL runs on both, and the cause is not isolated. One suspect is the `sin`-based launch hashes, whose precision differs between the VectorVM and the GPU.
- **Debug-draw arrow.** The direction arrow in the level is a debug draw, so it is missing in a Shipping build.
