# Paint Valley — Niagara at Scale (course project, release `v2.0-paintvalley`)

Cheng Guo (cg401@duke.edu), developed in collaboration with Claude Opus 5.5 (Anthropic).

A puzzle game where **Niagara particles paint the level**. Structures of grid blocks float in Monument Valley's style. You steer a stream of paint with a trackball until it covers 90 % of the cream target blocks; the clock is the score, and the pool of paint can run out. Blockers and the ground take no paint. Paint spreads as puddles that join, and drops that land on paint grow it. The mechanic borrows from *Auditorium* (a particle stream bent by the player until it fills its targets). The earlier version, Rain Shield, is the tag `v1.0-rainshield`; its section below is unchanged.

The same mechanic has two configurations; press **Tab** before a round to switch (the idle one is paused and hidden):

| | System 1: paint rain (simple, many) | System 2: paint balls (complex, few) |
|---|---|---|
| Asset | `NS_PaintRain` | `NS_PaintBalls` (CPU), `NS_PaintBallsGPU` (the same HLSL on the GPU, the measured port) |
| Simulation, renderer | GPU compute, sprites | CPU (VectorVM), meshes |
| Count | 100,000 (`rain.paint.Count`) | 3,500 (`rain.paint.Balls`) |
| Per-particle logic | 3 rules: steer towards the player's direction (a hose-like lag), swept collision against up to 32 boxes, recycle on landing | State machine (cloud, flying, resting, burst); swept collision with bounce, friction and squash; collision with the 2 nearest balls (a neighbour query on a Particle Attribute Reader bound to the emitter) |
| Back to gameplay | Each drop landing on a target face that is not full → Export Particle Data (GPU readback) → paint, pool | Each burst on the target → Export Particle Data → paint, pool |

**Performance report:** `Docs/PaintValleyReport.pdf` (2 pages). Data: `Docs/Perf/paint_sweep.csv`. Profiler captures: `Docs/Perf/paint_*`.
**Video:** [PaintValley_video.mp4](https://github.com/Iwan000/PaintValley/releases/tag/v2.0-paintvalley) on the release page (86 s, 1080p; video files are not in git, locally `Renders/PaintValley_video.mp4`). It is a recording of the scripted tour `-PaintDemo` (`Scripts/paint_demo.sh`). `stat fps` is on throughout, and `stat unit` and the Niagara debug HUD join it while the particle count changes live. In the tour a scripted player aims like a person would: it finds the target faces still bare, turns the view to them (Q / E) and steers the paint there.
**Design notes:** `Docs/PaintValley.md` (rules, both systems, levels, code map). Art notes: `Docs/ArtDirection.md`.

## Build and run (Paint Valley)

1. Build the editor target with Unreal Engine 5.7 (C++ project; the plugins it uses ship with the engine):
   ```sh
   # macOS (where it was developed and measured: MacBook Pro M2 Pro, Metal)
   "<UE_5.7>/Engine/Build/BatchFiles/Mac/Build.sh" ProjectRainEditor Mac Development -Project="$PWD/ProjectRain.uproject" -WaitMutex
   # Windows (the standard command; not tested on Windows), or right-click the .uproject > Generate Visual Studio project files
   "<UE_5.7>\Engine\Build\BatchFiles\Build.bat" ProjectRainEditor Win64 Development -Project="%CD%\ProjectRain.uproject" -WaitMutex
   ```
2. Open `ProjectRain.uproject`. It opens **`/Game/RainMagic/Maps/L_PaintValley`**, which is also the game's default map. Press Play.
3. All assets are generated from code, and the generated `.uasset` files are committed. To regenerate them, run this in the editor's Python console:
   ```python
   import unreal
   L = unreal.RainBuilderLibrary
   L.build_paint_assets()              # MPC_Paint, M_PaintSurface, M_PaintBackdrop, M_PaintBall
   L.build_paint_rain_assets(100000)   # System 1: NS_PaintRain + M_PaintDrop
   L.build_paint_ball_assets(3500)     # System 2: NS_PaintBalls, NS_PaintBallsGPU + M_PaintBlob
   ```

## Controls (Paint Valley)

| Input | Action |
|---|---|
| Drag the ball (bottom right) | Turn the direction, like a globe. WASD / arrow keys also turn and tilt it |
| Hold the ball still | Freeze the rain and stop the clock |
| Enter / Space, R | Start the round; reset it |
| Tab (before a round) | System 1 (rain) or System 2 (paint balls) |
| F (before a round) | Drops in the air: paint look or film look |
| Panel sliders | Pool size (before a round); drop opacity |
| L or the panel's level button (before a round) | The level list: click a level to play it. Number keys 1–8 pick one directly; PgUp / PgDn step through them |
| Q / E, wheel | Turn the view, zoom |

## Console variables (Paint Valley)

| Cvar | Default | Effect |
|---|---|---|
| `rain.paint.Count` | 100000 | System 1's pool: drops, respawned at once |
| `rain.paint.Balls` | 3500 | System 2's pool: paint balls, respawned at once |
| `rain.paint.BallsGPU` | 0 | 1 runs System 2 on the GPU (the CPU → GPU port) |
| `rain.paint.PauseIdle` | 1 | 0 keeps the idle system simulating |
| `rain.paint.CloudWait`, `rain.paint.BallWait` | 8, 30 | Longest wait (s) of a recycled drop / ball before it falls again |
| `rain.paint.Budget`, `rain.paint.BallBudget` | 2.0, 1.5 | Paint in each pool, as a multiple of what the goal needs |
| `rain.paint.BigShare`, `rain.paint.BigSeed` | 0.03, 60 | Share of big drops; paint a big drop seeds |
| `rain.paint.DropOpacity` | 1.0 | Opacity of the drops in the air (0–2) |

Command-line options: `-PaintLevel=N`, `-PaintBalls`, `-PaintBallsGPU`, `-PaintCount=N` and `-PaintBallCount=N` (the pools from the start), `-PaintMeasure` (measure and quit), `-PaintDemo` (the video's tour).

## Reproducing the numbers (Paint Valley)

```sh
Scripts/paint_sweep.sh rain        # System 1: 0 to 1.6M drops, one standalone 1920x1080 run per count
Scripts/paint_sweep.sh balls       # System 2 on the CPU: 500 to 16k balls
Scripts/paint_sweep.sh ballsgpu    # System 2's GPU port: 500 to 64k balls
Scripts/paint_sweep.sh pause       # pausing the idle system, on (1) and off (0)
python3 Scripts/paint_perf.py      # -> Docs/Perf/paint_sweep.csv and the report's figure
Scripts/build_report.sh PaintValleyReport
```

---

# Rain Shield — Niagara at Scale (course project)

A puzzle game where **Niagara particles decide the outcome**. A shape floats in a 10×10×10 grid. You place the blocker pieces the level gives you, pick the direction the weather comes from (26 directions, locked once it starts), and then it falls for a fixed time. Every drop that reaches the shape is read back from the simulation; how wet the shape gets decides the stars. It borrows from *Auditorium* (particle streams shaped by placed geometry).

The same mechanic has two configurations; press **G** to switch between them while planning:

| | System 1: rain (simple, many) | System 2: hail (complex, few) |
|---|---|---|
| Asset | `NS_RainGame` | `NS_RainHail` (CPU), `NS_RainHailGPU` (the same HLSL on the GPU) |
| Simulation, renderer | GPU compute, sprites (the film rain's drop shading) | CPU (VectorVM), meshes |
| Count | 200,000 (`rain.game.Count`) | 2,000 (`rain.game.HailCount`) |
| Per-particle logic | 3 rules: fall along the chosen direction; divergence-free (ABC) gusts; swept collision against the grid's boxes. Blockers absorb drops | Gravity and drag; swept sphere-vs-box collision with bounce and friction; state machine (falling, bouncing, resting, melting); impact events |
| Back to gameplay | Each hit on the shape → Export Particle Data (GPU readback) → score | Each impact → Export Particle Data → hit on the shape, or damage to a blocker (it breaks at 0 hit points) |

**Performance report:** `Docs/PerformanceReport.pdf` (2 pages). Data: `Docs/Perf/*.csv`. Profiler captures: `Docs/Perf/*.jpg`.
**Video:** `Renders/RainShield_video.mp4` (90 s, 1080p; video files are not in git). It is a recording of the scripted tour `-RainDemo` (`Scripts/record_demo.sh`). The stat overlay segment shows `stat unit` and the Niagara debug HUD while the particle count changes live. The video was recorded in a 1600×900 window at the automatic screen percentage (the overlay shows a render resolution of 83.4%, 1336×751), so its frame times are not the report's 1080p numbers.

## Build and run

Built and tested only on **macOS 26 with an Apple M2 Pro and Unreal Engine 5.7** (Metal). The project is C++; the code doesn't depend on the platform, but no other platform was tried.

1. Build the editor target:
   ```sh
   "<UE_5.7>/Engine/Build/BatchFiles/Mac/Build.sh" ProjectRainEditor Mac Development -Project="$PWD/ProjectRain.uproject" -WaitMutex
   ```
   On Windows, run `Build.bat ProjectRainEditor Win64 Development ...`, or open the generated solution.
2. Open `ProjectRain.uproject`, open **`/Game/RainMagic/Maps/L_RainShield`** and press Play. It is also the game's default map.
3. All assets are generated from code and the generated `.uasset` files are committed, so step 3 is optional. To regenerate them, run this in the editor's Python console:
   ```python
   import unreal
   L = unreal.RainBuilderLibrary
   L.build_rain_assets(230000, 15000, 60000)   # film rain + the drop material the game reuses
   L.build_stage_materials()
   L.build_rain_game_assets(200000)            # NS_RainGame (System 1)
   L.build_rain_hail_assets(2000)              # NS_RainHail + NS_RainHailGPU (System 2), M_Hail
   ```
   The game level is `L_RainStage` copied on disk, plus the game actor (`Scripts/build_game_map.py`).

## Controls

| Key | While planning | While it falls / after |
|---|---|---|
| W A S D, Q E | move the piece (along the grid axes nearest the view), down/up | — |
| R / T | rotate the piece about the vertical / the view's horizontal axis | R: back to planning with the same pieces |
| Tab, Space, Backspace | next piece, place it, undo | — |
| 1–6, Z / X | rain direction: the six axes / step through all 26 | — |
| G | rain ↔ hail | — |
| Enter | start (locks the direction) | next level (or retry when too wet) |
| Right mouse drag or arrow keys, wheel | orbit, zoom | orbit, zoom |
| PageUp / PageDown | next / previous level | next / previous level |

**Wetness** = hits ÷ the hits an unshielded shape of the same outline would take (computed from the drop density and fall speed, so it doesn't change with the particle count). Stars: ≤ 4 %, ≤ 12 %, ≤ 30 %.

Every level was played to its result in both modes with a sensible placement from above (`Scripts/gamelib.py`, `play_level`):

| Level | Rain | Hail |
|---|---|---|
| 1 Cube | 3 stars, 0 % wet | 3 stars, 3.0 % wet, plate held |
| 2 Lamp post | 3 stars, 2.5 % wet | 1 star, 25 % wet, both pieces broken |
| 3 Arch | 3 stars, 3.3 % wet | 1 star, 20 % wet, 2 pieces broken |

Hail is the harder mode: stones bounce off the edges and wear the blockers down.

## Console variables and commands

| Name | Default | What it does |
|---|---|---|
| `rain.game.Count <n>` | 200000 | Rain drops (System 1). Reinitialises the system with exactly *n* particles; the HUD shows requested vs live. |
| `rain.game.HailCount <n>` | 2000 | Hailstones (System 2). |
| `rain.game.Mode 0/1` | 0 | Rain / hail (the G key). |
| `rain.game.HailGPU 0/1` | 0 | Hail simulated on the CPU (`NS_RainHail`) or on the GPU (`NS_RainHailGPU`). This is the CPU → GPU port measured in the report. |
| `rain.game.PauseUnselected 0/1` | 1 | 1 pauses and hides the system that isn't selected. 0 is the "before" of that optimisation. |
| `rain.game.Sweep [hail] [counts…] [settle=s] [measure=s]` | — | Scaling sweep. For each count it waits for the live count, measures frame, game-thread, render-thread and GPU time (`stat unit`) and Niagara game/render-thread time (per-system particle perf stats), then writes `Saved/Sweeps/RainShield_<system>_<time>.csv`. Default counts: rain 0, 50k, 100k, 200k, 400k, 800k, 1.6M; hail 0, 500, 1k, 2k, 5k, 10k, 20k, 40k. |

Engine tools used in the report: `stat unit`, `stat Niagara`, `NiagaraDebugHud 1`, `ProfileGPU`, and Unreal Insights (`-trace=default,gpu`). `stat GPU` shows nothing on Metal.

## Reproducing the numbers

Standalone, 1920×1080, 100 % screen percentage. The sweep sets `r.ScreenPercentage 100`, uncapped frame rate and vsync off by itself, then quits:

```sh
<UnrealEditor> ProjectRain.uproject /Game/RainMagic/Maps/L_RainShield -game -windowed -ResX=1920 -ResY=1080 -ForceRes \
    -RainSweep=0,50000,100000,200000,400000,800000,1600000 -RainSweepQuit                          # System 1
    -RainSweep=hail:0,500,1000,2000,5000,10000,20000,40000 -RainSweepQuit [-RainHailGPU]           # System 2 (CPU / GPU port)
    ... -RainNoPauseUnselected                                                                     # the optimisation's "before"
```

On this Mac: `Scripts/sweep.sh rain|hail|hailgpu` (closes the editor first, prints the CSV), `Scripts/capture.sh` (stat overlays), `python3 Scripts/plot_perf.py` (figure), `Scripts/build_report.sh` (PDF). In PIE, `rain.game.Sweep` does the same in the editor viewport, where the numbers are lower.

## Code

| Path | What |
|---|---|
| `Source/ProjectRain/Public/RainShieldGame.h`, `Private/RainShieldGame.cpp` | The game: levels, grid and pieces, rounds and scoring, the readback callback (`ReceiveParticleData`), blocker damage, orbit camera, HUD, cvars, sweeps |
| `Source/ProjectRain/Public/RainShieldTypes.h` | Constants shared by the game and the asset builder (box count, drop and hail sizes, export capacity) |
| `Niagara/HLSL/GameRainSpawn.hlsl`, `GameRainUpdate.hlsl` | System 1 (+ `RainLook.hlsl`, the drop shape/streak/lens/lamp tail shared with the film rain) |
| `Niagara/HLSL/HailSpawn.hlsl`, `HailUpdate.hlsl` | System 2. Branch-free for the VectorVM; the comments list the VM pitfalls hit (safe division, swizzle writes, branch flattening) |
| `Source/ProjectRainEditor/Private/RainAssetBuilder.cpp` | Builds every Niagara system, module and material from the HLSL above (`BuildRainGameAssets`, `BuildRainHailAssets`) |
| `Source/ProjectRain/Private/RainMagicController.cpp` | The film rain's controller. In the game it supplies the drops' camera, lens and lamp inputs (`ExtraRainComponents`, `bGameMode`) |

The rest of this README documents the original *Project Rain* (the film's rain-stopping trick).

---

# Project Rain

A from-scratch recreation in UE 5.7 Niagara of J. Daniel Atlas's rain trick from the finale of *Now You See Me 2* (2016): the rain stops in mid-air, hovers, is pushed around by his gestures, swirls, rises back into the sky and finally falls again. The goal is the particle effect itself; the stage only gives the rain its lights and a background.

The effect has two layers:

1. **Realistic rain:** drop sizes and fall speeds, energy-conserving motion-blur streaks, ball-lens refraction, forward scattering of the lights, gusts, splashes, a wet ground.
2. **Controlling the rain:** the stop wave, a slight drift while hovering, a gesture influence field, chaotic swirling, lifting away, and the rain resuming. In PIE you can also control it yourself with the keyboard and mouse.

The rain is an infinite rain field that follows the camera (see "The rain field" below): the camera always stands in the rain, with big drops in front of the lens and small ones in the distance; near-large, far-small comes from perspective alone. Around the performer there is also a cluster of big drops from the "rain towers".

v9 focuses on making the stopped rain read as water rather than a screen full of glowing white dots:
- Water is transparent; it only looks like water when it refracts what is behind it. So an HDR panorama of a city on a rainy night was added as the background, and every drop shows the street upside down.
- Depth of field uses the camera's real lens settings: defocused drops get fainter and softer, not brighter.
- The drops are flattened, slightly wobbling beads of water with a bright core.
- New camera positions that show off the rain; press `V` in PIE to cycle them.

PIE screenshot comparison: `Renders/v9_pie_sheet.jpg` (v8 and v9 from the same view, the four new cameras, rain, chaos). `Renders/v9_hover_static_hero.mp4` is a clip (MRQ) of a 4 s hover from the fixed hero camera, to check for strobing and flicker: the overall brightness changes by less than 0.1 % from frame to frame, and the per-pixel changes are the tracks of drops slowly pushed by the hand. The finished film is still v8's `Renders/RainTrick_v8.mp4` (46 s, 23.976 fps, 2.39:1); the shot sequence has not been redone with v9's cameras yet. The v6–v8 frame sheets are kept under `Renders/` (video files are not in git); from v8 on, each version is tagged in git (`git checkout v8`).

## Contents

| Path | Contents |
|---|---|
| `Docs/Research/01_NYSM2_Scene_Reference.md` | Beat-by-beat analysis of the film scene: Framestore production material, frame-by-frame measurements, a list of visual traits (notes in Chinese) |
| `Docs/Research/02_Rain_Physics_And_Rendering.md` | The literature on rain physics and rendering, with a table of implementation parameters (notes in Chinese) |
| `Docs/Research/03_Niagara_Programmatic_Authoring.md` | How to generate Niagara assets from C++ in UE 5.7 (notes in Chinese) |
| `Docs/Research/frames/` | Key frames and contact sheets of the reference clip |
| `Niagara/HLSL/RainField.hlsl` | The rain field's definition (periodic window, wrapping, distant fade), prepended to every particle module at build time |
| `Niagara/HLSL/*.hlsl` | Source of the custom Niagara HLSL modules (drop spawn and update, splashes) |
| `Materials/HLSL/*.hlsl` | Source of the materials' Custom nodes (drop shading, wet ground, night backdrop) |
| `SourceArt/` | Downloaded source art (not in git; the imported assets are in `Content/`), see `Scripts/import_backdrop.py` |
| `Source/ProjectRainEditor` | Editor module: generates every asset from the HLSL above (`URainBuilderLibrary`), plus the Python file command queue |
| `Source/ProjectRain` | Runtime: `ARainMagicController` (performance timeline, player control, gestures, lights, camera and lens sync) |
| `Scripts/` | Tools for the stage, the shot sequence, rendering and tuning (see below) |
| `Scripts/legacy/` | v6's Greenwich stage (colonnade, crowd, bus) and its shot scripts |

Every `.uasset` is generated from code. To change the effect, edit the `.hlsl` or the C++ and rebuild.

## Building and rendering

With the editor open, run Python in it with `python3 Scripts/ue_cmd.py "<python>"` or `python3 Scripts/ue_cmd.py -f <file.py>`. This is a file command queue, used instead of Python Remote Execution, whose multicast discovery does not work on this Mac.

```python
# First time only: download the background panorama and import it (see the notes in Scripts/import_backdrop.py)
#   curl -o SourceArt/rathaus_4k.hdr https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/rathaus_4k.hdr
#   python3 Scripts/ue_cmd.py -f Scripts/import_backdrop.py
import unreal
L = unreal.RainBuilderLibrary
print(L.build_rain_assets(230000, 15000, 60000))  # total drops, splash particles, rain-tower drops among them -> materials, MPC, HLSL modules, NS_RainMagic
print(L.build_stage_materials())                   # M_RainBasic, M_WetGround, M_NightSky
# Then run Scripts/build_stage.py (the level) and Scripts/build_sequence.py (the shot sequence)
exec(open(unreal.Paths.project_dir() + "Scripts/render_mrq.py").read()); render(0, 46, "RainTrick")  # MRQ render
```

- The drop count sets the density. The window is 16 m × 16 m × 6 m (1536 m³), so 170k rain-field drops make 110 per m³; the 60k rain-tower drops add another 270 per m³ at the centre of the stage.
- `build_rain_assets` returns a compile report: each Niagara script's status, HLSL/GPU errors, and material compile errors.
- When `build_rain_assets` rebinds the level's `RainMagic`, it calls `ResetSystem()` once, which spawns an extra batch of drops in the editor. After rebuilding the assets in the editor, restart it before judging density and frame rate.
- `build_stage.py` clears the level and deletes every camera, so always rerun `build_sequence.py` afterwards, or `LS_RainTrick`'s bindings break.
- `render_mrq.py` sets the controller not to loop and saves the level. After rendering, set `loop` back to `True` and `render_width_override` back to 0 for interactive preview. `render(..., cvars={...})` sets console variables for that render only.
- `Scripts/restart_editor.sh`: compiles the C++ and restarts the editor (`nobuild` only restarts it).
- `Scripts/fps_probe.py <label> [seconds]`: measures the frame rate of the editor or PIE by wall clock and appends the result to `Saved/fps.txt`.
- `Scripts/rshot.sh` with `Scripts/rainlib.py`: freezes the performance at a moment and takes a screenshot from a given camera (`CAM=CineCam_Close` picks it); `rainlib.sky(exposure, yaw)` adjusts the background and recaptures the environment. `SETTLE=4` first lets it rain for 4 s (so drops parked in the sky fall back). The wait lives in the shell, because `sleep` in the editor's Python freezes the whole editor.
- Diagnostics: `describe_niagara_system` (RI parameters, including the loop duration), `dump_generated_hlsl` (exports the generated HLSL), `build_diag_system` (a single-emitter system for bisecting errors).
- Debug view: set the controller's `DropExposure` above 99 and every visible drop is drawn opaque magenta, to check where the drops are and how many there are.

## The rain field (`Niagara/HLSL/RainField.hlsl`)

The rain is defined as an **infinite, uniform rain field, periodic in x and y with a 16 m period**, and only one period is simulated: a 16 m × 16 m window from the ground to 6 m up, always placed in front of the camera (the camera stands 1.5 m inside the window's back edge).

- A drop leaving the window comes back in from the opposite side. In a periodic field that is the same drop, so the field is continuous from any camera and across any cut: there is never an "edge of the rain column".
- The camera is always in the rain. Drops start 0.6 m in front of the lens; near-large and far-small, sparse near and dense far, all come from perspective, with no separate "near layer".
- The forces on the drops stay periodic too: the forces centred on the performer (stop wave, vortex, hand) use the nearest periodic image and fade to zero within half a period; the ABC turbulence's wavelength is a third of the period.
- Drops fade out 8–14 m from the camera horizontally (earlier with wide lenses), so the wrapping always happens out of sight.
- A splash's impact point is a fixed world point in the field, wrapped into the window before drawing, so splashes do not slide when the camera moves. The ground ripples cover the whole ground.
- **Rain towers:** on a film set, rain towers above the set spray big drops, so that the rain reads on camera. The first 60k drops are this rain:
  - they fall only around the centre of the stage (the rain system actor's position), densest in the middle, in a Gaussian (σ 2.5 m, cut off at 6 m);
  - they are 2–6 mm across, about 3.6 mm on average;
  - they do not wrap, since the stage is in one place: pushed away, they come back to the stage only once the rain falls again;
  - the splashes of the ones landing on the stage are concentrated there too.

| Emitter | Count | Role |
|---|---|---|
| Rain | 230k: 170k rain field (110 per m³) + 60k rain towers (270 per m³ at the centre) | The only rain. Rain field: a truncated Marshall–Palmer distribution, 1–6 mm across (about 2 mm on average); rain towers: 2–6 mm |
| Splash | 15k (12 per burst) | Secondary droplets thrown up by impacts, about 30 % of them on the stage |

## The stage (`Scripts/build_stage.py`)

- **Night backdrop:** Poly Haven's "Rathaus" (Greg Zaal, CC0), a wet square in front of Hamburg's city hall under an overcast night sky. It is an HDR panorama on a 500 m sky sphere (`Materials/HLSL/NightSky.hlsl`).
  - It is also the environment the drops refract: the drops' environment cubemap is captured from the stage, with the backdrop in it.
  - The panorama is looked up by direction, so it shows no parallax. The lookup direction is taken from the ground right below the camera, so the panorama's horizon falls exactly where our ground ends, and its buildings stand on our paving.
  - The lens blurs it too (see "Depth of field"). Exposure 18, yaw 0° (the city hall is behind the hero camera).
- A 1.2 km wet paving that runs out to the sky sphere; the performer is the UE mannequin Manny.
- A thin fog lit warm by the city lights. It reaches only 450 m and does not cover the backdrop, since the panorama has its own atmosphere.
- Rain shows only against the light: a drop is bright only when a light falls in its forward-scattering lobe. So the lights are three big distant ones (40 m away, 10 m up, about 10° above the cameras' line of sight, just above the top of the frame), like lights on cranes on a film set:
  - +8°: right behind the hero camera;
  - −40°: opposite the high three-quarter shot (Cam_B);
  - +70°: opposite the orbiting shot (Cam_D).

  Being far away, they light every drop about equally, from in front of the lens to the backdrop, so near drops are not darker than far ones. They light only the performer and the rain, not the ground (lighting channel 1); otherwise they would reflect as a large glare on the wet ground. They are out of frame, and their bodies are not shown (the backdrop has real street lamps). Drops are lit only inside their cones.
- A near side light lights the ground, and a key light falls on the performer.
  - `Lamp_Sky`: a high point light with a 1 m attenuation radius, so it lights nothing in the scene. It only enters the drops' highlights and scattering, to backlight the rising drops in the upward shots.
- Volumetric fog is off by default, because it costs about 4 ms a frame; tick Volumetric Fog on the fog actor to bring the light shafts back.
- **Cameras** (Super35, 2.39:1): to read as water, drops must be both in focus and large enough on screen, so the cameras stand close with wide apertures, and the city behind melts into bokeh. `CineCam_Hero` is the default view in PIE; `V` cycles through them:

  | Camera | Lens | Framing |
  |---|---|---|
  | `CineCam_Hero` | 40 mm f/2.2, focus 2.05 m | A half-body shot slightly below eye level, as in the film, with the rain hanging all around him |
  | `CineCam_Close` | 50 mm f/2, focus 1.82 m | Over his head towards the square: drops in front of the lens, the city as bokeh |
  | `CineCam_Side` | 50 mm f/2.8, focus 2.18 m | From the side at hand height: the stopped rain against lit shop windows |
  | `CineCam_Low` | 35 mm f/2.8, focus 2.9 m | Low, looking up past him to the street lamps and the glowing night sky |
  | `CineCam_Wide` | 35 mm f/2.8, focus 6.5 m | Full body from 6.5 m (v8's hero camera) |
- The post-process volume turns off Lumen GI and Lumen reflections and uses SSR instead.
- The film's map is `L_RainStage`. The project's default map (editor startup and game) is now Paint Valley's `L_PaintValley`, so open `L_RainStage` from the Content Browser.

## Design notes

**Layer 1: realistic rain**

- Sizes: a truncated exponential distribution (Marshall–Palmer 1948). Terminal velocity: `v = 9.65 − 10.3·e^(−0.6D)` (Atlas 1973). The velocity relaxes towards it with `τ = v_t/g`.
- Gusts: a slowly drifting gust field (with the rain field's period), plus each drop's own sway, so the streaks lean at different angles. Small drops follow the air more. The strength is the controller's `Gust`.
- Motion blur: the sprite's length is `L = v·T + D`, with T a 180° shutter. The material integrates the shading of the "refracting sphere" analytically along the motion, and the coverage is about `D/L` (Garg & Nayar 2006), so one material goes smoothly from streak to bead.
- Optics: a drop is a ball lens of water (IOR 1.333, F0 0.02). It shows the front-surface reflection, the inverted image from two refractions (sampling the stage's HDR environment cubemap, which holds the night backdrop), and the reflected and transmitted highlights of 6 lights. So every bead holds an upside-down street: the glowing night sky along its lower edge, and in the middle a bright core, the image of the light behind it seen through the water.
- **Pixel-footprint filtering:** the area a pixel covers on a bead, and the lens blur, both make a pixel "see" a bundle of directions. So the environment is read from a correspondingly blurred mip, and the highlight lobes widen by `1/n' = 1/n + σ²` with their energy kept. A small bead shows a soft average of its surroundings instead of thin crawling arcs; a defocused bead is the same bead smeared out.
- **Shape:** a drop is an oblate spheroid, flattened along its fall, with the aspect ratios of Beard & Chuang 1987's fit (2 mm 0.93, 4 mm 0.78, 6 mm 0.64). Each is tilted a little; while hovering, big drops slowly wobble and "breathe", so no two beads have their highlight in the same place. Seen from the camera it is an ellipse, shaded as an ellipsoid.
- **Streaks from sideways motion only:** the sprite always faces the camera, and the streak's length and direction use only the part of the velocity across the line of sight. A drop flying straight at the lens or away from it stays a round bead and only grows, instead of turning edge-on into a thin sheet as in v8.
- Screen-size LOD: for drops smaller than 2.5 pixels, Niagara computes the lights' scattering once per particle (forward-scattering lobe `exp((cosθ−1)/ScatterLobe)`, `ScatterLobe = 0.15`: brightest with the light right behind the drop, half as bright 30° off), and the material computes only the coverage. That light is drawn as a bright core (same total energy), not as a uniformly glowing dot. Big drops get the full ball-lens shading and blend in the per-particle light near the threshold, so the brightness does not jump across it.
- **Brightness variation:** each drop's shape and tilt decide how much of the lights' image reaches the lens, so the light part is multiplied by 0.45–1.55 per drop. Spotlights light only the drops in their cones.
- Minimum size: no drop is drawn smaller than 1.5 pixels; its energy is spread over the area. Sub-pixel sprites crawl and flicker under temporal anti-aliasing.
- **Depth of field:** a thin-lens model with the current camera's real lens settings (focal length, aperture, focus distance; `DofScale = 1`). A defocused drop is drawn over its circle of confusion, its brightness spread by area (alpha / blur²), and it keeps the ball-lens shading. So the further out of focus, the fainter: never a bright bokeh disc, and no switch to a brighter shading path when defocused (that switch is why bokeh appeared halfway through in v8). The same lens acts on the opaque stage (MPC `Lens`):
  - the backdrop panorama takes a blurred mip by the angle of aperture over focus distance;
  - out-of-focus detail on the wet ground is smoothed away;
  - ground and backdrop blend at the horizon by the share of the circle of confusion that crosses it, and the grazing specular reflection gives way within the blur radius, so there is no hard line where they meet;
  - the engine's depth of field stays off (`r.DepthOfFieldQuality=0`): it costs 3–6 ms a frame, and on some frames it also blurs the drops rendered "after DOF".
- Streak gain: thin long streaks are physically too dark, so they are brightened by `max(1, G·s/max(s,60))` (`StreakGain = 40`). Streaks a few pixels wide (very close to the lens) already read, so they are not brightened; otherwise they would become bright white bars across the frame.
- Brightness is tuned against the night backdrop: drops show mainly by refracting the backdrop, and the lights only add a bright core (`ScatterGain = 2`, `GlintGain = 8`). `SkyGlow` (an analytic sky light for stages without a bright sky) is 0 by default.
- Frozen drops are drawn 2.5 times larger (in the film the hovering beads look big).

**Layer 2: control**

- Each drop has a freeze weight that blends its "natural falling velocity" with a "controlled velocity field". The controlled field holds the hover breathing, the lift, the chaos and the gesture drag. The player's push and brush add on top of both: wind for falling rain, flight for hovering rain.
- The stop wave spreads out from the hand. The controlled relaxation time is 0.07 s, 6–8 frames at 24 fps, as measured in the film.
- Chaos: a vortex around the performer (within 6.5 m), an ABC divergence-free flow, and each drop's own slowly turning random velocity. It is centred on the performer: full strength within 2 m, down to 15 % beyond 5 m; otherwise every drop in front of the lens would streak across the frame. Only two constraints are kept: the ground and the top of the rain.
- Lift: drops take off staggered by height and by a random amount, accelerate at 60 cm/s² to at most 4 m/s, fade out before 24 m, and are then parked.
- Drops are never destroyed. After landing they fall again from a random point at the top of the rain; drops lifted out of frame are parked first, and fall from the sky again when the rain resumes. Invisible drops (out of view, parked, faded out, too close to the lens) get a zero sprite size, so the GPU does not rasterise them.
- A single step is capped at 0.1 s, so a hitch cannot send a falling drop tens of metres underground in one step.
- The timeline is a deterministic function from time to parameters. MRQ advances in fixed steps of 1/23.976 s, so renders are reproducible.
- Each frame the controller first gets an `FRainTrickState` (frozen or not, stop wave, chaos, lift, how hard it rains, the hand), either from the performance timeline or from the player, and then writes it to the rain and the ground in one place.
- The controller runs in `TG_PostUpdateWork`, after the camera manager updates, so it reads the very view this frame renders; the rain's Niagara system ticks last (`ForceTickLast`) and uses these parameters in the same frame. When they were read earlier, MRQ's sequence could still be a frame ahead (it sets the current frame again before rendering), and on the last frame before every cut the rain field was placed for the next shot.

## Interaction (PIE)

Pressing any control key switches from the scripted performance to player control, carrying on from the performance's current state (if it is hovering, it keeps hovering).

| Key | Action |
|---|---|
| `Space` | Freeze / release: the stop wave spreads from the hand and the rain stops in mid-air; press again and the rain falls again |
| `W` `A` `S` `D` | Push the rain along the camera (forward, left, back, right): wind while it rains; when frozen, the drops fly off in a flock, each at a slightly different speed |
| `E` / `Q` | Push up / down |
| Left mouse drag | Brush: the drops under the cursor (at any depth) follow it. The brush also works during the performance |
| `C` | Chaos on / off (freezes the rain first if it is falling) |
| `R` | Lift: the drops rise back into the sky (freezes the rain first if it is falling); after a release with `Space`, the rain falls from the sky again |
| `1`–`6` | Back to the scripted performance: rain, stop, hover, chaos, lift, rain again |
| `V` | Switch camera (`ShowcaseCameras`: hero, close, side, low, wide); the rain field and lens settings follow the new camera |
| `P` | Pause the performance timeline |
| `H` | Show / hide the status and key hints at the top left |

These are all Blueprint-callable functions too (`SetHeld`, `SetPushInput`, `SetBrush`, `SetChaos`, `StartLift`, `TakeControl`, `JumpToBeat`), so Blueprint or Python can drive them; `GetTrickState` returns the current state. The keyboard and mouse write only when the state changes or a key is held, so they do not override values a script sets.

The push speed `PushSpeed`, the brush radius `BrushRadiusDeg` and the chaos ramp time `ChaosRamp` are under the *Rain|Player* category; the look parameters are under *Rain|Look*, *Rain|Hand* and *Rain|Beats*.

Note: the rain field is infinite, so a push moves the whole field. Drops flying out on one side are replaced by new ones flying in on the other, so the frame never "empties". The rain towers' cluster does not wrap: pushed away it leaves the stage, and it comes back only after `Space` lets the rain fall again.

## Performance

Measured on an M2 Pro with 32 GB of memory:

| Version | Editor viewport | PIE |
|---|---|---|
| v6 (50 m rain field + Greenwich stage, after a minute) | 2.5 fps, slowing down over time | — |
| v7 (6 m rain column, 135k drops) | about 34 fps | 34–36 fps |
| v8 (camera-following rain field, 230k drops) | about 30 fps (the viewport has a 90° field of view and no letterbox, so it sees more rain) | 35–37 fps, steady over the whole timeline |
| v9 (+ night backdrop, rain towers, real depth of field, 2.5 px LOD) | — | rain 31.9, hover 30.6–33.9 (by camera), chaos 30.1 fps |

- The rain itself costs about 4–6 ms a frame: simulation 0.4–0.7 ms, sorting about 0.4 ms, the rest translucent drawing. The particle count is no longer the main bottleneck: from 460k down to 310k drops the frame rate barely changes.
- The engine's depth of field (with bokeh scatter) costs 3–6 ms a frame and is off in `Config/DefaultEngine.ini`; the unused translucency lighting volume is off too.
- Anti-aliasing (2026-10-28): TSR's history buffer went from 2× the output resolution (the Epic setting) to 1×, and the editor-only sky atmosphere notification was turned off; PIE went from 33.2 to 38.3 fps. Rain, hover and chaos look the same. Offline renders (`render_mrq.py`) still use 2×.
  - Other options were tried. TAA reaches 49.6 fps, but moving streaks get soft and smear. FXAA cannot rebuild detail at the same render resolution (63.5 %), so the image is blurry; at native 100 % it is as slow as before and also flickers.
- Niagara's `VisibilityTag` GPU culling is not used: on UE 5.7 (Metal), combined with depth sorting, it drops the end of the sorted list, which is the drops nearest the lens.
- Restart the editor before measuring the frame rate: shader compilation after rebuilding assets, and the profiler window that `ProfileGPU` opens, both lower it.

v6's slowdown came from two bugs that piled up particles (the system's default 5 s loop re-fired the spawn burst, and `ResetSystem()` spawned an extra batch); both were fixed in v7.

## Environment notes (the development Mac)

- This Mac has Xcode 27, while UE 5.7 accepts only up to Xcode 26.9 by default. `MaxVersion` in `Engine/Config/Apple/Apple_SDK.json` was changed to 27.9.0, with the original backed up as `Apple_SDK.json.bak_projectrain`. Xcode 27 also needs the Metal toolchain installed separately: `xcodebuild -downloadComponent MetalToolchain`.
- By default the editor lowers its CPU use in the background, which stops Niagara from simulating. The editor module forces this off at startup.
- The editor viewport ignores `r.ScreenPercentage`; compare resolutions in PIE.
- Disk space is tight. While the editor runs, the system's swap takes another 4–5 GB.

## Known issues and next steps

- The shot sequence `LS_RainTrick` and the finished film still use v8's cameras (mostly wide shots); they have not been redone with v9's framing or re-rendered.
- Real keyboard and mouse input has not been tested by hand. Every control function was verified from scripts in PIE (freeze, push, a brush stroke, chaos, lift, release, camera switch), and the keys only call the same functions. Keys could not be sent to the PIE window from outside (macOS accessibility permission), so a hands-on check in PIE is still to do.
- The panorama has no parallax. That is invisible while the camera moves only a few metres, but a shot that travels far would give it away. To bring v6's Greenwich scene back later, the cheapest route is to shoot a 360° HDR panorama of that scene and use it in place of this one.
- The high overhead shot (Cam_B) and the orbiting shot (Cam_D) are darker than the hero camera: the rain is bright only against the light, and these cameras are far from the backlights' direction. On a set, each camera position gets its own lighting.
- The upward shot of the lift (the later part of Cam_E) has few drops; most have already risen out of frame or are too far from the lens.
- In MRQ renders the performance timeline runs slightly ahead of the shot sequence, because the timeline also advances during the warm-up frames. A partial render starting at 15.75 s measured about 3 frames, so the beats come slightly before the cuts. A full render was not measured separately.
- The rain field is infinite: pushing (WASD) moves the whole field and never empties the frame; only the lift (`R`) sends the rain away.
- Right after a long command in the editor (rebuilding the stage or the assets), the rain may not have settled yet for a screenshot; wait a few seconds, or use `rshot.sh`'s `SETTLE`.
