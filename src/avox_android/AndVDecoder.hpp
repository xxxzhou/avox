#pragma once
#include <android/surface_texture.h>
#include <android/surface_texture_jni.h>
#include <media/NdkMediaCodec.h>
#include <queue>

#include "AndCommon.hpp"
#include "JniSurfaceTexture.hpp"
#include "avox/video/VideoDecoder.hpp"
#include "avox_egl/GLESContext.hpp"

namespace avox {

#define AVOX_ANDROID_MEDIACODEC_TIMEOUT_US 2000

#if __ANDROID_API__ >= 21

class AndVDecoder : public VideoDecoder, public GLESContext {
public:
  AndVDecoder();
  virtual ~AndVDecoder();

private:
  AMediaCodec *mediaCodec = nullptr;
  AMediaFormat *format = nullptr;
  // 解码后的数据，解码后直接输出到对应OES纹理
  std::unique_ptr<JniSurfaceTexture> surfaceTexture = nullptr;  
  // 是否直接把解码数据渲染到OES纹理，不经过CPU
  bool bOpenglRender = false;
  // 是否打开
  bool bOpen = false;

  YUVFormat yuvFormat = {};
  int32_t stride = 0;

  std::atomic<bool> bFrameAvailable = false;

  // 可能有B帧，需要自己处理
  // std::priority_queue<GpuFrame, std::vector<GpuFrame>, GpuFrameSortPTS>
  //     sortQueue;
  // size_t FRAME_QUEUE_THRESHOLD = 5;

public:
  void updateYuvFormat();

  // AVDecoder
public:
  // 初始化
  virtual bool onVaild() override;
  // 初始化
  virtual DecodeResult onPreDecoder() override;
  // 解码
  virtual DecodeResult decode(const AvoxPacket &packet) override;
  // flush
  virtual void flush() override;

  // VideoDecoder
public:
  virtual void onClose() override;

public:
  // Render为true表明MediaCodec队列的数据压入到OES纹理
  // 释放纹理
  virtual void onFrameRelease(bool bRender, const GpuFrame &frame) override;
};

#endif
}
