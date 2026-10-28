# Paint Valley (branch `paint`) — the redesign

The `paint` branch turns the game into **Paint Valley**: a structure of blocks in Monument Valley's style, painted by a rain that you steer. The submitted Rain Shield is the tag `v1.0-rainshield`; its section below is unchanged.

- **Goal.** Cover 90 % of the cream target blocks. Blockers and the ground repel paint. Paint spreads as puddles that join; small drops grow the paint they land on and are used up only there.
- **System 1:** 200,000 GPU drops (`NS_PaintRain`).
- **System 2:** 1,500 elastic CPU paint balls (`NS_PaintBalls`). They bounce off the level, burst on the target, and collide with each other through a neighbour query.
- **Switching.** `Tab` switches between the systems before a round; the idle one is paused.
- **Play:** open `/Game/RainMagic/Maps/L_PaintValley` and press Play. Drag the ball at the bottom right to turn the direction; hold it still to stop the rain and the clock; Enter starts.

Details are in **`Docs/PaintValley.md`**: the rules, both systems, the levels, the cvars, measured performance and a code map. Art notes are in `Docs/ArtDirection.md`.

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

The rest of this README documents the original *Project Rain* (the film's rain-stopping trick, in Chinese).

---

# Project Rain

用 UE 5.7 Niagara 从零复刻《惊天魔盗团2》（*Now You See Me 2*，2016）终场里 J. Daniel Atlas 的控雨魔术：雨在半空停住、悬停、被手势拨动、乱舞、升回天空，最后重新落下。目标是粒子效果本身，舞台只为雨提供灯光和背景。

效果分两层：

1. **雨的真实感**：雨滴尺寸与下落速度、能量守恒的运动模糊条纹、球透镜折射、灯光前向散射、阵风、溅水、湿地面。
2. **控制雨滴**：停雨波、悬停微漂移、手势影响场、混沌乱舞、抬升离场、恢复降雨；在 PIE 里还可以用键盘和鼠标亲手控制。

雨是一个跟着相机走的无限雨场（见下文"雨场"）：相机永远站在雨里，镜头前有大雨滴，远处有细小雨点，近大远小全靠透视自然产生。表演者周围另有一团"造雨塔"的大雨滴。

v9 的重点是让停住的雨读起来像水，而不是满屏发光的白点：
- 水是透明的，只有折射出背后的景物才像水。所以加了一张城市雨夜的 HDR 全景图作背景，雨珠里折射出倒过来的街景。
- 用摄影机的真实镜头参数做景深：离焦的雨滴变淡变软，不会变亮。
- 雨滴是扁圆、会轻微晃动的水珠，中间有一个亮芯。
- 换成了能凸显雨的机位，PIE 里按 `V` 切换。

PIE 截图对比：`Renders/v9_pie_sheet.jpg`（v8 与 v9 同视角、四个新机位、下雨、乱舞）。`Renders/v9_hover_static_hero.mp4` 是主机位固定不动、悬停 4 秒的片段（MRQ），用来看有没有频闪和闪烁：整体亮度逐帧变化在 0.1% 以内，逐像素的变化都是雨滴被手势缓慢拨动的轨迹。成片仍是 v8 的 `Renders/RainTrick_v8.mp4`（46 秒，23.976 fps，2.39:1），镜头序列还没有按 v9 的机位重做。v6–v8 的成片和帧图都保留在 `Renders/` 下；从 v8 起每个版本在 git 里打了标签（`git checkout v8`）。

## 目录

| 路径 | 内容 |
|---|---|
| `Docs/Research/01_NYSM2_Scene_Reference.md` | 电影场景逐拍分析：Framestore 制作资料、逐帧测量、视觉特征清单 |
| `Docs/Research/02_Rain_Physics_And_Rendering.md` | 雨的物理与渲染文献整理，含实现参数表 |
| `Docs/Research/03_Niagara_Programmatic_Authoring.md` | 在 UE 5.7 里用 C++ 生成 Niagara 资产的方法 |
| `Docs/Research/frames/` | 参考片段的关键帧与联系表 |
| `Niagara/HLSL/RainField.hlsl` | 雨场的定义（周期窗口、折回、远端淡出），构建时拼在每个粒子模块前面 |
| `Niagara/HLSL/*.hlsl` | Niagara 自定义 HLSL 模块源码（雨滴生成/更新、溅水） |
| `Materials/HLSL/*.hlsl` | 材质 Custom 节点源码（雨滴着色、湿地面、夜景背景） |
| `SourceArt/` | 下载的源素材（不进 git，导入后的资产在 `Content/` 里），见 `Scripts/import_backdrop.py` |
| `Source/ProjectRainEditor` | 编辑器模块：从上面的 HLSL 生成全部资产（`URainBuilderLibrary`），另含 Python 文件命令队列 |
| `Source/ProjectRain` | 运行时：`ARainMagicController`（表演时间线、玩家控制、手势、灯光、相机与镜头同步） |
| `Scripts/` | 舞台、镜头序列、渲染、调校工具（见下文） |
| `Scripts/legacy/` | v6 的格林尼治舞台（柱廊、人群、巴士）和对应的镜头脚本 |

所有 `.uasset` 都由代码生成。要改效果，改 `.hlsl` 或 C++，然后重新构建即可。

## 构建与渲染

编辑器开着时，用 `python3 Scripts/ue_cmd.py "<python>"` 或 `python3 Scripts/ue_cmd.py -f <file.py>` 在编辑器里执行 Python。这是一个文件命令队列；这台 Mac 上 Python Remote Execution 的组播发现不可用，所以用它代替。

```python
# 第一次：下载背景全景图并导入（见 Scripts/import_backdrop.py 的说明），之后不用再做
#   curl -o SourceArt/rathaus_4k.hdr https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/rathaus_4k.hdr
#   python3 Scripts/ue_cmd.py -f Scripts/import_backdrop.py
import unreal
L = unreal.RainBuilderLibrary
print(L.build_rain_assets(230000, 15000, 60000))  # 雨滴总数、溅水粒子数、其中造雨塔雨滴数 → 材质、MPC、HLSL 模块、NS_RainMagic
print(L.build_stage_materials())                   # M_RainBasic、M_WetGround、M_NightSky
# 然后执行 Scripts/build_stage.py（关卡）和 Scripts/build_sequence.py（镜头序列）
exec(open(unreal.Paths.project_dir() + "Scripts/render_mrq.py").read()); render(0, 46, "RainTrick")  # MRQ 渲染
```

- 雨滴数决定密度：窗口是 16 m × 16 m × 6 m（1536 m³），17 万颗雨场雨滴即每立方米 110 颗；6 万颗造雨塔雨滴在舞台中心每立方米再加 270 颗。
- `build_rain_assets` 会返回编译报告：每个 Niagara 脚本的状态、HLSL/GPU 错误，以及材质编译错误。
- `build_rain_assets` 重新绑定关卡里的 `RainMagic` 时会调用一次 `ResetSystem()`，编辑器里会因此多出一批雨滴。在编辑器里重建资产后，重启编辑器再看密度和帧率。
- `build_stage.py` 会清空关卡并删掉所有相机，所以之后一定要重跑 `build_sequence.py`，否则 `LS_RainTrick` 的绑定会失效。
- `render_mrq.py` 会把控制器设为不循环，并保存关卡。渲染完要把 `loop` 改回 `True`，`render_width_override` 改回 0，才能交互预览。`render(..., cvars={...})` 可以只为这次渲染设置控制台变量。
- `Scripts/restart_editor.sh`：编译 C++ 并重启编辑器（`nobuild` 参数只重启）。
- `Scripts/fps_probe.py <标签> [秒数]`：按墙钟时间测编辑器或 PIE 的帧率，结果追加到 `Saved/fps.txt`。
- `Scripts/rshot.sh` 配合 `Scripts/rainlib.py`：把表演定格在某个时刻，从指定相机截图（`CAM=CineCam_Close` 选机位）；`rainlib.sky(曝光, 偏航)` 调背景并重新采集环境。加 `SETTLE=4` 会先下 4 秒雨再执行（让之前升空停放的雨滴落回来）；等待放在 shell 里，因为编辑器 Python 里的 `sleep` 会让整个编辑器停住。
- 诊断工具：`describe_niagara_system`（RI 参数，包括循环时长）、`dump_generated_hlsl`（导出生成的 HLSL）、`build_diag_system`（二分排错用的单发射器系统）。
- 调试视图：把控制器的 `DropExposure` 设成大于 99，所有可见雨滴都会画成不透明洋红色，用来检查雨滴在哪、有多少。

## 雨场（`Niagara/HLSL/RainField.hlsl`）

雨被定义成一个**在 x、y 方向以 16 m 为周期的无限、均匀的雨场**，只模拟其中一个周期：一个 16 m × 16 m、从地面到 6 m 高的窗口，始终放在相机前方（相机站在窗口后边缘内 1.5 m）。

- 雨滴离开窗口时从对面回来。在周期场里，那正是同一颗雨滴，所以从任何机位看、在任何切镜处，雨场都是连续的，不会出现"雨柱的边"。
- 相机永远在雨里。镜头前 0.6 m 起就有雨滴，近大远小、近少远多全由透视产生，不需要另外的"近景层"。
- 作用在雨滴上的力也保持周期性：以表演者为中心的力（停雨波、旋涡、手）用最近的周期像计算，并在半个周期内衰减到零；ABC 湍流的波长是周期的 1/3。
- 雨滴在离相机水平 8–14 m 处淡出（广角镜头会更早），所以折回永远发生在看不见的地方。
- 溅水的撞击点是雨场里固定的世界点，折回窗口后再画，相机移动时溅水不会跟着滑动。地面涟漪铺满整个地面。
- **造雨塔**：拍电影时，场地上方架着造雨塔，喷出的是大水滴，这样在镜头里才看得清。前 6 万颗雨滴就是这种雨：
  - 只落在舞台中心（雨系统 Actor 的位置）周围，按高斯分布由密到疏（σ 2.5 m，6 m 截断）；
  - 直径 2–6 mm，均值约 3.6 mm。
  - 它们不折回窗口，因为舞台只在一个地方。推走以后，要等雨重新落下才会回到舞台上。
  - 落在舞台上的那部分溅水也跟着集中在这里。

| 发射器 | 数量 | 作用 |
|---|---|---|
| Rain | 23 万：雨场 17 万（110 颗/m³）+ 造雨塔 6 万（中心 270 颗/m³） | 唯一的雨。雨场：直径 1–6 mm 的截断 Marshall–Palmer 分布（均值约 2 mm）；造雨塔：2–6 mm |
| Splash | 1.5 万（每簇 12 颗） | 撞击溅起的二次水珠，约 30% 落在舞台上 |

## 舞台（`Scripts/build_stage.py`）

- **夜景背景**：Poly Haven 的 "Rathaus"（Greg Zaal，CC0），汉堡市政厅前湿漉漉的广场、阴天夜空，是一张 HDR 全景图，贴在 500 m 的天空球上（`Materials/HLSL/NightSky.hlsl`）。
  - 它同时是雨珠折射的环境：雨滴的环境 Cubemap 从舞台里采集，背景就在其中。
  - 全景图按方向查找，所以看不出视差；查找方向从相机正下方的地面算起，于是全景图的地平线正好落在我们地面的尽头，建筑立在我们的铺地上。
  - 它也按镜头虚化（见"景深"）。曝光 18，偏航 0°（主机位背后是市政厅）。
- 一块 1.2 km 的湿铺地，一直铺到天空球；表演者是 UE 小白人 Manny。
- 薄雾被城市灯光照成暖色。雾只到 450 m，不罩住背景，因为全景图本身已经带着空气感。
- 雨靠逆光才看得见：只有灯落在雨滴前向散射光瓣里时，雨滴才亮。所以灯光是三盏远处的大灯（40 m 外、10 m 高，比机位视线高约 10°，正好在画框上沿外），像片场吊车上的灯：
  - +8°：主机位正后方；
  - -40°：高位三分之四镜头（Cam_B）的反方向；
  - +70°：环绕镜头（Cam_D）的反方向。
  
  灯离得远，从镜头前到背景的每颗雨滴受光都差不多，近处雨滴不会比远处暗。它们只照人和雨，不照地面（lighting channel 1），否则会在湿地面上反射出一大片眩光。它们在画外，不显示灯体（背景里有真实的路灯）。雨滴只在它们的光锥里被照亮。
- 一盏近处侧灯照亮地面，一盏主光打在表演者身上。
  - `Lamp_Sky`：高位点光源，照射范围只有 1 m，所以不照亮场景，只参与雨滴的高光和散射计算，给仰拍镜头里升空的雨滴逆光。
- 体积雾默认关闭，因为它每帧要多花约 4 ms；勾选雾 Actor 的 Volumetric Fog 可以找回光束。
- **机位**（Super35 2.39:1）：雨要读成水，雨滴必须既在焦内、又在屏幕上够大，所以机位都离得近、用大光圈，背后的城市虚成光斑。`CineCam_Hero` 是 PIE 默认视角，`V` 键依次切换：

  | 机位 | 镜头 | 构图 |
  |---|---|---|
  | `CineCam_Hero` | 40 mm f/2.2，对焦 2.05 m | 略低于视线的半身镜头，像片中那样，雨悬在他四周 |
  | `CineCam_Close` | 50 mm f/2，对焦 1.82 m | 越过他的头看向广场：镜头前的雨珠，城市化成光斑 |
  | `CineCam_Side` | 50 mm f/2.8，对焦 2.18 m | 从侧面、手的高度：停住的雨衬着亮着的橱窗 |
  | `CineCam_Low` | 35 mm f/2.8，对焦 2.9 m | 低机位仰拍，越过他看向路灯和发亮的夜空 |
  | `CineCam_Wide` | 35 mm f/2.8，对焦 6.5 m | 6.5 m 外的全身（v8 的主机位） |
- 后期体积关闭了 Lumen GI 和 Lumen 反射，改用 SSR。
- 项目默认地图（编辑器启动和游戏）都指向 `L_RainStage`。

## 设计要点

**第一层：雨的真实感**

- 尺寸：截断指数分布（Marshall–Palmer 1948）。终端速度：`v = 9.65 − 10.3·e^(−0.6D)`（Atlas 1973）。速度按 `τ = v_t/g` 向目标弛豫。
- 阵风：一个随时间缓慢漂移的阵风场（与雨场同周期），加上每颗雨滴自己的摆动，让雨丝各有倾角。小雨滴更跟随空气。强度是控制器的 `Gust`。
- 运动模糊：精灵长度 `L = v·T + D`，T 为 180° 快门。材质沿运动方向对"折射球体"的着色做解析积分，覆盖率约等于 `D/L`（Garg & Nayar 2006），同一个材质能从雨丝连续过渡到雨珠。
- 光学：雨滴按水做的球透镜处理（IOR 1.333，F0 0.02）：前表面反射、两次折射得到的倒像（采样舞台的 HDR 环境 Cubemap，里面有夜景背景），加上 6 盏灯的反射和透射高光。所以每颗雨珠里都有一个倒过来的街景：下沿是发亮的夜空，中间一个亮芯是背后灯光透过水珠成的像。
- **像素足迹过滤**：一个像素在雨珠上覆盖的范围，和镜头虚化的范围，都会让这个像素"看到"一束方向。所以环境从相应模糊的 mip 读取，高光瓣按 `1/n' = 1/n + σ²` 加宽并保持能量。小雨珠显示的是周围景物的柔和平均，不会抖成乱爬的细弧；离焦的雨珠就是同一颗雨珠被抹开。
- **形状**：雨滴是扁球，沿下落方向压扁，高宽比取 Beard & Chuang 1987 的拟合（2 mm 0.93、4 mm 0.78、6 mm 0.64）。每颗略有倾斜；悬停时大雨滴缓慢摇晃、"呼吸"，所以没有两颗雨珠的高光落在同一处。从相机看是一个椭圆，按椭球着色。
- **拖尾只算横向运动**：精灵永远正对相机，拖尾长度和方向只取速度里垂直于视线的那部分。正对镜头飞来或飞走的雨滴保持圆珠、只是变大，不会像 v8 那样翻成侧面、变成一片薄片。
- 屏幕尺寸 LOD：小于 2.5 像素的雨滴在 Niagara 里逐粒子算一次灯光散射（前向散射瓣 `exp((cosθ−1)/ScatterLobe)`，`ScatterLobe = 0.15`：灯在雨滴正后方时最亮，偏 30° 降一半），材质只算覆盖率。这份光画成一个亮芯（总能量不变），不是一个均匀发光的圆点。大雨滴走完整的球透镜着色，并在阈值附近混入逐粒子光，跨阈值时亮度不跳变。
- **亮度差异**：每颗雨滴的形状和倾斜决定了有多少灯光的像能落进镜头，所以灯光部分逐颗乘 0.45–1.55。聚光灯只照亮光锥里的雨滴。
- 最小尺寸：任何雨滴都不画得小于 1.5 像素，能量按面积摊开。亚像素精灵在时域抗锯齿下会乱爬、闪烁。
- **景深**：用当前摄影机的真实镜头参数（焦距、光圈、对焦距离；`DofScale = 1`）做薄透镜模型。离焦雨滴画在它的模糊圆上，亮度按面积摊薄（alpha / 模糊²），并保留球透镜着色。因此越虚越淡，不会变成亮光斑，也不会在离焦时切换到更亮的着色路径（v8 中途出现光斑就是这个原因）。同一个镜头还作用于不透明的舞台（MPC `Lens`）：
  - 背景全景图按 光圈/对焦距离 的角度取模糊 mip；
  - 湿地面离焦处的细节被抹平；
  - 地面与背景在地平线处按模糊圆跨过地平线的比例混合，镜面掠射反射在模糊半径内让位，所以交界处不会出现一条硬线。
  - 引擎景深仍然关闭（`r.DepthOfFieldQuality=0`）：它每帧 3–6 ms，还会在某些帧把"景深之后"渲染的雨滴也虚化掉。
- 雨丝增益：细的长雨丝在物理上太暗，按 `max(1, G·s/max(s,60))` 提亮（`StreakGain = 40`）；几个像素宽的雨丝（离镜头很近）本来就看得清，不再提亮，否则会变成贯穿画面的亮白竖条。
- 亮度是对着夜景背景调的：雨滴主要靠折射背景显形，灯光只加一个亮芯（`ScatterGain = 2`、`GlintGain = 8`）。`SkyGlow`（给没有亮天空的舞台用的解析天光）默认是 0。
- 冻结时雨滴视觉尺寸放大 2.5 倍（片中悬停的水珠显得大）。

**第二层：控制**

- 每颗雨滴有一个冻结权重，在"自然下落速度"和"受控速度场"之间插值。受控速度场包括：悬停呼吸、抬升、混沌、手势拖拽。玩家的推力和笔刷叠加在两者之上：对下落的雨是风，对悬停的雨是飞行。
- 停雨波从手部向外扩散。受控弛豫时间 0.07 s，对应 24 fps 下 6–8 帧，与片中实测一致。
- 混沌：绕表演者的旋涡（6.5 m 内）、ABC 无散度流，再加每颗雨滴各自缓慢转向的随机速度。整体以表演者为中心：2 m 内满强度，5 m 外降到 15%，否则镜头前每颗雨滴都会拖出横跨画面的条纹。只保留地面和雨顶两处约束。
- 抬升：雨滴按高度和随机量错开起飞，以 60 cm/s² 加速到最高 4 m/s，升到 24 m 前淡出，然后停放。
- 雨滴不销毁：落地后从雨顶的随机位置重新落下；抬升出画的雨滴先停放，恢复降雨时从天空重新落下。看不见的雨滴（视野外、停放、淡出、离镜头太近）精灵尺寸置零，GPU 不会光栅化它们。
- 单步时长限制在 0.1 s 以内：卡顿一帧不会把下落的雨滴一步送到地下几十米。
- 时间线是"时间 → 参数"的确定性函数。MRQ 以固定 1/23.976 s 步长推进，所以渲染可复现。
- 控制器每帧先得到一份 `FRainTrickState`（是否冻结、停雨波、混沌、抬升、下雨程度、手），来源要么是表演时间线，要么是玩家操作，然后在同一处写给雨和地面。
- 控制器在 `TG_PostUpdateWork` 运行，也就是相机管理器更新之后，读到的视角正是这一帧要渲染的视角；雨的 Niagara 系统排在最后 tick（`ForceTickLast`），当帧就用上这些参数。早一些读取时，MRQ 的序列可能还超前一帧（渲染前它会再定回当前帧），每个切镜前的最后一帧，雨场都会按下一个镜头摆放。

## 交互（PIE）

按任一控制键，就从脚本表演切换到玩家控制，并从表演当前的状态接着来（比如正在悬停，就保持悬停）。

| 按键 | 作用 |
|---|---|
| `Space` | 冻结 / 释放：停雨波从手部扩散，雨停在半空；再按一次，雨重新落下 |
| `W` `A` `S` `D` | 沿相机方向推动雨（前、左、后、右）：下雨时是风，冻结时雨滴成群飞走，每颗速度略有不同 |
| `E` / `Q` | 向上 / 向下推 |
| 鼠标左键拖拽 | 笔刷：光标下的雨滴（任何深度）跟着光标走。笔刷在表演模式下也能用 |
| `C` | 乱舞开 / 关（雨没冻结时会先冻结） |
| `R` | 升空：雨滴升回天空（雨没冻结时会先冻结）；按 `Space` 释放后，雨从天上重新落下 |
| `1`–`6` | 回到脚本表演：下雨、停雨、悬停、乱舞、升空、恢复降雨 |
| `V` | 切换机位（`ShowcaseCameras`：主机位、特写、侧拍、低机位、全身），雨场和镜头参数跟着新机位走 |
| `P` | 暂停表演时间线 |
| `H` | 显示 / 隐藏左上角的状态与按键提示 |

这些操作也都是蓝图可调用的函数（`SetHeld`、`SetPushInput`、`SetBrush`、`SetChaos`、`StartLift`、`TakeControl`、`JumpToBeat`），可以从蓝图或 Python 驱动；`GetTrickState` 返回当前状态。键盘和鼠标只在状态变化或按住时才写入，不会覆盖脚本设的值。

推力速度 `PushSpeed`、笔刷半径 `BrushRadiusDeg`、乱舞渐变时间 `ChaosRamp` 在 *Rain|Player* 分类下；外观参数在 *Rain|Look*、*Rain|Hand*、*Rain|Beats* 分类下。

注意：雨场是无限的，推动时整片雨都在流动，雨滴从一边飞出、另一边会一直有新的飞进来，不会"推空"画面。造雨塔的雨团不会折回，推走就离开舞台，按 `Space` 让雨重新落下后才回来。

## 性能

在 M2 Pro、32 GB 内存上测得：

| 版本 | 编辑器视口 | PIE |
|---|---|---|
| v6（50 m 雨场 + 格林尼治舞台，开一分钟后） | 2.5 fps，越跑越慢 | — |
| v7（6 m 雨柱，13.5 万颗） | 约 34 fps | 34–36 fps |
| v8（跟随相机的雨场，23 万颗） | 约 30 fps（视口是 90° 视场、无遮幅，看到的雨更多） | 35–37 fps，整条时间线稳定 |
| v9（+ 夜景背景、造雨塔、真实景深、2.5 px LOD） | — | 下雨 31.9、悬停 30.6–33.9（按机位）、乱舞 30.1 fps |

- 雨本身每帧约 4–6 ms：模拟 0.4–0.7 ms，排序约 0.4 ms，其余是半透明绘制。粒子数已经不是主要瓶颈：从 46 万降到 31 万，帧率几乎不变。
- 引擎景深（含 bokeh scatter）每帧 3–6 ms，在 `Config/DefaultEngine.ini` 里关闭；用不到的半透明光照体积也关了。
- 没有用 Niagara 的 `VisibilityTag` 做 GPU 剔除：在 UE 5.7（Metal）上，它和按深度排序一起用时，会把排序列表末尾、也就是离镜头最近的雨滴丢掉。
- 测帧率前先重启编辑器：重建资产后的着色器编译、`ProfileGPU` 打开的分析窗口，都会让帧率偏低。

v6 卡顿的根因是两个粒子数累积的 bug（系统默认 5 秒循环会重复生成爆发；`ResetSystem()` 会多生成一批），已在 v7 修复。

## 环境说明

- 本机是 Xcode 27，UE 5.7 默认只接受到 Xcode 26.9。已把 `Engine/Config/Apple/Apple_SDK.json` 的 `MaxVersion` 改为 27.9.0，原文件备份为 `Apple_SDK.json.bak_projectrain`。Xcode 27 还需要单独安装 Metal 工具链：`xcodebuild -downloadComponent MetalToolchain`。
- 编辑器在后台时默认会降低 CPU 占用，这会让 Niagara 停止模拟。编辑器模块启动时已强制关闭这一项。
- 编辑器视口不受 `r.ScreenPercentage` 控制；要比较分辨率影响，请在 PIE 里测。
- 磁盘空间很紧。编辑器运行时，系统交换文件会多占 4–5 GB。

## 已知问题与下一步

- 镜头序列 `LS_RainTrick` 和成片还是 v8 的机位（大多是远景），没有按 v9 的构图重做，也没有重新渲染。
- 真实键盘和鼠标输入没有亲自验证。控制函数在 PIE 里都用脚本调用验证过（冻结、推动、笔刷划过、乱舞、升空、释放、切换机位），按键只是调用同一组函数；但我没法从外部给 PIE 窗口发按键（macOS 辅助功能授权），需要你在 PIE 里试一下。
- 全景图没有视差：机位只在几米内移动时看不出来，大范围移动的镜头会露馅。如果之后要把 v6 的格林尼治场景搬回来，最省的办法是从那个场景拍一张 360° HDR 全景替换这张。
- 高位俯拍（Cam_B）和环绕镜头（Cam_D）比主机位暗：雨只在逆光时亮，这两个机位离背光灯方向较远。片场的做法是每个机位单独打灯。
- 升空的仰拍镜头（Cam_E 后段）里雨滴很少，大部分已经升出画面或离镜头太远。
- MRQ 渲染时，表演时间线比镜头序列略有超前：渲染预热帧期间时间线也在推进。在一次从 15.75 秒开始的局部渲染里，实测约 3 帧，所以节拍会比切镜稍早一点。整段渲染没有单独测过。
- 雨场是无限的：推动（WASD）时整片雨都在流动，画面不会被"推空"；按 `R` 升空才能把雨送走。
- 编辑器里刚执行完耗时较长的命令（重建舞台、重建资产）后马上截图，雨可能还没稳定；等几秒再拍，或用 `rshot.sh` 的 `SETTLE`。
