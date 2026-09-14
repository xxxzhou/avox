# Apple GPU 直通计划 (macOS/iOS IOSurface 导出)

> 2026-09-14 调研与规划, 同日 M1 SDK 侧落地。
> **已完成**: VkSharedHandle 扩展 ioSurface/ioSurfaceId(非所有权契约) +
> enableVkOutput/getVkOutputHandle Apple 分支(Avox.cpp) + VkOutputLayer/
> VkIosImage 访问器 + 探针 `samples/vulkantest/iosharedtest.mm`(B 端改用
> CVPixelBuffer→Metal 读回 —— 即 panvox/Flutter 真实消费链路, 比计划里
> 的第二 VkDevice 方案更贴验收; 位置也比 avoxtest 更贴 vksharedtest 家)。
> win 门禁全绿(build + ctest 2/2 + 离线回归 8/8)。
> **M1 macOS 无头验证 PASS**(2026-09-14, Mac mini M2 + MoltenVK): iosharedtest
> enableVkOutput → getVkOutputHandle(IOSurface id=44) → CVPixelBuffer('BGRA')
> → Metal 读回 **match0xAB=100%**, 轮询期 ioSurfaceId 稳定无 churn。
> Mac 构建备忘: 非交互 ssh 需 `PATH=/opt/homebrew/bin:$PATH` +
> `VULKAN_SDK=$HOME/VulkanSDK/1.4.313.0/macOS`(iOS 用 iOS slice), 探针构建需
> `AVOX_CMAKE_ARGS="-DAVOX_ENABLE_SAMPLES=ON"`(Mac 缓存默认 OFF)。
> **待办**: M2 iOS 真机 + panvox Flutter 桥(iOS 侧复用同一探针逻辑)。
> 背景: GPU 直通 win/android 已通 ([多平台GPU共享](../../player/decode/多平台GPU共享.md),
> win 双形态 Vk↔Vk/D3D11 + android AHB 均实测 PASS); panvox(Flutter)消费端在
> win/android 已跑通, mac/iOS 缺口补齐后全端拉平。本计划行号基于当前 main,
> 仅作定位参考。
> 结论先行: **Apple 侧数据通路已在产线运行, 不是纸面设计** —— VkOutputLayer
> 的 `__APPLE__` 分支无条件建 VkIosImage 并每帧 blit 进 IOSurface-backed
> VkImage, 该链已被 VideoToolbox 编码管线消费 (IOSVEncoder)。
> 缺的是: ①对宿主的正式导出 API ②独立设备读回验证 ③panvox/引擎消费桥。

## 1. 现状盘点 (比「IOSurface(待做)」靠前)

### 1.1 SDK 侧已有

- MoltenVK 构建链在位: [LinkVulkan.cmake](../../../cmake/LinkVulkan.cmake#L41)
  从 `$VULKAN_SDK/lib/MoltenVK.xcframework` 拉 ios-arm64/macos 两个 slice。
- 输出通路常开: [VkOutputLayer.hpp:38](../../../src/avox_vulkan/layer/VkOutputLayer.hpp#L38)
  Apple 分支持有 `vkIosImage`; [VkOutputLayer.cpp:179](../../../src/avox_vulkan/layer/VkOutputLayer.cpp#L179)
  每帧 `blitFillImage` 进 IOSurface-backed VkImage (顺带 RGBA→BGRA 转换),
  不需要任何 enable 前置。
- IOSurface 生命周期: [VkOutputLayer.cpp:93](../../../src/avox_vulkan/layer/VkOutputLayer.cpp#L93)
  按管线 outFormat 建; 图重建/尺寸变化会重建 → 指针/ID 变化, 与 win
  「句柄 churn 需幂等重声明」同款契约。
- Vulkan→IOSurface 写入已被 VT 编码消费:
  [IOSVEncoder.mm:140](../../../src/avox_apple/IOSVEncoder.mm#L140)
  `getIOSurface()` → `CVPixelBufferCreateWithIOSurface` → VideoToolbox。
  即「包装成 CVPixelBuffer 喂给苹果生态」这半步有现成产线代码可抄。
- 反方向导入在位: [VkIosImage.mm:90](../../../src/avox_vulkan/layer/VkIosImage.mm#L90)
  `VkImportMetalIOSurfaceInfoEXT` (VK_EXT_metal_objects, 标准扩展)。
- Metal 直渲路径同样产 IOSurface: [MetalRender.mm:356](../../../src/avox_apple/MetalRender.mm#L356)。

### 1.2 缺口

- 公共 API 无 Apple 形态: `enableVkOutput` 的 handleType 只写
  `_WIN32`/`__ANDROID__` 分支 ([Avox.cpp:716](../../../src/avox/Avox.cpp#L716));
  `VkSharedHandle` 只有 `memHandle`(NT)/`ahb`(AHB) 两个字段
  ([AvoxLayer.h:474](../../../src/avox/AvoxLayer.h#L474))。
- 无独立设备读回验证 (win 有 vksharedtest/dx11sharedtest 三件套, Apple 零)。
- 消费端: panvox 的 Flutter 桥无 iOS/macOS 路径; unity 插件 GpuPassthrough
  仅 `_WIN32` 分支 (README 自记「iOS 需 Metal 路径」); UE Metal RHI 未做。

## 2. 方案

### 2.1 导出 API (对齐现有三 API 形态, M1)

`VkSharedHandle` 追加 Apple 字段:

```c
struct VkSharedHandle {
  uint64_t memHandle = 0;  // win: NT 句柄, 所有权转移
  void* ahb = nullptr;     // android: AHardwareBuffer*, 所有权转移
  void* ioSurface = nullptr;       // Apple: IOSurfaceRef, **非所有权**
  uint64_t ioSurfaceId = 0;        // IOSurfaceGetID(), 消费端换面探测
};
```

- `enableVkOutput(sr,w,h)`: Apple 分支校验 vkIosImage 就绪即返回 true
  (数据通路本就常开, enable 是消费语义标记; w/h 仅告警不干预 —— 出图分辨率
  跟随管线 outFormat 含字幕合成, 消费端自行缩放, 与 android 同逻辑)。
- `getVkOutputHandle`: Apple 返回当前 `IOSurfaceRef` + `IOSurfaceGetID()`。
- `disableVkOutput`: Apple 清消费标记, 不析构 IOSurface (通路常开)。
- **所有权契约与 memHandle 不同, 必须写进头注释**: 非所有权, avox 持有;
  消费端不得 CFRelease, 需要跨帧持有必须 CFRetain/CFAutorelease;
  `ioSurfaceId` 变化 = 旧面作废, 须重取 (图重建/尺寸变化即触发, 对齐 win
  「全周期幂等重声明」契约)。
- 像素序: 语义色序全链路 RGBA 一致 (win/android 导出字节序也全 RGBA)。
  全库唯一例外: Apple Vulkan 导出路径的 IOSurface fourcc 是 `'BGRA'`
  (VkIosImage.mm:29 为喂 VT 编码所选, VT 收 BGRA 不收 RGBA; MetalRender
  自身的 IOSurface 就是 `'RGBA'`)。blit 的 r→r 语义转换保证颜色不错, 仅内存
  排布不同。消费契约 = **按 `IOSurfaceGetPixelFormat` 查询, 勿硬编码**:
  CVPixelBuffer/Metal 端透明处理; VkDevice 导入端按 bindVK:75 现成映射
  `'BGRA'`→`VK_FORMAT_B8G8R8A8_UNORM`。不改成 RGBA —— 会断 IOSVEncoder
  的 VT 编码链。

### 2.2 消费桥 (M2/M3)

| 宿主 | 形态 | 说明 |
| --- | --- | --- |
| panvox (Flutter) iOS | `FlutterTexture.copyPixelBuffer` → `CVPixelBufferCreateWithIOSurface` | Flutter 内部经 CVMetalTextureCache 转 MTLTexture, GPU 零拷贝; AVPlayer 类插件标准模式, iOS 最成熟通路。桥在 panvox 仓 (跨仓), avox 侧交 API+契约+参考片段 |
| panvox (Flutter) macOS | 同上 | macOS 的 copyPixelBuffer 支持需 Flutter 3.x 较新版本, 需确认 panvox 的 Flutter 版本 |
| Unity iOS/macOS | IOSurface → MTLTexture → `CreateExternalTexture` 每帧换绑 | M4 后续项, 本期不做 |
| UE Metal RHI | IOSurface → FRHITexture | 仅列调研项, 工作量最大, 另立计划 |

### 2.3 同步语义 (比 win 简单, 但有一个真实风险)

- iOS/macOS 单 GPU, 无需 win 的 shared fence / 防重试规矩。
- **Metal 对 IOSurface 访问不做跨 API 隐式同步** (与老观念相反, 需实测确认):
  首版按 win D3D11 同形态走 —— 单 IOSurface + 消费端按 vsync 拉取后立即拷走,
  60fps 拉取节奏下预期可接受。
- 若真机出现撕裂/半帧: 回退方案 = VkIosImage 改 2 面 IOSurface 轮换,
  `ioSurfaceId` 变化即翻面信号 (消费端零改动)。MTLSharedEvent 跨 API 通道
  (MoltenVK timeline semaphore ↔ MTLSharedEvent) 列为后续研究项, 不进首版。

## 3. 里程碑

### M0 环境前置 (Mac 侧, 半天)

- Mac (Apple Silicon) + Xcode + Vulkan SDK ≥1.4.313 (含 MoltenVK.xcframework,
  `VULKAN_SDK` 环境变量); iPhone/iPad 真机 (模拟器无 Metal, **全部 iOS 验证必须真机**)。
- `python build_mac.py` / `python build_ios.py` 全绿, 确认
  VkOutputLayer ios 分支已编译进 (现状已编, 复核即可)。
- 注: 本计划实施需在 Mac 上进行, 当前 win 机仅能改代码不能验证。

### M1 SDK 导出 API + macOS 无头探针 (核心, 2-3 天)

1. 按 §2.1 改 `AvoxLayer.h` + `Avox.cpp` Apple 分支 (编译 win/android 不受影响)。
2. `avoxtest` 加 `iosharedtest` 用例 (macOS 无头 CLI 先行, 同一份 avoxtest.mm
   双平台出包的机制照用):
   - avox 侧: 放彩条+时间码测试资产, `setVulkan(true)` + enableVkOutput;
   - 探针: 独立创建第二个 VkInstance/VkDevice (MoltenVK), 按
     `VkIosImage::bindVK` 同款配方 (import pNext + 分配绑定内存) 导入同一
     IOSurface, usage 加 TRANSFER_SRC, 拷 staging 读回;
   - 判定口径对齐 win: 数据 100% 到达对端 + 连续帧持续变化 + PPM 转储地面
     真值 (彩条+时间码+字幕, 无黑帧/绿带);
   - churn 用例: 中途触发图重建 (加载字幕), 断言 ioSurfaceId 变化可被消费端
     捕获并重取成功 (对齐 win 幂等重声明契约)。
3. 文档: [多平台GPU共享](../../player/decode/多平台GPU共享.md) 的「IOSurface(待做)」
   更新为 Apple 分支说明 + 消费端契约。

### M2 iOS 真机 + panvox Flutter 桥 (跨仓, 2-4 天)

1. `avoxtest` iOS app 跑通 iosharedtest 真机 (UIKit MetalView 可旁路上屏目视)。
2. avox 侧交付: API 契约文档 + C 参考片段
   (`CVPixelBufferCreateWithIOSurface` 包裹 → `copyPixelBuffer` 返回,
   写法照抄 IOSVEncoder.mm:140 现成产线代码)。
3. panvox 仓侧 (跨仓工作项, 单独排期): FlutterTexture 注册 iOS 桥 → 真机
   验收 (彩条+字幕视频, 无黑帧/撕裂, 帧率达标, 后台切换恢复)。
4. macOS Flutter 桥同法复制 (先确认 panvox Flutter 版本支持 macOS external
   texture)。

### M4 引擎侧 (后续, 不进本期)

- Unity Metal 桥: platform/unity GpuPassthrough 加 `__APPLE__` 分支,
  IOSurface→MTLTexture→CreateExternalTexture。
- UE Metal RHI (avox-ue 仓): 单独调研计划。
- 输入方向 (`enableVkInput`/`setVkInputHandle` Apple 分支): VkInputLayer
  已有 VkIosImage 挂点, 随需求启动。

## 4. 风险与坑 (预判)

- **跨 API 同步**: §2.3, Metal 不保证 IOSurface 隐式同步, 真机撕裂即翻面环。
- MoltenVK 对不同 usage 组合 (加 TRANSFER_SRC) 的 IOSurface image 可能挑剔;
  探针失败则退 SAMPLED + render-to-texture 读回, 不动 SDK。
- IOSurface 重建时机与 win 不同源 (win 是 interop 层重建, Apple 是 outFormat
  变化即重建), 消费端契约必须以 ioSurfaceId 为准而不是指针比较。
- 字节序契约进 `getVkOutputHandle` 头注释: Apple IOSurface fourcc `'BGRA'`
  (其余全 RGBA), 消费端一律按 `IOSurfaceGetPixelFormat` 查询, 禁止硬编码。
- HDR/10-bit 直通不在本期 (IOSurface 支持 10-bit 像素格式, 但管线是 8-bit
  BGRA/RGBA), 与 [HDR管线改造计划](HDR管线改造计划.md) 联动另排。
- win 门禁不受影响: ctest + 离线回归子集均在 win 跑, Apple 改动带平台守卫。

## 5. 验收定义

| 项 | 口径 |
| --- | --- |
| M1 | macOS 无头: 独立 VkDevice 读回 100% 到达 + 帧持续变化 + PPM 地面真值 + churn 重取成功 |
| M2 | iOS 真机: iosharedtest PASS; panvox Flutter 真机播放彩条+字幕视频无黑帧/撕裂, 字幕随帧出 |
| M3 | macOS: panvox Flutter 桌面端同口径 PASS |
