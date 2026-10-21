# 02 雨的物理与渲染：深度技术调研（UE 5.7 Niagara「冻结雨」）

> **目标镜头**：《惊天魔盗团2》(Now You See Me 2, 2016) 中的雨停桥段。雨正常下落（运动模糊条纹），逐渐减速，停成悬浮水滴，再被角色随手拨动。
> **本文内容**：汇总一手资料（论文、SIGGRAPH/GDC/FMX 讲座、官方文档）中的物理与渲染知识，并给出可落地的参数。
> **标注约定**：**[文献]** 表示数值直接来自所引来源；**[推导]** 表示由文献公式计算得到（公式均在文中给出，可复算）；**[美术取值]** 表示经验或艺术取值；**（推论）** 表示基于文档的综合判断，尚未实测。
> **默认相机**（除非另注）：24 fps、180° 快门（T = 1/48 s）、35 mm 镜头、Super35（宽 24.89 mm）、4K（3840 px），f_px ≈ 5400。
> 调研日期：2026-10-13。

**目录**：0 核心结论 · 1 雨的物理 · 2 经典雨渲染研究 · 3 次级真实感层 · 4 冻结状态 · 5 UE 5.7 Niagara 实践 · 6 实现建议参数表 · 7 一手资料清单与冲突

## 0. 核心结论速览

1. **雨滴尺寸**：Marshall–Palmer 指数谱 `N(D) = 8000·exp(−Λ·D)` m⁻³·mm⁻¹，`Λ = 4.1·R^−0.21` mm⁻¹，R 为雨强 (mm/h)。中雨 (R = 5–10 mm/h) 的中值体积直径 D0 = 3.67/Λ ≈ 1.3–1.5 mm。直径 > 0.5 mm 的雨滴约 600–900 个/m³，平均间距约 10–12 cm；直径 > 2 mm 的只有约 8–20 个/m³。**[推导]**
2. **末速度**：`v(D) = 9.65 − 10.3·exp(−0.6·D)` m/s（D 单位 mm，Atlas et al. 1973）。1 mm 约 4.0 m/s，2 mm 约 6.5 m/s，4 mm 约 8.7 m/s，上限约 9.6 m/s。**[文献+推导]**
3. **形状**：D < 1 mm 基本是球；越大越扁，呈“汉堡包上半个面包”形（底平顶圆），**从来不是泪滴形**。轴比 b/a 约为：2 mm → 0.94，3 mm → 0.87，4 mm → 0.79，5 mm → 0.72（Brandes 2002 多项式）。**[文献+推导]**
4. **相机里的条纹**：世界空间条纹长度 `L = v·T_exp + D`。24 fps、180° 快门时 T = 1/48 s，2 mm 雨滴的条纹约 13.8 cm 长。条纹单个像素的不透明度只有静止雨滴的 `α = D/(v·T)`，约 1.2–2.2%，所以条纹又淡又透明（Garg & Nayar）。**能量守恒的减速过渡**由此自然得到：速度降低时条纹变短，每像素亮度按 `D/(v·T + D)` 上升；v → 0 时回到一颗完整、明亮的水滴。**[文献+推导]**
5. **静止水滴的样子**：一颗水滴把约 **165°** 视场的环境折射进相机，相当于一个鱼眼镜头，成倒像。折射只损失约 6% 的光（透射 94%）。镜面反射和内反射只在边缘明显。所以在**阴天或白天天光下**，静止水滴通常比它挡住的背景更亮，且亮度几乎不随背景变化（Garg & Nayar, IJCV 2007 的实验条件是阴天）。到了夜景或路灯环境，165° 锥内的平均亮度很低，水滴主要表现为灯光的倒像、高光闪点和亮边或暗边，不会整体发亮。实现上的推论：只做“屏幕空间折射背后画面”是不对的，必须采样大视场环境（cubemap、反射捕获或光线追踪）。**[文献]**
6. **球形水滴内部不会发生全反射**（Garg & Nayar 明确指出）**[文献]**。边缘发暗来自两方面：边缘像素折射到的是偏离视线约 83° 的侧向环境，这些方向往往较暗；同时掠射角下 Fresnel 反射升高、透射降低。原因不是 TIR。所以暗边在逆光或夜景中明显，在均匀天光下较弱。**[推导]**
7. **可见距离**：35 mm 镜头、Super35、4K 下，2 mm 水滴在约 10.8 m 处缩到 1 像素宽。按 Garg 的经验系数 R ≈ 3，约 30 m 以外的雨只表现为雾状散射。冻结状态下的高质量折射网格只需覆盖相机前几米。**[推导]**

---

## 1. 雨的物理

### 1.1 雨滴尺寸分布（DSD）

**Marshall–Palmer (1948)**。原文（J. Meteor. 5, 165–166）基于 1946 年夏季渥太华的染色滤纸记录：

- `N_D = N0·e^(−ΛD)`，原文常数 `N0 = 0.08 cm⁻⁴`（= 8000 m⁻³·mm⁻¹），`Λ = 41·R^−0.21 cm⁻¹`（= 4.1·R^−0.21 mm⁻¹），R 单位 mm/h。
- 原文明确指出：**直径小于约 1.5 mm 时，观测值低于公式**。也就是说，MP 公式会高估小雨滴的数量。
- 原文表 1 给出单位体积含水量 `M = 89·R^0.84 mg/m³`（由公式推得）。
- 来源：原文扫描 https://www.radartutorial.eu/15.weather/pubs/marshall-palmer-1948.pdf ；单位换算见 https://en.wikipedia.org/wiki/Raindrop_size_distribution

**Gamma 分布**：`N(D) = N0·D^μ·e^(−ΛD)`，属于截断 gamma 形式。Ulbrich (1983) 给出云内 μ ≈ 0，云下因小雨滴蒸发 μ ≈ 2（来源同上 Wikipedia 条目，二手）。Bringi et al. (2003) 用 2D 视频雨滴谱仪数据说明，对流雨与层状雨的 DSD 可用归一化 gamma 描述（https://training.weather.gov/wdtd/courses/rac/principles/pps/story_content/external_files/Bringi_et_al_2003.pdf ）。对视觉特效来说，MP 指数谱再加上小滴截断（例如 D_min = 0.5 mm）已经够用。gamma 的 μ > 0 能压低小滴数量，正好对应 MP 原文“小滴偏少”的观测。

**分类阈值（AMS Glossary）**：
- 毛毛雨 (drizzle)：直径 0.1–0.5 mm，雨滴 > 0.5 mm。毛毛雨强度分级：轻 < 0.3 mm/h，中 0.3–0.5 mm/h，重 > 0.5 mm/h。
- 雨的强度分级：小雨 ≤ 2.5 mm/h，中雨 2.6–7.6 mm/h，大雨 > 7.6 mm/h（原文单位为 cm/h）。
- 来源：https://glossary.ametsoc.org/wiki/Drizzle ，https://glossary.ametsoc.org/wiki/Rain

**最大雨滴**：
- 自然界中单个雨滴的最大等效半径约 4–5 mm（直径 8–10 mm）。
- Gatlin et al. (2015) 统计了 2.4×10⁸ 个雨滴：仅 0.4% 的等效半径 ≥ 2.5 mm（D ≥ 5 mm），等效半径 ≥ 4 mm 的仅占 1.9×10⁻⁵ %。
- 来源：转引自 Loftus & Wordsworth 2021, https://arxiv.org/pdf/2102.09570

**由 MP 推导的数密度表** [推导]：用 `N(D>Dmin) = (N0/Λ)·e^(−Λ·Dmin)` 计算，并用 Atlas 末速度积分 `R = 3.6e-3·(π/6)·∫N(D)·D³·v(D) dD` 回代验证。回代得到的 R 与输入相差 +5% 到 +18%，属于 MP 与速度公式本身不完全自洽的正常误差。含水量与 MP 原文 `89·R^0.84` 完全吻合，说明单位换算正确。

| R (mm/h) | Λ (mm⁻¹) | D0=3.67/Λ | N(全部) /m³ | N(D>0.5mm) /m³ | N(D>1mm) | N(D>2mm) | D>0.5mm 平均间距 | D>0.5mm 雨滴通量 /m²/s |
|---|---|---|---|---|---|---|---|---|
| 1 | 4.10 | 0.90 mm | 1951 | 251 | 32 | 0.5 | 15.8 cm | 752 |
| 2.5 | 3.38 | 1.09 mm | 2365 | 436 | 80 | 2.7 | 13.2 cm | 1382 |
| 5 | 2.92 | 1.26 mm | 2736 | 634 | 147 | 7.9 | 11.6 cm | 2104 |
| 10 | 2.53 | 1.45 mm | 3165 | 894 | 253 | 20 | 10.4 cm | 3114 |
| 25 | 2.09 | 1.76 mm | 3836 | 1352 | 477 | 59 | 9.0 cm | 5036 |
| 50 | 1.80 | 2.04 mm | 4437 | 1801 | 731 | 121 | 8.2 cm | 7069 |
| 100 | 1.56 | 2.35 mm | 5132 | 2354 | 1080 | 227 | 7.5 cm | 9747 |

> 注：`N(全部)` 包含 MP 高估的小滴，实际数量更少。视觉上有意义的是 `D > 0.5 mm` 那一列。Garg & Nayar (2006) 也指出，可以人为加大雨滴尺寸来制造更戏剧化的暴雨效果，这与电影实拍用大水滴喷淋头的做法一致（脚注 11，https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_TOG06.pdf ）。

### 1.2 末速度

- **Atlas, Srivastava & Sekhon (1973)**（Rev. Geophys. 11(1), 1–35，拟合 Gunn & Kinzer 1949 的实验数据）：`v(D) = 9.65 − 10.3·exp(−0.6·D)` m/s，D 单位 mm。D < 约 0.12 mm 时公式输出 ≤ 0，DisdroDB 把它截断为 0。
  - 代码实现：https://disdrodb.readthedocs.io/en/latest/_modules/disdrodb/fall_velocity/rain.html
  - 拟合来源说明：https://par.nsf.gov/servlets/purl/10533483
  - 有一篇 arXiv 论文 (https://arxiv.org/pdf/1707.05225) 把系数写成 “1030”，属于笔误。
- **Garg & Nayar 的近似**：`v = 200·√a`（a 为半径，单位 m，v 单位 m/s；引自 Gunn & Kinzer）。来源：https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_IJCV07.pdf
- Gunn–Kinzer 数据（经 Beard 气压修正）至今仍是“金标准”。湍流或阵风条件下实测下落速度可能低 25–30%。来源：https://par.nsf.gov/biblio/10533483 （检索摘要）

| D (mm) | Atlas v (m/s) | 200√a (m/s) |
|---|---|---|
| 0.5 | 2.02 | 3.16 |
| 1.0 | 4.00 | 4.47 |
| 1.5 | 5.46 | 5.48 |
| 2.0 | 6.55 | 6.32 |
| 3.0 | 7.95 | 7.75 |
| 4.0 | 8.72 | 8.94 |
| 5.0 | 9.14 | 10.00 |
| 6.0 | 9.37 | 10.95 |

[推导]：`200√a` 在 D > 4 mm 时明显偏高，在 D < 1 mm 时也偏高，因此**推荐使用 Atlas 公式**。

### 1.3 雨滴形状

- **不是泪滴形**。随尺寸增大，雨滴从球形变成扁球，最后变成“汉堡包上半个面包”形（底部平、顶部圆）。来源：Loftus & Wordsworth 2021，引 Pruppacher & Pitter 1971、Beard & Chuang 1987，https://arxiv.org/pdf/2102.09570
- **Beard & Chuang (1987)** 平衡形状：`r(θ) = a·[1 + Σ_{n=0..10} c_n·cos(nθ)]`。θ = 0 指向下落方向，底部被气压压平（Garg & Nayar IJCV 2007 图 2）。
  - D = 2.0–6.0 mm、步长 0.5 mm 的系数表见 https://en.wikipedia.org/wiki/Beard_and_Chuang_model （单源二手）。
  - 摘录（c_n×10⁴，n = 0…10）：
    - D = 2 mm：−131, −120, −376, −96, −4, 15, 5, 0, −2, 0, 1
    - D = 4 mm：−458, −335, −1211, −227, 83, 89, 12, −21, −13, 1, 8
  - [推导] 用该表重建轮廓得到的高宽比：2 mm 为 0.927，3 mm 为 0.853，4 mm 为 0.777，5 mm 为 0.707，6 mm 为 0.642，与下面 Brandes 多项式相差 < 0.015，说明系数方向正确。轮廓在 θ = 0（底）一侧比 θ = π（顶）一侧更扁。**可直接用于程序化生成水滴网格。**
- **轴比拟合公式**（单位一定要看清楚）：
  - Brandes et al. (2002)：`b/a = 0.9951 + 0.02510·D − 0.03644·D² + 0.005303·D³ − 0.0002492·D⁴`，**D 单位 mm**（等效球直径）。来源：Météo-France T-matrix 文档 https://redmine.umr-cnrm.fr/attachments/download/3759/TmatrixDocumentation.pdf
  - Andsager et al. (1999)：`r = 1.012 − 0.144·D − 1.03·D²`，**D 单位 cm**，适用于 D < 0.44 cm。D ≥ 0.44 cm 时用平衡形状线性近似 `r = 1.02 − 0.62·D`（D 单位 cm）。来源：Matrosov et al., NOAA PSL, https://psl.noaa.gov/data/obs/instruments/SbandXbandComparison.pdf
  - Bringi et al. (2003) 推荐的组合：1–4 mm 用 Andsager，D < 1 mm 或 D > 4 mm 用 Beard–Chuang。来源同上 Bringi PDF。

| D (mm) | Brandes 2002 | Andsager/线性 |
|---|---|---|
| 0.5 | 0.999 | 1.002 |
| 1.0 | 0.989 | 0.987 |
| 2.0 | 0.938 | 0.942 |
| 3.0 | 0.865 | 0.876 |
| 4.0 | 0.788 | 0.790 |
| 5.0 | 0.717 | 0.710 |
| 6.0 | 0.656 | 0.648 |

[推导，上表]

### 1.4 雨滴振荡

- Garg & Nayar (2006) 引用大气科学研究（Kubesh & Beard 1993；Andsager 1999；Tokay & Beard 1996）指出，下落雨滴的形变主要来自两种球谐模态：轴对称的扁–长模态 (n=2, m=0) 和非轴对称的横向模态 (n=3, m=1)。
  - 形状模型：`r(t,θ,φ) = r0·(1 + A₂,₀·sin(ω₂t)·P₂,₀(θ) + A₃,₁·sin(ω₃t)·cos(φ)·P₃,₁(θ))`，且 `ω₃ ≈ 2ω₂`。
  - 他们把 810 张实拍条纹与渲染结果对比，测得振幅范围：A₂,₀ 为 0.1–0.4，A₃,₁ 为 0.05–0.2。
  - **最佳匹配组合**为 (A₂,₀, A₃,₁) = (0.2, 0.1) 和 (0.1, 0.1)。
  - 实验用的水滴从 15 m 高处释放，平均 r0 = 2.0 mm，点光源距 1 m，相机 Canon 20D 距 3 m。
  - 来源：https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_TOG06.pdf
- 振荡频率（Rayleigh）：`f_n = (1/2π)·sqrt(n(n−1)(n+2)·σ/(ρ·r0³))`，σ = 0.0728 N/m，ρ = 1000 kg/m³。[推导] 用 n = 2 计算的结果与 Garg 的说法一致：r0 = 1.6 mm 时周期 = 1/60 s。

| D (mm) | f₂ (Hz) | 周期 (ms) |
|---|---|---|
| 1 | 344 | 2.9 |
| 2 | 122 | 8.2 |
| 3 | 66 | 15.1 |
| 4 | 43 | 23.3 |

  **意义**：24 fps 下，一帧曝光 (1/48 s ≈ 20.8 ms) 内 2 mm 雨滴会振荡约 2.5 个周期，所以条纹内部会出现周期性的亮斑和弯曲高光（Garg：“speckles, multiple smeared highlights and curved brightness contours”）。到了冻结状态，如果想表现“时间几乎停止但水滴仍在微微颤动”，振荡必须同步大幅放慢。按真实频率播放会在 24 fps 下产生混叠。

### 1.5 风的影响

- 雨滴会获得水平风速，落雨倾角为 `tan(α) = U_wind / v_t`（从竖直方向量起）。小雨滴受风影响更大。来源：Helming（ISCO 1999），https://topsoil.nserl.purdue.edu/nserlweb/isco99/pdf/ISCOdisc/SustainingTheGlobalFarm/P253-Helming.pdf ；HESS 2021 引 Blocken & Carmeliet 2004，https://hess.copernicus.org/articles/25/6023/2021/hess-25-6023-2021.html
- [推导] 5 m/s 风中的倾角：0.5 mm 约 68°，1 mm 约 51°，2 mm 约 37°，4 mm 约 30°。速度响应时间量级为 `τ ≈ v_t/g`（二次阻力近似）：1 mm 约 0.4 s，2 mm 约 0.7 s。Niagara 中可以用 `Drag` 加风力场实现，阻力系数按尺寸缩放。

### 1.6 相机怎样“看见”雨：曝光、条纹长度与透明度

这一节来自 Garg & Nayar 的三篇论文：ICCV 2005《When Does a Camera See Rain?》、IJCV 2007《Vision and Rain》、SIGGRAPH 2006。PDF：
- https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_ICCV05.pdf
- https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_IJCV07.pdf

- **雨滴停留在单个像素上的时间极短**。τ ≈ 2a/v，最大约 **1.18 ms**（对应最大 a = 3.5 mm），远小于常见曝光 T ≈ 30 ms。因此条纹像素的强度是背景与雨滴的线性混合：`I_r = τ·Ē_r + (T−τ)·E_b`，斜率 `β = τ/T < 0.039`。[文献]
- **条纹透明度**（SIGGRAPH 2006 渲染算法）：`I_r = (1−α)·I_b + α·I_streak`，`α = 2·r0/(v·T_exp)`。曝光越短，条纹越不透明。[文献]
- **1 ms 快门下的静止雨滴“很亮、看起来不透明”；30 ms 曝光下条纹变得透明**。“正是高速运动让雨看起来透明”（IJCV 2007 图 9）。[文献] 这条正是冻结效果的物理依据：雨停下来后，每颗水滴应当从“淡淡的透明条纹”变成“明亮的小透镜”。
- **可见区域**：
  - 水滴在 `z < z_m = 2·f·a`（f 以像素计）时投影大于 1 像素，此时强度变化与距离无关。`z > z_m` 时强度变化按 1/z 衰减。超过 `R·z_m` 时雨只表现为雾一样的聚合散射。
  - Canon XL1 相机实测 R ≈ 3。10° 视场（f ≈ 4000 px）、a = 1 mm 时，`R·z_m ≈ 24 m`（ICCV 2005）。
  - 远处的雨只起到类似雾的效果（IJCV 2007 脚注 18、20）。[文献]
- **景深**：散焦水滴的强度变化按 `w/(w+b_c)` 被稀释（w 为水滴像宽，b_c 为弥散圆直径）。大光圈加短景深能“抹掉”雨，小光圈加对焦在近处能“增强”雨（ICCV 2005 式 6–7）。[文献]

**条纹长度与透明度** [推导]。T = 快门角/360°/fps；24 fps、180° 时 T = 20.83 ms。

| D (mm) | v (m/s) | L @180° (cm) | L @90° (cm) | L @45° (cm) | α @180° |
|---|---|---|---|---|---|
| 0.5 | 2.02 | 4.3 | 2.2 | 1.1 | 0.012 |
| 1 | 4.00 | 8.4 | 4.3 | 2.2 | 0.012 |
| 2 | 6.55 | 13.8 | 7.0 | 3.6 | 0.015 |
| 3 | 7.95 | 16.9 | 8.6 | 4.4 | 0.018 |
| 4 | 8.72 | 18.6 | 9.5 | 4.9 | 0.022 |

**减速过渡（2 mm 雨滴，T = 1/48 s）** [推导]。每像素亮度系数为 `D/(v·T + D)`：

| v (m/s) | 6.55 | 3 | 1 | 0.5 | 0.2 | 0.1 | 0.05 | 0 |
|---|---|---|---|---|---|---|---|---|
| 条纹长度 (cm) | 13.9 | 6.5 | 2.3 | 1.2 | 0.62 | 0.41 | 0.30 | 0.20 |
| 每像素亮度系数 | 0.014 | 0.031 | 0.088 | 0.16 | 0.32 | 0.49 | 0.66 | 1.0 |

- `v·T = D`，即 2 mm 雨滴在 v ≈ 0.096 m/s 时，条纹长度等于水滴直径，**视觉上就“停住”了**。
- 每像素亮度从 1.4% 升到 100%，**动态范围约 70 倍**。
- 如果用真正的累积运动模糊（多个时间子采样），这个变化会自动出现。如果用拉伸 sprite 模拟，就必须按此公式手动调整不透明度和亮度。

**投影尺寸（2 mm 水滴，35 mm 镜头，Super35 传感器宽 24.89 mm，水平 3840 px，f_px ≈ 5400）** [推导]：

| 距离 z | 水滴像宽 | 180° 下落条纹长 | 1 mm 水滴像宽 |
|---|---|---|---|
| 0.3 m | 36 px | ≈2490 px（超出画面高度） | 18 px |
| 0.5 m | 21.6 px | 1495 px | 10.8 px |
| 1 m | 10.8 px | 747 px | 5.4 px |
| 2 m | 5.4 px | 374 px | 2.7 px |
| 5 m | 2.2 px | 150 px | 1.1 px |
| 10 m | 1.1 px | 75 px | 0.54 px |
| 20 m | 0.54 px | 37 px | 0.27 px |

不同焦距下 2 mm 水滴缩到 1 px 的距离 z_m，以及按 Garg R ≈ 3 推算的“可见雨”上限：

| 焦距 | z_m | 3·z_m |
|---|---|---|
| 24 mm | 7.4 m | 22 m |
| 35 mm | 10.8 m | 32 m |
| 50 mm | 15.4 m | 46 m |
| 85 mm | 26.2 m | 79 m |

**结论**：
- **冻结状态下**，1 m 处的 2 mm 水滴只有约 11 px 宽，0.3 m 处约 36 px。只有约 2 m 以内的水滴能看出“内部倒像”，2–10 m 的水滴主要是一个亮点加一个暗边，10 m 以外只剩亚像素的闪光。LOD 应据此设计。
- **下落状态下**，5 m 处的条纹仍有约 150 px 长、约 2 px 宽，所以中景条纹非常重要。

**景深弥散**（薄透镜 CoC，换算成 4K 像素）[推导]：

| 镜头 | z = 0.3 m | 0.5 m | 1 m | 2 m | 10 m |
|---|---|---|---|---|---|
| 35 mm f/2.8，对焦 3 m | 205 px | 114 px | 46 px | 11 px | 16 px |
| 50 mm f/2，对焦 3 m | 588 px | 327 px | 131 px | 33 px | 46 px |
| 85 mm f/1.4，对焦 5 m | — | 1458 px | 648 px | 243 px | 81 px |

近处水滴会散成几百像素的大光斑 (bokeh)，所以冻结镜头的近景层是“光斑层”，而不是“水滴层”。

### 1.7 静止水滴的光学（球透镜）

- **水的折射率**取 n ≈ 1.333。[推导]
  - 正入射 Fresnel 反射率 `F0 = ((n−1)/(n+1))² ≈ 0.020`。
  - 水→空气全反射临界角 `arcsin(1/n) ≈ 48.6°`。
  - 球透镜有效焦距 `EFL = n·D/(4(n−1))`：D = 2 mm 时 EFL ≈ 2.0 mm，后焦距 `BFL = EFL − D/2 ≈ 1.0 mm`。所以水滴后方几米外的景物，会在水滴的出射侧（即**靠相机一侧**）离表面约 1 mm 处成一个**倒立、缩小**的实像。相机透过水滴看到的就是这个倒像。
- **165° 视场**：“A raindrop refracts light from a wide range of angles into the camera… the field of view of the raindrop is approximately 165°… a typical fish-eye lens has a very similar field of view.”（IJCV 2007 §4.2.2）[文献]
- **能量构成**：“a raindrop transmits 94% of the incident radiance towards the camera. Specular and internal reflection are only prominent at the periphery of the drop.” 在强光源（如阳光）下，表面镜面反射的贡献显著，内反射还会把光色散成彩虹色（IJCV 2007 脚注 8）。[文献]
- **球形水滴内部不会全反射**：“total internal reflections can never occur in a spherical drop since the maximum angle q_max that a ray inside the drop can make with the surface normal is always less than the critical angle”（IJCV 2007 脚注 7）。所以内反射都是**部分**反射（Fresnel）。[文献]
- **静止水滴比背景亮，且与背景无关（实验条件：阴天天光）**：
  - 水滴折射的是 165° 的环境（含天空），而它遮挡的背景只占很小的立体角。
  - 实验视频在阴天下拍摄，1 ms 快门、30 fps。水滴的峰值亮度明显高于背景，且在 5 条不同灰度的背景前几乎相同（IJCV 2007 §4.2.3、图 8）。[文献]
  - [推导] 这个结论成立的前提是 165° 锥内有明亮的天空。夜景中锥内平均亮度很低，水滴不一定比背景亮，观感由灯光的倒像、两个高光点和边缘决定。目标镜头的照明条件（日景或夜景）需要以参考画面为准。
- **对实时渲染的直接启示** [推导]：
  - 水滴的 165° 折射视场以视线方向为中心、朝向水滴后方，半角约 82.5°，覆盖了画框外的大片天空和侧向光源。只用 SceneColor 做屏幕空间折射，视场会被压缩到相机视锥以内（35 mm 镜头水平半角约 20°）。这样画框外的天空和侧光都会丢失，水滴会显得“发灰、像玻璃珠贴在画面上”。
  - 相机**后方**的光源不经折射进入水滴，而是通过前表面镜面反射和内反射（彩虹几何）出现。这部分要靠反射项（Reflection Capture / Lumen / RT 反射，或解析点光高光）来补。
  - 正确做法：用解析球面折射计算出射方向，再去查**覆盖全方向的环境**，例如场景局部的 cubemap SceneCapture、Reflection Capture、Lumen/RT 反射，或者路径追踪。
  - 屏幕空间折射只能作为中心小角度区域的补充。


---

## 2. 经典雨渲染研究

### 2.1 Garg & Nayar, “Photorealistic Rendering of Rain Streaks”（SIGGRAPH 2006 / ACM TOG 25(3):996–1002）

PDF：https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_TOG06.pdf ；DOI 10.1145/1141911.1141985

- **核心观点**：近景雨条纹内部有斑点、多重拉长高光和弯曲亮度等值线。这些图案来自下落过程中的振荡与光照方向、视线方向的耦合。传统的“恒定亮度矩形或椭圆条纹”只适用于远处细雨。《黑客帝国3》用的是手绘条纹贴图（Lomas 2005，私人交流）。[文献]
- **实测**：
  - 水滴从 15 m 高释放（保证达到末速度，并产生与真实雨相近的振荡），平均 r0 = 2.0 mm。
  - 点光源距水滴 1 m，Canon EOS-20D 在 3 m 处拍 HDR。
  - 光源仰角 θ_l 取 50°/90°/130°，方位 φ_l 取 10°–170°（步长 20°）；视角 θ_v 取 70°/90°/110°；每种组合拍 10 张，共 810 张。[文献]
- **条纹数据库**：
  - 用 PBRT 光线追踪渲染。水滴网格 16200 个顶点，递归深度 5，每像素 64 个样本，光源为点光照射的哑光圆盘。
  - 参数范围：θ_l 0°–180°，φ_l 与 θ_v 均为 10°–170°（步长 20°）；振荡参数 (A₂,₀, A₃,₁) 取 (0.2, 0.1) 和 (0.1, 0.1)；另含球形无振荡、只有 A₂,₀ = 0.2 两种情况；φ_rot 取 0/90/180/270°。
  - 渲染条件：r0 = 1.6 mm，T_db = 1/60 s（恰好是一个振荡周期）。
  - 输出：16 位单色，32×1042 px（另存条纹宽 16/8/4 三档 mip），共约 6300 张，约 400 MB。
  - 作者认为 20° 的角度采样已经足够，因为条纹只有几像素宽、运动模糊严重。[文献]
- **用于新尺寸雨滴**：
  - 振荡周期 `T_new = 2π/ω₂` 随尺寸变化。若 `T_new > T_exp`，截取数据库条纹的 `T_exp/T_new` 部分；否则把条纹首尾拼接后再截取，两端做模糊。
  - 靠近相机或光源时角度沿条纹变化，此时用上下两端的贴图按线性遮罩混合：`I = I1·M + (1−M)·I2`。[文献]
- **光源效果**：
  - 光晕：每个点光源的条纹贴图乘以 `1/d_i²` 遮罩（d_i 为水滴到光源的 3D 距离），就能生成“halo”。
  - 锥光：乘以光源的各向异性分布遮罩。
  - 各光源条纹与环境光条纹分别按光源强度和颜色缩放后相加。[文献]
- **相机效果**：
  - 景深外的条纹按深度做高斯模糊。
  - 透明度 `α = 2r0/(v·T_exp)`，曝光越短越不透明。
  - 用粗略深度图做遮挡。[文献]
- **观察结论**：
  - 阴天环境光下条纹内部是平滑的亮度渐变；强方向光（阳光）下会出现高频图案。
  - 光照方向强烈影响条纹外观，Fig.5 对比了光源方位 60° 和 10° 的两种情况（0° = 相机视线方向）。
  - 车灯从前方照向相机和从侧面照，条纹亮度与外观完全不同。[文献]
- **性能**：2 GHz Pentium-4 上约 10 s/帧（离线）。
- **对本项目的意义**：
  1. 条纹贴图应体现“振荡斑点”，而不是一根平滑的线。
  2. 每个光源单独计算贡献，逆光时更亮。
  3. 条纹贴图的有效长度应随 `T_exp/T_osc` 变化。
  4. 数据库仍可通过邮件 rainstreaks@cs.columbia.edu 申请（论文脚注 1；当前是否可用未核实）。NVIDIA DX10 Rain 示例用的就是这套数据库，见 2.4 节。

### 2.2 Garg & Nayar, “Vision and Rain”（IJCV 75(1), 2007）与 “When Does a Camera See Rain?”（ICCV 2005）

内容已并入 1.6、1.7 节，要点如下：
- 165° 视场、94% 透射、静止水滴比背景亮。
- τ < 1.18 ms、β < 0.039；`R·z_m` 以外的雨呈现为雾；曝光、光圈和对焦都能调节雨的可见度。
- ICCV 2005 给出了“雨可见度”的参数表，例如 Canon XL1 默认 T = 16 ms，把 T 拉长到 66 ms 或 120 ms 能显著压制雨。反过来，用短曝光、小景深并对焦在近处，可以“增强”雨。这一点可以直接指导镜头里“雨的存在感”调节。[文献]

### 2.3 Tatarchuk, “Artist-Directable Real-Time Rain Rendering in City Environments”（ATI ToyShop，SIGGRAPH 2006 Course Ch.3）

讲义：https://advances.realtimerendering.com/s2006/Chapter3-Artist-Directable_Real-Time_Rain_Rendering_in_City_Environments.pdf ；幻灯片：https://advances.realtimerendering.com/s2006/Tatarchuk-Rain.pdf

- **主体雨采用屏幕空间合成层，不用粒子**。在最终后处理之前，用一个全屏 pass 合成多层雨。
  - 来源是一张 8 bit 的 rainfall texture，按艺术家指定的世界空间雨向和雨速滚动。
  - 多层视差在一次 projective fetch 里完成：`w_i = p_r·r_i`，其中 p_r 为 rain parallax，r_i 为每滴的随机值。
  - 雨滴用切线空间法线贴图计算反射和空气→水折射，两者都乘 Fresnel 向边缘衰减。
  - “mistiness” 和运动模糊不在纹理里做，而是在合成后做一次后处理模糊（§3.6.1）。[文献]
- **“牛奶”偏置**：真实的雨在亮区很淡、在暗区更明显，完全按物理做“太淡”。于是借用好莱坞往水里加牛奶的做法，把雨的颜色和不透明度偏向白色。闪电时，粒子和溅射的不透明度乘以 `1 − ½·lightning brightness`。[文献]
- **滴落粒子**：屋檐、灯具等处的滴落用屏幕对齐 billboard，原文称“约 10–15,000 个粒子效果很好”。billboard 按速度拉伸，法线贴图事先模糊并拉伸以模拟运动模糊，不透明度按距离衰减并乘以 Fresnel（§3.6.2）。[文献]
- **溅射**：粒子与代理碰撞体碰撞后生成，每帧约 5–8,000 个。
  - 外观来自一段高速拍摄的牛奶滴溅射序列，随机化尺寸、透明度和 u 方向翻转。
  - 光源在溅射背后时按增亮的背光物体渲染，否则只算 ambient 和 specular。
  - 路灯等伪光源编码进一张 overhead lightmap（§3.6.3）。[文献]
- **水洼涟漪**：GPU 上用 256×256 网格解薄膜波动方程 `∂²z/∂t² = v²(∂²z/∂x² + ∂²z/∂y²)`，Euler 积分。
  - 共 4 个 pass：①把雨滴以点的形式写入，用阻尼正弦；②③积分；④Sobel 生成法线。
  - 全场景共用一张模拟，按世界坐标 xz 查表，再用 puddle depth mask 混合法线（§3.7）。[文献]
- **光晕和雾**：
  - misty glow 用 Kawase bloom。场景渲染到 10-10-10-2 缓冲，2 bit alpha 存 glow 量；降采样到 1/4，用 1-2-1 核做 4 次 ping-pong 模糊。
  - 雾为线性距离雾，按亮度减弱：`fogFactor = fFog·(1 − Lum/10)`。雾量同时加进 glow alpha。
  - 灯下光锥：在锥面上用两层不同方向的滚动噪声做伪体积，中间插一张竖直滚动的雨贴图 quad。
  - 物体轮廓的雨雾用 fins，表面溅点用 shells（§3.4、§3.8）。[文献]
- **玻璃上的水滴**：Kaneda99 方法的 GPU 版，质量场导出法线，用来做折射和反射；大水滴在阴影中心加一个伪焦散亮点（§3.8）。[文献]

### 2.4 Sarah Tariq，NVIDIA D3D10 SDK “Rain”（2007）

白皮书：https://developer.download.nvidia.com/SDK/10/direct3d/Source/rain/doc/RainSDKWhitePaper.pdf

- **粒子管线**：
  - Stream Out 在两个 VB 之间 ping-pong 做动画，Geometry Shader 把每个点扩成 sprite。
  - 雨区跟随相机，出界的粒子在相机周围重生。[文献]
- **Garg–Nayar 条纹库的用法**：
  - 为省内存去掉 θ_view 维，θ_light 只用正角。
  - 300 多张 L8 纹理装进 2D Texture Array，需要 mip 和各向异性过滤；不用 atlas 或 3D 纹理，以免边缘伪影。
  - 像素着色器算出 θ_light、φ_light，取最近 4 张纹理做双线性 lerp。
  - 每个光源有一个“light response”滑块。场景里有 2 个点光（可切成 spotlight）和 1 个方向光。[文献]
- **雾**：Sun et al. 2005 的解析单次散射模型，“correctly renders the glows around light sources”。[文献]
- **性能**（8800 GTX，1280×1024，simple shader）：
  - Stream Out 动画：20 万粒子 574 fps，100 万 257 fps，500 万 67 fps。
  - 扩展加光栅化：20 万 545 fps，100 万 126 fps，500 万 26 fps。[文献]
- **作者结论**：粒子法适合需要复杂行为（如风变化）的雨；恒定小雨用 Tatarchuk06 或 Wang04 的双锥滚动纹理更划算。[文献]
- **数据库版本差异**：Columbia 数据库网页（http://www1.cs.columbia.edu/CAVE/databases/rain_streak_db/rain_streak.php ，仅读到摘要）描述为 3150 张、最大 32×1050、约 50 MB，与 TOG06 论文中的 6300 张、400 MB 不一致，应以实际下载为准。

### 2.5 Rousseau, Jolivet, Ghazanfarpour：“Realistic real-time rain rendering”（C&G 2006）与 “GPU Rainfall”（JGT 2008/09）

- 预印本：https://faculty.digipen.edu/~gherron/references/References/Modeling/Realistic-real-time-rain-rendering.pdf
- GPU Rainfall：https://classes.cs.uchicago.edu/archive/2022/fall/23700-1/papers/gpu-rain.pdf
- **折射模型**：
  - 每帧从观察者位置用广角相机把场景渲染到纹理，相当于贴在雨滴后方的一个平面上。
  - 按 Chuang–Beard 形状，为几种典型直径预计算 refraction mask，mask 编码每个像素的出射方向。
  - 像素着色器按 mask 偏移查纹理。[文献]
- **视场角在两版中不同**：C&G 预印本 §4.3 为最大偏折 75°、FOV 150°；GPU Rainfall §2.2.1 改为最大偏折 82.5°、FOV **165°**，与 Garg 一致。[文献]
- **末速度表**（预印本 Table 2，按**半径**）：
  - 0.1 mm → 0.72 m/s；0.25 mm → 2.06 m/s；0.45 mm → 3.67 m/s；0.5 mm → 4.0 m/s；1.0 mm → 6.59 m/s；1.5 mm → 8.1 m/s；2.0 mm → 8.91 m/s；≥ 2.5 mm 约 9.2 m/s。
  - 与 Atlas 公式按直径换算后吻合，例如 D = 2 mm 时 Atlas 给出 6.55 m/s。[文献]
  - 注意：Lagarde 2a 写的“1 mm 约 3 m/s”与此不符，疑似混用了半径和直径。
- **条纹（视觉暂留）**：
  - 视网膜成像消退需 60–90 ms。
  - 粒子竖向拉伸，每像素平均 5–10 个采样位置的折射色，并降低 alpha（预印本 §5；GPU Rainfall）。[文献]
- **光源交互**：经验公式 `C_F = C_O·C_amb + Σ A1·(A2 + A3)`，三项分别对应直射光、外反射和内反射，支持 2 个点光。[文献]
- **粒子数量**：
  - 预印本称至少 5000 个才有真实感，半径 < 1 mm 时至少 10000 个；4000 条 r = 1 mm 的条纹在 GeForce 6800 GT 上跑 60 Hz。
  - GPU Rainfall：雨盒每轴约 100 m，10,000–20,000 滴即可；碰撞纹理 128×128，风场 64³。8600M GT 上 4 万粒子，条纹模式 59 fps，折射模式 108 fps。[文献]

### 2.6 Crytek：CryENGINE 3 / Crysis 2 / Crysis 3

- **任务书点名的两场讲座里没有雨**：SIGGRAPH 2011 “Secrets of CryENGINE 3 Graphics Technology”（https://advances.realtimerendering.com/s2011/index.html ）和 GDC 2013 “The Rendering Technologies of Crysis 3”（https://archive.org/download/crytek_presentations/Sousa_Tiago_Rendering_Technologies_of_Crysis3.pptx ）全文检索 rain、wet、puddle、splash，均零命中。
- **真正讲雨的是 FMX 2013 “The Art and Technology behind Crysis 3”**（Donzallaz & Sousa，第 114–115 页，仅读到抓取摘要）：https://www.slideshare.net/slideshow/the-art-and-technology-behind-crysis-3-fmx-2013/25052563
  - deferred rain 渲染成 box volume，用 stencil culling 或 depth bounds。
  - 组合了 mist、puddles、rain layers、water drops，外加快速屏幕空间反射近似（竖直模糊）。
  - 水洼用自上而下投影的动画纹理。
  - 雨层是以相机为中心的多个锥体，相机移动超过阈值时跳到新位置，UV 向下滚动。
  - 用 bloom source 近似雨的光散射。
- **CRYENGINE 3 Rain 实体参数**：Amount、RainDropsAmount/Speed/Lighting、PuddlesAmount、PuddlesMaskAmount、PuddlesRippleAmount、DisableOcclusion、Radius、MistAmount 等（https://cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/11240646 ）。
- **Ryse（GDC 2014）**：雨水湿润会修改 G-buffer 粗糙度，给 specular AA 带来问题（https://archive.org/download/crytek_presentations/2014_03_25_CRYENGINE_GDC_Schultz.pdf ）。

### 2.7 Sébastien Lagarde “Water drop” 系列（Remember Me，Dontnod，UE3）

实时雨领域数字最全的公开来源：
- 1 Observe rainy world：https://seblagarde.wordpress.com/2012/12/10/observe-rainy-world/
- 2a Dynamic rain and its effects：https://seblagarde.wordpress.com/2012/12/27/water-drop-2a-dynamic-rain-and-its-effects/
- 2b：https://seblagarde.wordpress.com/2013/01/03/water-drop-2b-dynamic-rain-and-its-effects/
- 3a Physically based wet surfaces：https://seblagarde.wordpress.com/2013/03/19/water-drop-3a-physically-based-wet-surfaces/
- 3b：https://seblagarde.wordpress.com/2013/04/14/water-drop-3b-physically-based-wet-surfaces/

**观察（Water drop 1）**
- 小雨时雨滴几乎看不见，只有被光照亮时才显眼。
- 夜间溅射因为背光比白天更显眼，涟漪和雨滴同理。
- 强雨下物体顶部有 misty halo，光源周围有 misty glow。[文献]

**雨幕（2a）**
- 4 层预先模糊的条纹纹理，贴在随相机移动的“半柱半锥”网格上。远层缩放更大，加周期性旋转模拟风。
- 每层用 heightmap 给出虚拟深度，做软深度测试；近两层再用溅射深度图做天空遮挡，在 1/4 分辨率下完成。
- 最终颜色 `RainColor·0.09·RainIntensity`。
- 耗时：PS3 约 0.40 + 1.29 ms。[文献]

**溅射（2a）**
- 从相机前上方沿雨方向渲染一张 256×256 正交深度图，覆盖 20 m × 20 m，每格约 7.8 cm。回读到 CPU 后在其上随机生成溅射位置，**不跟踪每个雨滴**。
- 溅射外观为缩放的 crown 网格加液滴 sprite。
- 耗时：PS3 深度图 0.32 ms，重度雨溅射 0.33 ms。[文献]

**其他**
- 镜头水滴是挂在相机上的屏幕空间粒子。
- 雨相关效果合计约 2.8 ms（1280×720），作者建议预算约 3 ms。[文献]
- 雨强预设（雨强 / Number splashes / 雾量）：Light 0.33 / 20 / 0.15；Moderate 0.66 / 40 / 0.4；Heavy 1.0 / 60 / 1.0（2b；溅射数单位原文未注明；涟漪常数原文自称“just visually tweak value”）。[文献]

### 2.8 其他游戏与引擎（一手资料检索结果）

- **Far Cry 6**（GDC 2022 “Simulating Tropical Weather”）：
  - 育碧官方文章（https://www.ubisoft.com/en-us/company/how-we-make-games/technology/articles/simulating-tropical-weather-in-far-cry-6 ）：
    - 湿度分 static 和 dynamic。static 受 wetness shadow map 遮挡；dynamic 用于武器、载具、角色，靠射线检测雨暴露。
    - 水洼是地形 decal，用 alpha 渐变当伪 SDF 从中心生长，并写入地形 porosity；表面带程序化的雨和风涟漪。
  - 第三方转录（二手，https://delosadajoaquin.wixsite.com/gdctalkstotext/post/talk-title-simulating-tropical-weather-in-far-cry-6-part-2 ）：
    - 试过 blur、折射、反射后，最终用透明纹理 GPU 粒子在玩家周围复用，加 3D 噪声。
    - rain shadow map 把室内的雨排除掉。
    - 溅射先是碰撞事件，后改为在玩家附近额外生成。
- **Ghost of Tsushima**（SIGGRAPH 2021 Patry，https://www.advances.realtimerendering.com/s2021/jpatry_advances2021.pdf ）：只提到 haze 用 rain shadow map 防止室内过亮。雨粒子和湿表面：未找到一手资料。
- **Red Dead Redemption 2**（SIGGRAPH 2019，https://advances.realtimerendering.com/s2019/slides_public_release.pptx ）：
  - rain fog 由 cloud map 密度推出，带高度衰减。
  - “water droplet density” 参数驱动彩虹强度。
  - 雨粒子：未找到一手资料。
- **Horizon Zero Dawn**（SIGGRAPH 2015 云讲座）：weather map 的 G 通道为 precipitation，把云过渡为 70% 覆盖的积雨云并触发雨效。雨的渲染：未找到一手资料。
- **以下均未找到一手资料**：The Order: 1886（SIGGRAPH 2015 幻灯片零命中）、Watch Dogs、Frostbite/Battlefield、Assassin's Creed Unity/Syndicate、Marvel's Spider-Man（GDC 2019 postmortem 摘要无雨）、GTA V、Detroit: Become Human、UE5 City Sample / Matrix Awakens（官方文档 rain/wet/puddle 零命中）。
- **AC3**：GDC 2013 “Rendering Assassin's Creed III” 摘要提到 weather system，幻灯片需会员才能查看；Lagarde 3b 转述 AC3 按雨型改变湿度系数（二手）。

---

## 3. 次级真实感层

### 3.1 溅射 (splash)

**Garg, Krishnan, Nayar, “Material Based Splashing of Water Drops”（EGSR 2007）**：https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_EGSR07.pdf

- **两种溅射** [文献]：
  - corona（皇冠）：一圈薄水膜竖起来后再破碎成液滴，需要表面有薄水层且倾角低。
  - prompt：液滴直接从雨滴底部射出，没有明显皇冠。其他情况都属于这种。
- **皇冠时长**：“Typically, a crown lasts for 10–20 ms”。渲染时 crown 的高度和半径随时间线性缩放；运动模糊后在 24/30 fps 下只出现在 1–2 帧内。[文献]
- **影响因素**：
  - 溅出液滴数随冲击速度的**平方**增长；雨滴变大只会让溅出的液滴变大。
  - 粗糙度是决定是否溅射的关键因素。
  - 测了 22 种材质、1320 次溅射。[文献]
- **统计模型**：数量和速度用 Gaussian，仰角用 GEV，方位角均匀分布。正面冲击需要 7 个系数，任意角度需要 54 个。[文献]
- **示例数值**（木材，正面冲击，半径 1 mm 的水滴从 5 m 落下，图 7）：
  - 溅出液滴数 N：μ = 73.3，σ = 9.78。
  - 速度 v：μ = 2.08 m/s，σ ≈ 0.28。
  - 仰角 θ：μ = 67.9°，σ ≈ 11.9。
  - [文献] 可以直接当作 Niagara 溅射的初始分布。
- **飞溅判据**：`K = We·√Re`，K 超过约 3000 时可以预期发生溅射（Josserand & Thoroddsen, Annu. Rev. Fluid Mech. 2016，https://yakari.polytechnique.fr/people/josserand/papier/annrev.pdf ）。[文献]
- **对冻结镜头**：片中“停雨”时没有溅射。如果要做“雨落回水坑”的收尾，溅射应按上述分布在 1–2 帧内完成。Lagarde 引用 corona 持续 10–20 ms，与 Garg 一致。

### 3.2 地面涟漪

- **物理**：
  - 毛细–重力波的最小相速约 0.23 m/s，对应波长约 1.7 cm，`c_min = (4gγ/ρ)^(1/4)`（https://arxiv.org/pdf/cond-mat/9909148 ）。最小群速约为 0.77·c_min（https://arxiv.org/pdf/1406.0422 ）。[文献]
  - 雨滴激发的是以 k_m 为主的多重环，振幅向中心增大，尾波按指数衰减，振幅与冲击动量成正比（Le Méhauté 1988 JFM 摘要）。[文献，仅摘要]
  - 雨滴涟漪的衰减时间：未找到一手资料。
- **图形学做法**：
  - ToyShop：256×256 波动方程，见 2.3 节。
  - Lagarde 2b：程序化圆环纹理。R 通道存到圆心的反距离，GB 存方向，A 存时间偏移。公式 `sin(clamp(TimeFrac·9, 0, 3)·π)`，振幅 0.35。4 层，TimeMul = (1, 0.85, 0.93, 1.13)，TimeAdd = (0, 0.2, 0.45, 0.7)，整体 ×1.6。每 0.25 雨强开启一层，纹理 256×256。只有积水处才有涟漪，而且要有亮光才看得见。[文献]

### 3.3 湿表面与积水（Lagarde 2b / 3a / 3b）

- **光学常数**：水的 F0 ≈ 0.02，IOR 1.33，水吸收 α ≈ (0.35, 0.04, 0) m⁻¹，薄水层的吸收可以忽略。F0 从 0.04 变到 0.02 在游戏里看不出来。[文献]
- **简单版**：`Diffuse *= lerp(1, 0.3, WetLevel)`；`Gloss = min(Gloss·lerp(1, 2.5, WetLevel), 1)`。[文献]
- **PBR 版（按孔隙率）**：
  - `factor = lerp(1, 0.2, Porosity)`
  - `Diffuse *= lerp(1, factor, WetLevel)`
  - `Gloss = lerp(1, Gloss, lerp(1, factor, 0.5·WetLevel))`
  - 没有孔隙率贴图时用 `Porosity = saturate(((1−Gloss)−0.5)/0.4)` 估计；金属乘 (1−Metalness) 排除。[文献]
- **孔隙率参考**：瓷砖 35%，混凝土 25%，石灰岩 30–45%，沙 25%，黏土 40–50%，白垩 30–40%。Lekner–Dorf 模型给出沥青干反照率 0.12，湿后约 0.08（×0.68）。[文献]
- **积水**：用 heightmap 或顶点色，配合两个 FloodLevel（水洼和缝隙），过渡宽 0.4。积水处 `Gloss → 1`，法线 `N → (0,0,1)`。[文献]
- **干燥**：过程呈 sigmoid、不均匀，高光先于漫反射消失，小坑和缝隙先干。游戏里约 1 分钟 lerp 干即可，水洼要更慢。与雨平行的墙面几乎不湿。[文献]
- **其他引擎**：
  - Far Cry 6：wetness shadow map 加地形 decal 水洼，写入 porosity。
  - Ryse：湿润修改粗糙度，会影响 specular AA。
  - 来源见 2.6、2.8 节。
- UE 侧可参考 Ultra Dynamic Weather 的材质函数（Surface Weather Effects、Dynamic Landscape Weather Effects、Water Surface Rain Ripples 等，https://www.ultradynamicsky.com/Documentation/V9/9-5 ）。

### 3.4 远处雨雾（rain haze）

- **远雨如雾**：远处雨滴的投影远小于一个像素，像素强度来自聚合散射，“similar to steady weather (fog)”（Garg & Nayar IJCV07 脚注 18、20）。[文献]
- **消光与雨强**：
  - 实测拟合（R > 10 mm/h）：σ = 0.11·R^0.88 和 σ = 0.12·R^0.81（σ 单位 km⁻¹）。
  - Koschmieder 能见度 MOR ≈ 3/σ。实测中 MOR < 1 km 对应 R > 60 mm/h。
  - 来源：Montero-Martínez & García-García 2025，https://www.scielo.org.mx/scielo.php?script=sci_arttext&pid=S0187-62362025000100020&lng=en&nrm=iso&tlng=en 。[文献]
  - [推导] R = 10 mm/h 时 σ ≈ 0.83 km⁻¹，MOR ≈ 3.6 km；R = 50 mm/h 时 σ ≈ 3.5 km⁻¹，MOR ≈ 0.86 km。
  - 也就是说，**纯雨本身的消光很弱**。镜头里常见的“雨中灰蒙”主要来自伴随的雾和湿气、溅起的水雾，以及 bloom 和光晕。Lagarde 也是“雨越大雾越浓”，雾色向去饱和插值。
- **UE 落地**：Exponential Height Fog 或 Volumetric Fog 的密度随雨强提高，雾色去饱和。[美术取值]

### 3.5 路灯周围的光晕 (halo / glow)

- **物理**：Narasimhan & Nayar, “Shedding Light on the Weather”（CVPR 2003，https://www.cs.columbia.edu/CAVE/publications/pdfs/Narasimhan_CVPR03.pdf ）给出多重散射的大气点扩散函数 (APSF)，基于 Henyey–Greenstein 相函数。
  - 光学厚度 T = σ·R。T ≤ 1 时多重散射弱，看不到光晕。
  - 前向散射参数 q：气溶胶约 0.2，霾约 0.75，雾约 0.9；雨处在 q 轴的高端（强前向散射，光晕较窄）。
  - 文中用实测 APSF 渲染了 “heavy mist and rain” 中的光源光晕。[文献]
- **工程做法**：
  - Garg TOG06：条纹乘 1/d² 遮罩。
  - ToyShop：Kawase bloom，雾量叠加进 glow。
  - Crysis 3：用 bloom source 近似雨的散射。
  - NVIDIA：Sun05 解析单次散射。
  - Lagarde：建议加大 bloom 核。[文献]
- **UE 落地**：
  - 雨条纹和水滴材质里按到最近光源的距离做 `1/d²` 增亮。Niagara 中可以用 User Parameter 传入关键灯的位置和颜色。
  - 光源本身加 Volumetric Fog 的 Scattering Intensity 和 Light Shafts。
  - 后处理 Bloom 用 Convolution，核略大。[美术取值]

---

## 4. 冻结状态：悬停水滴应该长什么样

### 4.1 目标镜头本身（Now You See Me 2, 2016）

- **供应商**：雨停镜头由 **Framestore**（伦敦）制作，是该公司全片 200 多个镜头中的一部分。
  - 人员：VFX Supervisor Stephane Nazé 与 Anthony Smith；Head of FX Andy Hayes；CG Supervisor Stefan Putz；Compositing Supervisor Alex Payman。
  - 全片其他供应商有 BlueBolt、Cinesite、Blind Ltd，Production VFX Supervisor 为 Matt Johnson。
  - 来源：https://www.framestore.com/work/now-you-see-me-2 ，https://www.artofvfx.com/now-you-see-me-2/ ，https://www.cgw.com/Press-Center/Web-Exclusives/2016/Conjuring-VFX-Magic.aspx
- **Framestore 的原话** [文献]：
  - 镜头内容："…shows magician Daniel Atlas (Jesse Eisenberg) stopping rain in mid-air, moving the droplets around with some deft hand movements…"，背景是格林尼治的真人观众。
  - 景深的处理："We experimented with various levels of defocus to get the right look, intentionally deviating from the real world camera settings when required to make sure that the foreground droplets weren't distracting for the viewer."
  - 悬停行为的研发："lots of development on how the rain drops should behave while they're hanging in the air, especially given that the magical effect had to be based on a physically plausible idea which is explained in the film"
  - 渲染方式："the millions of beautifully refracting droplets were rendered with deep data"。合成部门借此逐镜头调整人群层前方雨的数量，并在合成阶段生成景深和 bokeh。
  - 停雨与手势："The team spent a lot of time on how the rain should look when it comes to a stop, and just how much influence Atlas' movements should have on the rain surrounding him."
- **片内给出的解释**：Atlas 事后称雨停是用 rain machine 加频闪 (strobe lights) 做到的。影迷还注意到“停雨”期间地面和收起伞的人身上都没有水花（https://www.moviemistakes.com/film11465 ）。这对应 Framestore 说的 “physically plausible idea which is explained in the film”。
  - 频闪的物理含义：每颗水滴被极短的闪光冻结成一个明亮、不透明的点，而不是一条条纹（见 1.6 节 Garg 的 1 ms 实验）。
- **只有二手来源的情节细节**：雨滴最后“倒升回天空”，出自影评公开信（http://www.dearcastandcrew.com/content/2016/6/21/now-you-see-me-2.html ）和 Empire 探班摘要（https://www.pressreader.com/uk/empire-uk/20160331/281505045336663 ）。
- **未找到一手资料**：所用软件、具体水滴数量（只有 “millions”）、快门参数、减速曲线时长、布光。
- **对本项目的直接启示**：
  1. 冻结雨的数量级是“数百万颗折射水滴”。
  2. 景深和 bokeh 是关键，而且可以**故意偏离真实相机参数**，避免前景水滴分散注意力。
  3. 手势的影响半径需要专门设计。
  4. “频闪冻结”给了物理合理的外观依据：明亮、清晰、不透明的小球。

### 4.2 高速摄影参考

- Edgerton 的牛奶皇冠照片曝光为 1/10,000 s，“the light itself essentially acts as a shutter”（https://www.sciencefriday.com/articles/picture-of-the-week-milk-drop/ ）。
- 闪光灯低功率时的闪光时长（Nikon SB-800 规格）：M1/128 为 1/41600 s，M1/64 为 1/32300 s，M1/32 为 1/17800 s（以 t.5 计）。来源：https://scantips.com/speed.html
- 佳能摄影指南（摘要）建议用 1/8000 s 冻结雨滴，背光时雨最明显（https://snapshot.canon-asia.com/article/en/how-to-take-beautiful-photos-in-the-rain ）。
- [推导] 2 mm 雨滴 (v ≈ 6.5 m/s) 在 1/10000 s 内移动约 0.65 mm，即约 1/3 个直径，基本冻结。在 1/41600 s 内移动约 0.16 mm，完全冻结。
- **冻结水滴的外观要素** [文献]：
  - **形状**：半径 < 1 mm 为球形，更大的呈汉堡包上半个面包形，半径超过约 4.5 mm 变成降落伞状后破碎（USGS，https://www.usgs.gov/water-science-school/science/are-raindrops-shaped-teardrops ）。
  - **倒像**："the appearance of the drop is dominated by refraction as is evidenced by the inverted view of the environment"（Garg TR04，https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_TR04.pdf ）。
  - **镜面高光**：晴天时，太阳在水滴表面的镜面反射对亮度贡献显著（IJCV07 注 8）。
  - **暗边**：背光阴影成像中，“the light refracted at the interface is dispersed and thus the interface appears dark”（https://www.redalyc.org/pdf/570/57020263016.pdf ）。
    - [推导] 暗边并非水滴固有属性。边缘像素折射的是偏离视线约 83° 的侧向环境，而且 Fresnel 透射率在掠射角下降，所以只有侧向环境较暗时（典型如影棚背光、夜景）才会出现明显暗环。
    - 这与“水滴通常比背景亮”并不矛盾：中心亮、边缘暗，是同一套映射造成的。
  - **振荡**：低分辨率下几乎看不出，只在近景加复杂光照时明显（IJCV07 注 1）。

### 4.3 球透镜光学细节（补充 1.7 节）

- **水的色散**：Daimon & Masumura 2007，20 °C，取自 https://refractiveindex.info/database/data/main/H2O/nk/Daimon-20.0C.yml 。[推导] 由该数据计算各波长的 n：

| 波长 (nm) | 400 | 450 | 486 | 546 | 589 | 656 | 700 |
|---|---|---|---|---|---|---|---|
| n | 1.3436 | 1.3396 | 1.3375 | 1.3348 | 1.3334 | 1.3315 | 1.3305 |

- **Fresnel F0 = 0.02**（线性），sRGB 下为 0.15（Hoffman, SIGGRAPH 2013 PBS Course，https://blog.selfshadow.com/publications/s2013-shading-course/hoffman/s2013_pbs_physics_math_notes.pdf ）。
- **视场推导**：把 θn = 90° 代入 IJCV07 式 (7)，`θr = 2(π−θn) − 2·asin(sin θn/μ)`，得 θr,max ≈ 82.8°，总视场 ≈ 165.6°，与 Garg 的 “约 165°” 一致。[推导]
- **两个高光点 (glare points)**：
  - 定义：van de Hulst & Wang 1991（https://pubmed.ncbi.nlm.nih.gov/20717278/ ）。
  - Sax et al. 2023（https://arxiv.org/pdf/2303.16013 ）：p = 0 是表面反射，p ≥ 1 是折射，其中经过 p−1 次内反射。
    - p = 0 与 p = 1 这一对只在散射角 θ ≤ 2·arccos(1/m) ≈ 82.8° 内存在，约 66° 时两者强度相等。
    - 后向散射时主导的是 p = (0, 2) 这一对。
  - 结论：**一个点光源通常在水滴上产生两个亮点**，一个是前表面反射，另一个是透射或内反射。不要只做一个 Phong 高光。
- **彩虹色闪光**：
  - 主虹 40°–42°（距反日点），副虹约外移 10°，宽度约两倍，强度约为主虹的 1/10，色序相反（HyperPhysics，http://hyperphysics.gsu.edu/hbase/atmos/rbowpri.html ）。
  - 每颗雨滴只贡献一种颜色：红光在 42°，蓝光在 40.6°（https://www.asu.edu/courses/phs208/patternsbb/PiN/rdg/rainbow/rainbow2.shtml ）。
  - [推导] 单颗水滴只有在**光源位于相机背后**、且水滴处于离反日点约 40–42° 的环带上时，才会闪出彩色。其他几何下是白色闪点。
  - 毫米级雨滴不产生 glory，glory 来自约 10 µm 的小雾滴（Laven，https://philiplaven.com/Publications/AO-44-27-p5675.pdf ）。

### 4.4 不同距离上的 2 mm 水滴（35 mm 镜头 / Super35 / 4K）[推导，见 1.6 节表格]

| 距离 | 像宽 | 应该看到的内容 | 推荐渲染方式 |
|---|---|---|---|
| 0.1–0.5 m（通常焦外） | 20–70 px，焦外可达数百 px 光斑 | 焦外：带猫眼和洋葱纹的光斑，亮度由点光源高光主导。焦内：清晰倒像、两个高光点、暗边 | 焦内用真正的折射网格或球体 impostor（解析球折射）。焦外交给 DOF，光斑亮度由高光贡献 |
| 1 m | ≈11 px | 能分辨出“小鱼眼倒像”。日景：天空被倒到水滴下半部（下亮上暗）。夜景：路灯和霓虹被倒成若干小亮点，整体偏暗。此外有锐利高光，边缘略暗或略亮 | 解析球折射 sprite（ray-sphere）加 cubemap 环境查找 |
| 2–5 m | 2–5 px | 一个亮点加一点暗边，倒像细节基本丢失 | 简化 sprite：平均环境亮度加高光点 |
| 10 m | ≈1 px | 只有闪烁的亮点（光源附近才明显） | 点状 sprite，亮度按光源距离和角度计算 |
| 20 m 以上 | < 0.5 px | 单滴不可见，整体呈雾状提亮或幕状光晕 | 体积雾或高度雾的密度略增，再加光源光晕 |

- **焦外亮度稀释** [推导，基于 IJCV07 式 (23)]：静止水滴散焦后，对比度约降为 `(w/(w+b_c))²`。例如 2 px 的水滴散成 20 px 的光斑，只剩约 0.8%。所以**只有点光源的高光能在焦外留下明显的光斑**，这与 Framestore 用 deep data 在合成里做景深和 bokeh 一致。
- **“牛奶雨”**：Tatarchuk 2006 发现物理准确的雨 “appears too faint”，于是借用好莱坞往水里加牛奶的老办法，把颜色和不透明度往白偏（https://advances.realtimerendering.com/s2006/Tatarchuk-Rain.pdf ）。[美术取值] 下落状态可以适度采用，冻结状态不建议使用，因为冻结水滴的卖点是透明的折射感。
- **电影实拍的布光共识**：
  - 雨要靠**逆光**才能被看见，正面光会让雨“消失在背景里”（Filmmaking 教材，Apocalypto 案例，https://digfir-published.macmillanusa.com/filmmaking1e/filmmaking1e_ch9_8.html ）。
  - David Mullen ASC 的经验：“The rain closest to the light source will look the brightest”；背景越暗，雨越明显；避免以天空为背景（CML，https://cinematography.net/edited-pages/Photographing_Rain.htm ）。
  - 反例：《Hard Rain》的摄影师刻意少用逆光，以免显得“舞台化”（https://theasc.com/magazine/jan98/flood/pg1.htm ）。

### 4.5 如何高效渲染大量折射水滴（学术方案 + UE 5.7 能力）

**学术与业界方案**
- **Garg TR04 §7**：用球面全景环境图，加上水滴的几何映射与传递函数，逐像素渲染水滴，原文称可用于 “fast rendering … large number of water drops”（https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_TR04.pdf ）。这是本项目首选方案的理论基础。
- **Wang et al., MSR-TR-2006-102**（https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/RealTimeRain_MSTR.pdf ）：
  - 球形雨滴的传递函数有解析闭式，用 PRT 计算，取 16 个 SH 系数，预计算 98,304 个视向。
  - 把饱和的光源像素替换为实测辐亮度。
  - 用 `C′ = C + c·B` 补偿低频损失。
- **Rousseau, Jolivet, Ghazanfarpour 2006**（Computers & Graphics 30(4):507–518）：把场景捕获到纹理，再按水滴的光学畸变贴到每颗水滴上（仅读到摘要；“165° 纹理”的说法未在其原文中核实到，165° 的一手出处是 Garg）。
- **Slomp et al. 2010/2011**（CAVW 22:393–404，DOI 10.1002/cav.421）：分层的反射和折射图贴在 billboard 上，用 mip 做距离过渡（摘要）。
- **球体 impostor**：在 sprite 的像素着色器里做解析 ray–sphere 求交，透视正确。
  - Sigg et al. 2006，SPBG：https://diglib.eg.org/handle/10.2312/SPBG.SPBG06.059-065
  - iq 的 `sphIntersect`，可同时返回入射点和出射点的 t：https://iquilezles.org/articles/intersectors/
  - GPU Gems 3 第 21 章 True Impostors：https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-21-true-impostors
- **Heartfelt（BigWings，Shadertoy ltffzl）**：模拟的是玻璃上的水滴，做法是 2D 屏幕空间法线偏移加 mip 模糊。**许可为 CC BY-NC-SA 3.0，不可商用**，而且不是球体折射，只适合“镜头上的雨”。

**UE 5.7 原生能力**（来源均为 5.7 文档；凡标“推论”的条目是综合判断）
- **光栅化折射**：材质 Refraction Method 有 `Index of Refraction` / `Index of Refraction from F0` / `Pixel Normal Offset` / `2D Offset` / `None`（https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/RefractionMode?application_version=5.7 ）。它本质上是**屏幕空间**的，看不到屏幕外或被遮挡的内容，正好撞上 165° 视场的问题。
- **半透明光照模式**（https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/TranslucencyLightingMode?application_version=5.7 ）：
  - `Surface ForwardShading` 逐光源逐像素计算，支持局部光的高光，适合水滴的点光高光，但最贵。
  - `Surface TranslucencyVolume` 只有漫反射。
  - 反射方面：TLM_Surface 只取单个反射捕获（https://dev.epicgames.com/documentation/en-us/unreal-engine/lit-translucency-in-unreal-engine?application_version=5.7 ）。
- **Substrate**：
  - 5.7 发行说明称 Substrate 已 production-ready，并对新项目默认启用；但 Path Tracer 和 HWRT 支持表仍标为 experimental。**文档自相矛盾，需要实测**。
  - 支持 Rough Refraction（项目开关 “Substrate translucent material rough refraction”），代价是 distortion pass 更贵。
  - https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-substrate-materials-in-unreal-engine?application_version=5.7
- **Lumen**：High Quality Translucency Reflections 为最前层半透明提供镜面反射（https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine?application_version=5.7 ）。
- **硬件光追半透明在 5.7 仍然存在**：后处理里 Translucency Type 可选 Ray Tracing，Max Refraction Rays 默认 3（https://dev.epicgames.com/documentation/en-us/unreal-engine/ray-tracing-and-path-tracer-features-properties-in-unreal-engine?application_version=5.7 ）。
  - HWRT 对 Niagara 是 “Partially – Currently supports Sprites, Ribbons, and Mesh emitters”（https://dev.epicgames.com/documentation/en-us/unreal-engine/hardware-ray-tracing-in-unreal-engine?application_version=5.7 ）。
- **Path Tracer**（https://dev.epicgames.com/documentation/en-us/unreal-engine/path-tracer-in-unreal-engine?application_version=5.7 ）：
  - 支持 Niagara。实体玻璃的设置是 Translucent + Surface Forward Shading + Refraction Method = Index of Refraction。
  - 原文提醒 “if the Refraction Method is not set to Index Of Refraction, the path tracer will use transparency instead of refraction”。
  - Thin Translucent 在低粗糙度下不会改变光线方向。
  - 推论：**只有带厚度的 Mesh 粒子（球或 Beard–Chuang 网格）配 IOR 材质，才能在路径追踪里得到真实的球透镜倒像**；扁平 sprite 做不到。

**推荐的分层方案** [推导 + 美术取值]：

| 层 | 距离（35 mm / 4K） | 表示 | 折射方式 | 说明 |
|---|---|---|---|---|
| Hero 层 | < 1.5 m，几百到几千颗 | Niagara Mesh 粒子（低模球 80–200 tri，或按 Beard–Chuang 系数做的网格，D > 2 mm 时略扁） | 实时：材质里用解析球折射算出射方向，查本地 cubemap（SceneCapture Cube 放在雨区中心）；离线：Path Tracer 真实 IOR 1.333 | 手势交互的主角。必须有两个高光点和边缘 Fresnel |
| 中景层 | 1.5–8 m，几万到几十万颗 | Sprite + 像素着色器球体 impostor（ray–sphere） | 同一个 cubemap 查找；可选与 SceneColor 混合 | 写入深度，便于 DOF |
| 远景层 | 8–30 m | 小 sprite（1–3 px），亮度 = 平均环境亮度 + 高光点 | 不做折射 | 靠近光源时提亮 |
| 雾层 | > 30 m | 体积雾 / 高度雾密度略增 | — | 对应 Garg 所说的“远雨如雾” |

- **为什么首选“解析球折射 + cubemap”**：
  - 它与 Garg 的 165° 视场物理一致。
  - 不受屏幕边界限制。
  - 成本与粒子数量线性相关，开销小。
  - 屏幕空间的 IOR 折射可以作为中心小角度区域的叠加项，补上“背后物体在水滴里的倒影”细节，但不能单独使用。
- **离线终版**：Framestore 用的是 deep data 加合成 DOF。UE 中对应的做法是 MRQ/MRG 输出多通道，或者直接用 Path Tracer 渲染 Mesh 水滴加 Reference Motion Blur。

---

## 5. UE 5.7 Niagara 实践：构件、坑点与资源

> 所有 Epic 链接都带 `?application_version=5.7`。不带版本参数的 Python API 页面现在默认指向 5.8。

### 5.1 下落条纹（Sprite）
- **Alignment**：`Velocity Aligned` 让贴图的“向上箭头”指向 `Particles.Velocity`；`Custom Alignment` 指向 `Particles.SpriteAlignment`。在 Velocity/Custom 模式下，除非设置了 CustomFacingVector，否则 FacingMode 会被忽略（https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/NiagaraSpriteAlignment?application_version=5.7 ）。
- **Facing Mode**：可选 `FaceCamera` / `FaceCameraPlane` / `CustomFacingVector` / `FaceCameraPosition` / `FaceCameraDistanceBlend`（https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/NiagaraSpriteFacingMode?application_version=5.7 ）。
- **Scale Sprite Size by Speed**（Particle Update）：用速度模长缩放 SpriteSize（https://dev.epicgames.com/documentation/en-us/unreal-engine/particle-update-group-reference-for-niagara-effects-in-unreal-engine?application_version=5.7 ）。
- 材质必须勾选 **Use with Niagara Sprites**。Sub UV 用 `Sub Image Size` 加 `Sub UV Blending Enabled`。Cutout（4/8 顶点包围）可以减少细长条纹的 overdraw（https://dev.epicgames.com/documentation/en-us/unreal-engine/render-module-reference-for-niagara-effects-in-unreal-engine?application_version=5.7 ）。
- **⚠ 减速时的退化**（推论）：速度趋近 0 时 Velocity Aligned 的方向会退化、乱转，按速度拉伸的长度也会塌成 0。正确做法：
  - 生成时把下落方向写入 `Particles.SpriteAlignment`，改用 `Custom Alignment`。
  - 条纹长度用 1.6 节公式 `L = v·T + D` 由自定义参数驱动；`v` 用“名义速度”，不受 Drag 噪声影响。
  - 亮度和不透明度乘以 `D/(v·T + D)`，保持能量守恒。

### 5.2 条纹到水滴的交接
- Sprite 和 Mesh 渲染器都支持 `renderer_visibility_tag_binding`（“particles whose tag matches this value will be visible in this renderer”）（https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/NiagaraMeshRendererProperties?application_version=5.7 ）。
- 推论：同一个发射器上挂两个渲染器，Sprite 负责条纹，Mesh 负责水滴。速度降到 `v·T ≈ D` 附近时切换每个粒子的 visibility tag。粒子 ID 和位置保持连续，不会“跳帧”。
- Mesh Renderer Facing Mode 有 `Default` / `Velocity` / `Camera Position` / `Camera Plane`，材质需勾选 **Niagara Mesh Particles**。

### 5.3 相机跟随雨体积与预算
- 用 **Box Location** 生成，**Kill Particles in Volume** 回收（文档注明需要 Interpolated Spawn；5.7 中该属性已改为枚举 `interpolated_spawn_mode`，旧布尔 `interpolated_spawning` 已弃用）。另有 Camera Offset 模块。来源：
  - https://dev.epicgames.com/documentation/en-us/unreal-engine/particle-spawn-group-reference-for-niagara-effects-in-unreal-engine?application_version=5.7
  - https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/VersionedNiagaraEmitterData?application_version=5.7
- GPU 发射器 **必须设 Fixed Bounds**（https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-a-gpu-sprite-effect-in-niagara-for-unreal-engine?application_version=5.7 ）。每帧 GPU 生成数受 `max_gpu_particles_spawn_per_frame` 和 `fx.MaxNiagaraGPUParticlesSpawnPerFrame` 限制。文档建议避免使用 Warmup（https://dev.epicgames.com/documentation/en-us/unreal-engine/scalability-and-best-practices-for-niagara?application_version=5.7 ）。
- Epic 官方的粒子总数上限：未找到一手资料（SideFX 论坛转述为 CPU 1M / GPU 2M，非官方）。
- **预算**（35 mm / Super35 16:9 视锥，只计 D > 0.5 mm）[推导]：

| 视锥深度 | 视锥体积 | R=5 mm/h | R=25 mm/h |
|---|---|---|---|
| 5 m | 11.9 m³ | 7.5k | 16k |
| 10 m | 95 m³ | 60k | 128k |
| 20 m | 759 m³ | 481k | 1.03M |
| 30 m | 2560 m³ | 1.62M | 3.46M |

  - 生成速率等于“通量 × 盒子顶面积”。生成时的尺寸要按通量 `N(D)·v(D)` 加权，见 6.1 节。例如 R = 10 mm/h 时，D > 0.5 mm 的通量约 3114 个/m²/s，10 m × 10 m 的顶面需要约 31 万个/s。
  - 粒子寿命 ≈ 盒高 / v。
  - 结论：真实密度的粒子只铺在视锥 10–20 m 以内（几十万颗），更远处交给“雨幕”层（大 sprite 或滚动纹理）和雾。
  - 远景用雨幕层是三个项目的共同做法：ToyShop 的屏幕空间多层合成（2.3 节）、Crysis 3 的相机中心锥体层（2.6 节）、Remember Me 的 4 层“半柱半锥”网格（2.7 节）。
  - 交叉验证：20 m 视锥、R = 10 mm/h 时，D > 0.5 mm 的雨滴约 68 万颗；30 m 时约 230 万颗。这与 Framestore 所说的冻结雨中有 “millions of … droplets” 在同一数量级。

### 5.4 碰撞与溅射
- **Collision 模块**：CPU 发射器用射线检测，GPU 发射器用 Scene Depth 或 Global Distance Field。
  - 深度缓冲碰撞“low-cost, but also low-accuracy”，粒子离屏后就会失效。
  - 5.7 有 **GPU Ray Traces (Experimental)**，需要 DX12 并开启 Support Hardware Ray Tracing，结果晚一帧；可在 Project Settings > Plugins > Niagara > Async Gpu Trace DI 里设回退顺序（HWRT → Global SDF）。
  - https://dev.epicgames.com/documentation/en-us/unreal-engine/gpu-raytracing-collisions-in-niagara-for-unreal-engine?application_version=5.7
- **事件只支持 CPU 模拟**（“events with GPU simulations will not work”）（https://dev.epicgames.com/documentation/en-us/unreal-engine/events-and-event-handlers-in-niagara-effects-for-unreal-engine?application_version=5.7 ）。
- GPU 方案：
  - Particle Attribute Reader，Epic 称“generally more performant than events”。
  - **Niagara Data Channels**：5.4 为 Experimental，并新增 `GetNDCSpawnData`，解除每帧 8 个的限制；5.5 起 Production Ready；5.7 新增 Access Contexts。来源：
    - https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-data-channels-overview?application_version=5.7
    - https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-5-release-notes?application_version=5.5
  - GPU 发射器能否写入 NDC：未找到一手资料。一篇第三方 5.3 教程称必须用 CPU 发射器（https://www.ragataga.com/vfx/using-unreals-niagara-data-channel ）。
- **对冻结镜头的意义**（推论）：冻结时没有落地，也就没有溅射，这与片中“停雨期间无水花”的设定一致。溅射只在下落段需要。
  - 溅射可以先用“地面随机生成，密度按雨强算”的独立发射器，不依赖逐粒子碰撞。Remember Me 是在顶视深度图上随机生成（2.7 节），Far Cry 6 最终改为在玩家附近额外生成（2.8 节）。ToyShop 则是粒子碰撞代理体后生成（2.3 节）。

### 5.5 遮雨（屋檐、伞下）
- 官方教程：未找到一手资料。
- 社区方案：高空 SceneCapture2D 拍顶视深度，材质或 Niagara 用 Render Target 2D DI 比较高度后杀粒子（https://forums.unrealengine.com/t/updated-for-4-20-1-let-it-rain-but-not-indoors/31124 ）。
- 商业插件 UDW 的做法：Simple Collision 在生成帧做一次 CPU 线检测；Distance Field Collision 在 GPU 上采样全局 SDF；`Weather Occlusion Volume` 用全局 DF 缓存天空暴露度（https://www.ultradynamicsky.com/Documentation/V9/9-5 ）。
- CryEngine 3 和 Frostbite 的顶视遮挡图方案见第 2 节。

### 5.6 时间控制：减速 → 冻结 → 被手拨动
- **Drag** 模块直接作用于速度，与质量无关（Particle Update 参考）。
- **Life Cycle Track** 的 Age Update Mode 选 `Desired Age`，Epic 原话 “In almost all scenarios, the Desired Age mode is preferred”（https://dev.epicgames.com/documentation/en-us/unreal-engine/controlling-your-niagara-systems?application_version=5.7 ）。
- **确定性**：发射器开 Determinism 并设 Random Seed，组件可用 Random Seed Offset。User Parameter 可以在 Sequencer 里 K 帧。
- **Niagara Sim Cache**：
  - 5.2 起可从 Sequencer 录制，需要启用 Niagara Sim Caching 插件。
  - MRQ 做时间子采样时，点类模拟可选 Interpolation 或 Extrapolation，两者都勾时 Interpolation 优先。
  - 5.7 增强为支持所有 float 类型的插值。
  - 来源：
    - https://dev.epicgames.com/documentation/en-us/unreal-engine/caching-and-reusing-your-system?application_version=5.7
    - https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-7-release-notes?application_version=5.7
- **组件 API**：`set_custom_time_dilation`、`set_paused`、`advance_simulation_by_time`、`set_desired_age`、`set_variable_float` 等（https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/NiagaraComponent?application_version=5.7 ）。
- **推荐**（推论 + 美术取值）：
  - 用一个 User Parameter `TimeScale`（1 → 0）乘到速度积分上。不要用全局 time dilation，否则手势交互也会被冻住。
  - 手势影响：每个粒子按到手部骨骼的距离计算一个衰减场，叠加速度，然后施加强阻尼。
  - 终版录制成 Sim Cache，再用 Interpolation 配合 MRQ/MRG 的时间采样。

### 5.7 运动模糊与离线渲染
- **半透明默认不写速度**（TSR 文档原话：半透明 “never draw velocities”）。材质 Translucency 分类可勾选 **Output Depth and Velocity**（API 属性 `output_translucent_velocity`）。半透明默认在 After DOF pass 渲染。
  - https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine?application_version=5.7
  - https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/Material?application_version=5.7
- Niagara 半透明 sprite 开启后是否参与后处理运动模糊：Epic 没有明说（未找到一手资料）。
- Sprite 和 Mesh 渲染器的 `motion_vector_setting` 可选 AutoDetect / Precise / Approximate / Disable（https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/NiagaraRendererMotionVectorSetting?application_version=5.7 ）。
- **MRQ/MRG 时间采样**（https://dev.epicgames.com/documentation/en-us/unreal-engine/cinematic-rendering-image-quality-settings-in-unreal-engine?application_version=5.7 ）：
  - 按快门开启时间把一帧切成若干片，每片 tick 一次引擎。文档建议把样本优先分配给时间采样。
  - 开 TAA 时，唯一的抖动位置数受 `r.TemporalAASamples`（默认 8）限制；样本数超过它时应把 Override Anti-Aliasing 设为 None。
  - Temporal Sample Count > 1 时需要把 Sequencer 轨道向前延长一帧。
  - Shutter Timing 可选 Frame Open / Center / Close。
- **MRG**（https://dev.epicgames.com/documentation/en-us/unreal-engine/movie-render-graph-nodes-in-unreal-engine?application_version=5.7 ）：
  - Sampling Method 只能在作业级别统一设置，可以把 TemporalSampleCount 暴露成 pin，逐镜头设值。
  - Path Traced Renderer 节点有 **Reference Motion Blur** 和 NFOR Temporal Denoiser。
  - Warm Up Settings 可以模拟运动模糊。
  - MRQ 的 `render_warm_up_frames` 原文说明：“some systems (such as gpu particles) need to be rendered to actually perform their warm-up”。
- **条纹的“真”运动模糊** [推导]：MRQ 时间切片之间是靠速度矢量运动模糊来填补的，而半透明 sprite 默认不写速度。所以每个时间切片只画出一个离散的水滴副本，样本不够就会出现**点状虚线**。
  - 要让未拉伸的水滴连成线，需要 `N_temporal ≥ v·T/D`。24 fps、180° 时，0.5–1 mm 约 84，2 mm 约 68，4 mm 约 45。这个比例与距离无关，因为长度和宽度都按 1/z 缩放。Garg 渲染 1042 px 的条纹时也用了 46 个时间插值样本。
  - 三种做法：
    - ① 直接用约 85 个 temporal samples。
    - ② 每个切片渲染一段拉伸 sprite，长度 `v·T/N + D`，沿预存的下落方向，不透明度乘 `D/(v·T/N + D)`（即把 1.6 节公式中的 T 换成 T/N）。这样累积后的条纹长度和每像素 α 都正确，减速过渡也正确。
    - ③ 先实测材质勾选 Output Depth and Velocity 后，Niagara sprite 能否被切片间的运动模糊正确拉长。
  - Path Tracer 加 Reference Motion Blur 本来就需要数百个时间样本来收敛噪声（官方建议 Spatial = 1，样本全部给 Temporal），足以满足上式，不会出现断点。

### 5.8 教程与资源（二手，质量参差）
1. https://www.youtube.com/watch?v=v-hXyDX5msQ ：UE5 Niagara Rain Tutorial (Fast VFX Setup)
2. https://www.youtube.com/watch?v=T1o0Yu1VyyY ：Create Rain in UE5 with Niagara（入门）
3. https://www.youtube.com/watch?v=qmZCW7eQ6rc ：Niagara 雨加碰撞溅射（较旧）
4. https://kidswithsticks.com/how-to-create-rain-in-unreal-engine-4-niagara/ ：UE4 速度对齐雨滴加 CPU 碰撞事件溅射
5. https://forums.unrealengine.com/t/updated-for-4-20-1-let-it-rain-but-not-indoors/31124 ：顶视 SceneCapture2D 深度遮挡
6. https://www.youtube.com/watch?v=YlVMRTET2qM ：Sky Occlusion Mask（scene capture 天空遮挡遮罩）
7. https://forums.unrealengine.com/t/how-to-hide-niagara-particles-located-on-occluded-mesh-surfaces/1244304 ：用 RT 判断 Niagara 粒子是否被遮挡
8. https://realtimevfx.com/t/niagara-phantom-colliders/17546 ：深度碰撞用的不可见碰撞体技巧
9. https://www.fab.com/listings/9054808a-d4d2-454a-bd14-7ee74c302483 ：RaindropsOnGlass（镜头和窗户雨滴后处理）
10. https://80.lv/articles/master-nanite-displacement-for-dynamic-water-puddles-in-unreal-engine-5-5/ ：UE5.5 Nanite 置换积水
11. https://80.lv/articles/tutorial-building-streets-in-the-rain-with-ue4 ：雨中街道，涟漪法线 flipbook
12. https://80.lv/articles/tutorial-making-water-ripples-in-blender-ue5 ：Blender 制作涟漪，Alembic 导入 UE5
13. https://dev.epicgames.com/community/learning/tutorials/oDjx/unreal-engine-using-niagara-caches ：Epic 官方 Niagara Cache 教程
14. https://dev.epicgames.com/community/learning/tutorials/7B3y/unreal-engine-caching-niagara-fluids-with-sequencer ：在 Sequencer 中缓存 Niagara
15. https://www.ragataga.com/vfx/using-unreals-niagara-data-channel ：NDC 实操（5.3）
16. https://www.ultradynamicsky.com/Documentation/V9/9-5 ：Ultra Dynamic Sky/Weather 文档（遮雨、湿润、涟漪、镜头水滴的参数参考）
17. https://yelzkizi.org/ue5-dynamic-rain-system-niagara-global-wetness/ ：UE5 动态雨加全局湿润（权威性低）
18. https://cghow.com/poison-rain-in-ue5-niagara-tutorial/ ：kill volume 加独立溅射发射器
19. https://80.lv/articles/working-on-rain-for-an-upcoming-cyberpunk-environment ：赛博朋克场景的雨与积水

---

## 6. 实现建议参数表

标注：**文献**（附来源）/ **推导**（附公式）/ **美术取值**（经验值，可调）。
默认镜头假设：24 fps、180° 快门、35 mm 镜头、Super35、4K。换镜头时，距离类阈值按 `焦距/35 mm` 等比缩放。

### 6.1 雨滴尺寸与数量

| 参数 | 建议值 | 类型 | 依据 |
|---|---|---|---|
| 基准雨强 R | 下落段 10 mm/h（中雨偏大）；想要戏剧化可用 25 mm/h | 美术取值 | AMS 分级：>7.6 mm/h 为大雨 |
| 尺寸分布 | 截断指数分布 `N(D) ∝ exp(−Λ·D)`，`Λ = 4.1·R^−0.21` mm⁻¹；R = 10 时 Λ = 2.53，R = 25 时 Λ = 2.09 | 文献 | Marshall–Palmer 1948 |
| 采样公式 | `D = Dmin − ln(1 − U·(1 − e^(−Λ(Dmax−Dmin))))/Λ`，U ~ Uniform(0,1) | 推导 | 截断指数分布的反函数 |
| **顶面持续生成时按通量加权** | 穿过顶面的雨滴按 `N(D)·v(D)` 分布，而不是 `N(D)`。先按上式采样 D，再以概率 `v(D)/v(Dmax)` 接受（R = 10 时接受率约 37%）。这样箱内稳态才是正确的体密度谱 `N(D)`；否则大滴会少约 4.5 倍，冻结画面里尤其明显。**第 0 帧预填充**整个箱体时直接用 `N(D)` | 推导 | 通量 = N·v；箱内停留时间 ∝ 1/v |
| 直径范围 | Dmin = 0.5 mm（更小的属于毛毛雨，且 MP 本身高估小滴）；Dmax = 6 mm | 文献 + 美术取值 | AMS：雨滴 > 0.5 mm；Gatlin：D ≥ 5 mm 的仅占 0.4% |
| 戏剧化加大 | 可把 Λ 乘 0.7–0.8，增加大雨滴比例 | 美术取值 | Garg TOG06 脚注 11：电影常用大水滴喷头 |
| 体密度（D > 0.5 mm） | R = 5：634 /m³；R = 10：894 /m³；R = 25：1352 /m³ | 推导 | `(N0/Λ)·e^(−Λ·0.5)` |
| 冻结时 1 m³ 内的数量（R = 10） | D > 0.5 mm 约 900 颗，D > 1 mm 约 250 颗，D > 2 mm 约 20 颗；平均间距约 10 cm | 推导 | 同上 |
| 近景真实密度区 | 视锥 0–15 m（35 mm 镜头），约 20–40 万颗 | 推导 + 美术取值 | 第 5.3 节预算表 |
| Niagara 生成速率 | `SpawnRate = Flux × A_top`；R = 10 时 Flux(D > 0.5) ≈ 3114 /m²/s | 推导 | `∫N(D)·v(D) dD` |
| 粒子寿命 | `Lifetime = H_box / v(D)`，并加 Kill Volume 兜底 | 推导 | — |

### 6.2 速度、形状、振荡

| 参数 | 建议值 | 类型 | 依据 |
|---|---|---|---|
| 末速度 | `v = max(0, 9.65 − 10.3·e^(−0.6·D))` m/s（D 单位 mm）；1 mm → 4.0，2 mm → 6.55，4 mm → 8.7 | 文献 | Atlas 1973 |
| 速度抖动 | ±5–10%；阵风时最多降低 25–30% | 美术取值 + 文献 | NSF par 10533483：湍流下比 Gunn–Kinzer 低 25–30% |
| 风致倾角 | `tan α = U_wind / v`，小雨滴倾斜更大 | 文献 | Helming 1999 |
| 形状 | D < 1 mm 用球；D ≥ 1 mm 用扁球，竖直方向缩放 = Brandes `b/a`（2 mm → 0.94，3 mm → 0.87，4 mm → 0.79） | 文献 | Brandes 2002（D 单位 mm） |
| Hero 网格 | D ≥ 2 mm 时用 Beard–Chuang 余弦级数生成（底平顶圆）；**绝不用泪滴形** | 文献 | Beard & Chuang 1987；USGS |
| 冻结时的形状 | 由扁球在约 0.3–0.5 s 内放松为接近球形（悬停时没有气流压平底部）；或保持扁球，视剧情而定 | 美术取值 | 物理推论：扁平化来自气动压力 |
| 振荡振幅 | A₂,₀ = 0.1–0.2，A₃,₁ = 0.1，ω₃ = 2ω₂ | 文献 | Garg TOG06 |
| 振荡频率 | `f₂ = (1/2π)·sqrt(8σ/(ρ·r³))`：2 mm → 122 Hz，3 mm → 66 Hz；冻结时乘 TimeScale | 推导 | Rayleigh；Garg 给出 r = 1.6 mm 对应 1/60 s |

### 6.3 条纹（下落段）与减速过渡

| 参数 | 建议值 | 类型 | 依据 |
|---|---|---|---|
| 曝光时间 | `T = (shutter/360)/fps`；24 fps、180° 时为 20.83 ms | 推导 | — |
| 世界空间条纹长度 | `L = v·T + D`；2 mm 雨滴 @180° 约 13.8 cm，4 mm 约 18.6 cm | 推导 | — |
| 条纹宽度 | 等于 D。屏幕上不足 1 px 时钳到 1 px，亮度乘以 `projected_width_px`（覆盖率补偿） | 推导 | 能量守恒 |
| 每像素不透明度 | `α = D/(v·T + D)`；下落时约 0.012–0.022 | 文献 + 推导 | Garg：`α = 2r0/(v·T)` |
| 条纹外观 | 有振荡斑点和多重拉长高光，而不是平滑线；光源越靠后（逆光）越亮 | 文献 | Garg TOG06；Lagarde 1 |
| 对齐方式 | `Custom Alignment` 加预存的下落方向，不要用 Velocity Aligned（v → 0 时方向退化） | 推论 | UE 5.7 Sprite Alignment 文档 |
| 交接阈值 | v ≤ D/T（2 mm @1/48 s：约 0.1 m/s）时，条纹 renderer 切到水滴 renderer（visibility tag） | 推导 | 条纹长度等于直径 |
| 减速曲线 | TimeScale 从 1 到 0，ease-out 约 1.5–3 s；大雨滴先停、小雨滴后停，或按距离做波前传播 | 美术取值 | Framestore 提到花了大量时间研究“雨如何停下” |
| 能量守恒亮度 | 减速中每像素亮度按 `D/(v·T + D)` 从约 1.4% 升到 100%（约 70 倍） | 推导 | 1.6 节 |

### 6.4 亮度、颜色、折射

| 参数 | 建议值 | 类型 | 依据 |
|---|---|---|---|
| IOR | 1.333；如需色散：R 1.331 / G 1.335 / B 1.340（656 / 546 / 450 nm 附近） | 文献 + 推导 | Daimon & Masumura 2007 |
| F0 | 0.02（线性） | 文献 | Hoffman 2013；Lagarde |
| 折射视场 | 最大偏折约 82.8°，总视场约 165°，需要查全方向环境（cubemap） | 文献 + 推导 | Garg IJCV07 |
| 透射率 | 中心约 94%（“只衰减 6%”）；边缘 Fresnel 上升 | 文献 | Garg IJCV07 |
| 静止水滴亮度 | 约等于 0.94 × 165° 锥内环境平均辐亮度，加上镜面高光。**日景或天光下**通常比所挡背景更亮，不要压成暗灰色；**夜景**由灯光倒像、高光点和边缘主导，整体可以比背景暗 | 文献 + 推导 | Garg IJCV07 §4.2.3（阴天实验） |
| 高光 | 每个强光源产生两个亮点：前表面反射加透射或内反射；用 Surface ForwardShading 或解析高光 | 文献 | Sax 2023；van de Hulst |
| 彩虹闪光 | 只在光源位于相机后方、且水滴处于离反日点 40–42° 的环带上时出现单色闪光 | 文献 + 推导 | HyperPhysics |
| 光源光晕 | 条纹和水滴亮度乘 `1/d²`（d 为到光源距离），并加 Bloom 和 Volumetric Fog | 文献 | Garg TOG06；ToyShop |
| 布光 | 主光放在雨的**后方或侧后方**（逆光），背景压暗，避免以天空为背景 | 文献 | 电影实拍经验，CML / Filmmaking |
| 白偏置 | 下落段雨色可轻微偏白（参考：Lagarde 屏幕空间雨层 `RainColor·0.09·RainIntensity`；ToyShop“牛奶”）。冻结段不用 | 文献 + 美术取值 | 2.3、2.7 节 |

### 6.5 冻结段 LOD 与相机

| 参数 | 建议值 | 类型 | 依据 |
|---|---|---|---|
| Hero 层 | < 1.5 m：Mesh 粒子加解析折射或 Path Tracer IOR | 推导 | 2 mm 水滴在 1 m 处约 11 px |
| 中景层 | 1.5–8 m：sprite 加 ray–sphere impostor 加 cubemap | 推导 | 2–7 px |
| 远景层 | 8–30 m：点 sprite，亮度 = 环境平均 + 高光 | 推导 | ≤ 1.4 px |
| 雾层 | > 30 m：体积雾 / 高度雾 | 文献 | Garg：`R·z_m` 以外的雨呈雾状 |
| 景深 | 近景水滴应虚化成光斑（例：50 mm f/2，对焦 3 m，0.5 m 处 CoC 约 330 px），但可以**有意减弱**，避免喧宾夺主 | 推导 + 文献 | Framestore 原话 |
| 运动模糊（下落段） | 未拉伸水滴需要 `N_temporal ≥ v·T/D`（约 45–85）。或者每个切片用拉伸 sprite（长度 `v·T/N + D`，不透明度 `D/(v·T/N + D)`），这时 8–16 个样本即可（美术取值）。AA 设 None。Path Tracer 加 Reference Motion Blur 用数百个样本 | 推导 + 文献 | 5.7 节；UE 5.7 MRQ 文档 |
| 手势影响 | 以手部骨骼为中心，半径 0.3–0.6 m 的衰减速度场加强阻尼；越靠近越先动 | 美术取值 | Framestore 提到需要专门设计手势影响范围 |

### 6.6 溅射、涟漪、湿表面、雾

| 参数 | 建议值 | 类型 | 依据 |
|---|---|---|---|
| 溅射类型 | 有水膜的平面：corona（皇冠）；干燥或粗糙表面：prompt | 文献 | Garg EGSR07 |
| 皇冠寿命 | 10–20 ms，24 fps 下只出现 1 帧左右 | 文献 | Garg EGSR07 |
| 溅出液滴 | 数量 N ~ Normal(73, 10)（木材，r = 1 mm 雨滴），按 (v/v_ref)² 缩放；速度 ~ Normal(2.1, 0.3) m/s；仰角 ~ 68° ± 12°；方位均匀 | 文献 | Garg EGSR07 图 7 |
| 溅射数量 | Lagarde 预设“Number splashes”为 Light / Moderate / Heavy = 20 / 40 / 60（原文未注明单位，是每帧还是每秒不清楚；溅射分布在 20 m × 20 m 的顶视深度图上）。建议按物理雨滴通量取下限后，再由美术调节 | 文献 + 美术取值 | Lagarde 2a/2b |
| 涟漪 | 4 层程序圆环，TimeMul = (1, 0.85, 0.93, 1.13)，TimeAdd = (0, 0.2, 0.45, 0.7)，振幅 0.35，`sin(clamp(t·9, 0, 3)·π)`；主波长约 1.7 cm | 文献 | Lagarde 2b；毛细–重力波 |
| 湿表面 | `factor = lerp(1, 0.2, Porosity)`；`Diffuse *= lerp(1, factor, Wet)`；`Gloss = lerp(1, Gloss, lerp(1, factor, 0.5·Wet))`；积水处 Roughness → 0、法线 → 竖直 | 文献 | Lagarde 3b |
| 雨致消光 | `σ = 0.11·R^0.88` km⁻¹（R > 10 mm/h）；R = 10 时 MOR 约 3.6 km，所以“灰蒙感”需要额外加雾 | 文献 + 推导 | Montero-Martínez 2025 |

---

## 7. 主要一手资料清单

**雨的物理**
- Marshall & Palmer 1948, J. Meteor. 5:165–166：https://www.radartutorial.eu/15.weather/pubs/marshall-palmer-1948.pdf
- Atlas, Srivastava & Sekhon 1973（公式实现与引用）：https://disdrodb.readthedocs.io/en/latest/_modules/disdrodb/fall_velocity/rain.html
- Brandes 2002 轴比多项式（转引自 Météo-France T-matrix 文档）：https://redmine.umr-cnrm.fr/attachments/download/3759/TmatrixDocumentation.pdf
- Andsager 1999 轴比（转引自 Matrosov et al.）：https://psl.noaa.gov/data/obs/instruments/SbandXbandComparison.pdf
- Bringi et al. 2003 雨滴谱与轴比：https://training.weather.gov/wdtd/courses/rac/principles/pps/story_content/external_files/Bringi_et_al_2003.pdf
- Beard & Chuang 系数表（二手）：https://en.wikipedia.org/wiki/Beard_and_Chuang_model
- Loftus & Wordsworth 2021（雨滴形状与最大尺寸综述）：https://arxiv.org/pdf/2102.09570
- USGS “Are raindrops shaped like teardrops?”：https://www.usgs.gov/water-science-school/science/are-raindrops-shaped-teardrops
- AMS Glossary：https://glossary.ametsoc.org/wiki/Rain ，https://glossary.ametsoc.org/wiki/Drizzle

**Garg & Nayar 系列（Columbia CAVE）**
- Photorealistic Rendering of Rain Streaks（SIGGRAPH 2006）：https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_TOG06.pdf
- Vision and Rain（IJCV 2007）：https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_IJCV07.pdf
- When Does a Camera See Rain?（ICCV 2005）：https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_ICCV05.pdf
- Appearance of a Raindrop（TR 2004）：https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_TR04.pdf
- Material Based Splashing of Water Drops（EGSR 2007）：https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_EGSR07.pdf

**实时雨渲染**
- Tatarchuk 2006 讲义：https://advances.realtimerendering.com/s2006/Chapter3-Artist-Directable_Real-Time_Rain_Rendering_in_City_Environments.pdf
- Tatarchuk 2006 幻灯片：https://advances.realtimerendering.com/s2006/Tatarchuk-Rain.pdf
- Tariq 2007 NVIDIA Rain：https://developer.download.nvidia.com/SDK/10/direct3d/Source/rain/doc/RainSDKWhitePaper.pdf
- Rousseau et al. 2006 预印本：https://faculty.digipen.edu/~gherron/references/References/Modeling/Realistic-real-time-rain-rendering.pdf
- GPU Rainfall：https://classes.cs.uchicago.edu/archive/2022/fall/23700-1/papers/gpu-rain.pdf
- Wang et al. MSR-TR-2006-102：https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/RealTimeRain_MSTR.pdf
- Lagarde “Water drop” 系列：https://seblagarde.wordpress.com/2012/12/10/observe-rainy-world/ （以及 2a/2b/3a/3b，链接见 2.7 节）
- Crysis 3 FMX 2013：https://www.slideshare.net/slideshow/the-art-and-technology-behind-crysis-3-fmx-2013/25052563
- Far Cry 6 天气：https://www.ubisoft.com/en-us/company/how-we-make-games/technology/articles/simulating-tropical-weather-in-far-cry-6

**光学与摄影**
- Narasimhan & Nayar 2003 “Shedding Light on the Weather”：https://www.cs.columbia.edu/CAVE/publications/pdfs/Narasimhan_CVPR03.pdf
- Hoffman 2013 PBS 课程：https://blog.selfshadow.com/publications/s2013-shading-course/hoffman/s2013_pbs_physics_math_notes.pdf
- 水折射率（Daimon 2007）：https://refractiveindex.info/database/data/main/H2O/nk/Daimon-20.0C.yml
- 彩虹：http://hyperphysics.gsu.edu/hbase/atmos/rbowpri.html
- Glare points（Sax 2023）：https://arxiv.org/pdf/2303.16013
- 闪光时长：https://scantips.com/speed.html

**目标镜头**
- Framestore 案例页：https://www.framestore.com/work/now-you-see-me-2
- Art of VFX：https://www.artofvfx.com/now-you-see-me-2/

**UE 5.7**：链接见第 5 节，均带 `?application_version=5.7`。

---

## 附：资料冲突与待实测项

1. **Substrate 状态**：5.7 发行说明称其已 production-ready，并对新项目默认启用；Path Tracer 和 HWRT 支持表仍标注 experimental。需要在 5.7 中实测折射水滴。
2. **Niagara 半透明 sprite 的后处理运动模糊**：Epic 没有明确说明。勾选 Output Depth and Velocity 后需实测；终版建议依赖 MRQ/MRG 时间采样。
3. **GPU 发射器能否写入 Niagara Data Channels**：未找到一手资料，第三方教程称不行（5.3）。
4. **Garg 雨条纹数据库**：论文写约 6300 张、400 MB，数据库网页（摘要）写 3150 张、50 MB，当前是否还能获取未核实。
5. **Lagarde 2a 的速度数值**（“1 mm 约 3 m/s”）与 Gunn–Kinzer / Atlas 不符，疑似把半径和直径混用了，**以 Atlas 公式为准**。
6. **Rousseau 的视场角**：C&G 预印本为 150°，GPU Rainfall 为 165°，以 165°（与 Garg 一致）为准。
7. **任务书里三处说法需要更正**：
   - “暗边来自 TIR”：球形水滴内部不会发生全反射（Garg IJCV07 注 7）。
   - “逆光时出现彩虹或焦散闪光”：单滴彩色闪光要求光源在**相机后方**，且水滴位于离反日点 40–42° 的环带上（4.3 节）。逆光下看到的是白色的 p = 1 透射闪点，不是彩虹色。
   - Crysis 2/3 的 SIGGRAPH 2011 / GDC 2013 讲座里**没有**雨相关内容，讲雨的是 FMX 2013 的那场。
8. **未找到一手资料**：The Order: 1886、Watch Dogs、Frostbite/Battlefield、AC Unity/Syndicate、Spider-Man、GTA V、Detroit、City Sample 的雨实现；NYSM2 的具体参数（软件、水滴数、快门、减速曲线）；Gunn & Kinzer 1949 原始数据表（AMS 原文 403，本文改用 Atlas 拟合，以及 Rousseau 转录的速度表）。
