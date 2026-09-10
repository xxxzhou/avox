#pragma once

#include "../RtcHelper.hpp"
#include "api/video/video_frame_buffer.h"
#include "avox/video/VideoBuffer.hpp"

namespace avox {

// avox里的VideoBuffer与webrtc的VideoFrameBuffer的转换类
// 契约: 内部恒持有packed块(IImageBuffer), split布局只在消费边缘(toFrame/ToI420)物化
class RtcVideoBuffer : public webrtc::VideoFrameBuffer {
public:
  RtcVideoBuffer();
  virtual ~RtcVideoBuffer();

protected:
  VideoBufferPtr buffer = nullptr;
  YUVFormat format = {};
  // toFrame重排副本(仅420P/422P带padding时实际拷贝); 类型由buffer自身携带(getYuvType)
  std::shared_ptr<ImageBuffer> splitTmp;

public:
  // 解码输出帧(天生split),拷成packed持有
  void form(const YUVFrame &yuvFrame);
  // 渲染透传的packed帧(整块拷贝获得所有权), RTC发送路径用
  void form(IImageBuffer *imgBuffer, YuvType type);
  void form(const GpuFrame &gpuFrame);
  // android的buffer被用后,要说明一下,在后面不要再释放
  void use();

public:
  bool toFrame(YUVFrame &frame);
  bool toFrame(GpuFrame &frame);

public:
  void release();

public:
  virtual VideoFrameBuffer::Type type() const override;
  virtual int32_t width() const override;
  virtual int32_t height() const override;
  virtual webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;
};

}