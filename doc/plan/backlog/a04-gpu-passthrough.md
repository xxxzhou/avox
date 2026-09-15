# A-4 GPU 直通三平台

优先级 P0 · 里程碑 M4(建议提前,panvox P-1 零拷贝路径切换等它) · 计划状态:就绪
Windows 已通;本计划覆盖 Android AHB→Flutter Texture 与 iOS/macOS CVPixelBuffer 桥。

## 出口判据

1. Android:播放帧经 AHB 进 Flutter Texture,panvox 播放页零拷贝出图。
2. iOS/macOS:播放帧以 CVPixelBuffer/IOSurface 进 Flutter Texture,零拷贝出图。
3. 三平台 CPU 占用对比 CPU 帧泵有可测改善;帧可用通知与尺寸变化时序无黑屏。

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

## 验收

- 三平台样例出图,avox-test 归档时序/占用手册;panvox 接入后 CPU 帧率对比报告。

## 风险与开放问题

- Android 多平面 NV12 AHB 在 Flutter 侧 EGL 导入的兼容性(设备碎片化),需真机矩阵。
- Flutter GL/VK context 与 avox Vulkan context 的外部内存口径(requirement flags/handleType)。
- CVPixelBuffer 生命周期(谁 release、pool 深度),ioSurface「换面语义」依赖调用方遵守注释,
  Flutter 侧需把该约定固化成 API 而不是注释。
