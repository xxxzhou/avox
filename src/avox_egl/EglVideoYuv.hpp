#pragma once

#include "GLESContext.hpp"
#include "avox/video/VideoYuv.hpp"

#ifdef __ANDROID__
#include "avox_android/JniSurfaceTexture.hpp"
#include "avox_android/SharedGpuBuffer.hpp"
#endif

namespace avox {

// 提供一个独立的surface,用于android平台
// 如果直接传入surface,可能被别的EGL环境连接
// 所以需要自己创建一个surfaceTexture,单独管理一个纹理队列
class EglVideoYuv : public VideoYuv, public GLESContext {
 public:
  EglVideoYuv();
  virtual ~EglVideoYuv();

 public:
  // 输入的GPU数据上下文
  GLESContext* inContext = nullptr;
  uint32_t glProgram = 0;  
  int32_t posAttr = 0;
  int32_t uvAttr = 0;
  int32_t extAttr = 0;
  bool bUseHardwareBuffer = false;
#ifdef __ANDROID__
  ANativeWindow* surface = nullptr;
  PFNEGLPRESENTATIONTIMEANDROIDPROC eglPresentationTimeANDROID = nullptr;
  SharedGpuBuffer* hardwareBuffer = nullptr;
#endif

 public:
  virtual bool vaildAndInitGraph(const GpuFrame& frame) override;
  virtual void renderGpuFrame(const GpuFrame& frame) override;
  virtual void releaseGraph() override;

 public:
#ifdef __ANDROID__
  void setSurface(ANativeWindow* surface_) { surface = surface_;}
#endif

 private:
  void createProgram();
  void closeProgram();
};
}