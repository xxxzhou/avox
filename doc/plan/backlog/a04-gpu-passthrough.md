# A-4 GPU 直通三平台

> 状态: 进行中 · 上次核对: 2026-09-17 · 权威源: -


优先级 P0 · 里程碑 M4(建议提前,panvox P-1 零拷贝路径切换等它) · 计划状态:就绪
Windows 已通(分辨率变化不跟随已修并实测通过; 见「已知缺陷」节);本计划覆盖 Android AHB→Flutter Texture 与 iOS/macOS CVPixelBuffer 桥。

## 出口判据

1. Android:播放帧经 AHB 进 Flutter Texture,panvox 播放页零拷贝出图。
2. iOS/macOS:播放帧以 CVPixelBuffer/IOSurface 进 Flutter Texture,零拷贝出图。
3. 三平台 CPU 占用对比 CPU 帧泵有可测改善;帧可用通知与尺寸变化时序无黑屏。
   (「帧可用通知」已由渲染输出事件化承接: `ISurfaceRenderOb::onRender` 携带
   SurfaceRenderEvent 世代/尺寸/重建信号, 见 `doc/plan/player/open出图时延与渲染输出事件化.md`;
   「无黑屏」的换片重建间隔仍开放)

## 现状(代码落点)

- **Windows 全链通(参照系)**:D3D11VA 零拷贝出帧
  (`src/avox_ffmpeg/decoder/FFDx11Decoder.cpp:156-176`)→ Vulkan 渲染 → NT 共享纹理+fence
  (`src/avox_windows/dx11/Dx11SharedTex.cpp:75-105`)→ 导出接口
  (`src/avox/AvoxLayer.h:516-547`,enableVkOutputDx11/getVkOutputDx11Handle 等)。
  Unity 侧 flavor1 VK 导入 / flavor2 DX11 拷贝
  (`platform/unity/plugin/src/GpuPassthrough.h:20-60`)。注意:外部 DX11 通路是 CopyResource
  拷贝非零拷贝导入,如需真零拷贝是加分项。
- **Android:渲染输出 AHB 导出已有,解码不直出**。AHB 基建完整:
  `src/avox_android/SharedGpuBuffer.cpp:49-56`(dlsym 分配)、:144 EGL 绑定、
  VK 导入 `src/avox_vulkan/share/VkSharedImage.hpp:78,87`;`enableVkOutput` 有
  androidHwBuffer 导出分支(`src/avox/Avox.cpp` __ANDROID__ 分支)。
  但 MediaCodec 解码输出走 SurfaceTexture→OES 中转
  (`AndVDecoder.cpp:154-188`,onFrameRender releaseOutputBuffer+updateTexImage :365-387),
  解码帧直出 AHB 是另一条新路径。解码器仅注册 h264/h265(:20-40)。
- **Apple:VT 解码已是 CVPixelBuffer 零拷贝,缺对外导出**。
  `IOSVDecoder.mm:342-354` bMetalRender 时 CFRetain(imageBuffer)→GpuFrame;
  Metal 渲染直引用(:482-493 CVMetalTextureCache)。对外仅 Vulkan IOSurface 通道
  (`Avox.cpp:707-717` ioSurface,「非所有权语义」见 `AvoxLayer.h:489-493`),
  **Metal 纹理导出完全没有**(GpuPassthrough.h 无 Metal 函数)。
- **Unity 参考路径**:backlog 指明「抄 avox-unity 路径」——AHB 导入 flavor1 已有
  (`GpuPassthrough.cpp:357`),但仓内无 Android gradle/Java 宿主工程,真机未验证。

## 已知缺陷(已修 + 已实测通过):分辨率变化后输出内容不跟随(2026-09-16 复现, 2026-09-17 闭环)

换片到**不同分辨率的媒体**后,新共享纹理按新尺寸正确重建(NT 句柄换新、尺寸正确),
但拷进它的内容仍是上一部尺寸的画布(1:1 落在左上角),新画面始终不上屏。

**复现**(panvox 探针 `PANVOX_AUTOPLAY` + `PANVOX_AUTOPLAY_SWITCH`,1392x880 → 1920x1080,
窗口 3415x2196 物理):切前正常铺满(内容占比 85.7%)→ 切换后黑约 1.5s → 出现左上角一块
(占窗口宽 72.4%/高 77.2%,相邻帧差 0.2 = **静止**)→ 切回后再黑一次 → 恢复。

**量化**:该块在输出纹理自身的像素空间约 **1390x834 ≈ 上一部的 1392x880**(未缩放的原画布),
块外纯黑;共享纹理本身 1920x1080,换片时句柄/注册均换新(panvox shim 侧句柄门控与暂存纹理
逻辑无异常,日志见 `[panvox-shim] mirror 1920x1080` / `gpu src opened 1920x1080`)。

**判定**:Windows 通路是 1:1 `copyImage(cmd, inTexs[0], winImage->getImage())`
(`src/avox_vulkan/layer/VkOutputLayer.cpp:193-196`),源 = 上游画布节点纹理
(`src/avox_vulkan/layer/VkLayer.cpp:179-182` `getOutTex(inNode)`),目标 = 本层声明输出格式
(`outFormats[0]`,`bindD3D` 调用见同文件 :76)。**两侧 extent 不等时 vkCmdCopyImage 只覆盖
左上角重叠区,不报错也不缩放** —— 与实测吻合。`VkOutputLayer::outFormat` 只在 0x0 时从
`inFormats[0]` 取一次(`VkOutputLayer.cpp:69-70`),是本项第一嫌疑点。

**出口判据③「帧可用通知与尺寸变化时序无黑屏」据此当前不成立**(Windows 作为参照系也不达标)。

宿主侧放大观感(非根因,仅记录):切换瞬间 panvox shim 把 `texW/texH` 清零 →
`pvx_video_size` 读到 0 → `textureAspect()` 返回 0 → VideoView 走「无条件铺满」分支,
整张纹理被拉伸到窗口,残影于是显得"贴在窗口左上角"。

**修复落地(2026-09-16, 已暂存未提交, 待实测验证)**:根因不在 `outFormat` 单一成员, 而在 Vulkan
输入层 `VkInputLayer::inputGpuData` 仅首帧(`inFormats[0]` 为 0x0)读一次外部 DX/GL/Metal 纹理尺寸,
换源后不再重检 → 整图不重建 → 画布(源 `inTexs[0]`)停在旧分辨率, 而输出/共享纹理被宿主按新尺寸
重绑, 二者 extent 不等 → 1:1 拷贝静默截断到左上角。修复两层:(1) `VkInputLayer` 每帧重读外部
纹理尺寸, 真正变化才 `setLayerFormat`(触发整图 `resetGraph`)重建到新分辨率; (2) `VkOutputLayer`
的 DX11 输出与导出面在 src/dst extent 不等时改走 `blitFillImage` 缩放铺满兜底, 不再静默截断
(仅此兜底即可消除"左上角一块+黑"的截断现象)。对应改动:
`src/avox_vulkan/layer/VkInputLayer.cpp`、`src/avox_vulkan/layer/VkOutputLayer.cpp`、
`src/avox/Avox.cpp`(声明尺寸≠管线尺寸告警)。**验证**: 同播放器实例换片 640x360 ↔ 1920x1080
各一次, 确认比例正确、无左上角残影; 换片瞬间可能仍有重建黑屏间隔(重建耗时, 属时序问题, 不在
本次修复范围, 出口判据③的"无黑屏"部分仍待优化)。

### 补充根因(2026-09-17):上面两层修复不足以闭环,真凶是 CS 渲染的 constBuf 没重传

T5 的两层修复(输入层每帧重检 + 输出 blit 兜底)落地后,现象**依旧复现**:切到 960x540 后仍是
左上角一块。原因:Vulkan 侧的尺寸账是**完全自洽**的(日志 `inputGpuData dx texture change ...
to:960x540`、`outFormat sync ... to:960x540`、`bindD3D w:960 h:540`、`Pipegraph reset success`),
所以 `bSizeMismatch` 兜底根本不会触发 —— 问题在 Vulkan 上游的 **DX11 色彩转换(CS 通路)**。

`src/avox_windows/dx11/Dx11CSVideoRender.cpp`:

- `bParamsDirty` 只由 `setColorSpace/setHdrMeta/setHdrMode` 置位; `init()` 里即使把
  `imageWidth/imageHeight` 更新成了 960x540, 也不会置位 → `constData[0/1]`(着色器里的
  `inputSize`)**仍是旧的 320x240**。
- 而 `Dispatch` 用的是**新**的 `imageWidth/Height`(groupX/groupY 按 960x540 算,铺满整图)。
- CS 第一行 `if (DTid.x >= size.x/2 || DTid.y >= size.y/2) return;` 于是让超出旧边界的线程
  全部 early-return → 只有左上角 320x240 被写入, 其余保持黑。
- `releaseGraph()` 不释放 `constBuf`, 旧尺寸更是一路带下去。

**修复**:`init()` 中尺寸变化即置 `bParamsDirty = true`;`createProgram()` 末尾(constBuf 刚重建)
无条件置一次。日志新增 `cs render size change, constBuf re-upload from:WxH to:WxH`。

**实测**:panvox 探针 `PANVOX_AUTOPLAY` + `PANVOX_AUTOPLAY_SWITCH`,窗口 3399x2187 物理,
`PANVOX_GPU_SHARED=1`,等待 9.5s 抓图(8s 时第二路尚未出帧,会全黑,勿误判):

| 场景 | 修复前 | 修复后 |
|---|---|---|
| 320x240 → 960x540 | 左上角 1134x980(占宽 33.4%) | 满宽 3391x2041(占宽 99.8%) |
| 960x540 → 320x240 | — | 满高 2916x2157 居中(4:3 左右留边,正确) |

对照组(单路直接播 960x540)同样是满宽 3391x2041,与修复后一致 → 判定通过。

### 新发现(2026-09-17, 渲染输出事件化测试抓到):硬解在流中段分辨率变化处停帧(未修)

与上面「换片」缺陷不同断面:同一媒体流**中段**换分辨率(TS AnnexB, 640x360→960x540),
DX11VA 硬解播到切换点前帧/事件即停(~168 帧后无输出, 输出尺寸不更新); 软解同素材完整
走完且事件契约全对。上面 T5/CS constBuf 修复全在**渲染侧**且对换片场景实测通过, 此缺陷
在**解码/送渲侧**, 渲染侧修复覆盖不到。复现: avox-test `playtest --only=file-resize-event`
(用例已临时切硬解, avox-test 3d20159); 记录: avox-test README「已知取舍与悬案」+
`doc/plan/player/open出图时延与渲染输出事件化.md` §8。

## 任务拆解

- [ ] T1 通路定案(先做):Android 用现有「OES 管线 → VkOutputLayer → AHB 导出」延伸
      (改造小、已有全链代码),不追「MediaCodec 直出 AHB」新路径;Apple 用 CVPixelBuffer
      (iOS)/IOSurface(mac)直出——VT 解码已是 CVPixelBuffer,补导出即可。
- [ ] T2 Android Flutter 桥:导出接口暴露 AHB handle + 帧可用回调;Flutter 侧 TextureRegistry
      接入(AHB→EGLImage→纹理);与 panvox 对齐 FFI 契约(生命周期/尺寸变化/重建)。
- [ ] T3 Apple Flutter 桥:iOS CVPixelBuffer 直接喂 Flutter Texture;macOS IOSurface;
      补 IMediaPlayer 层帧可用通知(现 onFrame 观察者核实口径)。
- [ ] T4 样例先行:samples 下加最小宿主(或复用 avox-unity Android 路径)验证三平台导出,
      再接 panvox 播放页(P-1 的零拷贝切换在产品侧做)。
- [x] T5 分辨率变化正确性(**已落地 + 已实测通过 2026-09-17**; 原可提前于 T1/T2 插队):修 1:1 拷贝的
      extent 不匹配 —— **二选一**:(a) 分辨率变化后重建画布,并让本层输出格式(`outFormat`)
      跟随上游;(b) Windows 拷贝改走带 `viewRect` 的 `blitFillImage`,extent 不等时就缩放,
      别静默截断。验收:同一播放器实例换片 640x360 ↔ 1920x1080 各一次,比例正确、无黑屏
      无残影(复现与实测见上「已知缺陷」节)。

## 验收

- 三平台样例出图,avox-test 归档时序/占用手册;panvox 接入后 CPU 帧率对比报告。

## 风险与开放问题

- **拷贝通路 extent 一致性(T5 已修)**:分辨率变化已靠「输入层每帧重检触发重建 + 输出 blit 兜底」
  解决 `vkCmdCopyImage` 静默截断(见上「已知缺陷·修复落地」)。软硬解切换/图重建瞬间画布与输出格式
  仍可能短暂不等, 已由 blit 兜底, 但需实测核这两条路径是否还会触发黑屏间隔。
- **Vulkan 上游的 DX11 CS 转色是同类隐患**:`Dx11CSVideoRender` 曾因 constBuf 不随分辨率重传而
  静默只填左上角(2026-09-17 修复)。**教训**:凡是「着色器常量里带尺寸 + Dispatch 用另一处尺寸」
  的地方,换分辨率都必须重传常量;`VideoProcessRender`(DXVA)通路不受影响,因为它不吃这个 constBuf。
- Android 多平面 NV12 AHB 在 Flutter 侧 EGL 导入的兼容性(设备碎片化),需真机矩阵。
- Flutter GL/VK context 与 avox Vulkan context 的外部内存口径(requirement flags/handleType)。
- CVPixelBuffer 生命周期(谁 release、pool 深度),ioSurface「换面语义」依赖调用方遵守注释,
  Flutter 侧需把该约定固化成 API 而不是注释。
