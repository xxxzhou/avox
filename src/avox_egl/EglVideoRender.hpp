#pragma once

#include "GLESContext.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/video/VideoRender.hpp"

#ifdef __ANDROID__
#include "avox_android/AndVDecoder.hpp"
#include "avox_android/SharedGpuBuffer.hpp"
#endif
namespace avox {

class EglVideoRender : public VideoRender, public GLESContext {
public:
  EglVideoRender();
  virtual ~EglVideoRender();

private:
#ifdef __ANDROID__
  std::unique_ptr<SharedGpuBuffer> sharedBuffer;
  GLESContext *frameRCtx = nullptr;
#endif
  // 传入的EGL上下文
  EGLContext frameCtx = EGL_NO_CONTEXT;
  float aspect = 0.0f;

protected:
  uint32_t glProgram = 0;
  uint32_t fboId = 0;
  int32_t posAttr = 0;
  int32_t uvAttr = 0;
  int32_t extAttr = 0;

protected:
  virtual void onSetSurface() override;
  // 初始化图形管线
  virtual bool vaildAndInitGraph() override;
  virtual void releaseGraph() override;
  virtual void renderGpuFrame(const GpuFrame& frame) override;
  virtual bool fetchFrame(ImageBuffer *imageBuffer) override;

public:
  virtual IRenderContext *getGpuContext() override;

private:
  void createProgram();
  void useProgram(uint32_t oesId);
  void closeProgram();
};

}