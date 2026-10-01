# 多平台 HDR 统一重构方案

> 状态: 进行中 · 上次核对: 2026-10-01 · 权威源: 引擎侧(本稿) / 宿主侧见下

> ⚠️ **权威源边界**: 本稿对 **avox 引擎侧** 事实(链路/行号/接口/缺口)是权威源; **宿主侧(panvox)** 事实
> (车道决策/换道/探测口径/徽章/自愈节流 §6.6)以 panvox 仓文档为准, 本稿仅转载摘要——改动请回 panvox 改,
> 勿在此就地编辑以免两处漂移。

> **统一口径文档**: 三平台 HDR 情况矩阵/超 RGBA8 流转/统一接口/落地清单/挂账全在本稿, 由 panvox 侧三份文档(`hdr-chain-plan.md`/`vk-hdr-lane.md`/`hdr-unified-refactor-plan.md`)合并替代迁入——用户拍板 2026-10-01, 那三份已删; avox 侧旧 HDR 文档(HDR管线改造计划/a09-hdr-dv backlog/VT路径补RPU-HDR解析)同日按用户令一并删除, 实施史归 git/记忆。**HDR 主题文档仅存本篇**。
> 链路事实: 2026-10-01 逐文件复核(未动代码, 文中行号当日核实)。本稿由 avox 会话接手维护(sess_53de1d6b 成文, 同日按用户定稿重构): 先理清所有情况 → 定方案 → 再实施, 实施批次见 §七。
> 同域文档: [颜色空间矩阵统一设计](颜色空间矩阵统一设计.md)(「单一真相源」原则本稿沿用; 该篇主题是录制链 rgba2yuv 矩阵, 非 HDR)。
> 路径约定: 本文在 avox 仓, `src/` 相对路径直接写; panvox 侧路径用 `panvox:` 前缀。
> 上下游分工: 引擎渲染/HDR 链路归 avox(本稿); 车道决策/宿主接线(lane 换道/forceHDR 下发/探测/徽章)归 panvox(engine_controller.dart, §6.6)。

## 一、总则: 双链路架构(拍板 2026-10-01)

### 1.1 拍板口径 v2(用户定稿 2026-10-01, 同日二稿修订)

**两条通道全平台同构: HDR 直通通道不做图像处理; 图像处理只走 VK(硬解帧仅 8bit 对接, SDR/PQ 码两种载荷)。**

1. **HDR 直通通道(HDR 内容 × HDR 呈现面)**: YUV10 解出的 RGBA超10(即超 RGBA8 域)直接对接窗口的 RGBA超10 呈现域, **全链不做图像处理**——硬解/软解一律, VK 暂定同样(后续经链F 渐进把图像处理加入 VK 的 HDR 呈现)。硬解=平台渲染器超 RGBA8 输出→原生窗口(R1); 软解=原生腿吃 CPU 帧(mac 已有, Win 补齐=G9; Android 恒 VK 车道由 VK 承担)或 lane=0 时走 VK yuv2rgba 16F 链(G4 修复)。
2. **VK 图像处理通道**: VK 是**所有平台唯一的图像处理链**(增强/特效/超分等; YUV→RGB 转换与 tone map 是呈现必需, 不算图像处理, 仍在平台腿/层内做)。接入口径:
   - **软解全接**: YUV 直进 VK yuv2rgba 层, 含 10bit——走 VK 自有 YUV 通道, 不经原生对接面;
   - **硬解只接 8bit, 载荷二选一**(对接面 VkInputLayer 恒 rgba8; 载荷判定靠 HdrMode 路由, 见 §二「不可混淆」注记):
     a) **SDR 载荷(链E, `follow`/`forceSDR` 下发)**: HDR 硬解帧由原生 tone map 降成 SDR 再对接——**超 RGBA8 不进 VK**;
     b) **PQ 码载荷(链F, 宿主以新增意图值 `vkHDR(3)` 下发)**: 原生 forceHDR 分支把 PQ 码原样写进 rgba8(PQ 亮度动态范围全在 0-1 码值里, 8bit 载体保住动态范围只损梯度精度), VK 侧**升样层**做 PQ EOTF 解码→16F 线性→HDR 呈现+图像处理/字幕合成; 归一化**按 lane 分岔**见 §4.3(vkHDR 只在 lane=0 折成 forceHDR 行为并启用升样层; lane=1 折 follow——**不可一律折 forceHDR**, 否则原生腿误开直通把 SDR 载荷当 PQ 码呈现=发灰), UBO/shader 协议零改动, 与 G10 同批放开;
   - VK 完成图像处理后按呈现面呈现。HDR 呈现×图像处理的组合经链F 可达; 全程超 RGBA8 的直通链(链A)恒不做图像处理, 仍为画质首选。
3. **呈现面实态驱动输出格式**: 输出跟「窗口当前是否真直通」走, 不跟 setHdrMode 命令走——命令被拒(SDR 屏/格式不可得)时输出自然回 SDR 域, 不产生命令与实态的漂移。
4. **直通链全链超 RGBA8**: 呈现域是 10bit(Win)/16F(Apple/VK) 时, 像素处理输出与中间纹理一律不落 8bit(Win 现状唯一违例=G3→R1)。tone map 链终点本就是 SDR rgba8, 不受此约束。
5. **翻转全链一致**: 呈现面实态翻转时, 像素处理输出与呈现面同帧对齐重建, 过渡帧数值直传不花屏(§3.4)。
6. **SDR 片 × 任意屏**: lane=0 VK 常规路径, 零变化。

### 1.2 三类角色职责(用户框定)

| 类 | 职责 | HDR 相关面 |
|---|---|---|
| WindowRender | 协调者: 持 window+双渲染器(pVideoRender 平台渲染器 / vkVideoRender), 按 bVulkan 分流 lane | setHdrMode 双路转发+直通被拒降级 follow(WindowRender.cpp:63); onRenderWindow 现只把窗口指针给 VK(WindowRender.cpp:401)→统一检查后两路都给(§4.2) |
| Window | 呈现面: 原生句柄/交换链/层的持有者 | setHdrPassthrough 下行命令(Window.hpp:83)+setHdrMeta(:86); **缺直通实态查询口**(→§4.1 新增 hdrPassthroughActive) |
| VideoRender | 像素处理: YUV→RGB(+VK 图像处理链) | 平台渲染器吃硬解 GPU 帧; **CPU 帧腿统一补齐**(→§1.3, G9); 原生腿不做图像处理(只做 YUV→RGB+直通/tone map); 输出域按呈现面实态定 |

### 1.3 CPU 帧支持矩阵(统一目标: 原生腿全平台都吃)

| 渲染器 | 硬解 GPU 帧 | 软解 CPU 帧(现状) | 软解 CPU 帧(目标) | 依据 |
|---|---|---|---|---|
| Dx11CSVideoRender(Win) | ✓ | ✗(cpuIn 直接不建图) | **补齐(G9, 入 R1)**: yuv420P10 上传→CS→超 RGBA8 输出 | Dx11CSVideoRender.cpp:297 |
| MetalRender(Apple) | ✓ | ✓(yuv420P/yuv420P10→P010 pb 进同一 16F 管线) | ✓(样板) | MetalRender.mm:417 |
| EglVideoRender(Android) | ✓ | ✗ | 恒 VK 车道下软解由 VK 全接, 无原生直通腿; R4 若开原生呈现再补 | 无 renderCpuFrame |
| VkVideoRender(全平台) | ✓(import, 仅 8bit 对接) | ✓(yuv2rgba 层, 含 10bit) | ✓ | VkVideoRender.cpp:780 |

推论(v2): 原生腿统一吃两种帧后, **硬解失败 lane=1 就地落软解超 RGBA8 直通, 不再画面停滞、不再必须换道**(G6 降级为上报/徽章, §五); lane 选择回归本义=**呈现路由**(直通腿 vs VK 图像处理腿), 与解码成败解耦。

## 二、情况矩阵(所有情况一张表)

| # | 片源 | 显示器 | 解码 | 链路 | 像素处理(YUV10→?) | 呈现面 | 现状 | 批 |
|---|---|---|---|---|---|---|---|---|
| 1 | HDR | HDR | 硬解 OK | lane=1 原生直通 | 平台渲染器 PQ 码原样(超 RGBA8 输出) | Win: R10G10B10A2+G2084 / Mac: RGBA16F+EDR | Mac 达标(样板); Win 输出恒 rgba8=G3 | R1(Win) |
| 2 | HDR | HDR | 硬解 fail | lane=1 原生腿软解(G9 后全平台)或 lane=0 VK, 两路等价 | 原生: YUV10→超 RGBA8(同情况1) / VK: yuv2rgba 16F | 原生窗口超 RGBA8 / VK FP16 交换链 | mac 已有待真验; Win 原生腿待补=G9(此前硬解失败画面停滞), VK 腿盖回=G4 | R1(补腿)/R2 |
| 3 | HDR | SDR | 硬解 OK | lane=0 VK | 平台渲染器 tone map→SDR rgba8(Win CS follow / Mac follow / And uHdrMode) | VK 合成→SDR 呈现 | 达标(曲线三腿 10/1 已统一) | 回归 |
| 4 | HDR | SDR | 硬解 fail | lane=0 VK | VK V5.comp tone map→rgba8 | 同上 | 达标(V5 已通吃 10bit 归一化) | 回归 |
| 5 | SDR | 任意 | 硬解/软解 | lane=0 VK | 常规 | SDR | 零变化 | 回归 |

播中变化(全平台适用): 系统 HDR 开关/跨屏搬家/换片 → 呈现面实态变化 → 统一检查发现→过界翻转重建(§4.2); 硬解失败(G6 事件上报)→徽章实态/降级 UX(原生腿就地吃软解帧, 换道非必需)。截图/缩略: 直通态拒绝或临时 follow(SDR)抽帧, 防脏图(§3.4)。
**图像处理路由注记(v2)**: 硬解帧(HDR 片)进 VK 二选一——保 HDR 走链F(PQ 码 rgba8→VK 升样), 落 SDR 呈现走链E(tone map 后对接); 软解帧直接 YUV(含 10bit)进 VK。链A(原生直通)恒不做图像处理。
**两种 rgba8 载荷不可混淆**: 链E 载荷=tone map 结果(1.0=SDR 白/BT.709/不可逆), 链F 载荷=PQ 码(1.0=PQ 满刻度/BT.2020/可逆)——同为 0-1 字节流**从数据无法侦测区分**(同一 0.5 在两边含义完全不同), 载荷判定恒靠 `HdrMode` 路由(vkHDR/forceHDR 行为态→链F 走升样层; follow/forceSDR→链E 当普通 SDR 处理), 禁止在 VK 侧做数据侦测。
车道决策与换道在宿主 panvox: 开播定道(trc∈{pq,hlg}+HDR 屏闸→lane=1)+播中换道+换片复位+屏况双向纠偏+3s 自愈重发(§6.6)。

## 三、超 RGBA8 HDR 流转(先理清流转, 再动手)

### 3.1 格式与域字典

| 格式/域 | 语义 | 出现位置 |
|---|---|---|
| P010 / NV12 | 硬解输出纹理, PQ 码值 0-1 归一化 | 解码器→平台渲染器 |
| yuv420P10 | 软解 CPU 10bit 平面, PQ 码值 | 解码器→VK yuv2rgba 层 / Metal renderCpuFrame |
| R10G10B10A2_UNORM + G2084 | **Win 直通域**: PQ 码原样, 动态范围全在 0-1 码值 | Win CS 输出(R1 新增)与 Win 交换链(Dx11Window 已有) |
| RGBA16Float 扩展线性 | **Apple 直通域**: 线性, 1.0=SDR 白; EDR extended linear ITUR-2020 | Metal 层/管线(MetalRender.mm:310/737) |
| VK_FORMAT_R16G16B16A16_SFLOAT + VK_EXTENDED_SRGB_LINEAR | **VK 直通域**: 同 Apple 语义 | VkWindow::setHdrPassthrough(VkWindow.cpp:498); **永不启用 VK_EXT_swapchain_colorspace**(AMD 崩 0xC0000374) |
| R8G8B8A8 / BGRA8 | SDR 域 | tone map 终点/SDR 呈现(合理落点) |

- PQ 是非线性 HDR 编码, 亮度动态范围全在 0-1 码值里——**8bit 中转不破坏「能播」**(9/30 a/b+10/1 发灰修复后肉眼确认「和系统对上了」), 代价=梯度精度(10bit→8bit→10bit 暗部/高光可 banding)。R1 去 8bit 中转为画质, 非可行性。
- Win=PQ 码域 / Apple·VK=线性域(1.0=SDR 白): 域不同是呈现面差异不是处理差异, 「值原样呈现」语义三平台一致。

### 3.2 现状流转(逐平台逐腿; 回答「HDR 硬解是否已在 metal/dx11 转 rgba8」)

**Win lane=1(直通链)——已转, =G3**:
P010 → Dx11CSVideoRender CS(processColor: forceHDR=PQ 码原样 / follow=tone map 锚线性+Reinhard) → **R8G8B8A8 共享纹理(createProgram 唯一输出创建点写死, Dx11CSVideoRender.cpp:440, 无 HDR 分支, DV program 同写此输出)** → Dx11Window blit(像素着色器纯采样直出, 无色彩处理) → 交换链 R10G10B10A2+G2084 / SDR=BGRA8(默认)。
`setHdrMode` 只刷 UBO(bParamsDirty)不重建图——输出格式无从跟随 HDR 态(重构动因); 渲染器拿不到窗口指针(onRenderWindow 只给 VK), 窗口只有命令口无实态口。

**Mac lane=1(直通链)——未转, 16F 全程=统一语义样板**:
硬解 CVPixelBuffer → CVMetalTextureCache 直进 shader(无中间 rgba8 纹理) → 管线色附随直通态翻转(MTLPixelFormatRGBA16Float / RGBA8Unorm, MetalRender.mm:737) → CAMetalLayer(applyLayerHdrConfig :310 层翻转+EDR)。翻转检测在 vaildAndInitGraph 每帧过界(wantF16 != bF16Pipeline → 层+管线同帧切+releaseGraph 全图重建, :354); 其 bResetFlag 原子读清消费是基类契约范例(:356)。软解 CPU 帧经 renderCpuFrame(:417)包装 P010 pb 进同一管线。
iOS: `MetalWindow::setHdrPassthrough` 恒不受理(无 NSScreen); MetalRender 翻转代码带 @available 编译保护。

**进 VK 的硬解帧(三平台 lane=0)——已转, 一律 rgba8**:
平台渲染器先转 rgba8 → VkInputLayer import(VkInputLayer.cpp:297; Win=DX11 共享纹理按 GetDesc **格式自适应重建** / Android=GLES texture id+EGLContext 判重 / mac=Metal IOSurface vkIosImage) → VK 合成 → VkWindow。
→ **VK 的 HDR 处理对硬解帧不生效的根因**: 输入早被平台渲染器转成 8bit(实证推翻「Windows 恒走 VK+forceHDR」旧口径的依据)。

**NT 共享通路的边界(2026-10-01 核代码, 回答「10bit 能不能只关在 dx11 内部」)**:
`Dx11SharedTex` 这条 NT 共享通路**生产端与消费端都在 dx11 目录内, 外面拿不到**——由类型系统保证, 不需要新增任何隔离接口:
- **生产端唯一出口**: `Dx11CSVideoRender::getGpuContext()` 返回私有成员 `outSharedTex.get()`(Dx11CSVideoRender.hpp:73)——`Dx11SharedTex` 是 `unique_ptr` 私有成员, 构造参数(尺寸/格式)不外露, 外部拿不到也造不出第二个;
- **消费端的天然闸**: `Dx11Window::renderContext` 先 `dynamic_cast<IDx11Context*>`, 再用 `bInteropTexture()` 分流(Dx11Window.cpp:150-160)——`Dx11SharedTex::bInteropTexture()` 返回 **true**(Dx11SharedTex.hpp:79)会被收进 `sharedTexture`; 普通 `IDx11Context`/`Dx11Context` 返回 **false**(Dx11Context.hpp:23)被静默忽略。**这把锁不是新加的, 是既有设计**;
- **唯一的外部接触面**: `VkWinImage::updateInputContext`(VkWinImage.cpp:177)——当 context 是 `Dx11SharedTex` 时置 `interopType = inputNt`(直读该纹理不拷贝), 而它只在 `bVulkan` 时被喂入(WindowRender.cpp:193 把同一个 `getGpuContext()` 交给 `vkVideoRender`)。**这条路正是链F 自己**; lane=1(VK 不参与)时该接触面不存在。
⇒ 结论: 「把 10bit NT 共享限定在 dx11 内部」的诉求**已由现状满足**; 要做的不是加锁, 而是**把格式描述补全**(§6.1 三处映射)让这条通路在 VK 侧也能正确建 image——见 §6.3 通用性三层。

**VK 软解腿——16F 被盖回=G4**:
CPU yuv420P10 → VkYUV2RGBALayer(onInitLayer forceHDR 分支 ~104 行设 rgba16f 选 yuv2rgbaHDR.comp, ~112 行**无条件 `outFormats[0].imageType=rgba8` 盖回**——HDR 变体写出的 >1 线性值进 8bit 纹理被钳 1.0, HDR 高光全丢) → 合成 → 窗。SDR=V5.comp tone map(V5 已通吃 p010 上传归一化, transfer 为运行时 UBO 分支)。CPU 读回(fetchFrame/mapStagingFrame)按输出纹理格式走, 输出变 10bit/16F 后需防护(§3.4)。

**Android / Linux**:
Android 恒 VK 车道: EglVideoRender NV12→rgba8(EglVideoRender.cpp:435)+自有 uHdrMode tone map 分支(:95) → VK → VkWindow(Flutter 纹理桥)。EGL 无 HDR 呈现面口=HDR 另批(G8)。Linux: CPU 帧→VK(inputCpuData), 无平台 GPU 导入腿, HDR 不做。

### 3.3 目标流转(四条链)

- **链A 硬解直通(情况1, 不接 VK)**:
  - Win: P010 → CS(PQ 原样) → **R10G10B10A2**(R1) → 交换链 R10G10B10A2+G2084(已有) → 屏。全链 10bit, 无 8bit 中转。
  - Mac: P010 → shader → RGBA16Float → CAMetalLayer+EDR(现状已达标)。
- **链B 软解直通(情况2, 两路等价)**:
  - 原生腿(v2 主路): yuv420P10 → 平台渲染器 CPU 腿(mac 已有 / G9 补 Win) → RGBA超10 → 原生窗口超 RGBA8——与硬解帧同一条直通链;
  - VK 腿(lane=0): yuv420P10 → VK upload → yuv2rgba **16F**(修 G4) → 合成链 16F 域 → VK FP16 交换链(已有) → 屏。
  - mac 原生腿现状即: yuv420P10 → P010 pb → Metal 16F 管线(§1.3, 已具备待端到端真验)。
- **链C tone map(情况3/4, =「原来 vulkan 路径」)**: YUV10 → SDR rgba8(硬解帧=平台渲染器 follow 分支; 软解帧=V5.comp) → VK 合成 → SDR 呈现。现状达标, 本重构不动 tone map。
- **链D SDR(情况5)**: 常规 lane=0, 零变化。
- **链E 硬解帧×图像处理(降 SDR)**: 硬解帧(HDR 片)→原生 tone map 降 SDR→rgba8 对接面(VkInputLayer)→VK 图像处理→呈现; SDR 片硬解帧直接 rgba8 对接(现状即此)。软解帧×图像处理不走对接面, 直接 YUV 进 yuv2rgba(链C/D 的 VK 侧)。
- **链F 硬解 HDR 保真进 VK(升样口子, v2 补, 用户定稿)**: YUV10 → 原生 forceHDR 分支把 PQ 码原样写进 rgba8(不做 tone map) → rgba8 对接面(VkInputLayer) → **VK 升样层: PQ EOTF 解码→16F 线性** → 图像处理/字幕合成(16F 域) → VK FP16 交换链 → HDR 呈现。
  - 依据: PQ 是非线性压缩, 亮度动态范围全在 0-1 码值里——8bit 载体保住完整动态范围, 只损梯度精度(暗部/高光可 banding); Win 原生直通链(链A 现状)就是同一原理在 8bit 中转上成立(9/30 a/b+10/1 发灰修复实证);
  - 定位: 「又要硬解、又要 HDR、又要 VK 图像处理/字幕」的唯一组合路径, 全平台可用(对接面即现有 rgba8 通道)。Win CS forceHDR 已产出 PQ 码 rgba8(现状); mac/Android 交接面需核对/补 PQ 码直通口径;
  - 实现要点: VK 呈现域恒 FP16 扩展线性(HDR10 色空间被禁, §3.1), 升样=显式 PQ EOTF(与 yuv2rgbaHDR.comp 的 EOTF 段同源); 链A 的「升样」由交换链 G2084 免费完成, VK 无 HDR10 色空间必须显式做;
  - **模式映射(v2 修订 2026-10-01: 加意图值 `vkHDR = 3`)**: 链A 用 `forceHDR`(原生直通), 链F 用新增 `vkHDR`(「转 VK 的 HDR 处理」)。设计: vkHDR 是**宿主意图值**——引擎在 `WindowRender::setHdrMode` 入口**按 lane 分岔归一化**(lane=0 → 折 forceHDR 行为: 直通命令+PQ 码直出+VK 升样层; lane=1 → 折 follow 并告警, **不可一律折 forceHDR**, 详见 §4.3), UBO/shader 协议仍只见 2(ColorSpace.hpp:35 / Dx11CSVideoRender.cpp:39 / EGL uHdrMode), 零协议改动; SWIG 三端随枚举重生成。加值依据: ①本枚举本就是宿主意图语言——forceSDR 在引擎无任何独立分支(全仓只特判 ==2, follow≡forceSDR 同为 tone map), 意图值有先例; ②「lane=0+forceHDR」组合无名字正是旧禁令 bug 的根源(组合语义未定义→被禁→链F 无法表达), 命名即防复发; ③VK 升样层键控不依赖新值(按 forceHDR 行为态+输入类型分流), 引擎内零新路径。
  - 代价与缓解: 一次 8bit 量化(PQ 域)——画质仍以链A(全程超 RGBA8)为首选; banding 可选 dither 缓解, 另批评估。
- 域交接: mac lane 往返(Metal↔VK)交接面恒 rgba8 IOSurface(现状保持); Win lane=1↔lane=0 换道经宿主拆面复挂。⚠️ **「交接面恒 rgba8」只说了载体格式, 没说载荷语义**——mac 的 forceHDR 分支出的是**已 EOTF 的线性**(非 PQ 码), 且 lane=0 时 Metal 画的是 drawable 而非 IOSurface, 该交接面在 mac 结构上不成立, 详见 §6.3「交接面语义核对」。

### 3.4 过界与过渡

- **翻转时序差(风险等级: 中, 原稿「理论无害」定性已修正 2026-10-01)**: 交换链由 `setHdrPassthrough` 在**宿主线程**同步翻(Dx11Window.cpp:294), 输出纹理由 `bResetFlag` 在**渲染线程**消费后重建(VideoRender.hpp:46 契约), 二者**不同帧**——中间必然有过若干帧「交换链已 10bit+PQ、输出仍是 rgba8」或反向。此时 `Dx11Window` 的 blit 是**像素着色器纯采样直出、无任何色彩处理**(§3.2), 若输出是 tone map 结果(SDR 码)而交换链已挂 G2084, 就是把 SDR 码当 PQ 码呈现 → **这几帧必发灰/过曝**(与 §4.3 lane=1 误折 forceHDR 同一病根)。
  - 原稿「UNORM→UNORM 值域同 0-1, 只损位深不花屏」的论证**只在两端同为「PQ 码」或同为「SDR 码」时成立**; 值域不同(SDR 码 vs PQ 码)时位深论证失效。**位深与值域是两件事**。
  - **必须定义同帧对齐手段**(择一, 实施时定): ①交换链翻转推迟到输出重建完成后(输出先重建并在下一帧 blit 时一并切换); ②过渡帧按 SDR 口径呈现(follow 兜底); ③翻转期间用中间格式(R10G10B10A2 双端, 只切 colorSpace)。**§八验收「播中开关 HDR / 跨屏 / 缩放往返不花屏」依赖本条的落地, 缺它则验收无设计支撑**。
- **CPU 读回防护**: fetchFrame/mapStagingFrame 按输出纹理格式, 遇直通格式(rgba10/16F)拒绝并日志; 截图走 SDR 口径(临时 follow 抽帧), 防脏图。
- **3s 自愈重发节流(da27cbf)必须保留**(宿主侧, §6.6)。

## 四、统一接口设计(所有平台同构)

### 4.1 Window 基类: 直通实态查询口(G2, 新增)

`src/avox/video/Window.hpp`:

```cpp
// 呈现面直通实态(交换链/层已真切上 HDR 即 true); 命令被拒/SDR 屏= false。
virtual bool hdrPassthroughActive() const { return false; }
```

实现口径(**语义 = 交换链/层「当前」真切上的状态**, 供 §4.2 实态缓存比对; 各平台现成位不齐, 需按下表收口):
- Dx11Window = 直接返回 `bHdrActive`(Dx11Window.hpp:46)。它**本身就是实态**: `SetColorSpace1` 失败会回滚成 false(Dx11Window.cpp:295), `rebuildDevice` 重置(Dx11Window.cpp:191), resize 由 `applyHdrSwapchainState` 重入兜底(10/1 发灰案修复已验收)。**原稿「&& 交换链格式==R10G10B10A2」是冗余的**, 且与 `initBuffers()` 重入时序叠加时可能产生假阴——移除。
- VkWindow = **取 `bHdrActive`(交换链当前态), 不取 `bHdrPassthrough`(意愿位)**。两者是两个独立位(VkWindow.hpp:135-136): `bHdrActive` = 交换链当前是否直通; `bHdrPassthrough` = 直通意愿(重建/换面时保持选型)。`setHdrPassthrough` 的幂等短路用 `bHdrActive == bPassthrough`(VkWindow.cpp:502), 而换面重建路径**会重置 `bHdrActive` 而保留 `bHdrPassthrough`**——故「实态」必须取前者, 取后者会在换面后误报 true。
- MetalWindow = `std::atomic<bool> metalHdrPassthrough`(MetalWindow.mm:10, setHdrPassthrough 过 EDR 探测后置位——翻转在渲染器, 意愿位即实态)。⚠️ **它是 `namespace avox` 下的全局量, 多窗口/多实例会串**(方案未考虑多窗口); 若接受单窗口假设需在 §4.2 注明, 否则应改实例成员 + 原子。

### 4.2 VideoRender 基类: 统一检查点(G1/G2 收口)

`src/avox/video/VideoRender.hpp`(renderWindow 现为空虚函数 :186; bResetFlag 原子重建标志 :46, 消费契约见其注释):

- 基类实现 `renderWindow(Window*)`: 存 `Window* targetWindow`(弱引用, 生命周期归 WindowRender)并立即刷新一次实态;
- 基类字段 `bool bTargetPassthrough = false;`(实态缓存);
- 基类检查方法(渲染线程每帧入口, 或各派生 vaildAndInitGraph 首行调用): targetWindow 非空 → 读 `hdrPassthroughActive()` → 与缓存不一致 → 更新缓存+置 `bResetFlag`+日志一行;
- `WindowRender::onRenderWindow`: **两路都调** `renderWindow(window.get())`(现只给 vkVideoRender, WindowRender.cpp:401); 贴图腿/离屏时 window 为空, 判空即安全;
- 派生在重建时按实态定输出格式: Win=createProgram 分支 R10G10B10A2/R8G8B8A8(§6.1); Mac=已有翻转不动; VK=16F 变体条件(§6.3)。
- 参照范例: VkVideoRender::renderWindow(消费窗口 VkRenderContext)、MetalRender 对 bResetFlag 的原子读清消费(MetalRender.mm:356)。

### 4.3 命令下行(小改: vkHDR 归一化)

setHdrMode → WindowRender.cpp:63: 先 `window->setHdrPassthrough`(被拒→降级 follow, 防 shader 跳过 tone map 落 SDR 面过曝)再双路转发渲染器; setHdrMeta 同转发(:77)。

v2 增量: 新增 `HdrMode::vkHDR(3)` = 「转 VK 的 HDR 处理」意图值。**归一化必须按 lane 分岔, 不能一律折成 forceHDR**(2026-10-01 核代码修正):

| lane | 收到 vkHDR 后 | 理由 |
|---|---|---|
| **lane=0**(VK 腿) | 折成 `forceHDR` 下发: `setHdrPassthrough(true)` 切 FP16 交换链 + 双路转发 forceHDR + 启用升样层(链F) | 与 `forceHDR` 在 VK 腿的既有行为一致, 正是链F 要的 |
| **lane=1**(原生腿) | **折成 `follow`**(不是 forceHDR, 也不是「告警后按链A 执行」) | 宿主意图是「交给 VK 做图像处理」, 而 lane=1 没有 VK 处理链; 若按 forceHDR 执行会**误开原生直通交换链**(`Dx11Window.cpp:294`)并把 SDR 载荷当 PQ 码送上 G2084 面 = 直接发灰 |

**为什么不能一律折 forceHDR**(这是原稿的错): `WindowRender::setHdrMode` 第一件事就是 `window->setHdrPassthrough(mode == HdrMode::forceHDR)`(WindowRender.cpp:68)——折成 forceHDR 会让 lane=1 的原生窗口也去切直通交换链, 而链E(降 SDR 对接 VK)与链F 的前提都是「**原生不做直通、只产 rgba8 载荷**」。lane=1 收 vkHDR 属宿主路由错误, 正确动作是**降回 follow 走 SDR 呈现**并告警, 而不是嘴上说「按链A 执行」——代码里没有任何 lane 信息可用来拦, 只能靠这里折对。

(备选实现: 把 `setHdrPassthrough` 的口改成三态 `none/native/vk`, 语义更直白但改动面大; 上表是零新增路径的最小改法。)
UBO/shader 协议仍只见 2(forceHDR); setHdrMeta 照常转发。SWIG 三端随枚举重生成。

### 4.4 平台分治点(统一「检查」, 分治「翻转实现」)

| 平台 | 翻转实现 | 状态 |
|---|---|---|
| Win | 输出(createProgram)与交换链两端同翻(双向重建已有: 开向 10bit+SetColorSpace1 / 关向回 8bit+重挂, Dx11Window.cpp:272) | R1 补输出端 |
| Mac | 层+管线同帧翻转(vaildAndInitGraph 过界对齐+releaseGraph 全图重建) | 达标, 只接 §4.1/4.2 |
| VK | 交换链 FP16(VkWindow 已有)+层输出 16F 变体(条件化修 G4) | R2 |

## 五、缺口台账

| # | 缺口 | 位置 | 归属 |
|---|---|---|---|
| G1 | 呈现格式决策分散: Win 恒 rgba8 写死 / Metal 层翻转(已实现) / VK FP16+变体(已实现), 无统一语义 | 三平台 | §四统一 |
| G2 | Window 基类无直通实态查询口; pVideoRender 拿不到窗口指针 | Window/WindowRender | §4.1/4.2 |
| G3 | Win 直通链 8bit 中转(10bit→8bit→10bit) | Dx11CSVideoRender.cpp:440 | R1 |
| G4 | VK 软解 16F 覆盖: forceHDR 分支设 `outFormats[0].imageType = rgba16f` 后, 被**后续所有分支共用的格式归一化段**覆盖回 rgba8(`VkYUV2RGBALayer.cpp` onInitLayer, forceHDR 分支设 16F ≈ :103, 共用归一化 `inFormats[0]=r8 / outFormats[0]=rgba8` ≈ :117)——HDR 变体写出的 >1 线性值进 8bit 纹理被钳 1.0, 高光全丢。**修法不是简单加条件**: 该归一化段还承载「10bit 平面字节视图(`r8` 输入视图 + `outFormat` 高度按 10bit 重算)」约定(≈ :117-130), 需把 16F 赋值**移到归一化段之后**并理顺字节视图路径, 同时 `VkTexture` 16F 建纹理/内存尺寸/upload 字宽跟随; 「CPU YUV10→VK 16F→16F 窗」端到端未跑通 | VkYUV2RGBALayer.cpp onInitLayer + VkTexture | ✅ R2(2026-10-01(九)): 16F 赋值已移段末并条件化; `VkTexture` 经核无需改(`formatTable` 已含 16F)。仅剩真机端到端未跑 |
| G5 | iOS EDR 探测缺口(MetalWindow 恒不受理, 无 NSScreen) | MetalWindow | R3 |
| G6 | 硬解失败无事件上报(只有 `fallback to software` 日志, FFDx11Decoder)——v2 后换道非恢复必需, 上报保留用于徽章实态/降级 UX | FFDx11Decoder/shim/宿主 | R5 |
| G7 | mac VK 窗 MoltenVK 无 EDR(恒 SDR 呈现)→「硬解 fail→VK 接手真 HDR」在 mac 不可达; mac 场景② 改落原生腿(§1.3), VK 16F 链主供 lane=0 路径 | VkWindow(mac) | R3 评估 |
| G8 | Android HDR 呈现面口(EGL/Flutter 桥均无 HDR) | EglVideoRender / VkWindow(Flutter) | R4 另批 |
| G9 | Win 原生腿不吃 CPU 帧(cpuIn 不建图)→补 yuv420P/yuv420P10 上传→CS 腿(对齐 Metal 模式), 硬解失败 lane=1 就地直通; Android 由 VK 承担(R4 开原生呈现时 EglVideoRender 同补) | Dx11CSVideoRender | **R1 已落地** |
| G10 | 链F 升样层缺失: VK 无「rgba8 PQ 码→16F 线性」EOTF 层; mac/Android 交接面 PQ 码直通口径未核对/未补。**前置依赖(原稿漏记)**: 链F 输入侧(VkInputLayer 导入 D3D11 共享纹理)需要 `ImageType::rgba10` 在 `getVkFormat` 有映射(VkHelper.cpp:344, 现缺→`VK_FORMAT_UNDEFINED`)——即 §6.1 的三处映射补齐是 G10 的**硬前置**, 少补则 R2 一动导入 10bit 就撞 UNDEFINED | avox_vulkan 新层 + MetalRender/EglVideoRender | ✅ **主体 + Win/Android 半场已落**(2026-10-01(九)(十)): `VkPqUpsampleLayer`+`pqUpsample.comp` 已接入 graph; Win CS 与 Android `EglVideoRender` 的 forceHDR 分支均已产「PQ 码原样」rgba8, 与升样层契约一致。**mac 转独立缺口**（`MetalRender` forceHDR 出已 EOTF 的线性 + lane=0 交接面结构不成立, 详见 §6.3 交接面语义核对), 与 G7 叠加后收益不足 |
| G11 | **VK 交平台 GPU 资源的格式闸未含 16F**(**仅 `setVulkan(false)` 路径**): `VkOutputLayer::onCommand:184` `bCanMapGpu = (rgba8\|\|bgra8)`, HDR 内容在 VK 腿出的是 rgba16f ⇒ 整段 interop(平台资源)被跳过。**第二层**: `getImageDXFormt`/`getImageType`/`getVkFormat` 三处映射亦无 16F(Dx11Helper.cpp:12-48 落 default=RGBA8)。**受限面(17:45 订正)**: **panvox 不受影响**(走 `setVulkan(true)`→VkWindow 原生窗直渲, `outputGpuData` 格式无关); 仅影响用平台资源交帧的消费者(Unity/Avalonia/vulkantest 样例) | VkOutputLayer.cpp onCommand + Dx11Helper 三映射 | R2 补(随平台资源消费者启用 HDR); 详见 §十 D5 |

## 六、平台落地清单

### 6.1 Windows: 输出翻转(G3, R1 主体)

`src/avox_windows/dx11/Dx11CSVideoRender.cpp`:

- `createProgram` 输出创建分支: `bTargetPassthrough ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM`;
- `ImageType` 增 `rgba10`(AVOX_MAP_IMAGE, src/avox/AvoxImage.h:11; 现 rgba16f=15 无 10bit 表达) + Dx11Helper **三处**映射同时补: `getImageDXFormt`(现缺→default 落 rgba8)、`getImageType`(现缺→default 落 **`ImageType::other`**, Dx11Helper.cpp:46/76)、`getVkFormat`(现缺→**`VK_FORMAT_UNDEFINED`**, VkHelper.cpp:344/365)——**三处少补一处即断链**;
  - **改法=纯追加, 零行为变更**(2026-10-01 核代码): 三处现有分支**无一处被改动**, 只追加 `case`; 且**全仓当前没有任何路径产出 `ImageType::rgba10`**(枚举 0~15 全排满、无 16), 故补 case 的**实际生效面=0**——可作为链F 的**预备先落**, 不必等 R2 与升样层同批。风险仅在「枚举加值」这一动作本身, 见下方影响面;
  - **影响面四件套(原稿只列了「三端 shim 重编」, 不全)**: ①`AvoxImage.h` 追加 `XX(rgba10, 16, 4, 4, "rgba10")`(0~15 值不动, ABI/序列化向后兼容); ②上述三处映射; ③**SWIG 三端重生成**(csharp/java/nodejs——`ImageType` 是 X-Macro, 枚举值进绑定层, 不重生成则绑定与引擎不同步); ④doc 侧 `code-wiki/03-核心类与关键接口.md:245` 的 `ImageType` 清单需同步(全仓唯一列全该枚举的文档处);
- SRV 已按实际 texDesc 建(Dx11SharedTex.cpp:166), 核对 UAV 建口同跟随;
- **共享纹理格式支持面(2026-10-01 核代码澄清, 原「驱动待验」定性已修正)**: R10G10B10A2_UNORM 的 NT 共享本身**无代码障碍**——`Dx11Resource.cpp:89-99` 建纹理不校验 `textureDesc.Format`,`Dx11SharedTex` 默认即 `sharedNT`,D3D10 起该格式即标准 RT 格式。**真正的障碍是消费端格式映射表缺 10bit**(即上行三处),非驱动兼容性; 此条从「不可控待验」降级为「可控补码」;**D3D11↔Vulkan 互操作**才需要真机验(创建/内存导入/blit 驱动差异),纯 D3D11↔D3D11 无此风险;
- **该通路已天然内聚在 dx11 内(2026-10-01 核代码)**: 生产端=`Dx11CSVideoRender` 私有成员 `outSharedTex`(唯一出口 `getGpuContext()`, Dx11CSVideoRender.hpp:73 直接返回指针), 消费端=`Dx11Window` 以 `bInteropTexture()` 为闸(Dx11Window.cpp:155)——外部拿不到这个对象, **不需新增「仅 dx11 可用」标志位**; 唯一外部接触面是 VK 的 `inputNt` 导入(即链F), 详见 §3.2「NT 共享通路的边界」。故 10bit 化**不必先做隔离**, 直接把格式映射补全即可;
- `fetchFrame`/`mapStagingFrame`: 输出为 rgba10 时拒绝并日志(截图走 SDR 口径);
- shader 无需改(forceHDR=PQ 原样写 UAV, 10bit UNORM 同语义);
- **格式选型注记**: Win 输出跟 HDR10 呈现面(R10G10B10A2+G2084, 已验收链)走, 不用 mac 的 RGBA16Float——PQ 码值恒在 0-1, FP16 的 >1.0 动态余量只在线性呈现域(mac/VK)才有意义, 码值域里 64bpp vs 30bpp 纯属带宽浪费; 与 mac 真正同域的方案是换 scRGB FP16 交换链(G10 色空间)整链改版(重做验收+CS 加 PQ EOTF), 记为未来选项不列入本稿; R10G10B10A2 支持面: D3D10 起标准 RT 格式, 现役 GPU typed UAV 全覆盖, 唯一需真机验的是 NT 共享跨上下文组合(§九);
- CPU 帧腿补齐(G9, 已落地): 补 `renderCpuFrame` 上传 yuv420P/yuv420P10 平面(自建 DYNAMIC NV12/P010 纹理, 10bit `<<6` 归一化语义对齐 MetalRender.mm:417 的 P010 路)→进既有 CS→超 RGBA8 输出——硬解失败 lane=1 就地直通, 不再画面停滞。设备取自呈现窗口(同设备, 免跨设备句柄), `WindowRender::render(YUVFrame)` 同步补 `window->renderContext()` 下发。

### 6.2 macOS: 对齐(基本不动)

Metal 腿已有完整翻转(§3.2), 语义与 §一一致——只把 `MetalWindow::hdrPassthroughActive` 接上供基类统一检查/日志; 渲染器侧行为不变。

### 6.3 VK: HDR 内容链(G4+G10, R2 主体)

**前置**: §6.1 的 `ImageType::rgba10` + **三处映射**(getImageDXFormt/getImageType/**getVkFormat**)必须先落——链F 输入侧(VkInputLayer 导入硬解共享纹理)要靠 `getVkFormat` 认出 10bit, 否则建 image 得 `VK_FORMAT_UNDEFINED`(详见 G10)。

- `src/avox_vulkan/layer/VkYUV2RGBALayer.cpp` onInitLayer: 16F 赋值移到格式归一化段**之后**(原稿「~112 行盖回条件化」过简——该段还承载 10bit 字节视图约定, 见 G4); ✅ **已落**(2026-10-01(九), 条件化为「10bit + forceHDR」);
- `src/avox_vulkan/vulkan/VkTexture.cpp` 16F 建纹理/内存尺寸/upload 字宽适配(现默认 8bit); ✅ **核代码确认无需改**: `VkHelper.cpp:151` 的 `formatTable` 已有 `{VK_FORMAT_R16G16B16A16_SFLOAT, {8, 4}}`, `vkPixelSize` 直接命中; 上游 `VkOutputLayer` 复制 `inFormats[0]` 自动跟随, 格式协商无需额外代码;
- graph 下游格式协商(canvas→output→交换链)跟 16F; Blend/VR/font 层 16F 域兼容先保主链, 余项跟进批;
- 升样层(G10, 链F): 新增「rgba8 PQ 码→16F 线性」compute 层(EOTF 段与 yuv2rgbaHDR.comp 同源), 接在 VkInputLayer 导入之后、合成之前; ✅ **已落**(2026-10-01(九)): `VkPqUpsampleLayer` + `glsl/pqUpsample.comp`, 拓扑条件 = `hdrMode==forceHDR && !cpuIn && !bRgbaInput`, 插在 `inputLayer`(或 `yuv2RGBA`)之后、其余层之前; `glslindexcurrent.txt` 已登记, 12 shader 编译全绿。✅ **交接面口径已核**(2026-10-01(十), 见上「交接面语义核对」表): **Win/Android 就绪**; **mac 不一致转独立缺口**; ⚠️ 改 `.comp` 后**必须手动** `python glsl/compileglsl.py`(cmake 不含此步, 否则运行时仍加载旧 spv);
- 修完真验: 软解 HDR 片(或强制软解)×HDR 屏, FP16 交换链+16F 内容链端到端(Win); 链F 用硬解 HDR 片同屏验(高光顶出+图像处理/字幕可用)。

**链F 的真实定位(2026-10-01 核平台结构后修正——原稿按「给 Win 增强」理解, 偏了)**:

链F 不是「给已有直通的平台再添一项」, 而是**无原生 HDR 腿平台的唯一 HDR 出路**:

| 平台 | 原生直通腿 | 事实依据 | 链F 的意义 |
|---|---|---|---|
| Win | ✅ 有(Dx11Window 10bit+G2084) | 已验收 | 冗余(链A 画质更好) |
| Mac | ✅ 有(Metal EDR) | 已验收 | 冗余 |
| **Linux** | ❌ **无** | `src/avox_linux/` 只有 `X11Surface`/`WaylandSurface`(`ILinuxSurface`, **非 `Window` 派生**)+PulseAudio; 唯一的 Linux 窗口是 `VkWindow`(自己 `__ONLY_LINUX__` 直接吃 X11/Wayland, VkWindow.cpp:348/376) ⇒ **Linux 今天就只走 VK 呈现** | **唯一 HDR 路径**: 无链F 则 Linux HDR 片只能降 SDR(链C), 永远出不了 HDR |
| Android | ❌ 无 HDR 口 | 唯一的 `EglWindow` **零 HDR 代码**(`grep -i hdr src/avox_egl/EglWindow.*` 为空); `EglVideoRender` 的 `uHdrMode` 是 **tone map 分支**, 不是 HDR 交换链 | 同 Linux(若 EGL HDR 口不补) |

⇒ **链F 的优先级应由「跨平台 HDR 覆盖」驱动, 不由 Win 的画质驱动**。在 Win/Mac 上它是冗余;
在 Linux/Android 上是**唯/首要**手段。原稿把 R2 排在 R1 之后、定位为「Win 增强」,
掩盖了它对 Linux 的**必要性**——Linux 若确定「只做 Vulkan 渲染」, 链F 即其 HDR 前提。
- Linux 落地前提: `VkWindow::setHdrPassthrough` 是平台无关的(VkWindow.cpp:498, 已禁 `VK_EXT_swapchain_colorspace`——AMD 崩 0xC0000374), 呈面缺的只是「X11/Wayland WSI 上 FP16 交换链能否跑通」= **真机验项, 非代码缺失**。

**交接面语义核对(G10 跨平台半场, 2026-10-01(十) 逐平台核代码)**:

升样层假定输入是「**PQ 码原样**(未做 EOTF)」的 rgba8。三平台原生渲染器在
forceHDR(`hdrMode==2`)分支下到底往 rgba8 交接面写了什么, 差异很大——**这是 G10
能否跨平台的关键, 也是「同为 rgba8 载荷但语义不同」的又一实例**(§3.3 两种 rgba8 载荷注记):

| 平台 | forceHDR 分支产出 | 交接面载体/格式 | 与链F 契约 | 结论 |
|---|---|---|---|---|
| **Win** | `Dx11CSVideoRender` CS: **PQ 码原样**(不做 tone map) | `outSharedTex`(rgba8, VK 腿时 §6.1 已恒 rgba8) | ✅ 一致 | 链F 直接就绪 |
| **Android** | `EglVideoRender` `processColor`: `uHdrMode==2` → **`return rgb;`**(PQ 码原样) | FBO 纹理 `GL_RGBA`/`GL_UNSIGNED_BYTE`(rgba8, `EglVideoRender.cpp:305`) | ✅ 一致 | 链F 直接就绪 |
| **Mac** | `MetalRender` `processColor`: `hdrMode==2` → **`pqToLinear(rgb)*100.0`(已做 EOTF, 出线性)** | IOSurface `32RGBA`(rgba8, `MetalRender.mm:768/775`) | ❌ **不一致** | **链F 会二次解码** |

⚠️ **Mac 的两点事实(2026-10-01 核代码, 颠覆「mac 交接面照旧」的原判)**:
1. **mac 的 forceHDR 分支是「EOTF 已做」的线性域输出**, 与 Win/Android 的「PQ 码原样」**语义相反**。
   若 mac 走链F, 升样层会对已线性化的值再跑一次 `pqToLinear` ⇒ 画面严重错暗/错色。
2. **mac lane=0 的交接面在结构上也不成立**: `MetalRender.mm:933-938` 的渲染目标是
   **`metalLayer` 存在就画 drawable(直呈屏), 否则才画 `outputTexture`(IOSurface)**。
   即 mac 有窗口时 Metal **自己直接上屏**, IOSurface 这条 rgba8 路只在**离屏**时才是渲
   染目标 —— 与 Win(CS 恒产 `outSharedTex` 供 VK 导入)的「生产端恒在」结构不同。
   ⇒ mac 的 lane=0 HDR 交接**今天就没有可用的「Metal 产 → VK 导入」通道**,
   而非仅缺格式映射。

**⇒ G10 跨平台结论: Android 就绪(Win 亦就绪); mac 需先定义「Metal 侧 PQ 码直通变体
+ 切 IOSurface 为 VK 腿渲染目标」, 属独立批次**(与 G7「mac VK 窗无 EDR」叠加后,
mac 走链F 的收益本就不足——原生 EDR 腿已达标, 链F 在 mac 是冗余)。**Linux 无原生腿、
无 GPU 导入腿(`VkInputLayer.hpp:5-9` 三段条件编译不命中), 走 CPU 上传, 不涉本条**。


**通用性三层结构(2026-10-01 核代码, 回答「接口要通用, 后面能直接给 Linux/鸿蒙」)**:
链F 这条通路按「通用程度」分三层, 混在一起谈会误判工作量——**格式语义层是通用资产, 平台层各家自补**:

| 层 | 通用性 | 事实依据 | 本次要做的 |
|---|---|---|---|
| **格式语义层**(`ImageType::rgba10` + 三处映射) | ✅ **全平台共编, 完全通用** | `AvoxImage.h`/`Dx11Helper.cpp`/`VkHelper.cpp` 都是跨平台编译单元, 无 `#ifdef` 平台分支 | 补枚举+三处映射(§6.1)——**这一步就是给 Linux/鸿蒙铺路** |
| **导入机制层**(`VkWinImage`/`VkAndImage`/`VkIosImage`) | ❌ **逐平台各一套** | `VkInputLayer.hpp:5-9` 条件编译: `#ifdef WIN32`→VkWinImage / `__ANDROID__`→VkAndImage / `__APPLE__`→VkIosImage; **Linux 三段都不命中, 无 GPU 导入腿**(`inputGpuData` 的平台分支 :305/:348/:373 无 Linux 段), 只能 CPU 上传(`inputCpuData`→`onFormatChange`) | **不做**——Linux 的 GPU 导入属 Linux 批次 |
| **跨 VkDevice 层**(`VkSharedImage`) | ⚠️ **声明通用, 实现仅 Win32** | 声明层已按平台无关设计(`VkShareHandle` 含 `opaqueFd`/`androidHwBuffer`, 头注写「平台无关」); 但 `importFromHandle` 硬编码 `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT`(VkSharedImage.cpp:239-240), 且导入整段在 `#ifdef _WIN32` 内(:266-311), 非 Windows 时该段被跳过 | **不做, 只记缺口**(§九)——Linux/鸿蒙真要启用该通路前需补 fd 分支 |

⇒ **结论**: 「接口通用」的诉求在**格式语义层已经满足**(X-Macro 枚举 + 映射函数本就跨平台); 缺的是 Linux/鸿蒙各自的 handle 导入实现, 属各自批次。**现在补映射 = 给未来铺路, 不是给 Windows 打补丁**——Windows 用它与 Linux/鸿蒙用它是同一个枚举、同一份映射。

### 6.4 硬解状态上报(G6, R5)

- FFDx11Decoder 开档硬解判定失败(vp9/av1 profile 不支持等 `fallback to software` 路径)冒宿主事件/状态位;
- shim 加查询或回调口(与 panvox 徽章「下发成功≠直通生效」实态上报同批设计);
- panvox 收事件后用于徽章实态/降级 UX——v2 口径下原生腿就地吃软解帧(§1.3), 换道(`switchNvvLane`)**不再是恢复必需**, 保留为呈现路由手段。

### 6.5 iOS / Android / Linux

- iOS: EDR 探测另批(G5); MetalRender 翻转代码已带 @available 编译保护在包内;
- Android/Linux: R4 另批详设(不在本稿展开)。

### 6.6 宿主侧(panvox)已落地状态与分工

以下已在 panvox 仓落地(原 hdr-chain-plan.md 记载, 迁入存档):
- 车道体系: `applyNvvLaneForRef` 开播定道(HDR 片 trc∈{pq,hlg}+显示闸 → lane=1, 平台不分 Windows/Apple——10/1 口径反转后恢复, panvox 2e5d038)+播中换道(`switchNvvLane(1)` 拆面复挂)+换片复位+每拍屏况双向纠偏+3s 自愈重发(da27cbf, 必须保留);
- 显示闸: P2 探测口 `panvox:app/lib/core/platform/hdr_display_probe.dart`(DisplayConfig 主窗屏 HDR 激活, 3s TTL)+Apple EDR 头距; 设置档「HDR 直通」开关;
- 徽章: 播放页「HDR 直通/HDR→SDR」点亮式(判据=车道落地+setHdrMode 下发成功; 实态上报收口后吃引擎真值, 归 R5);
- tone map/直通呈现链的用户侧验收: DX11 直通 a/b(9/30)、发灰案(Dx11Window resize 静默掉交换链 HDR 态)修复验收(10/1)、tone map 曲线三腿统一(10/1, mac MetalRender.mm 同曲线已部署);
- **教训**: HDR 桌面截图走 HDR→SDR 转换恒提亮压平——HDR 屏验收一律只认肉眼, 截图对照作废。
- 口径 v2(10/1 二稿)注记: 硬解失败恢复不依赖换道(原生腿就地吃 CPU 帧, §1.3); 上述换道机制保留为呈现路由手段。

## 七、里程碑

| 批 | 内容 | 出口 |
|---|---|---|
| **R1 Windows lane=1 全程 10bit + CPU 腿**(先行, 用户拍板) | §4.1+§4.2+§6.1(含 G9 CPU 腿) | SU130 放 HDR10 片: 输出/交换链/呈现全 10bit, 肉眼对照系统播放器无 banding 差; 软解 HDR 片 lane=1 直通(G9); SDR 片回归零变化; 换道/跨屏/缩放往返无花屏 |
| R2 VK HDR 内容链(软解 16F + 硬解 PQ 升样) | §6.3(G4+G10) | Win 软解 HDR 片×HDR 屏: FP16 交换链上高光顶出; 链F 硬解片同验(高光顶出+VK 图像处理/字幕可用); SDR 口径截图正常 |
| R3 统一检查收口 | mac/VK 对齐 §4.2 检查点 + iOS EDR 探测口径评估(G5) + mac 场景② Metal 腿端到端真验(G7 结论) | 三平台检查路径同源; iOS 内建屏 EDR 可行性结论 |
| R4 Android/Linux HDR | EGL HDR 呈现面口等(G8) | 另批详设(不在本稿展开) |
| R5 硬解状态上报 | §6.4 | 硬解失败事件可上报可查(徽章实态); 宿主换道可选, 非恢复必需 |

## 八、验收矩阵

| 场景 | 期望 | 批 |
|---|---|---|
| PQ 片 × HDR 屏 × 硬解(Win lane=1) | 直通: 高光顶出; 输出/交换链全 10bit(rgba10→R10G10B10A2), 肉眼无 banding 差 | R1 |
| PQ 片 × HDR 屏 × 硬解失败(Win) | lane=1 原生腿软解超 RGBA8 直通(G9)或 lane=0 VK 16F 直通, 两路等价 | R1/R2 |
| PQ 片 × HDR 屏 × 软解直开(Win lane=0) | VK FP16 交换链直通(G4 修复后) | R2 |
| PQ 片 × HDR 屏 × 硬解失败(mac) | Metal 腿 16F 直通(renderCpuFrame 路), 免换道 | R3 |
| PQ 片 × Win HDR 关 / SDR 屏 | tone map 出 SDR, 与现状零变化 | 回归 |
| 播中开关 Win HDR / 跨屏搬家 / 缩放往返 | 自动翻转重建, 不花屏不崩 | R1 |
| SDR 片 × 任意 | 零变化 | 回归 |
| 直通态截图/缩略 | 拒绝或走 SDR 口径, 不产脏图不灰 | R1/R2 |
| 直通态字幕(内/外挂) | 域正确(直通裁字幕支路现状; V2 专项遗留) | R3 |
| mac lane=0/lane=1 往返 | Metal→VK 交接 rgba8 照旧 | R3 回归 |
| 硬解帧 × 图像处理需求(HDR 片) | 链F(PQ 码 rgba8→VK 升样, 保 HDR)或链E(tone map 降 SDR)按需路由; 超 RGBA8 不进 VK | R2/口径回归 |
| 链F 画质对照 | 升样后 FP16 窗口高光顶出, 与链A 对照暗部/高光可察觉轻度 banding(8bit 量化, 已知代价) | R2 |

## 九、风险台账

- **R10G10B10A2 NT 共享纹理兼容性**: **纯 D3D11↔D3D11 无风险**(`Dx11Resource.cpp:89-99` 建纹理不校验 Format, D3D10 起该格式即标准 RT, 已核代码); 需真机验的是 **D3D11↔Vulkan 互操作**(创建/内存导入/blit 的驱动差异)——原「驱动待验」定性已按 §6.1 收窄;
- **翻转时序差**: 交换链先切/输出后切的一两帧过渡——数值直传理论无害, 防花屏实测;
- **lane=0 的 HDR 下发(v2)**: 旧口径「宿主不向 lane=0 发 forceHDR」作废——链F 由宿主以新增意图值 `vkHDR(3)` 下发(§3.3 模式映射, 引擎入口**按 lane 分岔归一化**: lane=0 折 forceHDR 行为, lane=1 折 follow+告警, 见 §4.3); 时序约束: 升样层(G10)未就绪前宿主不得放开, 否则 PQ 码被当线性呈现(FP16 线性域)发灰/过暗——引擎先落 G10, 宿主后放开下发, 同批验收;
- **翻转两端的错帧过渡(2026-10-01 新增, 原稿列为「理论无害」已升格)**: 交换链(宿主线程)与输出(渲染线程 `bResetFlag`)翻转不同帧, 中间帧值域不一致会发灰/过曝; 详见 §3.4, 落地时必须选一种同帧对齐手段, R1 验收矩阵「播中开关/跨屏/缩放往返」依赖本条;
- **lane=1 误收 vkHDR**: 若归一化一律折 forceHDR(原稿写法), 原生腿会误开直通交换链并把 SDR 载荷当 PQ 码呈现 = 发灰; 已修入 §4.3(lane 分岔), 实施时需有对应单测/日志;
- **ImageType 加枚举**: AvoxImage.h 共享头, 枚举追加向后兼容(0~15 值不动); **影响面四件套**(§6.1): ①枚举 ②三处映射表(getImageDXFormt/getImageType/getVkFormat)同时补 ③**SWIG 三端重生成**(X-Macro 枚举值进绑定层) ④`doc/code-wiki/03-核心类与关键接口.md:245` 的 ImageType 清单; **实际生效面=0**(全仓无 rgba10 产出点), 属纯预备;
- **VkSharedImage 平台分支缺口(2026-10-01 新发现)**: 该类的**声明层已按平台无关设计**(`VkShareHandle` 含 `opaqueFd`(Linux fd)/`androidHwBuffer`, 头注写「平台无关」), 但**实现层只有 Win32**: `importFromHandle` 硬编码 `OPAQUE_WIN32_BIT`(VkSharedImage.cpp:239), 导入整段包在 `#ifdef _WIN32`(:266-311), 非 Windows 时 `memory` 不被分配; `exportHandle` 同样只有 `_WIN32`/`__ANDROID__` 两段。**影响**: Linux/鸿蒙若要用 VkDevice↔VkDevice 共享则该通路不可用(须改走 CPU 上传或补 fd 分支)——**不阻塞本稿 R1/R2**(Win 用 Win32 分支已够), 记此备查, 归 Linux/鸿蒙批次;
- **VK 16F 域层兼容**: Blend/VR/font 层在 16F 线性域的合成(per-pixel gamma→linear 按 SDR 白叠加), R2 先保主链, 字幕域专项跟进;
- **iOS EDR**: 无 NSScreen 探测口径(UIScreen maximumPotentialEDRHeadroom?)另评估;
- 遗留挂账: tone map sdrWhite 恒 100nit 不跟手显示(GET_SDR_WHITE_LEVEL, 潜在改进); `pvx_hdr_toggle.ps1 -Off` set ok 但状态不动(关态对照工具挂账, panvox 侧); HLG 直通线性化腿在直通分支同生效需确认;
- 旧 A-9 backlog(2026-10-01 删)了结注记: SDR→HDR 上变换与本稿「SDR 片零变化」口径相抵, 不做; DV P5 兼容层未列入本稿范围, 如需重启另立新账; VT 路径 RPU/HDR 自提施工记录已删, 其 P1 落地(e9d9d54)归 git, 战役纪要见 [DV-HDR与构建协同](../../reports/DV-HDR与构建协同.md)。

## 十、当前实现 vs 方案 差分(2026-10-01 核码)

逐条把 §六落地清单与 §七里程碑对着**实现代码**核过一遍, 分清「已落 / 未落 / 结构性不可落」。口径: 只记**代码级**差分, 真机验收项单列。

**已落地(代码就绪, 待真机验)**:
- §4.1 实态位: `Window::hdrPassthroughActive()` 三平台实现齐(Dx11Window.hpp:64 / VkWindow.hpp:124 / MetalWindow.hpp:24) ✅
- §4.2 统一检查点: `VideoRender::checkTargetPassthrough()` 存在(VideoRender.cpp:382), `WindowRender::onRenderWindow` 两路都下发窗口(WindowRender.cpp:430/433) ✅
- §4.3 `HdrMode::vkHDR(3)` + lane 分岔归一化(WindowRender.cpp:64-77) ✅
- §6.1 Win 输出端 10bit 翻转 + `fetchFrame` 直通态拒绝(Dx11CSVideoRender.cpp:299/652/818) ✅
- G4 `VkYUV2RGBALayer` 16F 条件化、G9 Win CPU 腿、G10 `VkPqUpsampleLayer` + `pqUpsample.comp` ✅

**未落地 — 代码缺口(可直接补, 归 R3 前后)**:

| # | 缺口 | 现状证据 | 影响 | 建议批 |
|---|---|---|---|---|
| **D1** | **§3.4 CPU 读回防护只 Win 有, Metal/EGL/VK 三腿全缺** | `bTargetPassthrough` 守卫**只**在 `Dx11CSVideoRender::fetchFrame`(:818); `MetalRender::fetchFrame`(:611)、`EglVideoRender::fetchFrame`(:433)无条件读; `VkOutputLayer::fetchData`(:399)**只查 `resourceReady` 不查格式/直通**, 出图直接把 `inTexs[0]`(链F 下是 16F 线性)download 进调用方 rgba8 `patchFormat` 缓冲 → **脏图/越界写** | 直通态截图(=§八验收「直通态截图/缩略: 拒绝或走 SDR 口径」)在 Mac/Android/VK 腿无防护; 链F 截图必错 | R3(与统一检查点同批) |
| **D2** | **§4.2 检查点只接了 VK 与 Win 两腿, Metal/EGL 未接** | `checkTargetPassthrough()` 调用点仅 `VkVideoRender.cpp:515`、`Dx11CSVideoRender.cpp:299`; `MetalRender::vaildAndInitGraph`(:354)仍走自有 `metalHdrPassthrough.load()` 路径(:364 `wantF16 != bF16Pipeline`), 未过基类检查点; EGL 完全没有 | 与 §4.2「三平台检查路径同源」目标未达成 → **R3 出口「三平台检查路径同源」未满足**; 当前 mac 因自有路径恰好等价而**行为正确**, 但两套机制并存是漂移源 | R3 |
| **D3** | **§3.4 同帧对齐手段未选型未实现** | §3.4 明列「必须定义同帧对齐手段(择一, 实施时定)」,**三种方案都未落** —— 交换链在宿主线程翻(`Dx11Window.cpp:290 initBuffers`)、输出在渲染线程 `bResetFlag` 重建, 二者仍不同帧 | §八验收「播中开关/跨屏/缩放往返不花屏」**无设计支撑**, 真机大概率能看到过渡帧发灰/过曝 | R3(验收前必须定) |
| **D4** | **R5 硬解状态上报未做** | §6.4 无任何实现; `engine_controller.dart:564` 注释自认「引擎交换链真切上与否待实态上报(R5), 当前按『下发成功』记」= 宿主侧徽章是猜的 | 徽章实态依赖项; 不影响画面, 但 R5 出口「硬解失败事件可上报可查」为 0 | R5 |
| **D5** | **VK 出图交平台 GPU 资源的格式闸只放行 rgba8/bgra8, 未纳入 rgba16f**(2026-10-01 核码; **17:45 订正受限面**) | `VkOutputLayer::onCommand`(:184) `bool bCanMapGpu = outFormat.imageType==rgba8 \|\| ==bgra8;` —— 该闸是 **`setVulkan(false)` 时** VK 腿交平台 GPU 资源(Win NT 共享 / Android EGLImage / Apple IOSurface)的唯一入口(:186 起整段)。**但 panvox 走 `setVulkan(true)`→`VkWindow` 路线(原生窗直渲, `outputGpuData` 格式无关), 不经此闸 ⇒ panvox 不受影响**。第二层: `VkWinImage::bindD3D:58` 用 `getImageDXFormt`(无 16F 分支, 落 RGBA8) 定纹理格式 ⇒ 放开闸须同补三处映射 | 仅影响**仍以平台资源交帧**的消费者: `samples/vulkantest/dx11sharedtest|dx11windowtest`, Unity/Avalonia 类插件; 这些若在 lane=0 走 HDR(16F)出图 → 平台资源不更新(停帧/黑) | R2 补(低优先, 随平台资源消费者启用 HDR 时) |

> **D5 影响的到底是哪条出图支路(2026-10-01 核码; **同日 17:45 二度订正——用户指出 avox 有原生窗与 VK 窗两种, `setVulkan` 选的就是 VK 原生窗**)**:
> VK 出图有**两支**, 由 `bVulkan` 选 `window` 类型决定(`WindowRender::onSurfaceChange:117-133`, 由 `WindowRender::setVulkan` 定)——
> - **`setVulkan(true)` → `window = VkWindow`(VK 原生窗, 持自有交换链 + `VkRenderContext`)** ⇒ 走 `outputGpuData`(:292, `blitFillImage`, **格式无关**) 直 blit 进 VkWindow 交换链图。**rgba16f 无碍, 与 `bCanMapGpu` 无关。**
> - **`setVulkan(false)` → `window = Dx11Window/MetalWindow/EglWindow`(平台原生窗)** ⇒ VK 腿若仍出图则交给平台资源, 走 `onCommand` 的 `bCanMapGpu` 闸(:184 只放行 rgba8/bgra8)。
>
> **panvox 现状 = 前者(走 VkWindow, D5 不适用)**: `native/shim/panvox_native.cpp:1743-1760` `pvx_player_create` 按 `g_nvvLane` 调 `setVulkan(lane!=1)` + `setSurface(g_nvvHwnd)`(宿主视频窗), 设计口径是**「原生窗口直渲唯一车道」**(§六130) —— lane=0 时 window 即 `VkWindow`, 交换链 present 到宿主窗口, **纹理直通链整条不建**。`enableVkOutputDx11`/NT 共享纹理那条(`PassthroughSig::wanted` 默认 false 且全仓无人置 true) 已是**死支路**, panvox 不再走。
> ⇒ **订正: D5 在 panvox 不成立**(此前两版判断均错, 缘于把 `enableVkOutputDx11` 的注释当现行, 且未核 `setVulkan` 选窗)。**D5 的实际受限面 = 仍用 `setVulkan(false)`+`enableVkOutputDx11` 的平台资源消费者**(Unity / Avalonia / `samples/vulkantest/dx11sharedtest|dx11windowtest` 等): 这类消费者若在 lane=0 走 HDR 出图, 会撞 `bCanMapGpu` 与非 Win 平台的 16F 资源支持问题。


**结构性缺口(非代码疏漏, 需独立批次/另批详设)**:
- **Mac 链F 通道不存在**(G10 跨平台半场): mac `MetalRender` forceHDR 出已 EOTF 线性(非 PQ 码), 且 lane=0 时 Metal 画 drawable 直呈屏、IOSurface 仅离屏路径 ⇒ 无「Metal 产→VK 导入」通道。需独立批(Metal 侧 PQ 直通变体 + 切 IOSurface 为 VK 腿目标), 且与 G7(原生 EDR 腿已达标)叠加后收益不足 —— **列为不排期**。
- **R4 Android/Linux HDR**: EGL 无 HDR 呈现面口(G8), Linux 无 GPU 导入腿; 本稿 §七明标「另批详设, 不在本稿展开」⇒ 非缺口, 是**范围外**。
- **iOS EDR(G5)**: `MetalWindow.mm:58-63` iOS 腿恒不受理(无 NSScreen); 探测口径(UIScreen EDR headroom?)未评估 ⇒ 归 R3 的「iOS EDR 探测口径评估」出口。
- **§3.5 VK 16F 域层兼容(Blend/VR/font)**: 16F 线性域合成未处理, 归 R2 字幕/图像处理专项。

**真机验收项(代码就绪但未验)**:
- R1: SU130 HDR10 片 lane=1 全 10bit 无 banding; 软解 HDR lane=1 直通(G9) ✅代码就绪
- R2: Win 软解 HDR×HDR 屏 FP16 交换链高光顶出; 链F 硬解片高光顶出 + VK 图像处理/字幕可用
- 回归: SDR 片零变化、播中开关/跨屏/缩放往返(**依赖 D3**)

> **一句话结论**: 计划内 R1/R2 的**编码**已收口; 剩余是 **R3(统一检查收口: D1+D2+D3)** 与 **R5(D4)**, 外加 mac 链F/R4 两个非排期结构项。**D3(同帧对齐)是唯一挡在「不花屏」验收前的硬缺口**, 建议 R3 最先动它。

## 十一、沿革

- 2026-09-14/15: 管线改造立项(trc/元数据/UBO 3a; 原 HDR管线改造计划.md 记载, 该文档已删, 史料归 git);
- 2026-09-24~25: Metal EDR 直通落地验收(forceHDR 分支线性化+ExtendedLinearITUR_2020);
- 2026-09-26: 全平台默认 VK 车道(lane=0)+HDR 自动换 Metal;
- 2026-09-30: Windows DX11 lane=1 直通 a/b 首验通过; P2 探测口/双向纠偏/换片复位; VK HDR V0 探测 gate(无扩展枚举 FP16 可见, cs-ext 扩展堆损坏不启用);
- 2026-10-01: VK V1 实施(FP16 交换链+yuv2rgbaHDR.comp+双向重建+护栏+setter 全状态化, 6de852e/5f20c78); 发灰案定谳修复(Dx11Window resize 掉 HDR 态); tone map 曲线三腿统一; **口径反转**(用户定稿): VK HDR 只服务软解帧, 硬解回归平台原生直通; panvox 恢复 HDR→lane=1 换道(2e5d038); 本稿成文统一三平台口径(cd5498e);
- 2026-10-01(二): avox 会话接手(sess_53de1d6b 交棒), 按用户定稿重构本稿: 双链路总则(§一)+全情况矩阵(§二)+超 RGBA8 流转专章(§三)+统一接口(§四); 全稿行号当日逐文件复核; 新定事实入档: mac 原生腿吃 CPU 帧(MetalRender.mm:417, 场景② mac 免换道、G7 成立), Win/And 原生腿不吃 CPU 帧(Dx11CSVideoRender.cpp:297, G6 升级为结构必需)。
- 2026-10-01(三): **统一口径 v2**(用户定稿): 双通道模型——HDR 直通通道全链不做图像处理(YUV10→RGBA超10→窗口超10, 硬解/软解一律, VK 暂同、后续渐进加入), 图像处理收口 VK 唯一链(软解全接含 10bit 走自有 YUV 通道; 硬解只接 8bit SDR 对接, HDR 硬解帧原生降 SDR 再对接, 超 RGBA8 不进 VK); Win/Android 原生腿补 CPU 帧处理统一(G9, 硬解失败 lane=1 就地直通, 不再依赖换道, G6 降级为上报/徽章); 新立链E(硬解帧×图像处理)。
- 2026-10-01(四): **留口子链F**(用户定稿): 硬解 HDR 保真进 VK——PQ 码原样写 rgba8(8bit 载体保动态范围只损梯度; PQ 性质+Win a/b/发灰修复实证背书)→VK 升样层 PQ EOTF→16F→图像处理/字幕→FP16 呈现。「硬解+HDR+VK 全能力」组合打通, 全平台可用; 链A 仍为画质首选, G10/R2 扩名落账。HdrMode 加宿主意图值 `vkHDR=3`(链F 专用; 引擎入口归一化为 forceHDR 行为, UBO/shader 协议零改动; forceSDR 无独立分支的意图值先例); lane=0 的 HDR 下发旧禁令作废, 与 G10 同批放开。
- 2026-10-01(六): **通路边界查证与预备归档**(核代码, 未改实现): ①§3.2 新增「NT 共享通路的边界」——生产端 `Dx11CSVideoRender::getGpuContext()` 唯一出口 + 消费端 `Dx11Window::renderContext` 的 `bInteropTexture()` 天然闸 + 唯一外部接触=`VkWinImage` 的 `inputNt`(即链F); ⇒「10bit 只关在 dx11 内」的诉求**现状已满足**, 不需新增隔离标志, 只须补格式映射; ②§6.1 三处映射补「**纯追加零行为变更**」论断(全仓无 `rgba10` 产出点 ⇒ 生效面=0, 可作链F 预备先落)+ 影响面四件套(枚举/映射/**SWIG 三端**/`code-wiki/03:245` 清单); ③§6.3 新增「**通用性三层结构**」——格式语义层全平台共编(补映射=给 Linux/鸿蒙铺路)/ 导入机制层逐平台各一套(`VkInputLayer.hpp:5-9`, Linux 无 GPU 导入腿)/ 跨 VkDevice 层声明通用实现仅 Win32; ④§九 三条收窄:R10G10B10A2 风险收窄(纯 D3D11 无风险, 仅 D3D11↔Vulkan 需真机验)、ImageType 影响面四件套、**新增 `VkSharedImage` 平台分支缺口**(声明含 `opaqueFd`, 实现仅 `_WIN32`, 归 Linux/鸿蒙批);
- 2026-10-01(五): **独立复查修订**(逐条对码, 未动实现代码): ①`vkHDR` 归一化改正为**按 lane 分岔**(lane=0 折 forceHDR / lane=1 折 follow+告警)——原「一律折 forceHDR」会让原生腿误开直通把 SDR 载荷当 PQ 码呈现(§4.3/§3.3/§1.1); ②§3.4 翻转过渡由「理论无害」升格为**错帧风险**并补同帧对齐手段(值域不同时位深论证失效); ③§6.1 `R10G10B10A2 NT 共享`定性由「驱动待验」修正为**消费端映射表缺 10bit**(三处 getImageDXFormt/getImageType/getVkFormat 需同补), 纯 D3D11 无驱动风险; ④G4 描述改准(共用归一化段 + 字节视图约定, 非简单盖回); ⑤G10 补前置依赖(getVkFormat 映射); ⑥§4.1 实态位收口(Vk 取 `bHdrActive` 非意愿位; Dx11 去冗余判据; Metal 全局量多窗口约束); ⑦权威源边界显式化(引擎侧本稿 / 宿主侧归 panvox)。
- 2026-10-01(七): **R1 接口收口 + Win 输出端翻转落地**(实施): ①§4.1 `Window::hdrPassthroughActive()` 新增 + 三平台实现(Dx11 返 `bHdrActive` / Vk 返 `bHdrActive` 非意愿位 / Metal 读 `metalHdrPassthrough`); ②§4.2 `VideoRender` 基类统一检查点(`targetWindow`+`bTargetPassthrough` 缓存 + `setTargetWindow`/`checkTargetPassthrough`), `WindowRender::onRenderWindow` 两路都下发窗口; ③§6.1 `Dx11CSVideoRender::createProgram` 输出格式按实态分支(R10G10B10A2/R8G8B8A8) + `vaildAndInitGraph` 首行接检查点 + `fetchFrame` 直通态拒绝; ④连同预备批 C1~C4(`ImageType::rgba10` + 三处映射 + code-wiki)。构建 0 错误, 单测 96/96·1210 断言全绿(提交 `1ac769a`)。**未含**: §4.3 `HdrMode::vkHDR`(v2, 归 R2)。
- 2026-10-01(八): **G9 Win 原生腿吃 CPU 帧落地**(实施, 提交前): `Dx11CSVideoRender` 新增 `renderCpuFrame(yuv420P/yuv420P10)`——平面收进自建 DYNAMIC NV12/P010 上传纹理(`initGraphCpu`/`uploadCpuPlanes`, 10bit 逐样 `<<6` 与 `MetalRender.mm:417` 同语义)→复用既有 CS(tone map/forceHDR 分支全同)→超 RGBA8 输出; `vaildAndInitGraph` 的 `cpuIn` 早退改为走 CPU 建图分支; `createProgram` 输入段在 CPU 腿跳过(否则上传纹理被覆盖成不可 Map 的 DEFAULT); 设备取自**呈现窗口**(`targetWindow->getRenderContext()` → `IDx11Context::getDevice()`, 与输出共享纹理/窗口 blit 同设备); `WindowRender::render(YUVFrame)` 补下发 `window->renderContext()`(缺它则 `Dx11Window::onTickWin` 因 `sharedTexture` 空而早退不上屏)。硬解失败 lane=1 就地直通, 不再依赖换道。
- 2026-10-01(九): **R2 主体落地: §4.3 vkHDR + G4 收口 + G10 升样层**(实施): ①§4.3 `HdrMode::vkHDR(3)` 新增, `WindowRender::setHdrMode` 入口按 lane 分岔归一化(lane=0 折 forceHDR / lane=1 折 follow+告警), UBO/shader 协议仍只见 2; ②G4 `VkYUV2RGBALayer::onInitLayer` 的 16F 赋值从 forceHDR 分支移到**格式归一化段之后**并条件化为「10bit + forceHDR」, 修掉共用归一化段把 16F 盖回 rgba8 的老 bug; ③G10 **新增 `VkPqUpsampleLayer` + `glsl/pqUpsample.comp`**(rgba8 PQ 码 → 16F 线性, `pqToLinear(rgb)*125.0` 与 `yuv2rgbaHDR.comp` EOTF 段逐字同源), 接在 `VkInputLayer` 之后、下游合成之前, 拓扑条件 = forceHDR 行为态 + 非 CPU-YUV 输入(`!cpuIn && !bRgbaInput`), 即链F 硬解 PQ 码载荷; ④输出端分岔走 `bVkOutput`: 新增 `VideoRender::bVkOutput` + `setVkOutput()`(随 `SurfaceRenderNative::setVulkan` 同步), `Dx11CSVideoRender::createProgram` 输出格式改按「给 VK 恒 rgba8 / 直呈窗口跟直通实态」两轴定(§6.1 R2 修订: 上位版 R10G10B10A2 只保留给后者); ⑤`glsl/glslindexcurrent.txt` 加 `pqUpsample.comp` + 手动 `python glsl/compileglsl.py`(12 shader 全绿, 产物已落 install 三目录)。构建 0 错误(`avox` 目标), 单测 96/96·1210 断言全绿(提交 `caafcda`)。**未含**: mac/Android 交接面 PQ 码直通口径核对/补齐(G10 的跨平台半场, 见下条)。
- 2026-10-01(十): **G10 跨平台交接面语义核对**(核代码, 未改实现): 逐平台核 forceHDR 分支往 rgba8 交接面写了什么——**Win `Dx11CSVideoRender` CS = PQ 码原样** ✅、**Android `EglVideoRender` `uHdrMode==2` → `return rgb;` = PQ 码原样** ✅(FBO 为 `GL_RGBA`/`GL_UNSIGNED_BYTE`), 二者与升样层契约一致, 链F 直接可用; **Mac `MetalRender` `hdrMode==2` → `pqToLinear(rgb)*100.0` = 已 EOTF 的线性** ❌ 语义相反(链F 会二次解码)。且 mac lane=0 交接面结构不成立: `MetalRender.mm:933-938` 有 `metalLayer` 时画 drawable 直呈屏, IOSurface(rgba8, `:768/775`)仅在离屏时才是渲染目标 ⇒ **mac 今天没有可用的「Metal 产→VK 导入」通道**, 需独立批次(Metal 侧 PQ 直通变体 + 切 IOSurface 为 VK 腿目标); 与 G7 叠加后 mac 走链F 收益本就不足(原生 EDR 腿已达标)。Linux 无 GPU 导入腿(`VkInputLayer.hpp:5-9` 条件编译不命中)走 CPU 上传, 不涉本条。**⇒ R2 的跨平台半边: Android 就绪, Mac 转独立缺口, Linux 不适用**。
- 2026-10-01(十一): **当前实现 vs 方案 差分入档**(核码, 未改实现): 新增 §十, 逐条核对 §六/§七 对码。结论: R1/R2 编码已收口; 剩余缺口 **D1 §3.4 读回防护只 Win 有(Metal/EGL/VK `fetchFrame`/`fetchData` 无守卫, 链F 截图必错)**、**D2 §4.2 检查点未接 Metal/EGL(R3「检查路径同源」出口未满足)**、**D3 §3.4 同帧对齐手段未选型(唯一挡「不花屏」验收的硬缺口, R3 需最先动)**、**D4 R5 上报未做**; mac 链F 通道不存在(=不排期), R4/iOS EDR/16F 域层=范围外或归批。

- 2026-10-01(十二): **VK 腿两条问项核码**(核代码, 未改实现)——回答「VK 自身 CPU HDR 通道做了吗 / 对接各平台 SDR·PQ 是否分别出 RGBA8·RGBA16F」(§十 D5): ①**VK 自身 CPU(软解)帧 HDR 通道 = 已做**: `VideoRender::renderFrame(YUVFrame)` 置 `cpuIn=true` → `VkVideoRender::renderCpuFrame` → `inputLayer->inputCpuData(frame,false)` → `yuv2RGBA`(10bit 时 `hdrMode==forceHDR` 选 `yuv2rgbaHDR.comp` + 输出 16F, G4 后条件化正确) → 16F 域合成 → VK FP16 交换链。即链B 的 lane=0 支路完整。②**「对接各平台」语义要看出图支路, 而支路由 `WindowRender::setVulkan` 选窗类型决定**: `setVulkan(true)` 得 **VkWindow(VK 原生窗, 自有交换链)**——出图走 `outputGpuData`(:292, `blitFillImage` **格式无关**, rgba8/16F 都成立); `setVulkan(false)` 得平台原生窗——VK 腿交平台 GPU 资源才走 `onCommand`(:184)的**格式闸 `bCanMapGpu`(只放行 rgba8/bgra8)**, HDR 出 rgba16f 被判 false, 整段 interop 跳过。③**panvox 走前者, 不受此影响**: `pvx_player_create` 调 `setVulkan(lane!=1)`+`setSurface(宿主视频窗)`(「原生窗口直渲唯一车道」), lane=0 即 VkWindow 直渲; `enableVkOutputDx11` 支路已死(`PassthroughSig::wanted` 无人置 true)。受限面 = 仍用 `setVulkan(false)` 的平台资源消费者(Unity/Avalonia/vulkantest 样例)。④**SDR(→rgba8)全路径正常**。⇒ **结论: VK 腿的「SDR→平台资源」正常; 「HDR(PQ)→平台资源」在 `setVulkan(false)` 交平台资源的路径上因 D5 断链**(panvox 不受影响, 它走 `setVulkan(true)`→VkWindow 原生窗直渲)。**勘误(同日 17:40/17:45)**: ①17:40 宿主传输细节订正——已由老式 `external_texture_d3d` 镜像 blit 改为 **Flutter GPU surface + DXGI 共享句柄**; ②**17:45 路径归属订正(用户指出 avox 有原生窗/VK 窗两种, `setVulkan` 即选 VK 原生窗)**——panvox 现行 `pvx_player_create` 调 `setVulkan(lane!=1)` + `setSurface(宿主视频窗)`, lane=0 得 **VkWindow**, 走 `outputGpuData`(格式无关), **D5 在 panvox 不成立**; 此前两版「panvox 必现」判断作废。`enableVkOutputDx11` 那条(`PassthroughSig::wanted` 无人置 true)已是死支路。
