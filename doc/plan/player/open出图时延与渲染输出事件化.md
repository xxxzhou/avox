# open 出图时延优化计划（onRender 事件化: SurfaceRenderEvent 全链路去轮询）

> 状态: 进行中 · 上次核对: 2026-09-17 · 权威源: -
>
> 实测数据与根因: panvox 仓 `.workbuddy/memory/2026-09-17.md`(跨仓, 仅溯源用)
>
> **硬约束: avox 对外头文件（.h）不得引入 std::function / std::vector 等 STL——
> 跨语言/跨模块回调一律 POD 结构 + 虚函数 / C 函数指针 + void\* userdata
> （参照 `pvx_player_set_state_cb` 的写法）。`SurfaceRenderEvent` 为纯 POD，FFI/SWIG 友好。**

## 1. 背景与决策: 轮询全部放弃，事件是唯一真相

实测（`avox/build/run_open_timing.py`，4K HEVC 本地素材，直通模式）:

| 阶段 | 耗时 | 事件化后 |
|---|---|---|
| io parse / ready | ~5ms | 不变 |
| 硬解 attach + 首帧解码 | ~140ms | 不变（本轮不动） |
| Vulkan 图首建(含 DX11 设备) | ~40ms | 不变 |
| **enableVkOutputDx11 轮询空窗** | **~250-320ms** | **≤16ms（首帧事件即击）** |
| **Dart 感知 texture id** | **~250-300ms** | **≤30ms（native 事件）** |
| Flutter 上屏 | ~30ms | 不变 |

**决策（2026-09-17）**: 回调来了就一定有图（渲染循环在跑才会派发），感知类轮询全部删除:

- shim 500ms `enableVkOutputDx11` 幂等轮询 → 删，首帧事件首击
- shim 16ms `getVkOutputDx11Handle` 句柄轮询 → 删，句柄随事件携带
- Dart `_tick`(250ms) / HUD(400ms) texture id 轮询 → 删，注册成功即发事件
- 兜底不做轮询，只做诊断: open 成功后 N 秒无 texture-ready 事件打一条 error 日志

## 2. 接口设计: `onRender` 升级为携带 `SurfaceRenderEvent`（破坏性修改）

### 2.1 为什么现在改签名

SDK 处于内部开发状态，ABI 未冻结，**这是最后一个可以破坏性改接口的窗口**；
对外发布后再改就要走新旧并存的路。`ISurfaceRenderOb::onRender()`（AvoxLayer.h:324）
现有 override 共 16 处，全部同批更新:

- avox 仓 13 处: `subtitle/SubtitleView.hpp:63`（实现 SubtitleView.cpp:419）+
  samples 10 处（functest: assmkvtest/latprobe/hdrtest/seektest/yuvouttest/
  subtitletexttest, vulkantest: webrtcpull/facelandmarktest/bodyposetest/
  enableimagetest）+ platform/godot 插件 2 处（body.cpp/video_face.cpp）
- panvox 仓 3 处: `native/shim/panvox_native.cpp`(FrameOb) + dual_instance_test +
  batch_play_test

**本仓与 panvox 必须同批合入**（shim 编不过 avox 新头即断，天然防漏改）。

### 2.2 结构定义（AvoxLayer.h，置于 ISurfaceRenderOb 之前）

```cpp
// 渲染输出事件: 每帧在渲染线程随 onRender 派发, 回调来了即有图。
// ev 指针生命周期仅限本次回调; 读取前校验 structSize(ABI 演进)。
struct SurfaceRenderEvent {
  uint32_t structSize = sizeof(SurfaceRenderEvent);
  uint32_t rebuilt = 0;   // 1=相对上一帧输出图重建过(首帧/换片/分辨率变化/特效开关)
  uint32_t hdr = 0;       // 1=本帧 HDR(PQ 落帧); 元数据(maxCLL等)走 onHdrMeta, 不在此重复
  uint32_t nativeType = 0; // nativeTexture 资源类型(0=无; 原生车道接入时定枚举)
  // 输出图世代: 图重建(VkOutputLayer 重造/重绑)时 +1, 全局单调只增。
  // 跨帧比较即可判断"GPU 资源要不要重新生成/导入", 比 rebuilt 更可靠
  uint64_t generation = 0;
  // 本帧输出图格式(宽高/rowPitch/像素类型 rgba8,bgra8,rgba16f…, 见 ImageFormat)。
  // 尺寸/格式变化即体现在此, 消费端导入/建纹理按它来, 勿硬编码 RGBA8
  ImageFormat format = {};
  // ── 稳定标识句柄(非转移语义, avox 持有; 未启用对应通道为 0/null) ──
  uint64_t dx11Handle = 0;   // Windows: 共享纹理 NT 句柄(enableVkOutputDx11 通道)
  uint64_t dx11Fence = 0;    // Windows: 共享 fence NT 句柄
  void* ioSurface = nullptr; // Apple: IOSurfaceRef(非所有权, 同 VkSharedHandle 注释)
  uint64_t ioSurfaceId = 0;  // Apple: IOSurfaceGetID(), 换面探测
  // 原生车道资源标识(HDR 原生窗口对接预留, 本轮仅定义)
  void* nativeTexture = nullptr; // Windows 原生车道: ID3D11Texture2D*
};
```

签名: `virtual void onRender(const SurfaceRenderEvent* ev) {};`
（原「每帧渲染插入」注释重写为输出事件语义；用指针不用引用，FFI 映射直观。）

### 2.3 所有权语义: 哪些句柄能随事件带，哪些不能

| 通道 | 能否随事件携带 | 原因 |
|---|---|---|
| DX11 共享纹理/fence (Windows) | ✅ 携带 | avox 自持 NT 句柄，`getVkOutputDx11Handle` 本就随意读 |
| Apple IOSurface | ✅ 携带 | 非所有权，avox 持有，消费端只读/CFRetain |
| Vk-Vk NT 句柄 (Windows) | ❌ 只给 generation | `getVkOutputHandle` 每次导出新句柄、所有权转移，逐帧带会泄漏 |
| Android AHardwareBuffer | ❌ 只给 generation | 同为转移语义（release 归消费方） |

转移类通道的消费模式: **事件告知 generation 变化 → 消费端显式调一次
`getVkOutputHandle` 转移获取 → 导入/释放**。事件是「通知 + 稳定身份」，
export 是「显式获取」，两件事分开，所有权不乱。

### 2.4 填充点与派发点

- **generation**: VkOutputLayer.cpp:49 `onInitVkBuffer()`——导出资源重建的确切位置
  （bindD3D / createIOSurface / createAndroidBuffer / sharedImage 都在这重造）。
  文件内 `static std::atomic<uint64_t>` 单调递增，层实例重造也不回退。
- **填充与派发**: SurfaceRenderVk.cpp:369 `onRenderOut()` 每帧组一个
  `SurfaceRenderEvent`（format 直接取 `getOutFormat()`，generation/winImage 句柄
  全是指针解引用，无系统调用），`dispatch(&ISurfaceRenderOb::onRender, &ev)`。
- **hdr 置位**: HdrMode 实际生效 forceHDR（或 follow 判定 HDR 直通）时置 1，
  从 vkVideoRender 读当前生效模式。
- 原生车道（WindowRender/SurfaceRenderNative）本轮不派发，nativeTexture 留待
  HDR 原生窗口对接时接。

### 2.5 契约

- 回调在**渲染线程**，禁止阻塞/加锁/直接调 Dart/做 D3D 重活——只置原子、快照、notify。
- `rebuilt` 按「本次派发与上次派发的 generation 比较」置位（SurfaceRenderVk 记一个
  lastDispatchGeneration），首个事件恒为 1。
- ev 恒非 null；format 为本帧输出图格式（直通模式宽高=视频尺寸，不带黑边；
  消费端用 `format.bVailid()` 判有效）。

## 3. 施工项 1: avox 侧

1. AvoxLayer.h: 加 `SurfaceRenderEvent`（2.2）+ `onRender` 改签名（2.1 全部
   13 处 override 同批更新，空实现处仅补参数）。
2. VkOutputLayer.hpp/.cpp: `getGeneration()` + onInitVkBuffer 里 `++` 全局原子。
3. SurfaceRenderVk.cpp: `onRenderOut()` 组事件 + 派发（2.4）。
4. Avox.cpp:837 `enableVkOutputDx11: outputLayer is null` warn 限频（事件化后
   首击时机已对，防御性保留）。

## 4. 施工项 2: panvox shim 事件化重构（与施工项 1 同批合入）

**FrameOb::onRender**（panvox/native/shim/panvox_native.cpp:256，渲染线程，只做置位）:

```cpp
void onRender(const avox::SurfaceRenderEvent* ev) override {
  if (!ev || !ev->format.bVailid()) return;
  if (!dx11Enabled_.exchange(true)) {
    enableVkOutputDx11(sr_);   // 幂等, 内部只置标志+请求重建, 下帧生效
  }
  // generation 变化: 快照句柄, 唤醒桥线程去 OpenSharedResource1
  if (ev->dx11Handle && ev->dx11Handle != lastHandle_.load()) {
    lastHandle_.store(ev->dx11Handle);
    { std::lock_guard<std::mutex> lk(cvMtx); genChanged_ = true; }
    cv.notify_one();
  }
}
```

- 时序: 帧 N 首帧渲染 → onRender 首击 enable；帧 N+1 onInitVkBuffer
  bindD3D 完成 → 事件携带 dx11Handle → 桥线程 Open。**两帧内闭环**，
  替代原 500ms 轮询首击（平均省 ~250ms）+ 16ms 句柄轮询。

**桥线程**（gpuThreadMain 重构，职责缩为「交付节拍」，不再是感知轮询）:

- 等 condvar（100ms 超时兜底）→ `genChanged_` 则走现有 openSrc 流程
  （seenSrc 环形黑名单、AMD 防重试规矩原样保留——OpenSharedResource 是 D3D 重活，
  必须留在桥线程，不上渲染线程）。
- fence 推进 → blitMirror + Flush + mark（原有逻辑不动）。
- `registerTex` 成功处（`gpu texture registered` 打印旁）发 texture-ready 事件到
  Dart（沿用 subtitle/state 的事件栈 `NativeCallable.listener`）:

  ```c
  // panvox_native.h: C 函数指针, 无 STL; ABI 只在尾部追加, pvx_abi_version 递增
  PVX_API void pvx_set_texture_ready_cb(pvx_player_t* pl,
                                        void (*cb)(int64_t tex_id,
                                                   int32_t w, int32_t h,
                                                   void* user),
                                        void* user);
  ```

  换片 epoch 重建纹理时同样触发；事件带 w/h，Dart 端即时适配布局。

**删除清单**: 500ms enable 轮询（:686-690）、16ms getVkOutputDx11Handle 轮询
（:691-706 的轮询触发部分，环形黑名单逻辑移入事件路径）、Dart `_tick` 250ms
与 HUD 400ms 的 texture id 拉取。CPU 读回兜底模式（PANVOX_GPU_SHARED=0）不受影响，
照走桥线程。

## 5. 施工项 3: 引擎插件迁移（一次设计，三端 + UE 同构）

| 端 | 现状 | 迁移后 |
|---|---|---|
| Unity 拷贝模式(flavor 2/3) | PlayerBridge.cpp:325 渲染事件内每帧比句柄 | 可不改（已是渲染节拍等价零空转）；可选: 收 generation 变化提前重开，省「读到 0 再重申」一拍 |
| Unity 直通(flavor 1) | updateGpu 轮询 enableVkOutput+getVkOutputHandle | 注册 observer: generation 变化 → 显式 export 一次 → 导入 |
| Godot | surface.cpp:494 每帧重调 enableVkOutput(幂等) | observer 首帧 enable + generation 变化才重新 import（Android AHB 走 export 一次） |
| avox-ue | RHI 直通，轮询探测句柄 | 同 Godot 模式，事件驱动重导入 |

引擎侧注册各自实现 `ISurfaceRenderOb`（或复用已有 bridge 对象加父类），
`addSurfaceRenderOb` 现成。跨线程纪律同 2.5: 事件回调只置原子，导入动作回
各引擎自己的渲染线程做。

## 6. 验收

```bash
# 时延复测(4K 素材 D:/Work/build/test_4k_hevc_30s.mp4 已生成):
python avox/build/run_open_timing.py D:/Work/build/test_4k_hevc_30s.mp4 12 0
# 判定: 黑→亮 diff≈114 尖峰时刻 - open 时刻 ≤ 0.85s(现状 1.4s)
# 同时跑 RESUME 与 SWITCH 场景确认无回归:
python avox/build/run_open_timing.py D:/Work/build/test_4k_hevc_30s.mp4 15 0 PANVOX_AUTOPLAY_RESUME=8
```

- 日志预期: `enableVkOutputDx11: ok` 与 `Pipegraph reset success` 间隔 < 50ms
  （现状 ~300ms）；Dart texture-ready 事件与 `gpu texture registered` 间隔 < 30ms。
- 分辨率变化素材: 事件 rebuilt=1 / generation+1 / format 宽高更新三者同时刻出现。
- 回归面: 换片(SWITCH)/字幕加载(会重建图)/readback 模式(PANVOX_GPU_SHARED=0)各跑一遍。
- 诊断兜底: 人为注释掉 texture-ready 发射，确认 N 秒 error 日志能报出（不做拉取）。

## 7. 风险与不做的事

- 不改 `getVkOutputHandle`/AHB 的转移语义（事件只给 generation，获取仍显式）。
- 不动 seenSrc 环形黑名单与 AMD 驱动防重试规矩（每值仅一次尝试）。
- 原生车道（WindowRender/SurfaceRenderNative）的事件派发与 nativeTexture 填充
  本轮不做，留给 HDR 原生窗口对接专项。
- 不在本轮追「图首建 40ms / 首帧解码 140ms」——收益小、动核心渲染路径，先不做。

## 8. 落地进度 (2026-09-17)

**已完成并验证（施工项 1 + 测试）**:

- 接口落地: `SurfaceRenderEvent` + `onRender(const SurfaceRenderEvent*)` 破坏性改签名,
  16 处 override 同批更新（2.1 清单）; VkOutputLayer generation（onInitVkBuffer
  `++s_outputGeneration`）; onRenderOut 组事件+派发（hdr 位取 `getHdrMode()==forceHDR`）;
  enableVkOutputDx11 null warn 只告警一次。
- 验证: `build_windows.py` 全量过; `ctest` 2/2; panvox shim 对新头编译过
  （`cmake --build native/shim/build`, 零错误）。
- 测试落地（avox-test）: 新用例 `file-resize-event`（CaseKind::renderEvent,
  RenderEventProbe 判事件契约）+ 素材脚本 `script/testenv/gen_resize_asset.py` +
  素材 `assets/video/test_h264_resize_640x360_960x540.ts`（TS 中段 640x360→960x540）,
  已接入 `play_regress.py` 离线子集。实测: 软解 354 个有效事件,
  `640x360->960x540`, 首事件 rebuilt=1 + 世代递增信号对全部命中, PASS。

**新发现缺陷（测试抓到, 待修）**: **硬解(DX11VA)在流中段分辨率变化处停帧**——
播到切换点前事件即止（~168 帧）且尺寸不更新; 软解同素材正常。用例因此固定软解
（`c.hardDecode=false` + 注释），硬解修复后改回 true 对照。详见
`avox-test/l1_avox/playmatrix/README.md`「已知取舍与悬案」。

**待施工**: 施工项 2（panvox shim 事件化重构: 删 500ms/16ms 轮询 + texture-ready
事件 + Dart 侧接事件删 tick）与施工项 3（引擎插件迁移）。本轮 shim 仅做了
编译适配, 行为未切换。另注意: avox 增量构建不刷新 `install/include` 旧头
（`copy_head` 在 cmake 重新配置时才拷）, 跨仓编译前先 `cmake .` 重配置一次。
