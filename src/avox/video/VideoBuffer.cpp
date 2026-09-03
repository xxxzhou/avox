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
  // 要求复制,以及平面格式如果不紧湊也需要重新排列
  if (bCopy || (bPlane && !bTightlyPacked(frame))) {
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

void SwVideoBuffer::to(YUVFrame& frame, YuvType type) {
  yuvType = type;
  // 当前格式要rgba8/r8
  image2YUVFormat(imageFormat, type, frame.format);
  frame.data[0] = data;
  frame.stride[0] = imageFormat.rowPitch;
  // 如果是平面格式
  bool bPlane = bVPlaneFormat(frame.format.type);
  if (bPlane) {
    int32_t ySize = imageFormat.rowPitch * frame.format.height;
    if (type == YuvType::nv12) {
      // NV12: UV 交织，步长同 Y
      frame.data[1] = data + ySize;
      frame.stride[1] = imageFormat.rowPitch;
      frame.data[2] = nullptr;
      frame.stride[2] = 0;
    } else if (type == YuvType::yuv420P10) {
      // yuv420P10: 每像素2字节, UV是Y的一半宽高
      int32_t uvPitch = imageFormat.rowPitch / 2;
      int32_t uvHeight = frame.format.height / 2;
      int32_t uvSize = uvPitch * uvHeight;
      frame.data[1] = data + ySize;
      frame.stride[1] = uvPitch;
      frame.data[2] = data + ySize + uvSize;
      frame.stride[2] = uvPitch;
    } else {
      // 处理 YUV420P / YUV422P / YUV444P
      int32_t uvWidthDiv = (type == YuvType::yuv444P) ? 1 : 2;
      int32_t uvHeightDiv = (type == YuvType::yuv420P) ? 2 : 1;
      if (type == YuvType::yuv420P || type == YuvType::yuv422P) {
        // YUV420P/422P的UV在buffer中4块连续排列:
        // [U even rows] [U odd rows] [V even rows] [V odd rows]
        // 每块 uvHeight/2 行，每行 rowPitch 字节
        // U起始在UV段开头，V起始在UV段中间
        int32_t uvHeight = frame.format.height / uvHeightDiv;
        int32_t halfUvSize = imageFormat.rowPitch * (uvHeight / 2);
        frame.data[1] = data + ySize;
        frame.stride[1] = imageFormat.rowPitch / 2;
        frame.data[2] = data + ySize + halfUvSize;
        frame.stride[2] = imageFormat.rowPitch / 2;
      } else {
        // YUV444P: UV与Y同尺寸，各自独立块 
        int32_t uvHeight = frame.format.height / uvHeightDiv;
        int32_t uvSize = imageFormat.rowPitch * uvHeight;
        frame.data[1] = data + ySize;
        frame.stride[1] = imageFormat.rowPitch;
        frame.data[2] = data + ySize + uvSize;
        frame.stride[2] = imageFormat.rowPitch;
      }
    }
  }
}

bool SwVideoBuffer::to(YUVFrame& yuvFrame) {
  if (yuvType == YuvType::other) {
    LOGFLF(LogLevel::info, "yuvType is other");
    return false;
  }
  to(yuvFrame, yuvType);
  return true;
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
