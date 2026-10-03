# 多平台 HDR 统一重构方案

> 状态: 进行中 · 上次核对: 2026-10-01 · 权威源: 引擎侧(本稿) / 宿主侧见下

> ⚠️ **权威源边界**: 本稿对 **avox 引擎侧** 事实(链路/行号/接口/缺口)是权威源; **宿主侧(panvox)** 事实
> (车道决策/换道/探测口径/徽章/自愈节流 §6.6)以 panvox 仓文档为准, 本稿仅转载摘要——改动请回 panvox 改。
> **统一口径文档**: 三平台矩阵/流转/接口/落地清单/挂账全在本稿(由 panvox 三份 HDR 文档合并迁入, 次日三份已删; avox 旧 HDR 文档同日删除, 实施史归 git/记忆)。**HDR 主题文档仅存本篇**。
> **分册(事实细节)**: [流转与落地清单](references/流转与落地清单.md)(§三 超 RGBA8 流转 + §六 逐平台落地清单) · [沿革](references/沿革.md)(§十一 决策史)。主稿留口径与索引, 细节在分册。
> **同域**: [颜色空间矩阵统一设计](颜色空间矩阵统一设计.md)(「单一真相源」原则本稿沿用)。
> **路径**: 本稿在 avox 仓, `src/` 直接写相对路径; panvox 侧用 `panvox:` 前缀。
> **分工**: 引擎渲染/HDR 链路归 avox(本稿); 车道决策/宿主接线(lane 换道/forceHDR 下发/探测/徽章)归 panvox(`engine_controller.dart`, §6.6)。

## 一、总则: 双链路架构(拍板 2026-10-01)

### 1.1 拍板口径 v2(用户定稿, 同日二稿修订)

**两条通道全平台同构: HDR 直通通道不做图像处理; 图像处理只走 VK(硬解帧仅 8bit 对接, SDR/PQ 码两种载荷)。**

1. **HDR 直通通道(HDR 内容 × HDR 呈现面)**: YUV10 解出的 RGBA超10 直接对接窗口的 RGBA超10 呈现域, **全链不做图像处理**——硬解/软解一律。硬解=平台渲染器超 RGBA8 输出→原生窗口(R1); 软解=原生腿吃 CPU 帧(mac 已有, Win 补齐=G9; Android 恒 VK 车道由 VK 承担)或 lane=0 时走 VK yuv2rgba 16F 链(G4 修复)。
2. **VK 图像处理通道**: VK 是**所有平台唯一的图像处理链**(增强/特效/超分等; YUV→RGB 转换与 tone map 是呈现必需, 不算图像处理, 仍在平台腿/层内做)。接入口径:
   - ⚠️ **边界补充(2026-10-01 晚)**: 上面「tone map 不算图像处理」**只覆盖「降域」(HDR→SDR)**。「**升域」(SDR→HDR 观感, 即 ITM) 是图像处理, 必须走 VK**」——方向相反、归属不同: 降域 tone map=呈现必需(原生腿可做); 升域 ITM=增强(**必在 VK**, 引擎零实现, §9.1); 同理「HDR 内容在非 HDR 屏装 HDR」也归 VK(§9.2)。
   - **软解全接**: YUV 直进 VK yuv2rgba 层, 含 10bit——走 VK 自有 YUV 通道, 不经原生对接面;
   - **硬解只接 8bit, 载荷二选一**(对接面 VkInputLayer 恒 rgba8; 载荷判定靠 HdrMode 路由, 见 §2 注记):
     a) **SDR 载荷(链E, `follow`/`forceSDR`)**: HDR 硬解帧由原生 tone map 降成 SDR 再对接——**超 RGBA8 不进 VK**;
     b) **PQ 码载荷(链F, 宿主以 `vkHDR(3)` 下发)**: 原生 forceHDR 分支把 PQ 码原样写进 rgba8(PQ 动态范围全在 0-1 码值, 8bit 保动态范围只损梯度精度), VK 侧**升样层**做 PQ EOTF→16F 线性→HDR 呈现+图像处理/字幕; 归一化**按 lane 分岔**见 §4.3(vkHDR 只在 lane=0 折 forceHDR 行为; lane=1 折 follow——**不可一律折 forceHDR**, 否则原生腿误开直通把 SDR 载荷当 PQ 码呈现=发灰), UBO/shader 协议零改动(协议仅见 2 态: `ColorSpace.hpp:35` / `Dx11CSVideoRender.cpp:39` / EGL `uHdrMode`);
   - VK 完成图像处理后按呈现面呈现。HDR 呈现×图像处理的组合经链F 可达; 全程超 RGBA8 的直通链(链A)恒不做图像处理, 仍为画质首选。
3. **呈现面实态驱动输出格式**: 输出跟「窗口当前是否真直通」走, 不跟 setHdrMode 命令走——命令被拒(SDR 屏/格式不可得)时输出自然回 SDR 域, 不产生命令与实态漂移。
4. **直通链全链超 RGBA8**: 呈现域是 10bit(Win)/16F(Apple/VK) 时, 像素处理输出与中间纹理一律不落 8bit(Win 现状唯一违例=G3→R1)。tone map 链终点本就是 SDR rgba8, 不受此约束。
5. **翻转全链一致**: 呈现面实态翻转时, 像素处理输出与呈现面同帧对齐重建, 过渡帧数值直传不花屏(§3.4, 分册)。
6. **SDR 片 × 任意屏**: lane=0 VK 常规路径, 零变化。

### 1.2 三类角色职责

| 类 | 职责 | HDR 相关面 |
|---|---|---|
| WindowRender | 协调者: 持 window+双渲染器(pVideoRender 平台渲染器 / vkVideoRender), 按 bVulkan 分流 lane | setHdrMode 双路转发+直通被拒降级 follow(WindowRender.cpp:63); onRenderWindow 两路都下发窗口(§4.2) |
| Window | 呈现面: 原生句柄/交换链/层的持有者 | setHdrPassthrough 下行命令(Window.hpp:83)+setHdrMeta(:86); 直通实态查询口=§4.1 新增 `hdrPassthroughActive` |
| VideoRender | 像素处理: YUV→RGB(+VK 图像处理链) | 平台渲染器吃硬解 GPU 帧; **CPU 帧腿统一补齐**(§1.3, G9); 原生腿不做图像处理(只 YUV→RGB+直通/tone map); 输出域按呈现面实态定 |

### 1.3 CPU 帧支持矩阵(统一目标: 原生腿全平台都吃)

| 渲染器 | 硬解 GPU 帧 | 软解 CPU 帧(目标) | 依据 |
|---|---|---|---|
| Dx11CSVideoRender(Win) | ✓ | ✓ **补齐(G9, 入 R1)**: yuv420P10 上传→CS→超 RGBA8 输出 | Dx11CSVideoRender.cpp:297 |
| MetalRender(Apple) | ✓ | ✓(样板: yuv420P/P10→P010 pb 进同一 16F 管线) | MetalRender.mm:417 |
| EglVideoRender(Android) | ✓ | 恒 VK 车道下软解由 VK 全接; R4 若开原生呈现再补 | — |
| VkVideoRender(全平台) | ✓(import, 仅 8bit 对接) | ✓(yuv2rgba 层, 含 10bit) | VkVideoRender.cpp:780 |

**推论(v2)**: 原生腿统一吃两种帧后, **硬解失败 lane=1 就地落软解超 RGBA8 直通, 不再画面停滞、不再必须换道**(G6 降级为上报/徽章, §五); lane 选择回归本义=**呈现路由**(直通腿 vs VK 图像处理腿), 与解码成败解耦。

## 二、情况矩阵

### 2.1 「源 × 屏」四格总表(2026-10-01 晚新增)

**核心判据: HDR 能力挂在「显示器」上, 不挂在「源」上**。源决定「有没有真 HDR 数据可放」, 屏决定「能不能放出来」。故「与 HDR 有关」的判据是**屏**——非 HDR 源在 HDR 屏上**恰恰有关**, 在非 HDR 屏上才无关。

| 源 \ 屏 | **HDR 显示器** | **非 HDR 显示器** |
|---|---|---|
| **HDR 源** | **3 档可选**(§6.6.1): ①原生直通(默认, 真HDR) ②vkHDR/链F(真HDR+可图像处理) ③降SDR(链E) | **1 档**(被动): 降 SDR(引擎拒直通→折 follow, §9.2); 若做 §9.2「装 HDR」→ +1 档(仍 SDR 码, 增强观感) |
| **非 HDR 源** | **2 档**: ①当 SDR 呈现(默认) ②**ITM 上变换**(§9.1, 用上屏的高动态范围)← **不是「与 HDR 无关」** | **1 档**: 普通 SDR |

**★ 哪些档位有实际意义 / 业界做过(按价值排序, 供 panvox 定设置项默认值)**:

| 档位 | 有意义吗 | 业界先例 |
|---|---|---|
| **HDR源×HDR屏 → 原生直通** | ★★★ **必需, 真 HDR 正解**(最全动态范围+无 8bit 量化) | **所有播放器基础路径**: madVR(passthrough)、mpv(`target-colorspace-hint`)、VLC、PowerDVD、系统播放器(Win「电影和电视」/ mac QuickTime 走 EDR) |
| **HDR源×HDR屏 → 降SDR** | ★★ 有意义(手动选择/旧屏兼容); 画质不如直通但**色准可控** | mpv `--tone-mapping`、madVR(不支持 HDR 时降级)、VLC(老口径默认) |
| **HDR源×非HDR屏 → 降SDR** | ★★★ **唯一可行(现状即此)** | 全行业默认(madVR/mpv/VLC 皆此路); 差异只在 tone map 曲线/峰值亮度设定 |
| **非HDR源×HDR屏 → ITM 上变换** | ★★ **有争议**: 能用上屏但**无真值**(推测性提亮), 口碑两极 | **做过**: madVR「HDR 输出 SDR 内容」、**NVIDIA RTX Video HDR**(显卡级)、Windows Auto HDR(游戏)、部分电视「HDR 增强」(索尼/三星常带, 用户常关) |
| **HDR源×非HDR屏 → 「装 HDR」增强** | ★ **噱头居多**: 屏没能力, 只是「SDR 里更冲」, 易过曝/失真 | 同上「HDR 增强」类(电视居多); 桌面播放器**少有专门做** |
| **HDR源×HDR屏 → vkHDR/链F** | ★★ **avox 特有**: 真实价值是「**HDR 保真同时还能加 VK 图像处理/字幕**」, 不是画质提升 | **无直接先例**——业界是「直通(无处理) **或** 降SDR(能处理)」二选一; 链F 是「保 HDR + 能处理」的第三条路 |

**结论(宿主定位用)**: **产品级必需的只有「HDR源×HDR屏 直通」+「HDR源×非HDR屏 降SDR」两条**(行业标配, 已达标); 「降SDR 档/ITM/装HDR/链F」都是**可选增强/实验档**——建议宿主**默认走标配两条**, 其余以「实验性开关」暴露。

### 2.2 逐情况明细(展开到解码/链路/批)

| # | 片源 | 显示器 | 解码 | 链路 | 像素处理(YUV10→?) | 呈现面 | 批 |
|---|---|---|---|---|---|---|---|
| 1 | HDR | HDR | 硬解 OK | lane=1 原生直通 | 平台渲染器 PQ 码原样(超 RGBA8 输出) | Win: R10G10B10A2+G2084 / Mac: RGBA16F+EDR | R1(Win) |
| 2 | HDR | HDR | 硬解 fail | lane=1 原生腿软解或 lane=0 VK, 两路等价 | 原生: YUV10→超 RGBA8(同①) / VK: yuv2rgba 16F | 原生窗口超 RGBA8 / VK FP16 交换链 | R1(补腿)/R2 |
| 3 | HDR | SDR | 硬解 OK | lane=0 VK | 平台渲染器 tone map→SDR rgba8 | VK 合成→SDR 呈现 | 回归 |
| 4 | HDR | SDR | 硬解 fail | lane=0 VK | VK V5.comp tone map→rgba8 | 同上 | 回归 |
| 5 | SDR | 任意 | 硬解/软解 | lane=0 VK | 常规 | SDR | 回归 |

**播中变化(全平台)**: 系统 HDR 开关/跨屏搬家/换片 → 呈现面实态变化 → 统一检查发现→过界翻转重建(§4.2); 硬解失败(→徽章实态/降级 UX; 原生腿就地吃软解帧, 换道非必需)。截图/缩略: 直通态拒绝或临时 follow(SDR)抽帧, 防脏图(§3.4)。

**图像处理路由注记(v2)**: 硬解帧(HDR 片)进 VK 二选一——保 HDR 走链F(PQ 码 rgba8→VK 升样), 落 SDR 走链E(tone map 后对接); 软解帧直接 YUV(含 10bit)进 VK。链A(原生直通)恒不做图像处理。
**两种 rgba8 载荷不可混淆**: 链E 载荷=tone map 结果(1.0=SDR 白/BT.709/不可逆), 链F 载荷=PQ 码(1.0=PQ 满刻度/BT.2020/可逆)——同为 0-1 字节流**从数据无法侦测区分**, 载荷判定恒靠 `HdrMode` 路由(vkHDR/forceHDR 行为态→链F 升样层; follow/forceSDR→链E 当普通 SDR), 禁止在 VK 侧做数据侦测。
**车道决策与换道在宿主 panvox**: 开播定道(trc∈{pq,hlg}+HDR 屏闸→lane=1)+播中换道+换片复位+屏况双向纠偏+3s 自愈重发(§6.6)。

## 三、超 RGBA8 HDR 流转 → [见分册 references/流转与落地清单.md](references/流转与落地清单.md)

格式与域字典(3.1)、现状流转逐平台逐腿(3.2)、目标流转六条链 A~F(3.3)、过界与过渡/翻转时序/竞态 A/读回防护(3.4)、方案② 逐平台落地清单——**全文已迁入分册**, 主稿不重复。

> 关键锚点速查: 六条链=链A 硬解直通 / 链B 软解直通 / 链C tone map / 链D SDR / 链E 硬解×图像处理降SDR / 链F 硬解 HDR 保真进 VK。翻转时序差与竞态 A(crash) 的根因、同帧对齐方案②、`hdrPassthroughActive()` 必须返实态——均在分册 §3.4。

## 四、统一接口设计

### 4.1 Window 基类: 直通实态查询口(G2, 新增)

`src/avox/video/Window.hpp`:

```cpp
// 呈现面直通实态(交换链/层已真切上 HDR 即 true); 命令被拒/SDR 屏= false。
virtual bool hdrPassthroughActive() const { return false; }
```

实现口径(**语义 = 交换链/层「当前」真切上的状态**, 供 §4.2 实态缓存比对):
- Dx11Window = 直接返回 `bHdrActive`(Dx11Window.hpp:73)。它**本身就是实态**: `SetColorSpace1` 失败回滚 false(Dx11Window.cpp:329/353), `rebuildDevice` 重置(:200), resize 由 `applyHdrSwapchainState` 重入兜底。原稿「&& 交换链格式==R10G10B10A2」冗余(且与 `initBuffers()` 重入叠加可能假阴)——移除。
- VkWindow = **取 `bHdrActive`(交换链当前态), 不取 `bHdrPassthrough`(意愿位)**(VkWindow.hpp:141-142 两个独立位)。`setHdrPassthrough` 幂等短路用 `bHdrActive == bPassthrough`(VkWindow.cpp:508), 而换面重建路径**会重置 `bHdrActive` 而保留 `bHdrPassthrough`**——取后者会换面后误报 true。
- MetalWindow = `std::atomic<bool> metalHdrPassthrough`(MetalWindow.mm:10, setHdrPassthrough 过 EDR 探测后置位——翻转在渲染器, 意愿位即实态)。⚠️ **它是 `namespace avox` 下全局量, 多窗口会串**; 单窗口假设需注明, 否则应改实例成员+原子。

### 4.2 VideoRender 基类: 统一检查点(G1/G2 收口)

`src/avox/video/VideoRender.hpp`(renderWindow 现为空虚函数 :186; bResetFlag 原子重建标志 :46):

- 基类实现 `renderWindow(Window*)`: 存 `Window* targetWindow`(弱引用, 生命周期归 WindowRender)并立即刷新一次实态;
- 基类字段 `bool bTargetPassthrough = false;`(实态缓存);
- 基类检查方法 `checkTargetPassthrough()`(:382, 各派生 vaildAndInitGraph 首行调用): targetWindow 非空 → 读 `hdrPassthroughActive()` → 与缓存不一致 → 更新缓存+置 `bResetFlag`+日志一行;
- `WindowRender::onRenderWindow`: **两路都调** `renderWindow(window.get())`(WindowRender.cpp:430/433); 贴图腿/离屏时 window 为空, 判空即安全;
- 派生在重建时按实态定输出格式: Win=createProgram 分支 R10G10B10A2/R8G8B8A8(分册 §6.1); Mac=已有翻转不动; VK=16F 变体条件(分册 §6.3)。

### 4.3 命令下行(小改: vkHDR 归一化)

setHdrMode → WindowRender.cpp:63: 先 `window->setHdrPassthrough`(被拒→降级 follow, 防 shader 跳过 tone map 落 SDR 面过曝)再双路转发渲染器; setHdrMeta 同转发。

v2 增量: 新增 `HdrMode::vkHDR(3)`。**归一化必须按 lane 分岔, 不能一律折成 forceHDR**:

| lane | 收到 vkHDR 后 | 理由 |
|---|---|---|
| **lane=0**(VK 腿) | 折 `forceHDR`: `setHdrPassthrough(true)` 切 FP16 交换链 + 双路转发 forceHDR + 启用升样层(链F) | 与 forceHDR 在 VK 腿既有行为一致, 正是链F 要的 |
| **lane=1**(原生腿) | **折 `follow`**(不是 forceHDR, 也不是「告警后按链A 执行」) | 宿主意图是「交给 VK 做图像处理」, lane=1 无 VK 处理链; 若按 forceHDR 会**误开原生直通交换链**(Dx11Window.cpp:294)把 SDR 载荷当 PQ 码送 G2084 面 = 直接发灰 |

**为什么不能一律折 forceHDR**(原稿的错): `setHdrMode` 第一件事就是 `window->setHdrPassthrough(mode == forceHDR)`(WindowRender.cpp:68)——折 forceHDR 会让 lane=1 原生窗口也去切直通交换链, 而链E/链F 前提都是「**原生不做直通、只产 rgba8 载荷**」。lane=1 收 vkHDR 属宿主路由错误, 正确动作是**降回 follow 走 SDR 呈现**并告警。(备选: 把 `setHdrPassthrough` 口改成三态 `none/native/vk`, 语义更直白但改动面大。)

### 4.4 平台分治点(统一「检查」, 分治「翻转实现」)

| 平台 | 翻转实现 | 状态 |
|---|---|---|
| Win | 输出(createProgram)与交换链两端同翻(双向重建已有: 开向 10bit+SetColorSpace1 / 关向回 8bit+重挂) | R1 补输出端 |
| Mac | 层+管线同帧翻转(vaildAndInitGraph 过界对齐+releaseGraph 全图重建) | 达标, 只接 §4.1/4.2 |
| VK | 交换链 FP16(VkWindow 已有)+层输出 16F 变体(条件化修 G4) | R2 |

## 五、缺口台账

| # | 缺口 | 位置 | 归属 |
|---|---|---|---|
| G1 | 呈现格式决策分散: Win 恒 rgba8 写死 / Metal 层翻转(已实现) / VK FP16+变体(已实现), 无统一语义 | 三平台 | ✅ §四统一 |
| G2 | Window 基类无直通实态查询口; pVideoRender 拿不到窗口指针 | Window/WindowRender | ✅ §4.1/4.2 |
| G3 | Win 直通链 8bit 中转(10bit→8bit→10bit) | Dx11CSVideoRender.cpp:440 | ✅ R1 已落(R10G10B10A2 分支) |
| G4 | VK 软解 16F 覆盖: forceHDR 分支设 `outFormats[0]=rgba16f` 后被**共用归一化段**覆盖回 rgba8——HDR 变体写出的 >1 线性值进 8bit 被钳 1.0, 高光全丢。**修法不是简单加条件**: 该归一化段还承载「10bit 平面字节视图」约定, 需把 16F 赋值**移到归一化段之后**并理顺字节视图路径 | VkYUV2RGBALayer.cpp onInitLayer | ✅ R2 已落(16F 移段末并条件化; `VkTexture` 经核无需改); 仅剩真机端到端 |
| G5 | iOS EDR 探测缺口(MetalWindow 恒不受理, 无 NSScreen) | MetalWindow | R3 |
| G6 | 硬解失败无事件上报(只有 `fallback to software` 日志)——v2 后换道非恢复必需, 上报保留用于徽章实态/降级 UX | FFDx11Decoder/宿主 | ✅ R5 撤销(分册 §6.4.1) |
| G7 | mac VK 窗 MoltenVK 无 EDR(恒 SDR 呈现)→「硬解 fail→VK 接手真 HDR」在 mac 不可达; mac 场景② 改落原生腿(§1.3) | VkWindow(mac) | R3 评估 |
| G8 | Android HDR 呈现面口(EGL/Flutter 桥均无 HDR) | EglVideoRender / VkWindow(Flutter) | R4 另批 |
| G9 | Win 原生腿不吃 CPU 帧(cpuIn 不建图) | Dx11CSVideoRender | ✅ R1 已落地。⚠️ **2026-10-03 订正**: 落地后该腿**仍恒失败** —— `Dx11Window` 从不派发 `IWindowOb::onRenderWindow`(`VkWindow:251`/`Dx12Window:193` 都派发) ⇒ `WindowRender::onRenderWindow` 从不执行 ⇒ `VideoRender::targetWindow` 恒 null ⇒ `initGraphCpu` 恒 `no window device, skip`。**故本项此前只到「代码就绪」, 从未真跑通**(与 §十 末「代码就绪但未验」一致)。已修 `888c156`(`onTickWin` 内补 `dispatch`, 置于 `sharedTexture` 早退之前), 顺带首次激活该腿的 `checkTargetPassthrough()` 统一检查点; 4K ProRes 422P10 软解实测 15/15 截图有画。详见 [422 方案](422-10bit解码与呈现方案.md) §5.7 |
| G10 | 链F 升样层缺失: VK 无「rgba8 PQ 码→16F 线性」EOTF 层。**前置依赖**: 链F 输入侧(VkInputLayer 导入 D3D11 共享纹理)需要 `ImageType::rgba10` 在 `getVkFormat` 有映射(VkHelper.cpp:344, 现缺→`VK_FORMAT_UNDEFINED`)——即三处映射是 G10 的**硬前置** | avox_vulkan 新层 | ✅ **主体+Win/Android 已落**(`VkPqUpsampleLayer`+`pqUpsample.comp` 已接入 graph; Win CS 与 Android `EglVideoRender` forceHDR 分支均产「PQ 码原样」rgba8); **mac 转独立缺口**(分册 §6.3) |
| G11 | **VK 交平台 GPU 资源的格式闸未含 16F**(**仅 `setVulkan(false)` 路径**): `VkOutputLayer::onCommand:184` `bCanMapGpu = (rgba8\|\|bgra8)` ⇒ HDR 出 rgba16f 时整段 interop 跳过。**第二层**: 三处映射亦无 16F。**受限面**: **panvox 不受影响**(走 `setVulkan(true)`→VkWindow 原生窗直渲); 仅影响用平台资源交帧的消费者(Unity/Avalonia/vulkantest 样例) | VkOutputLayer.cpp:184 + 三映射 | R2 补(低优先); 详见 §十 D5 |

## 六、平台落地清单 → [见分册 references/流转与落地清单.md](references/流转与落地清单.md)

Windows 输出翻转 6.1 / macOS 对齐 6.2 / VK HDR 内容链 6.3(G4+G10, 含链F 定位、交接面语义核对、通用性三层) / 硬解状态上报 6.4(G6, **无代码缺口**) / iOS·Android·Linux 6.5 / 宿主侧 panvox 6.6(含 **§6.6.1 三态呈现切换宿主待办**)——**全文已迁入分册**。

> 速查: **宿主待办「三态呈现切换」见分册 §6.6.1**——A 原生直通(默认,lane=1+forceHDR) / B vkHDR(lane=0+vkHDR,链F) / C 降SDR(lane=0+follow,链E); 引擎零改动, 缺宿主入口; 两条硬约束=切档须换道重开(`setVulkan` 对已有 window 直接 return)/ lane 必须真在 0。

## 七、里程碑

| 批 | 内容 | 出口 |
|---|---|---|
| **R1 Windows lane=1 全程 10bit+CPU 腿** | §4.1+§4.2+分册 §6.1(含 G9) | SU130 放 HDR10 片: 输出/交换链/呈现全 10bit, 肉眼无 banding 差; 软解 HDR 片 lane=1 直通(G9); SDR 片零变化; 换道/跨屏/缩放往返无花屏 |
| R2 VK HDR 内容链(软解 16F+硬解 PQ 升样) | 分册 §6.3(G4+G10) | Win 软解 HDR 片×HDR 屏: FP16 交换链高光顶出; 链F 硬解片同验(高光顶出+图像处理/字幕可用) |
| R3 统一检查收口 | mac/VK 对齐 §4.2 检查点+iOS EDR 口径评估(G5)+mac 场景②真验(G7) | 三平台检查路径同源; iOS EDR 可行性结论 |
| R4 Android/Linux HDR | EGL HDR 呈现面口等(G8) | 另批详设(不在本稿展开) |
| R5 硬解状态上报 | 分册 §6.4 | **2026-10-01 定案: 引擎内部已自洽, 无代码缺口**。**不新增任何接口**(分册 §6.4.1) |

## 八、验收矩阵

| 场景 | 期望 | 批 |
|---|---|---|
| PQ 片×HDR 屏×硬解(Win lane=1) | 直通: 高光顶出; 输出/交换链全 10bit(rgba10→R10G10B10A2), 肉眼无 banding 差 | R1 |
| PQ 片×HDR 屏×硬解失败(Win) | lane=1 原生腿软解超 RGBA8 直通(G9)或 lane=0 VK 16F 直通, 两路等价 | R1/R2 |
| PQ 片×HDR 屏×软解直开(Win lane=0) | VK FP16 交换链直通(G4 修复后) | R2 |
| PQ 片×HDR 屏×硬解失败(mac) | Metal 腿 16F 直通(renderCpuFrame 路), 免换道 | R3 |
| PQ 片×Win HDR 关 / SDR 屏 | tone map 出 SDR, 与现状零变化 | 回归 |
| 播中开关 Win HDR / 跨屏搬家 / 缩放往返 | 自动翻转重建, 不花屏不崩 | R1 |
| SDR 片×任意 | 零变化 | 回归 |
| 直通态截图/缩略 | 拒绝或走 SDR 口径, 不产脏图不灰 | R1/R2 |
| 直通态字幕(内/外挂) | 域正确(**已落地**(2026-10-03) VK+三平台腿 → [字幕画布多后端渲染计划](../player/字幕画布多后端渲染计划.md); HDR 屏真机人眼验收待做) | R3 |
| mac lane=0/lane=1 往返 | Metal→VK 交接 rgba8 照旧 | R3 回归 |
| 硬解帧×图像处理需求(HDR 片) | 链F(PQ 码 rgba8→VK 升样, 保 HDR)或链E(tone map 降 SDR)按需路由; 超 RGBA8 不进 VK | R2/口径回归 |
| 链F 画质对照 | 升样后 FP16 窗口高光顶出, 与链A 对照暗部/高光可察觉轻度 banding(8bit 量化, 已知代价) | R2 |

## 九、风险台账

- **R10G10B10A2 NT 共享兼容性**: **纯 D3D11↔D3D11 无风险**(`Dx11Resource.cpp:89-99` 建纹理不校验 Format, D3D10 起该格式即标准 RT); 需真机验的是 **D3D11↔Vulkan 互操作**(创建/内存导入/blit 驱动差异);
- **翻转时序差**: 交换链先切/输出后切的一两帧过渡——防花屏实测(分册 §3.4);
- **lane=0 的 HDR 下发(v2)**: 旧口径「宿主不向 lane=0 发 forceHDR」作废——链F 由宿主以 `vkHDR(3)` 下发(分册 §3.3, 引擎入口**按 lane 分岔归一化**, 见 §4.3); 时序约束: 升样层(G10)未就绪前宿主不得放开, 否则 PQ 码被当线性呈现发灰;
- **翻转两端的错帧过渡**: 交换链与输出翻转不同帧, 中间帧值域不一致会发灰/过曝; 详见分册 §3.4, R1 验收矩阵「播中开关/跨屏/缩放往返」依赖本条;
- **lane=1 误收 vkHDR**: 若归一化一律折 forceHDR, 原生腿会误开直通交换链把 SDR 载荷当 PQ 码呈现=发灰; 已修入 §4.3(lane 分岔);
- **ImageType 加枚举**: 枚举追加向后兼容(0~15 值不动); **影响面四件套**(分册 §6.1); **实际生效面=0**(全仓无 rgba10 产出点), 属纯预备;
- **VkSharedImage 平台分支缺口**: 声明层已平台无关(`VkShareHandle` 含 `opaqueFd`/`androidHwBuffer`), 但**实现层只有 Win32**(`importFromHandle` 硬编码 `OPAQUE_WIN32_BIT` VkSharedImage.cpp:239, 导入整段在 `#ifdef _WIN32` :266-311)。Linux/鸿蒙若要用 VkDevice↔VkDevice 共享则不可用——**不阻塞 R1/R2**, 归 Linux/鸿蒙批次;
- **VK 16F 域层兼容**: Blend/VR/font 层在 16F 线性域合成未处理, R2 先保主链, 字幕域专项跟进(方案已定稿 → [字幕画布多后端渲染计划](../player/字幕画布多后端渲染计划.md));
- **iOS EDR**: 无 NSScreen 探测口径(UIScreen maximumPotentialEDRHeadroom?)另评估;
- **遗留挂账**: tone map sdrWhite 恒 100nit 不跟手显示(GET_SDR_WHITE_LEVEL); `pvx_hdr_toggle.ps1 -Off` set ok 但状态不动(panvox 侧); HLG 直通线性化腿在直通分支同生效需确认;
- **旧 A-9 backlog(2026-10-01 删)**: SDR→HDR 上变换口径**现修正**为「要做就归 VK+显式开关」(§9.1), 不违背「SDR 片**默认**零变化」; DV P5 兼容层未列入本稿范围, 如需重启另立新账; VT 路径 P1 落地(e9d9d54)归 git, 战役纪要见 [DV-HDR与构建协同](../../reports/DV-HDR与构建协同.md)。

### 9.1 SDR 内容 → HDR 观感(ITM/inverse tone map) — **新账, 未做(2026-10-01 晚立)**

- **需求**: SDR 片源想在 HDR 屏上以「HDR 观感」呈现。**引擎现状: 零实现**——`grep -rni "inverseToneMap|sdr2hdr|sdrToHdr|itm|上变换|逆tone" src/ glsl/` **零命中**; glsl 只有 `yuv2rgbaHDR.comp`(HDR 内容解码)与 `pqUpsample.comp`(链F 升样), **皆降域/保真, 无升域**。
- **归属(据 §1.1 边界补充)**: ITM=图像处理 ⇒ **必在 VK**。落点同 `VkPqUpsampleLayer`: 挂 `VkInputLayer` 之后、合成之前; 输入 SDR rgba8、输出 16F 线性→VK FP16 交换链→HDR 呈现。
- **性质**: 无真值, 纯观感增强(SDR 的 0-1 码**推测性**扩展到 HDR 亮度域, 需 SDR 白点基准如 100nit→203nit)。效果两极, 属**可选增强档**不是默认行为。
- **前置**: 需新 `HdrMode` 意图值(现枚举无「SDR 片上变换」语义); 或复用 `vkHDR` 让 VK 层按输入 transfer 分流(需设计)。

### 9.2 HDR 内容 × 非 HDR 显示器「装 HDR」(增强型 tone map) — **新账, 未做(2026-10-01 晚立)**

- **需求**: HDR 片在**非 HDR 显示器**上切成「HDR 显示」。**结论: 真 HDR 不可达, 且非代码缺失而是呈现面物理不存在**——非 HDR 屏 `SetColorSpace1(G2084)` 不可得, `applyHdrSwapchainState` 失败回滚 `bHdrActive=false`(Dx11Window.cpp:329-353), `setHdrPassthrough(true)` 返 false 后 `WindowRender` **主动降级 follow**(WindowRender.cpp:83-85); mac EDR headroom≤1 同理。**「HDR 显示」= 显示器物理能力 × OS 开关, 无软件手段可绕过。**
- **唯一可做的「装 HDR」**: 增强型 tone map——tone map 时**故意抬高亮度/对比/饱和**做出「像 HDR」的观感, **但仍输出 SDR 码**。属图像处理 ⇒ **归 VK**(同 9.1 落点)。
- **与 9.1 是镜像关系**: 9.1=SDR 内容→HDR 观感(需 HDR 屏才兑现); 9.2=HDR 内容→SDR 屏装 HDR(任何屏可见)。**二者可合成一个「HDR 观感增强」VK 层**(一 shader 按输入/输出域分两支+一开关), **建议合并做, 不各立一层**。

## 十、当前实现 vs 方案 差分(2026-10-01 核码)

**已落地(代码就绪, 待真机验)**:
- §4.1 实态位三平台齐(Dx11Window.hpp:73 / VkWindow.hpp:124 / MetalWindow.hpp:24) ✅
- §4.2 统一检查点 `checkTargetPassthrough()` 存在(VideoRender.cpp:382), `onRenderWindow` 两路都下发窗口(WindowRender.cpp:430/433) ✅
- §4.3 `HdrMode::vkHDR(3)`+lane 分岔归一化(WindowRender.cpp:64-77) ✅
- 分册 §6.1 Win 输出端 10bit 翻转 + `fetchFrame` 直通态拒绝(Dx11CSVideoRender.cpp:299/652/818) ✅
- G4 16F 条件化、G9 Win CPU 腿、G10 `VkPqUpsampleLayer`+`pqUpsample.comp` ✅
- **D1 读回防护四腿齐**: Win 既有 + `MetalRender::fetchFrame`(bF16Pipeline 拒绝)/`EglVideoRender::fetchFrame`(bTargetPassthrough 拒绝)/`VkOutputLayer::fetchData`(VkFormat 16F/A2B10G10R10 拒绝) ✅
- **D2 统一检查点四腿同源**: `VkVideoRender`/`Dx11CSVideoRender` 既有 + `MetalRender::vaildAndInitGraph`(改走 `checkTargetPassthrough`, 不再自读 `metalHdrPassthrough`)/`EglVideoRender::vaildAndInitGraph` 新接 ✅
- **D3 同帧对齐方案②落码**: 见分册 §3.4 落地清单 ✅

**未落地 — 代码缺口**:

| # | 缺口 | 现状 | 影响 | 建议批 |
|---|---|---|---|---|
| **D5** | **VK 出图交平台 GPU 资源的格式闸只放行 rgba8/bgra8** | `VkOutputLayer::onCommand:184` `bCanMapGpu = rgba8\|\|bgra8`——该闸是 **`setVulkan(false)` 时** VK 腿交平台资源的唯一入口。**panvox 走 `setVulkan(true)`→`VkWindow`(原生窗直渲, `outputGpuData` 格式无关), 不经此闸 ⇒ 不受影响**。第二层: `VkWinImage::bindD3D:58` 用 `getImageDXFormt`(无 16F 分支) 定纹理格式 | 仅影响**仍以平台资源交帧**的消费者(`samples/vulkantest/dx11sharedtest\|dx11windowtest`、Unity/Avalonia 类): 在 lane=0 走 HDR(16F)出图会停帧/黑 | R2 补(低优先) |

> **D5 到底影响哪条出图支路(同日 17:45 二度订正)**: VK 出图有**两支**, 由 `bVulkan` 选 `window` 类型决定(`WindowRender::onSurfaceChange:117-133`)——
> - **`setVulkan(true)` → `window = VkWindow`(VK 原生窗, 自有交换链)** ⇒ 走 `outputGpuData`(:292, `blitFillImage` **格式无关**) 直 blit。**rgba16f 无碍。**
> - **`setVulkan(false)` → `window = Dx11Window/MetalWindow/EglWindow`** ⇒ VK 腿交平台资源, 走 `onCommand` 的 `bCanMapGpu` 闸(:184 只放行 rgba8/bgra8)。
>
> **panvox 现状 = 前者(D5 不适用)**: `native/shim/panvox_native.cpp:1743-1760` `pvx_player_create` 按 `g_nvvLane` 调 `setVulkan(lane!=1)`+`setSurface(宿主管口)`(「原生窗口直渲唯一车道」)。`enableVkOutputDx11`/NT 共享那条(**全仓无人置 true**)已是**死支路**。
> ⇒ **订正: D5 在 panvox 不成立**(此前两版判断均错)。**受限面 = 仍用 `setVulkan(false)`+`enableVkOutputDx11` 的平台资源消费者**(Unity/Avalonia/vulkantest 样例)。

**结构性缺口(非代码疏漏, 需独立批次/另批详设)**:
- **Mac 链F 通道不存在**(G10 跨平台半场): mac forceHDR 出已 EOTF 线性(非 PQ 码), 且 lane=0 时 Metal 画 drawable 直呈屏、IOSurface 仅离屏路径 ⇒ 无「Metal 产→VK 导入」通道。需独立批(与 G7 叠加后收益不足)——**列为不排期**。
- **R4 Android/Linux HDR**: EGL 无 HDR 呈现面口(G8), Linux 无 GPU 导入腿; §七明标「另批详设」⇒ **范围外**。
- **iOS EDR(G5)**: `MetalWindow.mm:58-63` iOS 腿恒不受理(无 NSScreen); 探测口径未评估 ⇒ 归 R3 出口。
- **VK 16F 域层兼容(Blend/VR/font)**: 16F 线性域合成未处理 ⇒ 归 R2 字幕/图像处理专项(方案已定稿 → [字幕画布多后端渲染计划](../player/字幕画布多后端渲染计划.md))。

**真机验收项(代码就绪但未验)**: R1(SU130 HDR10 片 lane=1 全 10bit 无 banding; 软解 HDR lane=1 直通) / R2(Win 软解 HDR×HDR 屏 FP16 高光顶出; 链F 硬解片高光顶出+图像处理可用) / 回归(SDR 片零变化、播中开关/跨屏/缩放往返, **依赖 D3**) 。

> **一句话结论**: R1/R2/R3 **编码已收口**(R3=D1+D2+D3 落地); **R5 经复核判定为非缺口**(硬解失败降级与继续播放已由引擎自愈链+既有埋点覆盖, 不新增接口, 分册 §6.4.1); 剩余仅**真机验收**, 外加 mac 链F/R4 两个非排期结构项。**D3(同帧对齐)已按方案②落码**(分册 §3.4), 「播中开关/跨屏/缩放往返不花屏」首次有了设计支撑; 该矩阵**必须真机跑**(重点: Win 竞态 A 的 crash 现场)。**新增(2026-10-01 晚)**: §6.6.1「三态呈现切换」(引擎零改动, 宿主加界面, 切档须走换道重开); §9.1/§9.2 SDR↔HDR 双向「观感增强」两新账(引擎需新 shader, 建议合并为一个 VK 层)。

## 十一、沿革 → [见分册 references/沿革.md](references/沿革.md)

立项与首验(2026-09-14~30) / 口径三定(一~四) / 独立复查与边界查证(五~六) / R1·R2 实施(七~九) / 跨平台核对与差分入档(十~十二) / R3 收口与宿主需求(十三~十五)——**全文已迁入分册**, 含逐次提交号与勘误(9173d98)。
