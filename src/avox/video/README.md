# avox/video 模块

视频渲染、解码、编码、帧数据管理的核心模块。

## 类层次

```
ISurfaceRender (接口)
  └─ SurfaceRenderVk (Vulkan 计算管线 + 图像处理 API)
       └─ SurfaceRenderNative (平台渲染对接: DX11/EGL/Metal + Vulkan 双路)
            └─ WindowRender (窗口管理 + 渲染循环 RunTask)
```

## 类说明

| 类 | 说明 |
|---|---|
| **SurfaceRenderVk** | Vulkan 计算管线基类，持有 VkVideoRender，实现图像处理 API（缩放、Anime4K、水印、LUT、亮度/对比度/饱和度/锐度），纯 Vulkan 渲染路径，离屏 CPU 输出 |
| **SurfaceRenderNative** | 平台渲染对接，持有 pVideoRender（DX11/EGL/Metal），负责原生 GPU NV12→RGBA 转换和 Vulkan 对接，setVulkan 控制双路数据流走向 |
| **WindowRender** | 窗口渲染，继承 SurfaceRenderNative，增加 Window 窗口管理和 RunTask 渲染循环（帧率控制、帧源拉取、窗口刷新），覆盖需要 window 配合的 render/setAutoAspect |
| **VideoRender** | 视频渲染基类，管理平台窗口渲染，将硬解原生 GPU 数据转为 RGBA8，处理 DX11/OpenGL/Metal 与 Vulkan 的交互 |
| **VideoDecoder** | 视频解码器，输入包数据并回调输出帧数据，支持软解和硬解 |
| **VideoEncoder** | 视频编码器，将 YUVFrame 或 GpuFrame 编码输出 |
| **VDecoderTask** | 视频解码任务，开启线程从 IO 队列读取并解码，管理软硬解码切换和配置帧保持 |
| **VideoBuffer** | 视频缓冲区基类，管理 Buffer 类型；SwVideoBuffer 是软件缓冲区，在 ImageBuffer 基础上增加 YUVFrame 交互 |
| **VideoFrame** | 视频帧，队列中存储的基本单元，包含 pts/dts 和帧缓冲区 |
| **VideoYuv** | GPU RGBA→NV12 转换，将 Vulkan 资源映射到 DX11/OpenGL/Metal 纹理再转 NV12 供硬编使用 |
| **ImageBuffer** | 图像数据缓冲区，支持内部数据或外部引用，可将 GPU 输出的 R8 对齐大图重排为标准 YUV 平面布局 |
| **Window** | 窗口观察者接口和平台原生窗口类型定义（HWND/ANativeWindow/CAMetalLayer） |

## 工具文件

| 文件 | 说明 |
|---|---|
| **Video.cpp** | 像素大小、图像格式计算等工具函数 |
| **ImageIO.cpp** | 基于 stb_image 的图像读写工具 |