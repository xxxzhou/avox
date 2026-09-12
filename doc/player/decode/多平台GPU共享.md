# 多平台GPU共享 (GPU 直通)

管线分工: 硬解可能是 DX11/MediaCodec/VT, 但图像处理与字幕渲染统一在 Vulkan
合成层 → 前台交互的本质是 **Vulkan(合成结果) ↔ 前台宿主** 的 GPU 直通。
unity/godot 插件是两个参考消费端 (`platform/unity/plugin/src/GpuPassthrough.cpp`,
`platform/godot/plugin/src/surface.cpp`)。

## 方案

windows/android/ios 分别使用 NT 句柄 / AHardwareBuffer / IOSurface(待做) 跨
设备共享合成结果。

- windows: 两种形态
  - Vk↔Vk: avox VkImage 导出 `OPAQUE_WIN32` NT 句柄, 宿主 VkDevice 经
    `VkImportMemoryWin32HandleInfoKHR` 导入 (godot 同款)
  - D3D11: avox 自建 D3D11 设备创建 `MISC_SHARED_NTHANDLE` 纹理 + 共享 fence,
    VK 管线每帧拷入并 Signal; 宿主 D3D11 设备 `OpenSharedResource1` 打开复制,
    `OpenSharedFence` 轮询去重 (Unity D3D11 后端同款)
- android: AHardwareBuffer。导出 `VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID`
  (enableVkOutput 的 android 分支), 导入端规范写法 = AHB 属性一次查询 +
  dedicated alloc + 内存类型严格交集 (Adreno 对违规写法直接 SIGSEGV)

## 导出/导入 API (avox/AvoxLayer.h)

| 方向 | API | 说明 |
| --- | --- | --- |
| 输出 Vk↔Vk | `enableVkOutput(w,h)` → `getVkOutputHandle` → `disableVkOutput` | R8G8B8A8_UNORM, TRANSFER_SRC/DST+SAMPLED; android 返回 AHB |
| 输出 D3D11 (win) | `enableVkOutputDx11` → `getVkOutputDx11Handle` / `getVkOutputDx11FenceHandle` → `disableVkOutputDx11` | 句柄就绪前返回 0 需轮询; 图重建(字幕加载等)后需幂等重 enable |
| 输入 | `enableVkInput(w,h)` → `setVkInputHandle` → `disableVkInput` | 外部写, avox 读 |
| 前台窗口直渲 | `setSurface(hwnd)` + `setVulkan(true)` | VkWindow 交换链直接上屏, 无任何中间拷贝 |

## 已知规矩 (踩坑换来的)

- outputLayer/共享图随渲染图异步构建, enable/handle 必须幂等轮询重试
- `OpenSharedResource1` 单次尝试失败即退, **严禁重试** (AMD 驱动重试崩溃案例)
- 严禁用 VK 导出的 OPAQUE_WIN32 内存直开 D3D12 (同上崩溃案例, D3D12 走
  D3D11 出生共享句柄 OpenSharedHandle)
- 硬解纹理数组会 11/19 来回覆写, 解码侧已拷到自有纹理数组再派发, 消费端
  持句柄+queueIndex 即可

## 验证 (2026-09-12, win11 + AMD RX 9070 XT)

| 消费形态 | 测试 | 结果 |
| --- | --- | --- |
| Vk↔Vk (godot 同款) | `vksharedtest` | PASS, 0xAB 数据 100% 到达对端 |
| D3D11 共享纹理 (Unity 同款) | `dx11sharedtest <mp4> 8` | PASS, reads=40 changed=24 fence=ok |
| 前台窗口 + D3D11 导出双路 | `dx11windowtest <mp4> 9` | PASS (h264 640x360 + h265 960x540 多轮), 交换链上屏 + 转储帧含 Vulkan 合成字幕 |

- 测试源: `assets/video/test/test_h264_aac_640x360.mp4` / `test_h265_aac_960x540.mp4`
  (→ `ff_h264_dx11` / `ff_hevc_dx11` 硬解, PB `renderType:d3d11`)
- 判定口径: 独立 D3D11 设备读回校验和持续变化 + fence 单调前进 + PPM 转储
  地面真值 (`window_dump.ppm`: 彩条+时间码+字幕, 无绿带/黑帧)
- 消费端契约 (dx11windowtest 实测确认): 图重建(字幕/字体层加载)会丢失 dx11
  声明并 churn 句柄 → 消费端须**全周期幂等重声明** `enableVkOutputDx11` +
  句柄变化重开; 首开撞陈旧句柄 E_INVALIDARG 属重建竞态, 等新句柄有界重试
  (同 Unity/Godot 桥; 同句柄绝不立即重试 —— AMD 驱动安全规矩)
- android: AHB 链路代码在位 (AndVDecoder GpuFrame → VkAndImage 导入 →
  enableVkOutput androidHwBuffer 导出), arm64-v8a 交叉编译通过, 真机验证待做
