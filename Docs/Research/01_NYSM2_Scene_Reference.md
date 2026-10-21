# 01 · 《惊天魔盗团2》(Now You See Me 2, 2016) 控雨魔术：场景参考研究

> 用途：为 UE 5.7 Niagara 复刻"雨停在半空 → 被操控 → 升回天空 → 失控乱舞 → 重新落下"这一效果提供视觉与技术依据。
> 研究日期：2026-10-13。
> 置信度标记：**[已证实]** 有一手或权威来源；**[帧分析]** 由本次对公开片段逐帧、光流和亮度统计得出（可复核）；**[推断]** 根据证据推出的结论；**[未证实]** 只有二手或粉丝来源，或无法打开原文。

---

## 0. 摘要（先读这一节）

- **VFX 厂商**：雨停段落由 **Framestore（伦敦）** 制作。**[已证实]** VFX Supervisor 为 Anthony Smith 与 Stephane Nazé，Head of FX 为 Andy Hayes，CG Supervisor 为 Stefan Putz，Compositing Supervisor 为 Alex Payman。全片 Production VFX Supervisor 是 Matt Johnson。
- **技术要点**（Framestore 官方案例页原话）：
  - 模拟、灯光、渲染都在 CG 中完成；
  - "millions of beautifully refracting droplets"，即数百万颗折射雨滴，用 **deep data** 渲染；
  - 合成阶段在逐镜头基础上调整人群 roto 分层的深度，以及人群前方的雨量；
  - 景深与 bokeh 在合成中生成；
  - **刻意偏离真实相机参数**，减弱前景雨滴的散焦，避免它们抢戏；
  - 重点开发了两件事：雨"停下来的过程"的样子，以及 Atlas 的动作对周围雨滴的影响程度；
  - 效果要建立在影片中解释过的"物理上说得通"的原理上，即**频闪灯 + 造雨机**。
- **实拍**：外景在格林尼治的**旧皇家海军学院**（Old Royal Naval College），夜戏，有真实人群。**[已证实]** 下落的雨来自 rain machine（Empire 片场探班报道为二手转述；片中台词"strobe lights and rain machines"与喷头揭秘镜头可以印证）。 片中还专门给了造雨喷头和楼顶喷淋装置的特写，作为"揭秘"镜头。**[帧分析]**
- **最关键的几个视觉事实**（全部来自 **[帧分析]**）：
  1. 雨从"长条下落条纹"变成"悬停点"，用时约 **6–8 帧（0.25–0.33 s）**。条纹长度逐帧缩短，不是瞬间切换。
  2. 雨停的同一刻，**全画面开始频闪**：按 23.976 fps 计，2 帧亮、2 帧暗，约 **6 Hz**，亮度调制约 20–30%。雨滴悬停和被操控期间一直闪；真正的雨重新落下后，频闪停止。
  3. 悬停雨滴在 1080p 下多为 **2–5 px 的亮点**，对焦清晰时呈"亮边暗心"的小圆环（折射球体）。近景散焦雨滴是 20–60 px 的半透明软圆盘。每帧肉眼可见约 **2000–4500 颗**。
  4. 悬停期间并非绝对静止，有很轻微的漂移（"呼吸感"）。"冻结感"主要靠**摄影机运动带来的视差**（后拉、升降、摇）来卖。

---

## 1. 场景逐拍分解

### 1.1 剧情位置与设定

- **剧情位置**：位于第三幕。四骑士在伦敦"全城巡演"（Jack 的扑克牌秀、Lula 的鸽子秀、Merritt 在河边的秀），Atlas 的"控雨"是其中一场，与其他几场交叉剪辑。**[已证实]**（字幕 / 剧本台词）
- **地点**：格林尼治旧皇家海军学院。**[已证实]**（Empire 报道"in front of the Royal Naval College"；Framestore："in front of a live-action crowd of cheering fans in Greenwich"）
  - 画面里能看到：两侧对称的 Wren 式柱廊；暖色（琥珀/钠灯色）向上打亮的立柱；带穹顶的建筑；远处中央有一栋被打亮的白色建筑；中间是草坪，其间有一条湿漉漉的石板路，这条路就是"舞台"。**[帧分析]**
  - 由此推测机位位于 King William Court 与 Queen Mary Court 之间的中轴草坪，向南望向 Queen's House。**[推断]**
- **时间**：新年夜（New Year's Eve）的夜晚。**[已证实]**（Wikipedia 情节："On New Year's Eve, they perform across the city"）
- **天气**：持续大雨（实为人工造雨）。
- **人群**：
  - Empire 报道称现场约 200 人。**[未证实]**（二手转述，原文未打开）
  - 画面中人群密集，大多撑伞：米字旗伞，红、白、蓝三色伞，绿/青色伞，黑伞等；也有人举着手机电筒。**[帧分析]**
- **Atlas 造型**：深色（黑/藏青）长款外套，在雨中呈湿亮反光；内搭深灰蓝针织衫；佩戴肤色头戴式耳麦（earset mic）。**[帧分析]**
- **场景光源**（全部为 **[帧分析]**）：
  1. 两侧柱廊顶部各有一排高亮白色泛光灯（每侧约 8–10 盏），形成从远到近的"灯串"，带星芒和光晕。它们是逆光的主要来源，戏里可视为"频闪灯"。
  2. 柱廊立柱由暖色洗墙灯向上打亮，色温偏琥珀。
  3. 石板路两侧地面嵌有蓝/青色 LED 灯带，界定舞台边缘，在积水中有强烈倒影。
  4. 后段出现蓝色探照光束扫过人群，结尾有一个俯拍的圆形追光光斑。
  5. 人群手机电筒，是小冷白点光源。
  6. 场景内停着一辆红色伦敦双层巴士，车内是冷白色荧光灯。
- **画幅与帧率**：2.39:1 宽银幕（评测分别写作 2.35 或 2.40）。片段帧率为 23.976 fps。摄影指导是 Peter Deming, ASC。**[已证实]**

### 1.2 台词骨架（决定节奏的"拍点"）

出自英文字幕稿（springfieldspringfield 剧本页）。为保持原样，以下只摘关键句：

- 开场白："I've been told I have some control issues… so I'm gonna try to control something that's a lot easier than people. I'm gonna try to control the weather."
- 引入"停"："I'm not just gonna make it clear up. No, no, no. I'm gonna make it actually stop."
- 引入"上升"："Or what about make it go up? Are you still with me?"
  - 此后与 Jack 的扑克牌秀交叉剪辑。
- 引入"乱舞"："Or, what about just go, I don't know, just go insane?"
- 揭秘（雨重新落下后）："Was that an act of God? No… that was an act of me. And the strobe lights and rain machines."
- 收尾悬念："Did I use a Barry Trap? Maybe some switches or a few duplicates? … You're gonna have to wait to find out."
  - 随后他倒进水洼，消失。

### 1.3 逐拍表（含时码）

参考片段：
- **A** = 官方 Movieclips《Controlling the Rain Scene (9/11)》，1080p，23.976 fps，2:49，与 Jack、Lula、Merritt 的段落交叉剪辑。
- **B** = 粉丝剪辑《Reverse Rain Trick》，720p，29.97 fps，1:30。去掉了大部分插叙，**并且包含 A 没有的结尾**：重新下雨、造雨机揭秘、倒入水洼消失。

时码是该片段内的秒数，取近似值。

| # | 拍点 | A 时码 | B 时码 | 时长 | 摄影机 / 构图 | 雨的状态 | 光照 |
|---|---|---|---|---|---|---|---|
| 1 | 开场白，大雨中讲话 | 0:00–0:27.6 | 0:01–0:28.5 | 约 27 s（含人群反应镜头） | 中景/近景正面 Atlas；高角度俯拍全景（草坪与石板路）；伞下人群近景 | **正常下落**：细长、近垂直的亮色运动模糊条纹，逆光下呈白/灰色 | 暖色柱廊 + 顶部泛光灯逆光；整体偏暗，**无频闪** |
| 2 | "抓"的手势 → 雨停 | 0:27.1–0:27.9 | 约 0:28.5–0:29 | **约 6–8 帧（0.25–0.33 s）** | 同一镜头连续拍摄，无剪辑：Atlas 右手前伸成爪形，左手护在胸前；镜头随即开始后拉 | 条纹逐帧缩短：长条纹 → 短划 → 点。前景大雨滴在减速中显示为拉长的模糊团块 | **亮度在停止瞬间跳升，频闪开始** |
| 3 | 悬停：镜头后拉，揭示全场 | 0:27.9–0:34.5 | 0:29–0:34.5 | 约 6.5 s | 从 Atlas 中近景后拉/升高到全景，人群放下或收起雨伞 | 满空间悬停的亮点，近景有散焦大圆斑；Atlas 脸上和外套上挂着水珠 | 2 亮 2 暗、约 6 Hz 的全局频闪 |
| 4 | 双层巴士车窗 | 0:34.6–0:38.3 | 0:36–0:38 | 约 3.7 s | 车窗外近景，车内一对情侣接吻 | **特写级悬停雨滴**：透明球体，带亮边高光和小镜面高光点，折射出车内倒像；窗外还有竖向"一串"水滴（像被冻结的滴流）；玻璃上有雨痕 | 车内冷白荧光灯，车身红色 |
| 5 | 高角度全景：泛光灯 + 柱廊 | 0:38.4–0:39.3 | 0:39 | 约 1 s | 中高机位，纵深透视 | 悬停雨在泛光灯附近最密、最亮，像"雪/闪粉云" | 泛光灯排成灯串，带星芒 |
| 6 | Atlas 手势"展示"；人群反应 | 0:39.4–0:47.6 | 0:39.5–0:47.6 | 约 8 s | Atlas 中景/全景，背后是灯串；穿插人群仰望的近景 | 悬停，伴有轻微漂移 | 频闪；到 0:46–0:48 前后频闪节奏变快（出现单帧交替，约 12 Hz） |
| 7 | 举手 → 仰拍天空 → **雨上升** | 0:48.0–0:52.2 | 0:48–0:52.3 | 约 4 s | 低角度仰拍 Atlas 举手，镜头上摇到"天空 POV"（黑底上布满亮点）；再切到全景 | 雨滴向上运动（**[帧分析]**：光流显示雨滴在画面中向上移动，而摄影机在上摇；置信度中等）。全景里雨滴云在灯串下"冒泡"般升起 | 天空 POV 镜头里整体亮度骤降（灯光渐暗）；全景回到频闪 |
| 8 | （A 中插入 Jack 扑克牌秀、Lula、伦敦航拍；B 中基本删除） | 0:52.3–1:46 | — | — | — | — | — |
| 9 | "go insane"：乱舞 | 1:46.3–1:57.6 | 0:53–1:04 | 约 10 s | Atlas 张开双臂仰望；高角度全景（地面蓝色灯带）；Atlas 在暖色建筑前的人群中"指挥"；握拳等大动作近景；一个人物背影（**[未证实]** 应为 Atlas）面对举着手机电筒的人群，背景是红色巴士，蓝色逆光 | 雨滴变成**多方向的短条纹**（运动模糊长度约 10–40 px），呈旋涡或混乱状，跟随手势；部分镜头整体上升 | 频闪持续；巴士处为蓝色冷光 |
| 10 | 雨重新落下 | — | 1:04–1:07 | 约 2–3 s | 暗场人群（失焦）→ 全景 | 先是像雪一样密集的点开始下落，随后转为**倾盆大雨**（长条纹，纵横比 p90 约 8.5） | 过渡时仍短暂频闪，**随后频闪停止**，整体转为偏蓝的冷色主光 |
| 11 | "act of God?… strobe lights and rain machines" | — | 1:07–1:11 | 约 4 s | Atlas 在暴雨中的中景，蓝紫色调 | 大雨 | 冷蓝主光 + 背后灯串 |
| 12 | 揭秘镜头：喷头与楼顶装置 | — | 1:11–1:14 | 约 3 s | 脚踩积水与地面 LED 灯带的特写；漆黑中竖杆顶端的喷头向外扇形喷水；仰拍穹顶建筑屋檐上的多个喷淋头 | 喷射的扇形水幕，被强光逆光照亮 | 黑底强逆光 |
| 13 | 戴上兜帽，张开双臂 | — | 1:14–1:20 | 约 6 s | Atlas 双手拉起兜帽，十字形张臂 | 大雨 | 暖色柱廊 + 灯串 + 蓝色补光 |
| 14 | **向后倒进水洼并消失** | — | 1:24–1:27 | 约 2–3 s | 正上方俯拍：Atlas 平躺张臂，位于圆形追光光斑中，后倒入水；溅起大片白色水冠后人消失 | 溅水 + 大雨 | 圆形硬光斑，周围较暗；画面角落有洋红、青色伞 |
| 15 | 余韵全景 | — | 1:27–1:30 | 约 3 s | 全景人群 | 大雨持续 | 灯串、蓝色地面灯带 |

> 说明：
> - 雨从"停"到"乱舞结束"，在正片中约 1.5–2 分钟的跨度内（含交叉剪辑）。只算纯雨效镜头，约 40–50 s。
> - Framestore 对该段的概括是："stopping rain in mid-air, moving the droplets around with some deft hand movements before falling back into a puddle and disappearing into the water."

### 1.4 关键过渡的帧级观察 [帧分析]

1. **停止过渡（A 0:27.1–0:27.9，逐帧查看）**
   - 前 12 帧：标准下落条纹。
   - 接下来约 6–8 帧：条纹明显缩短，近景出现大块拉长的散焦水团。
   - 最后：变为点状。
   - 整个过程同一镜头、无剪辑，Atlas 的"爪"手势与停止同步。
   - 停止同时画面变亮：按 A 片段逐帧 YAVG，全局平均亮度从停止前约 31–33（8 bit）升到停止后暗帧约 43、亮帧约 54，之后在约 47/60 之间频闪。
2. **频闪**（A 片段每帧平均亮度 YAVG 序列，摘录）：
   - 停雨之前：`33 33 33 … 32 32 31`，平稳。
   - 停雨之后：`43 54 54 43 43 55 55 43 …`，严格的 2 帧亮、2 帧暗。
   - 在 23.976 fps 下周期为 4 帧，约 6 Hz；亮暗比约 1.25–1.3。
   - 频闪是**全局**的：人群、建筑、地面都跟着闪，不只是雨。
   - B 片段 1:04–1:06 雨重新落下的过渡中，仍可见 `52 62 62 52` 的交替；进入正常大雨后亮度恒定。
3. **悬停中的微动**：
   - 用 640 px 宽度做光流，雨滴相对背景的速度约 0.1–0.4 px/帧，换算到 1080p 约 0.3–1.2 px/帧。
   - 即悬停中有很慢的漂移或"呼吸"，不是死冻结。
4. **"上升"段**（A 0:50.8–0:52.0）：
   - 摄影机在上摇。按常理，静止物体会在画面中向下移，但雨滴特征点的中位位移为向上（dy 约 −1 到 −16 px/帧，有噪声）。
   - 据此判断雨滴在真实地向上移动。**置信度：中**
5. **乱舞段**：雨滴变为短运动模糊条纹，方向杂乱（纵横比中位约 0.85，即横向条纹很多），亮度比悬停段更高。

### 1.5 雨滴外观（近景与远景）

| 状态 | 形态 | 尺寸（1080p 画面，**[帧分析]**） | 运动模糊 | 光学 / 颜色 |
|---|---|---|---|---|
| 正常下落 | 细长、近垂直的条纹，长度远大于宽度（中近景约 50–150 px 长、1–3 px 宽，目测） | — | 强（快门 180° 下的典型雨条纹） | 逆光下为白/灰色；在暗背景上清晰，在亮建筑前较淡 |
| 悬停 | 圆点；对焦清晰时为**亮边、暗心的小圆环**（像气泡），少数略呈竖向椭圆 | 等效直径中位约 3–4 px；p90 面积约 55–90 px²；p99 约 400–600 px²（即 20–28 px 的近景散焦盘）；每帧可检出约 2000–4500 个 | 无，或极弱 | 中性白，带一点暖色（暖色区 R>B 约 10/255）；雨滴亮度约为背景的 2.5–3 倍；泛光灯附近（前向散射）最亮最密 |
| 悬停特写（巴士段） | 清晰透明的小球，有一道明亮的边缘高光环和一个小镜面高光点；内部折射出车内的倒像；有一串由大到小的竖向水珠 | 屏幕上约 10–40 px | 无 | 高透，几乎不带颜色；"像透镜一样" |
| 近景散焦 | 大而软的半透明圆斑，可见背景被折射或扭曲，**不是不透明的白色 bokeh** | 20–80 px | 无 | 低对比、灰白；Framestore 刻意压低了它们的散焦强度 |
| 乱舞 | 短条纹，多方向，带旋涡感 | 中位约 6–7 px，最长约 40 px | 中等，方向随手势变化 | 更亮，偏中性 |
| 重新落下 | 粗而密的长条纹，"倾盆" | 纵横比 p90 约 8.5 | 强 | 偏冷蓝（蓝色补光 + 地面 LED） |
| 落在表面 | Atlas 脸部和外套上的水珠；石板路、积水呈镜面湿反射；悬停期间地面和伞面**没有溅水**（Movie Mistakes 也指出这一点） | — | — | — |

### 1.6 视频片段 URL

| 标题 | URL | 备注 |
|---|---|---|
| Now You See Me 2 (2016) - Controlling the Rain Scene (9/11) \| Movieclips | https://www.youtube.com/watch?v=DQU7X4QDX80 | 官方授权，1080p，2:49，2018-09-20 上传，含交叉剪辑。**首选** |
| Now You See Me 2 - Reverse Rain Trick（CHESTERS MOVIE CLIPS） | https://www.youtube.com/watch?v=SudyUB9Rzmg | 粉丝剪辑，720p，1:30。**唯一含结尾**（重新下雨、造雨机揭秘、倒入水洼） |
| Final Magic Show Scene \| NOW YOU SEE ME 2 (2016) Movie CLIP HD（JoBlo） | https://www.youtube.com/watch?v=NtyjfZafOcY | 9:50，整段终场秀（本次只下载，未逐帧分析） |
| Now You See Me 2: Official Clip - Controlling the Rain（Rotten Tomatoes） | https://www.rottentomatoes.com/m/now_you_see_me_2/videos/2QBMY4N57NCE | 2:39 官方片段 |
| Now You See Me 2 Rain scene - Coub | https://coub.com/view/13oo69 | 循环短片 |
| TikTok 话题 | https://www.tiktok.com/discover/now-you-see-me-2-rain-scene | Movieclips 等发布的短视频合集 |

---

## 2. VFX 制作方、幕后资料与实现方式

### 2.1 厂商与人员

| 角色 | 姓名 / 公司 | 来源 |
|---|---|---|
| 雨停段落 VFX 厂商 | **Framestore（伦敦）**；全片 200+ 镜头，伦敦与蒙特利尔两地团队 | Framestore 案例页 **[已证实]** |
| VFX Supervisor (Framestore) | Anthony Smith、Stephane Nazé | 同上 |
| Head of FX / FX Supervisor | Andy Hayes | 同上 |
| CG Supervisor | Stefan Putz（雨段）；Jason Quintana（蒙特利尔，泰晤士河 / 塔桥环境） | 同上 |
| Compositing Supervisor | Alex Payman（雨段 2D 负责人） | 同上 |
| Production VFX Supervisor | Matt Johnson | Art of VFX、IMDb **[已证实]** |
| 其他 VFX 厂商 | BlueBolt（Angela Barson、Stuart Bullen）、Cinesite（Nicolas Chevallier）、Blind Ltd（CG Sup Andrew Booth） | Art of VFX **[已证实]**；它们是否参与雨段**未知** |
| 特效（SFX，实拍造雨）主管 | Stephen Hutchinson | 仅见于 dearcastandcrew 页面标题"Dear Stephen Hutchinson, Special Effects Supervisor for NOW YOU SEE ME 2"，正文无法打开 **[未证实]** |
| 摄影指导 | Peter Deming, ASC | Wikipedia、American Cinematographer 2016 年 7 月刊 **[已证实]** |

### 2.2 Framestore 官方技术描述（原文摘录）

来源：https://www.framestore.com/work/now-you-see-me-2

> "One of the most spellbinding sequences in the film shows magician Daniel Atlas (Jesse Eisenberg) stopping rain in mid-air, moving the droplets around with some deft hand movements before falling back into a puddle and disappearing into the water. This is all set in front of a live-action crowd of cheering fans in Greenwich."

> "We experimented with various levels of defocus to get the right look, intentionally deviating from the real world camera settings when required to make sure that the foreground droplets weren't distracting for the viewer. The effect also required lots of development on how the rain drops should behave while they're hanging in the air, especially given that the magical effect had to based on a physically plausible idea which is explained in the film." — Anthony Smith

> "Head of FX, Andy Hayes, and CG Supervisor Stefan Putz got to work simulating, lighting and rendering the whole sequence with just the right look and feel. Once the simulations were approved, the millions of beautifully refracting droplets were rendered with deep data, which allowed the 2D team, led by Compositing Supervisor Alex Payman, to creatively adjust the depth of the rotoscoped crowd layers and therefore the amount of rain in front of them in each shot. It also allowed them to generate an impressive looking depth of field and bokeh effect."

> "The team spent a lot of time on how the rain should look when it comes to a stop, and just how much influence Atlas' movements should have on the rain surrounding him. In test screenings the end result proved to be one of the most-liked sequences in the film."

案例页头图就是这一段的成片剧照（见第 4 节）。

### 2.3 实拍部分

- **Empire 2016 年 3 月刊片场探班**："OH OH OH IT'S MAGIC"，https://www.pressreader.com/uk/empire-uk/20160331/281505045336663 。PressReader 返回 403，以下内容来自多次搜索摘要的转述 **[未证实：非原文直读]**：
  - 现场"absolutely bucketing down"，雨来自 rain machine；
  - Eisenberg 以 Atlas 身份在 Royal Naval College 前对约 200 人表演；
  - "the downpour reverses and the rain starts fizzing back up into the night sky"；
  - Eisenberg 说雨"up, sideways, even levitating"，以及："Believe it or not, this is a real trick""As in the first film, we've taken an illusion that can be done in the real world on a smaller scale and then pushed it."；
  - 这场戏是在格林尼治一带拍摄的若干大魔术中的第一场；
  - 次日在 Cutty Sark 拍 Lula 的鸽子秀（Keith Barry 提及）。
- **拍摄日期**：Alamy 有一张图片社照片，题为"Jesse Eisenberg filming a magic scene for 'Now You See Me 2' in Greenwich… When: 09 Feb 2015"，https://www.alamy.com/stock-photo-jesse-eisenberg-filming-a-magic-scene-for-now-you-see-me-2-in-greenwich-83934627.html 。很可能就是这场戏，**[推断]** 但未直接证实。全片拍摄期为 2014 年 11 月至 2015 年 5 月。
- **片中的揭秘镜头**：竖杆顶端喷头扇形喷水；穹顶建筑屋檐上的多个喷淋头。这些很可能就是真实造雨设备，或是按真实设备搭的镜头。**[帧分析 / 推断]**
- **导演态度**：Jon M. Chu 在 TechCrunch 采访中说："we take big efforts to make as many things practical as possible… We squeeze a little bit here and there to get that CGI stuff in there"。https://techcrunch.com/2016/06/09/now-you-see-me-2-jon-chu-interview/

### 2.4 实现方式：综合判断 [推断，依据见括号]

1. **下落的雨**：实拍 rain machine / rain tower（依据：Empire；片中喷头揭秘镜头；B 片段 1:05 之后的大雨呈典型造雨机的粗条纹）。
2. **停止、悬停、上升、乱舞**：全 CG 流体或粒子模拟，"millions" 级雨滴，带真实折射材质，用 deep 渲染（依据：Framestore 原文）。实拍素材是在真雨中拍的，所以合成时**必须先清除或替换实拍雨**，至少要处理人物前方的雨层。这一点 Framestore 没有明说，属推断。
3. **人群与雨的深度穿插**：对人群做 roto 分层，再用 deep 合成控制"每层人前有多少雨"（Framestore 原文）。
4. **景深与 bokeh**：在合成中由 deep 数据生成，**刻意弱于真实镜头**（Framestore 原文）。
5. **频闪**：画面全局 2 亮 2 暗的亮度调制。可能是实拍时就用了真实频闪或闪烁灯，也可能是合成后加的。**无公开来源，未知**。它在叙事上对应"strobe lights"的解释。
6. **软件**：坊间常说 Framestore 当时的 FX 管线以 Houdini 为主、渲染多用 Arnold，**[未证实]**，本次没有找到来源支持。**本片雨段的具体软件、粒子数、模拟方式均无公开资料**，不应引用为事实。
7. **运动模糊**：悬停段几乎无模糊，乱舞段有方向性短模糊，下落段为实拍真模糊（帧分析）。

### 2.5 查过但未找到 / 无法访问的资料（后续可补）

- **fxguide / befores & afters / Cinefex / VFX Voice / CG Society**：多轮搜索均未找到针对本片雨段的文章。
- **American Cinematographer 2016 年 7 月刊**的本片报道（nxtbook 第 58 页起）：https://www.nxtbook.com/nxtbooks/ac/ac0716_v2/index.php?startid=58 。返回 403，**很可能有现场造雨和灯光（含频闪）细节，值得人工查阅**。
- **蓝光花絮**：
  - "You Can't Look Away"（17:14）：讲数字合成与摄影技术，受访者含 VFX Supervisor Matt Johnson。**最可能有雨段幕后**。
  - "Bringing Magic to Life"（16:09）
  - "The Art of the Ensemble"（21:11）
  - Jon M. Chu 导演评论音轨
  - （DVD Talk 评测 https://www.dvdtalk.com/reviews/review/71238 ；IMDb tt6043860）
- **Framestore 2016 年 showreel**、Vimeo 上的个人 FX reel：未找到与本片雨段对应的公开拆解视频。

### 2.6 中文资料（多为原理讨论，非幕后）

- 知乎：《惊天魔盗团2》里的让雨停住的魔术原理是什么？ https://www.zhihu.com/question/47790029
- 豆瓣：小平头如何控制雨滴的？ https://m.douban.com/movie/subject/25662337/questions/720706
- 雷锋网：光学魔法：《惊天魔盗团2》中，魔术师如何控制雨滴悬停？ https://m.leiphone.com/category/zhuanlan/u75fe4cbhAc6ZI2N.html
  - 用 24 fps 车轮效应解释，并承认"电影中的效果还是经过了艺术夸张"。
- CSDN 博客：https://blog.csdn.net/GarfieldEr007/article/details/51892460
- 时光网 2016-06-29《"雨中魔术"片段曝光 艾森伯格控制雨滴方向 水中凭空消失》：http://news.mtime.com/2016/06/29/1556811.html 。现已 404，只剩标题。标题印证了"控制方向 + 水中消失"两个拍点。
- 腾讯新闻（2025）：https://news.qq.com/rain/a/20251120A038JD00 ；扬子晚报 / 紫牛（2025）：https://www.yzwb.net/news/wy/202511/t20251105_285729.html 。两篇只有观感描述。
- 粉丝观点："Atlas 早先在魔术店看到过水滴悬浮道具，是伏笔"。**[未证实]**，本次未核对正片。

---

## 3. 其他"冻结雨 / 悬停水滴"视觉参考

| 参考 | 价值 | URL |
|---|---|---|
| **频闪水滴 / Time Fountain**（Harold Edgerton 的 "Piddler" 一脉） | 本片戏中原理的真实版本。滴落频率等于频闪频率时，水滴看似静止；频闪略快时，水滴看似**上升**；略慢时看似缓慢下落。适合拿来定义"停 / 升"的运动学 | https://www.sciencebuddies.org/science-fair-projects/project-ideas/Phys_p120/physics/stroboscopic-effect-levitating-water-drops ；https://andygiger.com/science/e-strobe/index.html ；https://www.hackster.io/news/this-rgb-time-fountain-makes-water-droplets-float-in-midair-fabff27ecafd ；https://www.uwindsor.ca/people/thammond/sites/uwindsor.ca.people.thammond/files/timefountainposter.pdf |
| **Rain Room**（Random International, 2012, Barbican） | "雨在人周围停下"的真实装置：3D 摄像头追踪人体，关闭头顶喷头。可参考"局部影响半径"的概念 | https://sharjahart.org/sharjah-art-foundation/projects/rain-room ；https://www.lacma.org/rainroom |
| **X-Men: Days of Future Past** Quicksilver 厨房段（Rising Sun Pictures） | 近乎静止的时间里，CG 喷淋水滴必须精确反射和折射，没有运动模糊可以遮丑。是 3,200 fps 实拍与 CG 结合的标杆 | https://www.cgw.com/Press-Center/Web-Exclusives/2014/Rising-Sun-Pictures-Makes-Time-Stand-Still.aspx ；https://www.fxguide.com/?p=50744 |
| **The Matrix Revolutions** "Superpunch" 雨中慢动作（ESC） | 超慢镜头下大颗雨滴的形态、拳风推开雨幕。据论坛转述，Keanu Reeves 提到"big, fat, juicy raindrops that they'd taken two months to design" **[未证实]** | https://history.siggraph.org/?p=92904 ；https://www.sidefx.com/tutorials/matrix-punching-rain-with-pop-network/ （Houdini POP 复刻教程） |
| **Quantum Break**（Remedy, 2016）Time Stutter | 游戏中实时的"时间冻结、水悬停"，可参考其抽象化与闪烁的美术处理（棱镜、频闪摄影灵感） | https://www.fxguide.com/?p=185607 ；https://gameinformer.com/b/features/archive/2015/11/06/new-details-and-screenshots-on-quantum-breaks-frozen-reality |
| **英雄**（2002）棋馆雨中对决 | 东方美学的雨中慢动作与水滴特写构图（仅作美术参考） | https://moriareviews.com/fantasy/hero-2002.htm |
| **真实雨滴物理**（USGS） | 半径小于 1 mm 的雨滴为球形；更大的呈"汉堡包"扁球；半径大于约 4.5 mm 时破碎。**悬停雨滴应以球形为主，大滴略扁，不要做成泪滴形** | https://www.usgs.gov/water-science-school/science/are-raindrops-shaped-teardrops |
| **终端速度**（Gunn & Kinzer 1949，常引近似值：0.5 mm 约 2 m/s，1 mm 约 4 m/s，2 mm 约 6.5 m/s，3 mm 约 8 m/s，5 mm 约 9 m/s。**数值为常识性近似，需核对原表**） | 决定下落条纹长度：条纹长 = 速度 × 曝光时间（180° 快门、24 fps 下为 1/48 s，2 mm 雨滴约 13.5 cm） | 参考文献：Gunn, R. & Kinzer, G. D. (1949) J. Meteorology 6(4):243–248；综述 https://arxiv.org/pdf/2102.09570 |
| **雨条纹渲染**（Garg & Nayar, SIGGRAPH 2006 / IJCV "Vision and Rain"） | 雨条纹外观取决于光照方向、视角，以及雨滴的振荡形变；单颗雨滴像广角透镜，把大视场的光折射给观察者（后续文献给出约 165°） | https://history.siggraph.org/?p=113590 ；https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_TR04.pdf ；https://arxiv.org/pdf/1908.10335 |
| Houdini 雨滴教程（通用） | 可作为管线参考 | https://www.youtube.com/watch?v=7Vj511b9Hco |

---

## 4. 剧照与帧图 URL

**官方 / 可直接引用：**

1. **Framestore 案例页头图（雨段成片剧照，1980×820）**：https://www.framestore.com/sites/default/files/2025-10/Now-You-See-Me-2-Case-Study-Header-1980x820.jpg
   - **[帧分析]** 画面内容：Atlas 正面伸手；悬停雨滴大小混杂；近景有大颗散焦水滴；脸上挂着水珠；背景是米字旗伞、红伞、绿伞和柱廊建筑。这是**最佳单张参考**。
2. **TMDB 官方剧照（雨段"正常下落"阶段，3840×2160）**：https://image.tmdb.org/t/p/original/84gzexVHBRn9e3ZQI6at0wB1Zea.jpg
   - 来源页：https://www.themoviedb.org/movie/291805-now-you-see-me-2/images/backdrops
   - 画面内容 **[帧分析]**：Atlas 身穿湿亮的黑色长外套，抬手讲话；实拍人工雨在逆光下呈短而亮的条纹；背景是黑伞和红伞。
   - 注意：这是剧照摄影师用高速快门拍的，雨条纹比电影 180° 快门下**短得多**，只可作造雨形态、服装和布光的参考，**不要拿来匹配运动模糊长度**。
   - 该页其余 46 张 backdrop 均非雨段。
3. Framestore 案例页配图（Lula 鸽子魔术场景，位于 Cutty Sark 旁，有"COFFEE"招牌和光柱；非雨段，但与雨段同属当晚的格林尼治巡演，可作环境与灯光参考）：https://www.framestore.com/sites/default/files/styles/full_width_688_x_variable_height/public/2022-07/now%20you%20see%20me%202%20image.jpg?itok=MLb-8Ikr
4. YouTube 缩略图（静帧）：
   - https://i.ytimg.com/vi/DQU7X4QDX80/hqdefault.jpg （也可尝试 `maxresdefault.jpg`）
   - https://i.ytimg.com/vi/SudyUB9Rzmg/hqdefault.jpg
   - https://i.ytimg.com/vi/NtyjfZafOcY/hqdefault.jpg
5. Art of VFX 预告图：
   - http://artofvfx.com/wp-content/uploads/2016/03/NowYouSeeMe2_trailer2.jpg
   - http://artofvfx.com/wp-content/uploads/2015/11/NowYouSeeMe2_trailer.jpg
   - 条目页 http://www.artofvfx.com/now-you-see-me-2/
6. Alamy 片场照（2015-02-09，格林尼治）：https://www.alamy.com/stock-photo-jesse-eisenberg-filming-a-magic-scene-for-now-you-see-me-2-in-greenwich-83934627.html （页面 403，需浏览器访问）

**未能获取：**
- film-grab：返回 429，限流；
- ShotDeck：返回 403，需登录；
- movie-screencaps.com：本片页面 404；
- IMDb 图库 https://www.imdb.com/title/tt3110958/mediaindex/ ：反爬，需浏览器访问。

**本次本地提取的分析帧**：位于会话临时目录，会话结束后可能被清除。如需长期保留，请复制到项目目录。
- 目录：`/private/tmp/claude-501/-Users-guocheng-ProjectRain/fc6e0811-63ea-40c6-9361-bcae438cacf5/scratchpad/clips/`
  - `kf/stop_27.png`：停止过渡逐帧
  - `kf/transition_26_29.png`
  - `kf/g1.png`–`g5.png`：悬停、巴士、灯串、天空 POV、乱舞
  - `kf/skypov.png`
  - `kf/wide_up.png`
  - `kfB/b1.png`–`b4.png`、`kfB/resume.png`：重新落雨、喷头揭秘、倒入水洼
  - `sheets/`、`sheetsB/`：1 fps 联系表
- 复现方法：用 `yt-dlp` 下载上述 YouTube 片段（只取视频流），再用 `ffmpeg -vf "fps=1,scale=480:-1,tile=4x4"` 生成联系表；亮度频闪用 `signalstats` 的 YAVG 逐帧统计。

---

## 5. 关键视觉特征清单（复刻必须达成）

### A. 时间线与运动学

- [ ] **6 个状态**：①正常下落 → ②急停 → ③悬停（带微漂移，可被手势局部影响）→ ④上升（升回天空，空气变清）→ ⑤乱舞（多方向短条纹，跟随手势）→ ⑥重新下落为倾盆大雨。另有可选的收尾：倒入水洼的溅水冠。
- [ ] **急停用时 0.25–0.33 s（6–8 帧 @24 fps）**。条纹长度应随速度连续缩短，即运动模糊长度与速度成正比。不能硬切，也不能做成长时间慢速减速。
- [ ] 悬停不是死冻结：保留约 0.3–1.2 px/帧（1080p）量级的极慢漂移或"呼吸"；可选加入极轻微的形变或振荡。
- [ ] **上升**：从静止开始逐渐加速向上。全景里看起来像发光微粒云整体上浮，最终离开画面，空中清空。
- [ ] **乱舞**：速度明显高于悬停，方向随 Atlas 的手势、甩臂、握拳改变，带旋涡感；运动模糊为 10–40 px 的短划。
- [ ] **手势影响半径**：Atlas 身边的雨滴对手势有局部响应（Framestore 专门调过"influence"的强度）。复刻时建议做成可调的影响半径与衰减。
- [ ] 可选的物理解释层：按戏中原理（频闪频率与雨滴周期的关系）驱动"停 / 升 / 降"。

### B. 频闪（极易被忽略，但对"像"很关键）

- [ ] **从急停那一帧起**，全画面亮度按 **2 帧亮、2 帧暗（约 6 Hz @23.976 fps）** 交替，亮暗比约 1.25–1.3。作用于整个场景（人群、建筑、地面），不只作用于雨。
- [ ] 段落转折处可短暂加快为单帧交替（约 12 Hz）。
- [ ] 雨重新正常落下后**停止频闪**。下落阶段始终没有频闪。

### C. 雨滴外观

- [ ] **形状**：以球形为主，小于 1 mm 时为正球；大滴可略扁。**不要做泪滴形**。悬停时屏幕上多数只有 2–5 px（1080p 全景、中景）。
- [ ] **材质**：透明折射（IOR 1.33）。对焦清晰时呈"亮边暗心"的小圆环，并带一个小镜面高光点；特写时能看到折射出的倒像。
- [ ] **亮度**：雨滴约为背景的 2.5–3 倍。逆光、前向散射时最亮，所以**泛光灯附近要像"发光的雪 / 闪粉云"**，远离灯光处明显变暗变稀。
- [ ] **颜色**：基本中性白；在暖色柱廊区染一点暖色，在 LED 或蓝光区偏冷蓝。
- [ ] **密度**：成片每帧可见约 2000–4500 颗（实际模拟为"millions"）。分布有纵深，前、中、远景都要有。
- [ ] **前景散焦雨滴**：做成 20–80 px 的**半透明折射软圆斑**，不要做成高亮实心 bokeh。散焦强度要**弱于真实镜头参数**（Framestore 的刻意选择）。
- [ ] **下落雨（实拍造雨机质感）**：细长、近垂直的亮条纹（中近景约 50–150 px），逆光下发白；重新落下时更粗更密（纵横比约 8 以上）。
- [ ] 悬停期间**地面与伞面不出现溅水**，积水保持镜面倒影；人物脸上和外套上挂着水珠。

### D. 场景、灯光与摄影

- [ ] 夜景，对称柱廊纵深，暖色向上打亮的立柱，远处有一栋打亮的白色建筑，中间是草坪和湿石板路舞台。
- [ ] 两侧柱廊顶部各一排高亮白色泛光灯，带星芒和光晕，作为主逆光；地面有蓝/青 LED 灯带，积水中有强倒影。
- [ ] 人群撑彩色伞（米字旗伞、红、白、蓝、绿/青、黑），部分人举手机电筒；场景内可放一辆红色双层巴士，车内是冷白荧光灯。
- [ ] 色调：暗部偏青绿或中性；高光为暖琥珀（建筑）加冷白（泛光灯）加蓝（LED）；结尾大雨段整体偏冷蓝。
- [ ] 2.39:1 宽银幕，23.976 fps；正常下落段按 180° 快门的运动模糊。
- [ ] **镜头运动卖"冻结"**：急停后同一镜头内开始后拉、升高，揭示全场；还需要仰拍天空 POV（黑底满屏亮点，带少量散焦大圆斑）、高角度纵深全景、车窗特写。
- [ ] 人与雨的深度穿插正确：雨滴要分布在人群前后各层。Framestore 用 deep 合成控制；UE 中需要保证半透明排序和景深正确。

### E. 结尾（可选）

- [ ] 雨重新落下：先以"雪状"密集颗粒开始下落，同时短暂频闪，随后转为倾盆大雨，频闪停止，光调转向冷蓝。
- [ ] 倒入水洼：正上方俯拍，人物位于圆形追光光斑中，溅起白色水冠后消失。

---

## 附：主要来源一览

- Framestore 案例页：https://www.framestore.com/work/now-you-see-me-2
- Art of VFX 条目：http://www.artofvfx.com/now-you-see-me-2/
- Empire（2016-03）片场探班："OH OH OH IT'S MAGIC"，https://www.pressreader.com/uk/empire-uk/20160331/281505045336663 （未能直读原文）
- 英文字幕稿：https://www.springfieldspringfield.co.uk/movie_script.php?movie=now-you-see-me-2
- Wikipedia：https://en.wikipedia.org/wiki/Now_You_See_Me_2
- TechCrunch 对 Jon M. Chu 的采访：https://techcrunch.com/2016/06/09/now-you-see-me-2-jon-chu-interview/
- Collider 对 Jon M. Chu 的采访（magic vs science）：https://collider.com/now-you-see-me-2-magic-vs-science-jon-m-chu/
- DVD Talk 蓝光评测（花絮清单）：https://www.dvdtalk.com/reviews/review/71238
- Movie Mistakes（"悬停期间无溅水"等）：https://moviemistakes.com/film11465
- American Cinematographer 2016 年 7 月刊：https://www.nxtbook.com/nxtbooks/ac/ac0716_v2/index.php?startid=58 （403，待人工查阅）
- 视频片段：见 1.6 节；中文资料：见 2.6 节；其他参考：见第 3 节。
