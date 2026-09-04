# 多平台GPU共享设计

## 方案

windows/android/ios分别使用句柄/AHardwareBuffer/IOSurface在多线程/进程共享。

- 根据ImageFormat创建共享dx11纹理/AHardwareBuffer/IOSurface.windows平台需要dx11上下文，而AHardwareBufferf需要EGLImage.
- 