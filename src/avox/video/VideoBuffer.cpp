#include "VideoBuffer.hpp"

#include "avox/module/LogHelper.hpp"

#ifdef __ANDROID__
#include "avox_android/AndVDecoder.hpp"
#endif

#ifdef __APPLE__
#include <CoreVideo/CoreVideo.h>
#endif
namespace avox {

SwVideoBuffer::SwVideoBuffer() { bufferType = VBufferType::cpu; }

void SwVideoBuffer::form(const YUVFrame& frame, bool bCopy) {
  ImageFormat tempFormat = {};
  // 根据frame的stride,这里是多平面,会假定使用ystride
  yuv2ImageFormat(frame, tempFormat);
  // 重新分配连续的一块内存,如果frame不连续,要重排复制
  // 如果YUV420P/422P,其UV的stride不是Y的一半,也要重排复制
  setImageFormat(tempFormat);
  // 如果是平面格式
  bool bPlane = bVPlaneFormat(frame.format.type);
  // 记录最初始的YUV格式
  yuvType = frame.format.type;
  // 420P/422P的packed布局(每物理行[偶|奇|pad])与split只在无padding时字节重合,
  // 带padding的帧直接引用会让GPU按packed错读,必须拷贝
  bool bPaddedSplit =
      (frame.format.type == YuvType::yuv420P ||
       frame.format.type == YuvType::yuv422P) &&
      frame.stride[0] != tempFormat.width;
  // 要求复制,以及平面格式如果不紧湊也需要重新排列
  if (bCopy || (bPlane && !bTightlyPacked(frame)) || bPaddedSplit) {
    uint8_t* bdata = buffer.data();
    uint8_t* idata = frame.data[0];
    int32_t rowPitch = imageFormat.rowPitch;
    // 平面格式单独复制
    if (bPlane) {
      copyPlaneYUV2TightlyBuffer(frame, bdata);
      bDataReference = false;
    } else {
      // 交叉格式
      if (frame.stride[0] == 0 || frame.stride[0] == rowPitch) {
        memcpy(data, frame.data[0], size);
      } else {
        for (int i = 0; i < imageFormat.height; i++) {
          memcpy(bdata, idata, rowPitch);
          idata += frame.stride[0];
          bdata += rowPitch;
        }
      }
    }
    bDataReference = false;
  } else {
    data = frame.data[0];
    bDataReference = true;
  }
}

bool SwVideoBuffer::to(YUVFrame& yuvFrame, IImageBuffer* tmp) {
  if (yuvType == YuvType::other) {
    LOGFLF(LogLevel::info, "yuvType is other");
    return false;
  }
  // buffer恒为packed布局,420P/422P带padding转split时由tmp持副本
  return image2SplitYUVFrame(this, yuvType, yuvFrame, tmp);
}

HwVideoBuffer::HwVideoBuffer() {}

HwVideoBuffer::~HwVideoBuffer() { release(); }

void HwVideoBuffer::setGPUFrame(const GpuFrame& frame) {
  // 先清空原来老的
  release();
  gpuFrame = frame;
#ifdef __ANDROID__
  bufferType = VBufferType::opengles;
#endif
#if _WIN32
  bufferType = VBufferType::dx11;
#endif
#ifdef __APPLE__
  bufferType = VBufferType::metal;
#endif
  if (gpuFrame.context &&
      gpuFrame.context->getRenderType() == RenderType::Vulkan) {
    bufferType = VBufferType::vulkan;
  }
}

void HwVideoBuffer::reset() {
  gpuFrame.context = nullptr;
  gpuFrame.queueIndex = -1;
}

void HwVideoBuffer::release() { releaseGpuFrame(gpuFrame); }

void releaseGpuFrame(GpuFrame& gpuFrame) {
#ifdef __ANDROID__
  if (gpuFrame.queueIndex >= 0 && gpuFrame.context) {
    GLESContext* glesCtx = static_cast<GLESContext*>(gpuFrame.context);
    if (glesCtx) {
      glesCtx->onFrameRelease(false, gpuFrame);
    }
    gpuFrame.context = nullptr;
    gpuFrame.queueIndex = -1;
  }
#endif
#ifdef __APPLE__
  if (gpuFrame.buffer) {
    CVImageBufferRef imageBuffer =
        static_cast<CVImageBufferRef>(gpuFrame.buffer);
    // CFRelease(imageBuffer);
    CVBufferRelease(imageBuffer);
    gpuFrame.buffer = nullptr;
  }
#endif
}

bool needReset(const GpuFrame& one, const GpuFrame& two) {
  if (one.format.width != two.format.width ||
      one.format.height != two.format.height) {
    LOGFLF(LogLevel::info, "video size form:", one.format.width, "x",
           one.format.height, " to:", two.format.width, "x", two.format.height);
    return true;
  }
#ifdef WIN32
  if (one.context != two.context) {
    LOGFLF(LogLevel::info, "dx context from:", one.context,
           " to:", two.context);
    return true;
  }
#endif
#ifdef __ANDROID__
  GLESContext* oneCtx = static_cast<GLESContext*>(one.context);
  GLESContext* twoCtx = static_cast<GLESContext*>(two.context);
  if (oneCtx != twoCtx) {
    LOGFLF(LogLevel::info, "gles context from:", oneCtx, " to:", twoCtx);
    return true;
  }
  if (oneCtx && oneCtx->getContext() != twoCtx->getContext()) {
    LOGFLF(LogLevel::info, "gles egl context from:", oneCtx->getContext(),
           " to:", twoCtx->getContext());
    return true;
  }
#endif
  return false;
}

bool needReset(const YUVFrame& one, const YUVFrame& two) {
  if (one.format.width != two.format.width ||
      one.format.height != two.format.height) {
    LOGFLF(LogLevel::info, "video size form:", one.format.width, "x",
           one.format.height, " to:", two.format.width, "x", two.format.height);
    return true;
  }
  return false;
}

}
