#pragma once

#include <memory>
#include <vector>

#include "../Avox.hpp"
#include "ImageBuffer.hpp"

namespace avox {

using VideoBufferPtr = std::shared_ptr<class VideoBuffer>;

// 自定义比较函数，按 PTS 升序排序
struct GpuFrameSortPTS {
  bool operator()(const GpuFrame &a, const GpuFrame &b) {
    return a.pts > b.pts; // 小顶堆
  }
};

void releaseGpuFrame(GpuFrame &frame);
class AVOX_EXPORT VideoBuffer {
public:
  VideoBuffer() = default;
  virtual ~VideoBuffer() = default;

protected:
  // Buffer类型
  VBufferType bufferType = VBufferType::cpu;

public:
  VBufferType getBufferType() { return bufferType; }

public:
  // 有些值是野指针了，在这重置
  virtual void reset() {};
  virtual void release() {};
};

// 在ImageBuffer的基础上，增加与YUVFrame交互的功能
// 非YUV连续块,自动由ImageBuffer变成连续的,保留Y的stride
// 在类似YUV420P/422P如果不是Y stride的一半,会自动变成一半
// 方便提交到GPU
class AVOX_EXPORT SwVideoBuffer : public VideoBuffer, public ImageBuffer {
public:
  SwVideoBuffer();
  virtual ~SwVideoBuffer() = default;

protected:
  YuvType yuvType = YuvType::other;

public:
  // bCopy为true,会复制数据到buff中
  void form(const YUVFrame &yuvFrame, bool bCopy = false);
  // 转换为split布局(逻辑行等距,给ffmpeg/逐行读)的YUVFrame
  // 420P/422P带padding需重排时数据拷到tmp(buffer不被修改),布局已等价时零拷贝指向自身
  bool to(YUVFrame &yuvFrame, IImageBuffer *tmp = nullptr);

public:
  YuvType getYuvType() { return yuvType; }

public:
  // 检查原始数据是否是YUVFrame
  virtual bool bNoYUV() { return yuvType == YuvType::other; }
};

// 管理GpuFrame资源
class AVOX_EXPORT HwVideoBuffer : public VideoBuffer {
public:
  HwVideoBuffer();
  virtual ~HwVideoBuffer();

protected:
  GpuFrame gpuFrame = {};

public:
  void setGPUFrame(const GpuFrame &frame);
  GpuFrame &getGPUFrame() { return gpuFrame; }

public:
  virtual void reset() override;
  virtual void release() override;
};

bool needReset(const GpuFrame& old,const GpuFrame& now);
bool needReset(const YUVFrame& old,const YUVFrame& now);

// SwVideoBuffer* createSwVideoBuffer();
// HwVideoBuffer* createHwVideoBuffer();

}
