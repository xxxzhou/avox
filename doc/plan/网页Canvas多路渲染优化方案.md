# 网页 Canvas 多路渲染优化方案

> 适用场景:Electron 内嵌 `<canvas>` 多路播放(Intel UHD 770 集显,10 路 1080P 起卡顿)。
> 本文只做方案分析与取舍,不改代码。相关现状见 [播放器Electron](../player/platform/播放器Electron.md)、[OffscreenCanvas自适应分辨率设计](./OffscreenCanvas自适应分辨率设计.md)、[多平台GPU共享](../player/多平台GPU共享.md)。

---

## 1. 问题现象

集显(UHD 770)上用网页 canvas 打开 10 路 1080P 流,出现卡顿/掉帧/GPU 占用高。

## 2. 当前数据流

网页 canvas 路径(Electron,非原生窗口路径):

```
DXVA硬解(D3D11纹理)
  → VkInputLayer 取入 Vulkan 纹理
  → Vulkan 特效管线(水印/LUT/亮度对比度/缩放/字体/几何…)
  → VkOutputLayer [bCpu 分支]                     ← GPU→CPU 回读段
       imageToBuffer: Vulkan纹理 → outBuffer(staging,TRANSFER_DST)
       outBuffer.flush → cpuBuffer.setData
       写入 JS 申请的 ArrayBuffer(setRenderJsBuffer,零拷贝)
  → 回调 JS onFrame(frame)                        ← 跨边界
  → YuvGLRender/YuvWebGPURender.renderPbo         ← CPU→GPU 上传段
       WebGL2: gl.texSubImage2D(R8, width×height*1.5)
       WebGPU : device.queue.writeTexture(r8unorm)
  → drawArrays / draw → 自动 commit
```

关键代码位置:
- 回读:`src/avox_vulkan/layer/VkOutputLayer.cpp:82-106`(`onFrame` 的 `paramet.bCpu` 分支)、`:121-128`(`onCommand` 里 `imageToBuffer`)
- 零拷贝写 JS buffer:`swig/nodejs/preload.js:191-195`(`setRenderJsBuffer`)+ `JsSurfaceRenderOb.jsManagedBuffer`
- 上传:`swig/nodejs/yuvglrender.js:674-708`(WebGL `texSubImage2D`)、`:393-416`(WebGPU `writeTexture`)
- 合帧防堆积:`swig/nodejs/preload.js:196-208`(`requestAnimationFrame` 只渲染最新帧)

## 3. 瓶颈量化

I420/YUV420P = 1.5 字节/像素。1080P 单帧 = 1920×1080×1.5 ≈ **2.97 MiB**。

| 段 | 单路@30fps | 10 路@30fps |
|----|-----------|------------|
| GPU→CPU 回读 | 89 MiB/s | **~890 MiB/s** |
| CPU→GPU 上传 | 89 MiB/s | **~890 MiB/s** |
| 合计往返系统内存 | — | **~1.7 GiB/s** |

集显用**共享内存**,回读是 GPU tile→系统内存的 cache flush/invalidate 慢路径(PCIe 级),持续 ~1.7 GiB/s 已接近有效带宽上限,还要和 CPU 其它工作、GPU 命令缓冲争抢。这是 10 路卡顿的直接原因。

## 4. 现状盘点(已经做对的优化)

| 优化 | 位置 | 作用 |
|------|------|------|
| `jsManagedBuffer` 零拷贝 | `preload.js:60,191-195` | C++ 直接写 JS ArrayBuffer,省掉 staging→JS 的一次拷贝 |
| `autoSizeScale` 分级降档 | `preload.js:125-175` | Vulkan 管线内缩放输出,1/0.5/0.25/0.125 四档 |
| rAF 合帧 | `preload.js:196-208` | 多帧只渲染最新一帧,防上传堆积 |
| 页面 hidden 跳过 | `preload.js:178-180` | `visibilityState==='hidden'` 不上传 |
| >16 路切 WebGPU | `preload.js:77-82` | 规避 WebGL context ~16 上限 |
| I420/NV12(1.5B/px) | `preload.js:443,505` | 比 RGBA(4B/px)省 62% 带宽 |

这些都对,本文**不重复**,只在它们基础上找增量。

## 5. 根因:网页为什么被迫走 CPU 回读

`VkOutputLayer` 本身有**两条**输出路径(`OutputParamet.bCpu` / `bGpu`):

- `bCpu=true`(网页 `setOffSurface` 路径):`imageToBuffer` 回读到 CPU。← 网页走这条
- `bGpu=true`(原生窗口 `setSurface` 路径):`VkOutputLayer.cpp:132-181` 用 `VkWinImage` 把 Vulkan 结果 `copyImage` 到 D3D11 **共享纹理**,不过 CPU,返回 NT 共享句柄(`getOutGpuBuffer()`)。← 原生窗口走这条

也就是说,**零 CPU 回读的 interop 机制现成**,只是网页用不上。原因见 [播放器Electron.md:94](../player/platform/播放器Electron.md) 的作者实证:

> "把底层渲染结果通过 DX11 共享 NT 句柄,用 WebGPU 导入渲染"——**发现相应 API 根本没有,是 AI 自己编的**。Map 到 CPU 再给浏览器,硬解就没意义了。

结论:**标准 WebGL/WebGPU 不开放"导入外部 D3D11 共享纹理句柄"的能力**,这是 Chromium 的能力边界,非本项目问题。所以网页 canvas 必然要走 CPU 中转,优化的本质只能是「**让中转的数据量变小**」+「**让上传更便宜**」+「**换一种不中转的呈现方式**」。

### 5.1 关键:节流必须落在 C++ 侧(回读在回调之前)

这是最容易踩的坑。`onFrame(frame)` 回调被调用时,**C++ 已经回读完了**(JS buffer 已被写入)。因此:

- JS 侧 `rAF` 合帧、`return` 跳帧 —— **只省上传,不省回读**。回读每解码一帧仍在 C++ 线程发生一次。
- 真要降回读带宽,节流/暂停必须**作用在 C++ 输出层**(让 `VkOutputLayer` 每 N 帧才回读,或整路暂停输出)。

后续凡涉及"省回读"的方案,都以此为前提。

### 5.2 "C++ GPU 帧直接对接网页 canvas" 到底行不行

这是反复追问的核心。分三层看(均有据,链接见末"参考"):

**① 标准 Web 平台(stock Electron/Chrome):不行。**
- WebGPU 的 `importExternalTexture` 只接受 `HTMLVideoElement`/`MediaStreamTrack`/`VideoFrame`,**不接受裸 GPU 句柄**。导入平台共享纹理(DXGI/IOSurface/dmabuf)仍是 proposal:[gpuweb#5167](https://github.com/gpuweb/gpuweb/issues/5167)、可写共享纹理 [gpuweb#6236](https://github.com/gpuweb/gpuweb/issues/6236),未落地。
- 所以原生 Vulkan/D3D11 纹理无法零拷贝喂给网页 WebGL/WebGPU。这是 Web 平台边界,非本项目问题,也印证了 [播放器Electron.md:94](../player/platform/播放器Electron.md) 作者早年的实证。

**② 改 Chromium/Electron:技术可行,有先例。**
- ANGLE 内部扩展 `ANGLE_d3d_texture_client_buffer`(Chromium 自用它把视频帧喂进 `<video>`/WebGPU)——只对 Chromium C++ 开放,不对 JS 开放。见 [angleproject#2820](https://bugs.chromium.org/p/angleproject/issues/detail?id=2820)。
- [Biohazard90/chromium-webgl-dx11-shared-texture](https://github.com/Biohazard90/chromium-webgl-dx11-shared-texture):给 Chromium 打补丁加 `gl.dxImportSharedTexture(dxgiHandle)`,DXGI 共享句柄→WebGL 纹理,**真零拷贝**——证明技术上完全跑得通。代价:自己编译/维护 Electron/Chromium 分支。
- Electron 33+ 的 OSR `useSharedTexture`(`BrowserWindow` paint 事件带 `sharedTextureHandle`,见 [offscreen-rendering](https://electronjs.org/docs/latest/tutorial/offscreen-rendering)、[electron#45428](https://github.com/electron/electron/issues/45428))已打通 **web→native** 方向的 GPU 共享;native→web 方向有 [electron#49762](https://github.com/electron/electron/issues/49762) `getNativeVideoFrameHandle` proposal 在讨论。方向在演进,但 stock 版本到不了。

**③ 工业现实:没人用"C++ GPU 纹理零拷贝直通网页 canvas"。**
- 监控/会议/串流厂商处理"native 帧→浏览器显示",主流就三种:
  - **原生窗口叠加**(native child window 浮在 web 上):Teams 等、Steam Overlay。← 本项目 C 方案,工业主流之一。
  - **重新编码走标准媒体管道**:native 编 H.264/AV1 → WebRTC/`<video>` → Chrome 自己硬解 → `importExternalTexture` 零拷贝。游戏串流(Moonlight/Sunshine、GeForce Now)走这条。代价:多一次编码(有损+开销),且特效得在编码前于 native 侧做完。
  - **CPU 共享**(WASM/SharedArrayBuffer):线程受限、性能差,本项目已明确放弃。
- 几乎无公开工业实现走"C++ GPU 纹理零拷贝直通网页 canvas"——因为它要求改 Chromium 或自维护分支,得不偿失。

**结论**:AVOX 架构下,"native GPU 帧零拷贝进 canvas"要么不可行(标准平台),要么代价是自维护 Chromium 分支。务实选择是 A(压中转量)+ C(原生叠加);若坚持 canvas 内嵌 + 真零拷贝,唯一硬路是自维护 Electron/Chromium 分支打 DXGI 补丁(见推荐路径第三阶段)。

## 6. 方案

### 方案 A:负载侧深化(低成本,立刻见效)

> 不动架构,纯增量。10 路大概率能压住,**应先做满**。

**A1. C++ 侧回读帧率节流**
监控墙不需要 30fps,15/10fps 人眼无感。在 `VkOutputLayer`(或 `SurfaceRenderVk` 输出环节)加"每路目标 fps",按 PTS 丢回读帧。
- 收益:**回读带宽线性下降**,30→15 直接减半(~445 MiB/s),30→10 减到 1/3。
- 前提:C++ 改动 + JS 侧 `setTargetFps()` 配置口。**省的是回读段(最贵的那段)**,ROI 最高。
- 注意:解码仍可全速(保留 seek/抽帧能力),只在"输出到网页"这一步节流。

**A2. 可见性暂停(IntersectionObserver)**
`preload.js` 现只处理整页 hidden。监控墙滚出视口的 canvas 仍在回读上传。给每个外部 canvas 挂 `IntersectionObserver`,不可见时通知 C++ **暂停该路输出**(`bCpu` 回读停),可见再恢复。
- 收益:看不见的路带宽归零。
- 前提:C++ 加"暂停/恢复输出"开关;JS 用 IO 驱动。同样必须作用在 C++ 侧才省回读。

**A3. `autoSizeScale` 更激进 + 更早触发**
当前 `preload.js:156-162` 四档(1/0.5/0.25/0.125),阈值偏保守(`OffscreenCanvas` 设计文档担心降档伤画质)。监控小画面场景可:
- 加 0.75 档、把触发阈值上调(ratio≤0.75 即降 0.75)。
- 带宽按 `scale²` 下降:0.5→1/4、0.25→1/16、0.125→1/64。**回读+上传同时降**,平方级收益。
- 画质风险:监控墙小画面本就看不清细节,激进降档观感无损(设计文档已论证)。

**A4. 默认 WebGPU + 单 device 多 canvas(重要)**
现状 `yuvglrender.js:189-190` **每路各 `adapter.requestDevice()` 一次**,即 10 路 = 10 个独立 GPU device,各自 swapchain/sampler/pipeline,开销大且无法复用。改造:
- 全局共享一个 `adapter` + `device`,所有路 `gpuContext.configure({device})` 共用。
- `sampler`/`pipeline`/`shaderModule` 单例化(它们与画面无关,本就该共享)。
- 收益:device/context 开销从 O(N) 降到 O(1);`writeTexture` 在 Dawn/D3D12 后端上传普遍比 WebGL2 的 `texSubImage2D`(ANGLE/D3D11)高效、并行更好。
- 改造点集中在 `yuvglrender.js`,把 device 提到模块级单例。同时把 `preload.js:77-82` 的">16 路才切 WebGPU"改为**默认 WebGPU**(Electron Chromium 稳定支持),WebGL2 仅作 fallback。

### 方案 B:WebCodecs `VideoFrame` + WebGPU `importExternalTexture` —— ⚠️ 当前架构下无效(伪优化)

这条路反复被提出又反复被否,根因值得写清,避免重复踩。

**设想的路径:**
```
C++ 解码/渲染 → NV12 buffer → new VideoFrame(buffer,{format:'nv12',...})
   → device.importExternalTexture({source: videoFrame})   ← 期望零拷贝上传
   → WebGPU 用 texture_external 采样渲染
```

**为什么在"C++ 解码 + Node addon"架构下走不通:**

1. **C++ 访问不到 Chrome 的 VideoFrame。** `VideoFrame` 是 WebCodecs 的 JS 平台对象(blink/V8 上下文),addon 无法直接 `new` 一个塞给 WebGPU。唯一可行的是:JS 拿到 C++ 已回读到 CPU 的 ArrayBuffer,在 JS 侧 `new VideoFrame(cpuBuffer, ...)`。

2. **`new VideoFrame(arrayBuffer)` 是 CPU-backed。** `importExternalTexture` 零拷贝的前提是 VideoFrame 的 backing **已经是 GPU 纹理**(来自 `<video>` 硬解、`MediaStreamTrack`、或 WebCodecs 的 `VideoDecoder` 硬解输出)。CPU buffer 没有现成 GPU 纹理可复用,Chrome 仍要把这段内存**上传一遍**——回读没省,上传也没真省,且可能多一道格式/布局转换,**甚至比直接 `writeTexture` 更慢**。

3. **要让 VideoFrame 变 GPU-backed,等于换架构。** 只有把解码搬到 WebCodecs 的 `VideoDecoder`(JS 侧硬解)或走 `<video>`/`MediaStreamTrack`(Chrome 自管硬解)才拿得到 GPU-backed VideoFrame——那时 C++ 的 IO/解码/特效管线整个被架空,已经不再是"B 方案",而是接近方案 D。

**结论:保留 C++ 解码的前提下,方案 B 是伪优化,不推荐。** `importExternalTexture` 本身是真实 API(输入限视频源对象,不是任意 GPU 句柄,恰好绕开作者已证伪的"导入 NT 句柄"死路),但它的零拷贝语义依赖一个本架构给不出的前提。要在网页侧吃到 GPU-backed 视频源,需评估"解码整体迁 WebCodecs"的可行性(见方案 D 的前置)。

> 若坚持试 B 做对比验证:把 `new VideoFrame(cpuBuffer)` + `importExternalTexture` 与现有 `writeTexture` 同基准测上传耗时/GPU Copy 引擎占用,预期 CPU-backed 路径无优势甚至更慢——实测确认即可彻底排除,不必再反复论证。

### 方案 C:原生窗口叠加(已有方案,真零拷贝但非 canvas)

[播放器Electron.md:137-142](../player/platform/播放器Electron.md) 已实现:
`BrowserWindow.getNativeWindowHandle()` → `VkWinImage` 生成 D3D11/Vulkan Swapchain → `transparent:true` 浮窗叠在主窗口上,`VkVideoRender` 用 `bGpu=true` interop,**全程不过 CPU**。

- 收益:回读+上传**两段全消**,性能等同原生,10 路无压力。
- 代价:播放区不是真 canvas,不能与 HTML 元素自由叠加/CSS 变换(靠 mediaWindow 加载上层 HTML 变通,较 hacky);失去"网页内嵌"的灵活性。
- 适用:对"必须是 canvas 内嵌"没硬要求、只要能播多路的场景,这是现成解。

### 方案 D:整体迁 WebGPU(含 WebCodecs 解码)—— ⚠️ 在 AVOX 架构下走不通

> 之前把 D 当"唯一长期解"是错的。问题不在工作量,而在**浏览器承载不了 AVOX 的核心栈**。

- **IO/协议**:WebCodecs 的 `VideoDecoder` 只吃标准编码帧;`<video>` 只认标准容器。AVOX 的 zlmediakit/ffmpeg IO 层(RTSP/RTMP/私有协议、自定义解封装、IO plan)**进不去浏览器**——沙箱不让自由开 socket、做私有协议。
- **线程模型**:AVOX 的多线程解码/音视频同步/帧队列无法照搬;浏览器只有 worker + SharedArrayBuffer,语义和能力都不同。
- **特效**:Vulkan 100+ 效果 + 几何/字体图层要全迁 WGSL,且 WebGPU 共享纹理未落地时仍绕不开"解码帧→GPU"的上传。

所以 D 不是"工作量大",是**走不通**:把 AVOX 搬进浏览器 = 抹掉 AVOX 存在的理由(native IO/解码/特效/线程自由)。**WebCodecs 不是 AVOX 的解**。真要全 Web,等于另起一个产品,不在本文讨论范围。

### 方案 E:Media Foundation 虚拟相机源(D3D11 纹理)→ getUserMedia → importExternalTexture(Windows-only,可能真零拷贝,待 PoC)

> 用户提出的"虚拟相机"思路。绕的是另一道门:不让 C++ 导入裸 GPU 纹理(标准 API 拒),而让 Chrome 自己从一个虚拟捕获设备取帧(`getUserMedia` + `importExternalTexture`,标准 API 允许)。

**两种虚拟相机,结论相反:**

| 类型 | 传帧介质 | 回读 | 结论 |
|------|---------|------|------|
| OBS 式(DirectShow/MF filter + 共享内存) | CPU 共享内存 | **发生** | Chrome 能抓,但回读没省、多一跳,pass |
| MF 虚拟源 + D3D11 纹理输出 | GPU D3D11 纹理 | **可能不发生** | **可能真零拷贝**,有官方管道支持,可实测 |

**有希望的链路(MF + D3D11 源):**
```
C++ Vulkan → VkWinImage D3D11 共享纹理(已有,VkOutputLayer.cpp:132-181 的 bGpu interop)
  → Media Foundation 虚拟相机源,以 D3D11 纹理(IMFDXGIDeviceManager)输出
  → Chrome getUserMedia({video:{deviceId}}) → MediaStreamTrack(GPU-backed)
  → 经 <video> 或 VideoFrame → device.importExternalTexture({source})
  → GPUExternalTexture → WebGPU 采样  ← 查 .isZeroCopy 验证是否真零拷贝
```

**依据(2026):**
- Chromium 官方在优化 MF 相机捕获为 GPU 零拷贝:[crbug#40145992](https://issues.chromium.org/40145992)(Zero-Copy Camera Capture on Windows,帧保持 GPU 内存)。
- MF 原生支持 D3D11 纹理交接(`IMFMediaBuffer`/`IMFDXGIDeviceManager`),无需 CPU 拷贝([MS Supporting D3D11 Video in MF](https://learn.microsoft.com/en-us/windows/win32/medfound/supporting-direct3d-11-video-decoding-in-media-foundation))。
- Windows 上 D3D11↔D3D12 纹理共享让源 D3D11 纹理被 WebGPU D3D12 后端读取不经 CPU([gpuweb#700](https://github.com/gpuweb/gpuweb/issues/700))。
- **可验证**:Chrome 非标准属性 `GPUExternalTexture.isZeroCopy`([developer-features](https://developer.chrome.com/docs/web-platform/webgpu/developer-features))能运行时查这帧是否真零拷贝——不必猜。

**代价/限制:**
- **写 MF custom video source**(`IMFMediaSource`/`IMFStreamSink`,注册成系统捕获设备,输出 D3D11 纹理):Windows 重活,但有摄像头 MF 源范例可参。
- **Windows-only**(MF 是 Win API);Android/iOS/Web 无对应。
- **多路**:每个虚拟相机是独立系统设备,getUserMedia 并发流数有限,10 路要实测;可能注册多设备或单设备时分。
- MediaStreamTrack 须经 `<video>`/VideoFrame 才能 importExternalTexture。
- 特效(水印/LUT/OSD)必须在喂 MF 源之前于 native 侧做完;VkWinImage 已能产 D3D11 共享纹理,正好喂源。

**先做 PoC 再投入(最重要):** 整个方案成立与否取决于 Chrome 当前版本对 D3D11-backed MF 源是否真走零拷贝。最小验证:写一个吐固定测试 D3D11 纹理的最简 MF 虚拟源 → Chrome `getUserMedia` 打开 → `importExternalTexture` → 打印 `isZeroCopy`。成本不高,是 decision point;通过则它是 Windows 上比"自维护 Chromium"轻得多的 canvas 内嵌零拷贝路径。

**在方案谱系里的位置:** 介于 C(原生叠加,已零拷贝非 canvas)与"自维护 Chromium"(零拷贝但维护重)之间——用标准 API、比硬路轻、比 C 更 canvas 内嵌;代价是 Windows-only + 写 MF 源。

## 7. 方案对比

| 方案 | 改动量 | 消掉瓶颈 | 回读带宽收益 | 上传带宽收益 | 保留 canvas 内嵌 | 保留 Vulkan 特效 |
|------|--------|---------|------------|------------|----------------|----------------|
| A1 帧率节流 | 小(C+++) | — | **~1/2~1/3** | ~1/2~1/3 | ✅ | ✅ |
| A2 可见性暂停 | 小(C+++) | — | 看不见的路归零 | 同左 | ✅ | ✅ |
| A3 sizeScale 激进 | 小(JS) | — | 平方级 | 平方级 | ✅ | ✅ |
| A4 单 device WebGPU | 中(JS) | — | — | 上传更便宜 | ✅ | ✅ |
| B VideoFrame+WebGPU | — | — | — | ❌ 当前架构伪优化 | — | 需换架构才有效 |
| C 原生窗口叠加 | 已实现 | **回读+上传** | **全消** | **全消** | ❌ | ✅ |
| D 整体迁 WebGPU | — | — | — | — | — | ⚠️ 走不通(浏览器承载不了 AVOX 的 IO/线程/协议) |
| E MF虚拟相机(D3D11源) | 大(C++ Win) | **可能**回读+上传 | 待 PoC | 待 PoC | ✅ | Win-only;特效须喂源前于 native 做完;用 isZeroCopy 验证 |

## 8. 推荐路径(分阶段)

1. **第一阶段(先做满 A,几天)**:A1(C++ 回读节流)+ A2(可见性暂停)+ A3(sizeScale 激进)+ A4(单 device + 默认 WebGPU)。不动架构,纯增量,大概率把 10 路 1080P 压住。**关键:节流/暂停落在 C++ 侧才省回读。**
2. **第二阶段(若 A 仍不够)**:方案 B(VideoFrame + importExternalTexture)在当前架构下已证伪、排除。此时只剩两条:
   - 若"非 canvas 内嵌"可接受 → 直接用**现成的 C(原生窗口叠加)**,回读+上传两段全消,零重构成本。
   - 若必须 canvas 内嵌且接受重构 → 走 **D(整体迁 WebGPU)**。注意 D 的"解码迁 WebCodecs 硬解 + importExternalTexture"才是网页侧吃到 GPU-backed 视频源的唯一现实路径,届时 B 的思路才重新成立。
3. **第三阶段(若必须 canvas 内嵌 + 真零拷贝)**:唯一硬路是**自维护 Electron/Chromium 分支**,用 [DXGI 共享句柄补丁](https://github.com/Biohazard90/chromium-webgl-dx11-shared-texture)打通 native GPU 纹理→WebGL/WebGPU(`VkWinImage` 已能产出 NT 共享句柄,正好对接)。技术有先例、能跑通,代价是背 Chromium 升级维护成本,适合长期、有团队支撑的场景。方案 D(全栈迁 Web)在 AVOX 架构下走不通,排除。
4. **Windows 专用、canvas 内嵌 + 零拷贝的较轻选项(先 PoC)**:方案 E(MF 虚拟源 + D3D11 纹理)。在投入自维护 Chromium 之前,先花小成本写最简 MF 源 PoC,用 `GPUExternalTexture.isZeroCopy` 验证 Chrome 当前版本是否真零拷贝——通过则它是 Windows 上比自维护 Chromium 轻得多的 canvas 内嵌零拷贝路径,可替代第 3 阶段;不通过再考虑自维护 Chromium。

> 共同前提:UHD 770 本身算力有限,10 路即使全 GPU(解码+渲染+合成)也吃力。**传输优化(消回读)与负载优化(降帧/降分辨率/只渲染可见)必须并行**,光优化传输路径不够。

## 9. 验证方法

- **带宽**:`gpu_view`/任务管理器看 GPU 内存带宽;或在 `VkOutputLayer` 回读处埋点统计每秒字节数。
- **帧率/丢帧**:`MediaPlayer.getFps()` + 解码侧丢帧计数;监控 rAF 实际触发 fps。
- **GPU 占用**:任务管理器 GPU% / Intel Graphics Monitor,分"3D"/"Copy"/"Video"引擎看回读拷贝是否占满 Copy 引擎。
- **对比基准**:同 10 路 1080P,逐项开 A1/A2/A3/A4,记录 fps/GPU%/带宽曲线。

## 附录:相关代码索引

| 关注点 | 位置 |
|--------|------|
| 回读(bCpu) | `src/avox_vulkan/layer/VkOutputLayer.cpp:82-106, 121-128, 292-320` |
| interop 零回读(bGpu) | `src/avox_vulkan/layer/VkOutputLayer.cpp:132-181` |
| D3D11 共享纹理机制 | `src/avox_vulkan/windows/VkWinImage.hpp` |
| JS 零拷贝 buffer | `swig/nodejs/preload.js:60, 191-195` |
| onFrame/rAF 合帧 | `swig/nodejs/preload.js:176-208` |
| autoSizeScale 分级 | `swig/nodejs/preload.js:125-175` |
| WebGL 上传 | `swig/nodejs/yuvglrender.js:674-708` |
| WebGPU 上传(每路独立 device) | `swig/nodejs/yuvglrender.js:185-227, 393-416` |
| 作者对"DX11 共享句柄+WebGL"的证伪 | `doc/player/platform/播放器Electron.md:94` |
| 原生窗口叠加现成方案 | `doc/player/platform/播放器Electron.md:137-142` |
