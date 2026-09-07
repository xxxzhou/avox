# avox 视频 GPU 直通 Godot 方案(现状实现)

> 状态:**已实现并验证**(copy-based GPU 直通,texture_copy 实现,画面可播放,2026-08-07)
> 适用范围:avox_godot_plugin(Godot 4.7 / GDExtension,Windows 桌面 + Vulkan 后端,Intel igvk64 实测)
> 定位:本文描述**当前落地实现**,非早期可行性评审。真零拷贝(共享 VkDevice)路线见 §8。

---

## 1. 结论

**avox 用自己的 VkDevice 解码渲染 → 外部内存 VkImage(NT 共享句柄)→ Godot 导入 → 每视频帧一次 `RD::texture_copy` → 场景采样。全程零 CPU 回读、零软转。**

- 画面已确认播放;copy 频率 = **视频帧率**(经 avox `onRender` 回调门控),不再随 Godot 帧率(60-120fps)空转。
- 单 GPU 下**无需信号量**:avox 靠 A 端 `vkWaitForFences` + queue ordering 保证内存可见性(见 §6)。
- 实测 GPU(Intel UHD 770):GPU 直通 ~17% 3D 引擎 + 解码 ~5%;CPU 回退 ~13% 3D。见 §5。

---

## 2. 为什么是 copy-based(踩坑结论)

早期方案:avox 复用 Godot 的 VkDevice(`VkContext::initContext(instance, physDev, device)` 三参重载),Godot 直接收养 avox 输出 VkImage → 真零拷贝。

**实测失败**:avox 与 Godot 各自建了 VkDevice(`VkContext::Shared()` 无参建自己的 instance+device)。对 avox 的外部内存 VkImage 在 **Godot 设备上创建 ImageView** 时,Intel igvk64 驱动空指针崩溃(rip=0x0)。

**对策**:放弃跨设备直接采样,Godot 设备上自建 `sampledImage` 承接:

```
avox 设备:   输出 VkImage(external memory, NT handle)
                 │ import (VK_KHR_external_memory_win32)
Godot 设备: importedImage(TRANSFER_SRC) ─每帧 texture_copy──► sampledImage(SAMPLED|TRANSFER_DST)
                 (零 CPU, 一次 GPU copy)      │ texture_create_from_extension 收养
                                              ▼
                                        Texture2DRD → 场景采样
```

两个 image 都用 `texture_create_from_extension` 收养(只创建 ImageView,不跨设备,不崩)。copy 用 **`RD::texture_copy`** 记录进 Godot 自己的 draw graph、随帧提交,不做外部 `vkQueueSubmit`。

---

## 3. 关键实现(platform/godot/plugin/src)

### 3.1 surface.cpp — importSharedImage()
1. `avox::getVkOutputHandle` 拿 NT 内存句柄(`VkSharedHandle` 只含 `memHandle`,`semHandle` 已被 avox 删除,见 §6)。
2. `rd->get_driver_resource(LOGICAL_DEVICE, RID(), 0)` 拿 Godot VkDevice。
3. 建 `importedImage`:`VkExternalMemoryImageCreateInfo(OPAQUE_WIN32)` + `VkImportMemoryWin32HandleInfoKHR` 导入;内存类型 = image bits ∩ handle bits,`vkGetMemoryWin32HandlePropertiesKHR` 为 NULL 时 fallback type 0。
4. `texture_create_from_extension` 收养 `importedImage` → `importedRid`(usage `CAN_COPY_FROM`,texture_copy 源前置要求)。
5. 建 Godot 自己的 `sampledImage`(SAMPLED|TRANSFER_DST,DEVICE_LOCAL),收养 → `adoptedRid`(usage `SAMPLING|CAN_COPY_TO`)。
6. `Texture2DRD` 绑定 `adoptedRid`。

### 3.2 surface.cpp — performCopy()(每视频帧一次)
`rd->texture_copy(importedRid, adoptedRid, ...)` 整帧 RGBA copy。Godot 在 draw graph 里自行处理 barrier/layout/随帧提交,插件不再维护 command pool/buffer/fence。

### 3.3 按视频帧率门控(降 GPU 占用)
- avox 渲染线程每帧调 `ISurfaceRenderOb::onRender()`(`SurfaceRenderVk::onRenderOut` 无条件 dispatch)。
- `onRender()` 置 `frameReady`;Godot 主线程 `update()` 里 `frameReady.exchange(false)` 为真才 `performCopy()`。
- 效果:copy 频率 = 视频帧率(~24fps);暂停时 avox 无 onRender → 不 copy,GPU 空转消除。

### 3.4 godot_init.cpp — avox_gpu_passthrough_init()
- 主线程延迟初始化(SCENE 级 RenderingServer 未就绪,需 player 创建后触发):`volkInitialize` → `volkLoadInstance` → `volkLoadDevice`。
- Win32 扩展函数用 `vkGetDeviceProcAddr` 兜底(volk 需 `VK_USE_PLATFORM_WIN32_KHR` + `vulkan_win32.h`,见 `src/volk_win32.c`)。
- 设 `gGpuPassthroughAvailable = true`;开关:环境变量 `AVOX_ENABLE_GPU_PASSTHROUGH=1`。

---

## 4. 部署要求

avox.dll 相对**自身所在目录**查找 `assets/glsl/*.spv`(235 个,含 `resize.comp.spv`),缺失时报 `loadShaderModule assert failed`、画面可能空白。三部分必须放一起:

```
addons/avox_godot/bin/
  ├─ avox.dll + 各依赖 DLL
  ├─ avox_godot.dll        (插件)
  └─ assets/glsl/*.spv    ← 从 avox 构建产物 install/<cfg>/assets 复制
```

---

## 5. 已知限制

- 每视频帧一次 GPU copy(RGBA 全帧带宽),零 CPU 回读/软转。
- **Intel iGPU 上 copy 有 ~12% 3D 引擎固定开销**,与频率(6fps≈24fps)、提交路径(裸 submit = texture_copy)、是否显示均无关——Intel 无 dedicated transfer 队列,copy 跑在 graphics 队列,3D 引擎因此维持高功耗态。已排除软件优化(节流、换提交方式均无效),唯一消除办法是直接采样(Intel 驱动崩溃)。实测:GPU 直通 ~17% 3D;CPU 回退 ~13% 3D(含 NV12→RGBA 软转);空场景 30fps 渲染 ~4.5% + 解码 ~5.5% 是地板。
- 仅单 GPU(avox 与 Godot 同一物理 GPU)成立。
- 单缓冲,画面内容可能滞后 1 帧。
- 尺寸变化:avox 线程 `onWinSizeChange` → `needReimport` → 主线程重导。
- CPU 回退路径保留(非 Vulkan 后端 / 直通未开启时 NV12→RGBA 软转)。注意:直通开启后若运行时 import 失败,**暂无中途降级**到 CPU。
- GPU 直通开关 `AVOX_ENABLE_GPU_PASSTHROUGH=1`;未设时走 CPU 回退。

---

## 6. 信号量为什么不需要(avox 侧确认)

avox 的 `exportSemaphore/importSemaphore` 是"建好但未接 submit"的 Phase 1 基础设施;当前 `VkSharedHandle` 只含 `memHandle`,`semHandle` 字段已删。

- 单 GPU 下 avox 靠 **A 端 `vkWaitForFences` + queue ordering** 保证内存可见性,无需信号量。
- 插件 `vkImportSemaphoreWin32HandleKHR` 相关代码已全部移除(死代码),不 import 属预期。
- 将来跨 GPU 用 timeline semaphore 接,API 需在 avox 侧扩展。

---

## 7. 证据索引(实查)

| 事实 | 出处 |
|---|---|
| avox 自建 VkDevice | `avox/src/avox_vulkan/VkContext.cpp:81-88,162-187` |
| avox 每帧 dispatch onRender | `avox/src/avox/video/SurfaceRenderVk.cpp:330-338` |
| Godot 拿 VkDevice | `rd->get_driver_resource(LOGICAL_DEVICE, RID(), 0)` |
| Godot 收养外部 VkImage | `RenderingDevice::texture_create_from_extension` |
| texture_copy 走 draw graph 随帧提交 | `servers/rendering/rendering_device.cpp:2949`(add_texture_copy) |
| 跨设备 ImageView 崩溃根因 | Intel igvk64;`surface.cpp`(copy-based 方案) |
| avox 每帧写外部内存输出 | `avox/src/avox_vulkan/layer/VkInputLayer.cpp:244` |
| 12% 与频率无关(6fps≈24fps≈16.6-16.9%) | 实测 A/B(2026-08-07) |

---

## 8. 未来优化(真零拷贝)

avox 用 `VkContext::initContext(instance, physDev, device)` 三参重载**共享 Godot 的 VkDevice**,使 avox 输出 VkImage 直接长在 Godot 设备上 → 免每帧 copy,直接采样。风险:image layout/usage 需与 Godot 期望一致;需在 player 首次建图前注入 `gVkContext`。**注意**:此路线曾在 Intel igvk64 上以 ImageView 崩溃失败(§2),除非驱动修复,否则仅作为跨平台/离散 GPU 的候选。

## 9. Android 状态 (2026-09-07 真机 vermeer/Adreno 740)

全链两端代码已就绪并真机验证到引擎层限制处:

- **导出侧 (avox 核心, 已修)**: `enableVkOutput`→`VkSharedImage::createExportable` 创建 AHB 可导出 image。此前 Adreno 直接 SIGSEGV (tombstone: qglinternal::vkBindImageMemory), 根因是可导出内存未用专用分配; 已改 `VkMemoryDedicatedAllocateInfo` + `vkBindImageMemory2`, 真机通过。
- **导入侧 (插件 surface.cpp, 已就绪)**: AHB 属性/格式一次查询 + dedicated 导入 + 严格内存类型交集 + bind2; volk 扩展指针缺失时防护并走运行时降级 (连续 3 次失败自动 CPU, 不再无限重试)。
- **引擎层阻塞**: Godot 建 VkDevice 未启用 `VK_ANDROID_external_memory_android_hardware_buffer` → 驱动对未启用扩展: 设备级 GetDeviceProcAddr 返回 NULL; 实例级获取到的函数调用返回全零属性 (format=0/memTypeBits=0)。GDExtension 无设备创建注入点 (无 Unity InterceptVulkan 等价物), 零拷贝暂不可行。
- **当前行为**: gpu_passthrough 探测通过→GPU 尝试→3 帧内自动降级 CPU (Vulkan 管线保留 + YUV 回调), 真机播放正常无崩溃。待 Godot 侧启用该扩展 (官方支持或自编引擎) 后, 导入链路即插即用。
- **同步**: 沿用 CPU vkWaitForFences + gralloc 隐式同步, 无外部 fence。
