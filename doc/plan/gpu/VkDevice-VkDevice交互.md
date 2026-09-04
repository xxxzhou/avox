# VkDevice-VkDevice 交互

> 不同 VkDevice 实例间零拷贝共享 VkImage，GPU 结果直接作为另一 Device 的输入/输出，无需 CPU 中转。

## 1. 现状与缺失

已有交互仅限 Vulkan ↔ 平台原生：DX11/DX12（`VkWinImage`, external_memory_win32）、GLES/AHB（`VkAndImage`）、Metal/IOSurface（`VkIosImage`）。

**缺失**：VkDevice A ↔ VkDevice B。需求：多播放器串联（AI 推理→画质增强→渲染）、插件 GPU 直通（avox_onnx→avox_vulkan）、多 GPU 协作。

## 2. 核心机制

- **同 GPU**：两 VkDevice 指向同一 VkPhysicalDevice，OPAQUE handle（Win32/FD/AHB）零拷贝共享
- **跨 GPU**：需 LUID 匹配；异 GPU 一般不支持 OPAQUE，回退 D3D11 cross-adapter / CPU
- **同步**：单 GPU 靠 A 端 fence + queue ordering 足够；跨 GPU 需 timeline semaphore
- **回退**：L0 external(~0) → L1 HOST_ALLOCATION(~1-5ms) → L2 CPU 中转(~5-20ms)

## 3. 设计

### VkSharedImage（share/VkSharedImage.hpp）

导出端 `createExportable(desc)` / `exportHandle()`；导入端 `importFromHandle(h,desc)`；通用 `getImage()` / `getDesc()` / `isValid()` / `release()`。同步仅靠 A 端 fence + queue ordering（单 GPU）。

`VkShareHandle`：`{ VkShareHandleType, union {HANDLE, fd, AHardwareBuffer*, void*} }`，RAII 释放。流程参考 `VkWinImage::bindD3D()`，handle type 用 OPAQUE，增 semaphore。

### VkShareChecker（share/VkShareChecker.hpp）

静态：`checkCompatible/checkExportable/checkImportable`、`selectBestHandle`（优先零拷贝回退 host）、`bSameGpu`（LUID 匹配）。

### VkSharedRender（share/VkSharedRender.hpp）

实现 `IVkRenderContext`：`bSharedImage()=true`，携带 `VkSharedImage*`，`getTexture()/getImageFormat()` 取自共享图。

### 管线集成

`inputGpuData/outputGpuData` 增 `RenderType::Vulkan` 路径 → `VkSharedImage`（VkInputLayer/VkOutputLayer 用 `setVkInterop` 切换拷贝路径）。

### 高层 API：暂不实现

接口待定（可能 `inputVK`/`outputVK`），等底层验证后再封装。

## 4. 文件与扩展

```
src/avox_vulkan/share/      VkSharedImage / VkShareChecker / VkSharedRender (hpp/cpp)
src/avox_vulkan/layer/      VkInputLayer / VkOutputLayer 增 Vulkan→Vulkan 路径
src/avox_vulkan/VkCommon.cpp   Windows 增 EXTERNAL_MEMORY(+WIN32) / CAPABILITIES（仅内存，无 semaphore）
```

## 5. 实施计划

- **Phase 1 基础设施**：VkShareChecker → VkSharedImage（Windows OPAQUE_WIN32）→ semaphore 扩展 → 验证 ✅已完成
- **Phase 2 管线集成**：VkInputLayer/VkOutputLayer Vulkan 路径 → VkSharedRender → 同步协议
- **Phase 3 多平台+回退**：Android AHB → iOS MTLSharedTexture/IOSurface → L1/L2 回退
- **Phase 4 待定**：高层 API → 跨进程 → ping-pong 双缓冲 → Timeline semaphore

## 6. 风险

跨 GPU 不支持 OPAQUE（回退 cross-adapter/CPU）、Dedicated 分配必需、Format 不兼容（VkShareChecker 预检）、同步遗漏（严格 semaphore 协议）、NT handle 泄漏（RAII）、VkDevice 销毁顺序（引用计数或显式 disconnect）。

## 7. 实施记录（Phase 1 已完成）

**验证**：`vksharedtest` 通过 —— A 填 0xAB 图案 → NT handle → B 回读，match0xAB=100% nonZero=100%（确定性像素验证，非目测）。

**已实现**：VkSharedImage 内存导出/导入、`setVkInterop` 拷贝路径、VkSharedRender 类（待 Phase 2 接入分发）。

**根因修复（全零）**：VkPipeGraph 的 cmd 在 `onInitBuffers()` **只录一次**，每帧复用。`setVkInterop` 只改标志不重录 → interop 路径从未进 cmd → 全零。修复 = 标志变化时 `resetGraph()` 重录。教训：运行时改 onCommand 分支必须 resetGraph 重录。

**同步**：**semaphore 已整体删除**——`VkSharedHandle` 只留 `memHandle`，`VkSharedImage`/`VkCommon` 无 semaphore 代码。单 GPU（同机）靠 A 端 `vkWaitForFences` + queue ordering 已保证内存可见性。跨 GPU 需 **timeline semaphore**，届时从零实现（勿用二值信号量：signal/wait 需严格 1:1，解耦帧率会挂死）。

**变更历史**：2026-08-07 `VkSharedRenderContext` → `VkSharedRender` 更名；semaphore 导出/导入整体移除（单 GPU fence 够用）。
