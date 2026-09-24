#pragma once

#include <memory>

#include "../AvoxPlayer.h"
#include "VideoBuffer.hpp"

namespace avox {

using VideoFramePtr = std::shared_ptr<class VideoFrame>;

// 视频帧,队列中存储的就是这个
class VideoFrame {
 public:
  VideoFrame();
  ~VideoFrame();

 public:
  // 显示时间
  int64_t pts = 0;
  // 解码时间
  int64_t dts = 0;
  // 不同方案下有不同实现
  VideoBufferPtr buffer = nullptr;

 public:  
  void release();
};

void copyBufHost(VideoFramePtr& frame, const YUVFrame& curframe);
void copyBufGpu(VideoFramePtr& frame, const GpuFrame& gpuFrame);

// rgba 帧(离线画质增强: Vulkan 管线 yuv→rgba 后入队)的引用与深拷贝
struct RgbaFrameRef {
  IImageBuffer* buf = nullptr;
  int64_t pts = 0;
  int64_t dts = 0;
};
void copyBufRgba(VideoFramePtr& frame, const RgbaFrameRef& ref);
}