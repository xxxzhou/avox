# 多平台 HDR 统一重构方案

- 状态: 计划稿 v1(2026-10-01)。**统一口径文档: 三平台 HDR 现状/统一语义/重构计划/挂账全在本稿**, 由 panvox 侧两份文档(`hdr-chain-plan.md`/`vk-hdr-lane.md`)合并替代迁入——用户拍板 2026-10-01, 那两份已删。
- 链路事实: 2026-10-01 逐文件复核(未动代码); 实施批次见 §五。
- 同域文档: [HDR管线改造计划](HDR管线改造计划.md)(解码/元数据/tone map 管线改造台账, 其改造项已基本落地, 实施史见其 §6)、[颜色空间矩阵统一设计](颜色空间矩阵统一设计.md)(「单一真相源」原则本稿沿用)。
- 路径约定: 本文在 avox 仓, `src/` 相对路径直接写; panvox 侧路径用 `panvox:` 前缀。
- 上下游分工: 引擎渲染/HDR 链路归 avox(本稿); 车道决策/宿主接线(lane 换道/forceHDR 下发/探测/徽章)归 panvox(engine_controller.dart, §3.7)。

## 一、三平台渲染链路现状(逐文件核实)

### 1.1 通用骨架(三平台同构)

```
解码 → 帧三型: GpuFrame(硬解 NV12/P010 平台纹理) / YUVFrame(软解 CPU) / VideoFrame
  ↓
WindowRender(协调者: 持 window + 双渲染器; setHdrMode = 双路转发 + 触发窗口直通)
  ├─ pVideoRender(平台渲染器): Win=Dx11CSVideoRender / Apple=MetalRender / Android=EglVideoRender
  │    硬解帧 YUV→RGBA 转换都发生在这一层
  └─ vkVideoRender(VkVideoRender): VK 合成/增强(全平台默认车道)
  ↓ 分流(bVulkan = nvv lane)
  lane=0: pVideoRender 产 rgba8 → VK import → VK 合成 → VkWindow
  lane=1: pVideoRender 产 rgba8 → 平台原生窗口直渲(Win=Dx11Window / Apple=CAMetalLayer)
```

VK 的平台纹理导入(`src/avox_vulkan/layer/VkInputLayer.cpp:297` inputGpuData):
Win=DX11 共享纹理互操作(winImage, 按 GetDesc **格式自适应重建**)、
Android=GLES texture id(+EGLContext 判重)、mac=Metal IOSurface(vkIosImage)。
**三平台硬解帧进 VK 前一律已被平台渲染器转成 rgba8**; Linux 无 GPU 导入腿(纯 CPU 帧)。

### 1.2 Windows(lane=1 直通链已验收; 全链唯一精度损失点=8bit 中转)

```
硬解 P010 → Dx11CSVideoRender CS(processColor: forceHDR=PQ 码原样 /
  follow=tone map 锚线性+Reinhard) → R8G8B8A8 共享纹理
  (createProgram 唯一输出创建点, 恒 8bit, 无 HDR 分支, DV program 同写此输出)
  → Dx11Window blit(像素着色器 = SampleLevel 纯采样直出, 无色彩处理)
  → 交换链: 直通=R10G10B10A2+G2084 / SDR=BGRA8(默认)
```

- PQ 是非线性 HDR 编码, 亮度动态范围全在 0-1 码值里——**8bit 中转不破坏 HDR**
  (9/30 a/b 验收+10/1 发灰修复后用户肉眼确认「和系统对上了」), 代价=梯度精度
  (10bit→8bit→10bit, 暗部/高光可 banding), Apple 无此点(全程 16F)。
- `Dx11Window::setHdrPassthrough` **双向主动重建已就绪**(开向 10bit+SetColorSpace1 /
  关向回 R8G8B8A8+重挂; 早退路径 applyHdrSwapchainState 重入兜底)——2026-10-01
  发灰案修复批落地并验收。
- 输出恒 rgba8: `createProgram` 唯一创建点写死; `setHdrMode` 只刷 UBO(bParamsDirty)
  不重建图——输出格式无从跟随 HDR 态(重构动因)。
- **渲染器拿不到窗口**: `WindowRender::onRenderWindow` 只把窗口指针给 vkVideoRender
  (`if (bVulkan && vkVideoRender)`), pVideoRender 从未收到 `renderWindow(Window*)`;
  Window 基类只有 setHdrPassthrough 下行命令口, 无「直通实态」查询口。
- CPU 读回(fetchFrame/mapStagingFrame)按输出纹理格式, 输出若变 10bit 需防护。
- `ImageType`(src/avox/AvoxImage.h)有 rgba16f(15) 无 10bit 表达;
  DXGI 双向映射(src/avox_windows/dx11/Dx11Helper.cpp)无 R10G10B10A2。
- 播放链事实: `Dx11VideoYuv` 只在转码/录制链(FFDx11Encoder), 播放渲染器恒
  Dx11CSVideoRender(WinCommon 工厂, VideoProcessRender 弃用)。

### 1.3 macOS(参照实现——统一语义的样板)

- **lane=1(Metal 腿, 已验收)**: MetalRender=渲染器+呈现层合一(CAMetalLayer)。
  `metalHdrPassthrough`(原子意愿)→ `vaildAndInitGraph` 每帧过界检测
  (`wantF16 != bF16Pipeline` → `applyLayerHdrConfig`: 层 RGBA8Unorm ↔
  RGBA16Float + EDR extended linear ITUR-2020 色空间 → `releaseGraph()` 全图重建)。
  硬解帧直进 shader, **无 rgba8 中转, 全程 16F**。
- lane=0(VK 腿): MetalRender 先 NV12→RGBA8 IOSurface → MoltenVK import
  (vkIosImage) → VK 合成 → VkWindow(MoltenVK 无 EDR, 恒 SDR 呈现)。
- **iOS**: `MetalWindow::setHdrPassthrough` 恒不受理(无 NSScreen, EDR 探测另批);
  MetalRender 的翻转代码已带 @available 编译保护在包内。

### 1.4 Android / Linux

- Android: 恒 VK 车道——EglVideoRender NV12→RGBA8 GLES 纹理 → VK import →
  VK 合成(V5.comp tone map) → VkWindow(Flutter 纹理桥)。EglVideoRender 自带
  uHdrMode uniform(自有 shader tone map 分支); EGL 无 HDR 呈现面口。HDR=另批。
- Linux: CPU 帧 → VK(inputCpuData), 无平台 GPU 导入分支。HDR=不做。

### 1.5 缺口盘点(重构动因)

| # | 缺口 | 位置 |
|---|---|---|
| G1 | 「呈现格式」决策分散: Win 恒 rgba8 写死 / Metal 层翻转(已实现) / VK FP16+变体(已实现), 无统一语义 | 三平台 |
| G2 | Window 基类无直通实态查询口; pVideoRender 拿不到窗口指针 | 通用骨架 |
| G3 | Win 直通链 8bit 中转(10bit→8bit→10bit) | Dx11CSVideoRender |
| G4 | VK 软解 16F 盖回 bug: `VkYUV2RGBALayer.cpp` ~104 行设 rgba16f 后 ~112 行无条件 `outFormats[0].imageType=rgba8` 盖回——`yuv2rgbaHDR.comp` 写出的 >1 线性值进 8bit 纹理被钳 1.0, HDR 高光全丢; 「CPU YUV10→VK RGBA16F→16F 窗口」端到端未跑通 | VkYUV2RGBALayer/VkTexture |
| G5 | iOS EDR 探测缺口 | MetalWindow |
| G6 | 硬解失败无事件上报(只有 `fallback to software` 日志, FFDx11Decoder), shim 无查询口 | FFDx11Decoder/shim |

## 二、统一语义(定稿, 用户方向 2026-10-01)

**核心原则: 呈现面实态驱动像素处理输出——渲染器统一检查窗口直通实态, 过界全链翻转格式。**

- **A. 实态驱动**: 输出格式跟「窗口/呈现面当前是否直通」走, 不跟 setHdrMode
  命令走——命令被拒(SDR 屏护栏降级)时输出自然回到 SDR 格式, 不产生命令与
  实态的漂移。
- **B. 统一检查点**: 收在 `VideoRender` 基类——基类经既有 `renderWindow(Window*)`
  虚口持窗口弱引用, 渲染线程每帧读窗口实态, 过界置 `bResetFlag`(基类已有原子
  重建标志, 消费契约见 VideoRender.hpp 注释); 各平台派生在重建时按实态定输出
  格式。「检查」统一, 「翻转实现」按平台结构分治(Metal 层合一/DX11 两端/VK 交换链)。
- **C. 直通域按呈现面能力定稿**: Win=R10G10B10A2(PQ 码) / Apple=RGBA16F
  (extended linear, 1.0=SDR 白) / VK=RGBA16F(extended linear)——同为「值原样
  呈现」语义, 域不同是呈现面差异不是处理差异。
- **D. 全链一致**: 翻转过渡期(交换链已切而输出未切或反向)一两帧数值直传
  (UNORM 采样→UNORM RTV 值域同为 0-1, PQ 码不变形), 只损位深不花屏。
- **E. CPU 读回防护**: 直通态截图按既有口径先收直通(临时 forceSDR 抽帧);
  fetchFrame 遇直通格式输出应拒绝/标明, 防脏图。

**三场景**(用户定稿 2026-10-01, 所有平台按此分流):
①HDR 片 × HDR 屏 × 硬解成功 → lane=1 平台原生窗口直通(Win 全程 10bit 化=R1);
②HDR 片 × HDR 屏 × 硬解失败 → 落软解(CPU YUV10)→ VK RGBA16F 真直通(G4 修复=R2,
触发闭环=G6=R5)。注: 软解帧直进 VK yuv2rgba 层(V5 已通吃 p010 上传归一化),
不过平台渲染器转 rgba8 那道, 精度优于硬解帧进 VK;
③HDR 片 × 非 HDR 屏 → lane=0 默认车道 tone map(Win 计算在 DX11 CS / Mac 在
MetalRender follow 分支 / Android 在 V5.comp; 三腿曲线 2026-10-01 已统一为
锚线性+Reinhard, 本重构不动 tone map)。
SDR 片 × 任意屏: lane=0 VK, 行为零变化。

## 三、重构设计(改动点清单)

### 3.1 Window 基类: 直通实态查询口(G2)

`src/avox/video/Window.hpp`:

```cpp
// 呈现面直通实态(交换链/层已真切上 HDR 即 true); 命令被拒/SDR 屏= false。
virtual bool hdrPassthroughActive() const { return false; }
```

实现: Dx11Window=`bHdrActive && bPassthrough`; VkWindow=`bHdrActive && bPassthrough`;
MetalWindow=`metalHdrPassthrough`(意愿位即实态——翻转在渲染器, 探测在窗口侧已做)。

### 3.2 VideoRender 基类: 统一检查(G1/G2)

`src/avox/video/VideoRender.hpp`:

- 基类实现 `renderWindow(Window*)`: 存 `Window* targetWindow`(弱引用, 生命周期
  归 WindowRender)并立即刷新一次实态;
- 基类字段: `bool bTargetPassthrough = false;`(实态缓存);
- 基类检查(渲染线程每帧入口, 或各派生 vaildAndInitGraph 首行调用的基类方法):
  targetWindow 非空 → 读 `hdrPassthroughActive()` → 与缓存不一致 → 更新缓存 +
  置 `bResetFlag` + 日志一行;
- `WindowRender::onRenderWindow`: **两路都调** `renderWindow(window.get())`
  (现在只给 vkVideoRender); 贴图腿/离屏时 window 为空, 基类判空即安全。
- 参照: `VkVideoRender::renderWindow` 已是 override 范例(消费窗口 VkRenderContext)。

### 3.3 Windows: 输出翻转(G3, R1 主体)

`src/avox_windows/dx11/Dx11CSVideoRender.cpp`:

- `createProgram` 输出创建分支: `bTargetPassthrough ? DXGI_FORMAT_R10G10B10A2_UNORM
  : DXGI_FORMAT_R8G8B8A8_UNORM`;
- `ImageType` 增 `rgba10`(AVOX_MAP_IMAGE) + Dx11Helper 双向映射补 R10G10B10A2;
- SRV 已按实际 texDesc 建(Dx11SharedTex.cpp:166), 核对 UAV 建口同跟随;
- 共享纹理 flag 不变(MISC_SHARED_NTHANDLE; R10G10B10A2 支持 NT 共享, 真机验证);
- `fetchFrame`/`mapStagingFrame`: 输出为 rgba10 时拒绝并日志(截图走 forceSDR 口径);
- shader 无需改(forceHDR=PQ 原样写 UAV, 10bit UNORM 同语义)。

### 3.4 macOS: 对齐(基本不动)

Metal 腿已有完整翻转(applyLayerHdrConfig+releaseGraph), 语义与 §二一致——
只把 `MetalWindow::hdrPassthroughActive` 接上供基类统一检查/日志; 渲染器侧
metalHdrPassthrough 即实态, 行为不变。

### 3.5 VK: 软解 16F 链修复(G4, R2 主体)

- `src/avox_vulkan/layer/VkYUV2RGBALayer.cpp` ~112 行盖回条件化: 10bit+forceHDR
  时保持 rgba16f;
- `src/avox_vulkan/vulkan/VkTexture.cpp` 16F 建纹理/内存尺寸/upload 字宽适配
  (现默认 8bit);
- graph 下游格式协商(canvas→output→交换链)跟 16F; Blend/VR/font 层 16F 域兼容
  先保主链, 余项跟进批;
- 修完真验: 软解 HDR 片(或强制软解)×HDR 屏, FP16 交换链+16F 内容链端到端。

### 3.6 硬解状态上报(G6, R5)

- FFDx11Decoder 开档硬解判定失败(vp9/av1 profile 不支持等 `fallback to software`
  路径)冒宿主事件/状态位;
- shim 加查询或回调口(与 panvox 徽章「下发成功≠直通生效」实态上报同批设计);
- panvox 收事件后 `switchNvvLane(0)`+`setHdrMode(2)` 换 VK 腿(拆面闪一下)。

### 3.7 宿主侧(panvox)已落地状态与分工

以下已在 panvox 仓落地(原 hdr-chain-plan.md 记载, 迁入存档):
- 车道体系: `applyNvvLaneForRef` 开播定道(HDR 片 trc∈{pq,hlg}+显示闸 → lane=1,
  平台不分 Windows/Apple——10/1 口径反转后恢复, panvox 2e5d038)+播中换道
  (`switchNvvLane(1)` 拆面复挂)+换片复位+每拍屏况双向纠偏+3s 自愈重发;
- 显示闸: P2 探测口 `panvox:app/lib/core/platform/hdr_display_probe.dart`
  (DisplayConfig 主窗屏 HDR 激活, 3s TTL)+Apple EDR 头距; 设置档「HDR 直通」开关;
- 徽章: 播放页「HDR 直通/HDR→SDR」点亮式(判据=车道落地+setHdrMode 下发成功;
  实态上报收口后吃引擎真值, 归 R5);
- tone map/直通呈现链的用户侧验收: DX11 直通 a/b(9/30)、发灰案(Dx11Window
  resize 静默掉交换链 HDR 态)修复验收(10/1)、tone map 曲线三腿统一(10/1, mac
  MetalRender.mm 同曲线已部署);
- **教训**: HDR 桌面截图走 HDR→SDR 转换恒提亮压平——HDR 屏验收一律只认肉眼,
  截图对照作废。

## 四、历史沿革(简记, 详情归 git/记忆)

- 2026-09-14/15: 管线改造立项(trc/元数据/UBO 3a, 见 HDR管线改造计划.md §6);
- 2026-09-24~25: Metal EDR 直通落地验收(forceHDR 分支线性化+ExtendedLinearITUR_2020);
- 2026-09-26: 全平台默认 VK 车道(lane=0)+HDR 自动换 Metal;
- 2026-09-30: Windows DX11 lane=1 直通 a/b 首验通过; P2 探测口/双向纠偏/换片复位;
  VK HDR V0 探测 gate(无扩展枚举 FP16 可见, cs-ext 扩展堆损坏不启用);
- 2026-10-01: VK V1 实施(FP16 交换链+yuv2rgbaHDR.comp+双向重建+护栏+setter
  全状态化); 发灰案定谳修复(Dx11Window resize 掉 HDR 态); tone map 曲线三腿统一;
  **口径反转**(用户定稿): VK HDR 只服务软解帧, 硬解回归平台原生直通; panvox
  恢复 HDR→lane=1 换道; 本稿成文统一三平台口径。

## 五、里程碑

| 批 | 内容 | 出口 |
|---|---|---|
| **R1 Windows lane=1 全程 10bit**(先行, 用户拍板) | §3.1+§3.2+§3.3 | SU130 放 HDR10 片: 输出/交换链/呈现全 10bit, 肉眼对照系统播放器无 banding 差; SDR 片回归零变化; 换道/跨屏/缩放往返无花屏 |
| R2 VK 软解 16F 链 | §3.5 | 软解 HDR 片×HDR 屏: FP16 交换链上高光顶出; forceSDR 截图正常 |
| R3 统一检查收口 | Metal/VK 对齐 §3.2 检查点 + iOS EDR 探测口径评估 | 三平台检查路径同源; iOS 内建屏 EDR 可行性结论 |
| R4 Android/Linux HDR | EGL HDR 呈现面口等 | 另批详设(不在本稿展开) |
| R5 硬解状态上报 | §3.6 | 场景 2 全自动: 硬解失败→自动落 VK 16F 直通 |

## 六、验收矩阵

| 场景 | 期望 | 批 |
|---|---|---|
| PQ 片 × HDR 屏 × 硬解(Win lane=1) | 直通: 高光顶出; 输出/交换链全 10bit(rgba10→R10G10B10A2) | R1 |
| PQ 片 × HDR 屏 × 硬解失败 → 软解 | 换 VK: FP16 交换链 16F 直通 | R2/R5 |
| PQ 片 × HDR 屏 × 软解直开(VK lane=0) | VK FP16 交换链直通(G4 修复后) | R2 |
| PQ 片 × Win HDR 关 / SDR 屏 | tone map 出 SDR, 与现状零变化 | 回归 |
| 播中开关 Win HDR / 跨屏搬家 / 缩放往返 | 自动翻转重建, 不花屏不崩 | R1 |
| SDR 片 × 任意 | 零变化 | 回归 |
| 直通态截图/缩略 | 拒绝或走 forceSDR, 不产脏图不灰 | R1/R2 |
| 直通态字幕(内/外挂) | 域正确(直通裁字幕支路现状; V2 专项遗留) | R3 |
| mac lane=0/lane=1 往返 | Metal→VK 交接 rgba8 照旧 | R3 回归 |

## 七、风险台账

- **R10G10B10A2 NT 共享纹理兼容性**: 创建/fence/跨上下文 blit 需真机验证(驱动差异);
- **翻转时序差**: 交换链先切/输出后切的一两帧过渡——数值直传理论无害, 防花屏实测;
- **lane=0 误收 forceHDR 的鲁棒性**: VK 输入层按 GetDesc 格式自适应重建(已有逻辑),
  rgba10 输入可吃但域语义需核——新口径下宿主已不向 lane=0 发 forceHDR, 属兜底;
- **ImageType 加枚举**: AvoxImage.h 共享头, 枚举追加向后兼容, 三端 shim 重编生效;
- **VK 16F 域层兼容**: Blend/VR/font 层在 16F 线性域的合成(per-pixel gamma→linear
  按 SDR 白叠加), R2 先保主链, 字幕域专项跟进;
- **iOS EDR**: 无 NSScreen 探测口径(UIScreen maximumPotentialEDRHeadroom?)另评估;
- 遗留挂账: tone map sdrWhite 恒 100nit 不跟手显示(GET_SDR_WHITE_LEVEL, 潜在改进);
  `pvx_hdr_toggle.ps1 -Off` set ok 但状态不动(关态对照工具挂账, panvox 侧);
  HLG 直通线性化腿在直通分支同生效需确认。
