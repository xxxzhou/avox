#pragma once

#include "VideoFrame.hpp"

namespace avox {

// 视频编码前处理，各平台GPU相应RGBA纹理转NV12
// 1. Vulkan资源，先映射到DX11/OpenGL/Metal纹理
// 2. DX11/OpenGL/Metal转相应的NV12处理
// 3. GPU相应的NV12纹理硬编
// 和VideoRender不同,VideoYuv可以限定和传入的frame上下文一致
class VideoYuv {
 public:
  VideoYuv() = default;
  virtual ~VideoYuv() = default;

 protected:
  RenderType renderType = RenderType::other;

 public:
  // 输入RGBA纹理
  void renderFrame(const GpuFrame& frame);
  // 输出NV12纹理
  IRenderContext* getOutContext() { return nullptr; };

 protected:
  // 初始化DX11/OpenGL/Metal的RGBA纹理转对应的NV12纹理需要的资源
  virtual bool vaildAndInitGraph(const GpuFrame& frame) = 0;
  virtual void renderGpuFrame(const GpuFrame& frame) = 0;
  virtual void releaseGraph() {};
};

}