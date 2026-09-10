#include "RtcVideoBuffer.hpp"

#include "api/video/i420_buffer.h"
#ifdef WIN32
#include "libyuv.h"
#endif

namespace avox {

using namespace webrtc;

RtcVideoBuffer::RtcVideoBuffer() {}

RtcVideoBuffer::~RtcVideoBuffer() { release(); }

void RtcVideoBuffer::form(const YUVFrame& yuvFrame) {
  release();
  format = yuvFrame.format;
  auto temp = std::make_shared<SwVideoBuffer>();
  temp->form(yuvFrame, true);
  buffer = temp;
}

void RtcVideoBuffer::form(IImageBuffer* imgBuffer, YuvType type) {
  release();
  if (!imgBuffer) {
    return;
  }
  image2YUVFormat(imgBuffer->getImageFormat(), type, format);
  // packed块整块持有(源buffer下帧被覆盖), split只在消费边缘物化;
  // 类型记在buffer自身, toFrame直接用buffer体系
  auto temp = std::make_shared<SwVideoBuffer>();
  temp->setImageFormat(imgBuffer->getImageFormat());
  temp->copyFrom(imgBuffer, true);
  temp->setYuvType(type);
  buffer = temp;
}

void RtcVideoBuffer::form(const GpuFrame& gpuFrame) {
  release();
  format = gpuFrame.format;
  auto temp = std::make_shared<HwVideoBuffer>();
  temp->setGPUFrame(gpuFrame);
  buffer = temp;
}

bool RtcVideoBuffer::toFrame(YUVFrame& frame) {
  if (!buffer) {
    return false;
  }
  if (buffer->getBufferType() != VBufferType::cpu) {
    return false;
  }
  if (!splitTmp) {
    splitTmp = std::make_shared<ImageBuffer>();
  }
  SwVideoBuffer* swBuffer = (SwVideoBuffer*)buffer.get();
  return swBuffer->to(frame, splitTmp.get());
}

bool RtcVideoBuffer::toFrame(GpuFrame& frame) {
  if (!buffer) {
    return false;
  }
  if (buffer->getBufferType() == VBufferType::cpu) {
    return false;
  }
  HwVideoBuffer* hwBuffer = (HwVideoBuffer*)buffer.get();
  frame = hwBuffer->getGPUFrame();
  return true;
}

void RtcVideoBuffer::use() {
  // android的surface里的GPU资源不能重复释放
  // 在上面toFrame后,丢失引用关系,因此在这主动释放
  if (buffer->getBufferType() == VBufferType::opengles) {
    HwVideoBuffer* hwBuffer = (HwVideoBuffer*)buffer.get();
    GpuFrame& frame = hwBuffer->getGPUFrame();
    frame.queueIndex = -1;
    frame.context = nullptr;
  }
}

void RtcVideoBuffer::release() {
  if (buffer) {
    buffer->release();
    buffer.reset();
  }
}

VideoFrameBuffer::Type RtcVideoBuffer::type() const {
  if (!buffer) {
    return VideoFrameBuffer::Type::kNative;
  }
  if (buffer->getBufferType() != VBufferType::cpu) {
    return VideoFrameBuffer::Type::kNative;
  } else {
    switch (format.type) {
      case YuvType::yuv420P:
        return VideoFrameBuffer::Type::kI420;
      case YuvType::nv12:
        return VideoFrameBuffer::Type::kNV12;
      case YuvType::yuv422P:
        return VideoFrameBuffer::Type::kI422;
      case YuvType::yuv444P:
        return VideoFrameBuffer::Type::kI444;
      default:
        return VideoFrameBuffer::Type::kNV12;
    }
  }
}

int32_t RtcVideoBuffer::height() const { return format.height; }

int32_t RtcVideoBuffer::width() const { return format.width; }

webrtc::scoped_refptr<webrtc::I420BufferInterface> RtcVideoBuffer::ToI420() {
  if (buffer->getBufferType() == VBufferType::cpu) {
    YUVFrame yuvFrame = {};
    if (!toFrame(yuvFrame)) {
      return nullptr;
    }
    webrtc::scoped_refptr<webrtc::I420Buffer> i420buffer =
        webrtc::I420Buffer::Create(yuvFrame.format.width,
                                   yuvFrame.format.height);
    if (yuvFrame.format.type == YuvType::yuv420P) {
      // 软编场景：直接拷贝
      i420buffer->Copy(yuvFrame.format.width, yuvFrame.format.height,
                       yuvFrame.data[0], yuvFrame.stride[0], yuvFrame.data[1],
                       yuvFrame.stride[1], yuvFrame.data[2],
                       yuvFrame.stride[2]);
      return i420buffer;
    } else if (yuvFrame.format.type == YuvType::nv12) {
#ifdef WIN32
      // Windows Map 后的 NV12 场景：转换后返回
      libyuv::NV12ToI420(yuvFrame.data[0], yuvFrame.stride[0], yuvFrame.data[1],
                         yuvFrame.stride[1], i420buffer->MutableDataY(),
                         i420buffer->StrideY(), i420buffer->MutableDataU(),
                         i420buffer->StrideU(), i420buffer->MutableDataV(),
                         i420buffer->StrideV(), yuvFrame.format.width,
                         yuvFrame.format.height);
      return i420buffer;
#endif
    }
  }
  return nullptr;
}

}