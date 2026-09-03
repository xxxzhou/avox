#pragma once

#include "../RtcHelper.hpp"
#include "api/video/video_frame_buffer.h"
#include "avox/video/VideoBuffer.hpp"

namespace avox {

// avox里的VideoBuffer与webrtc的VideoFrameBuffer的转换类
class RtcVideoBuffer : public webrtc::VideoFrameBuffer {
public:
  RtcVideoBuffer();
  virtual ~RtcVideoBuffer();

protected:
  VideoBufferPtr buffer = nullptr;
  YUVFormat format = {};

public:
  void form(const YUVFrame &yuvFrame);
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