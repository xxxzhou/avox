#pragma once

#include "EglHelper.hpp"

namespace avox {

// 1 所有线程同一EGLDisplay,不同线程使用不同EGLContext
// 这种方式，使用不同EGLContext需要eglMakeCurrent切换
// 不同线程使用不同EGLDisplay/EGLContext
class GLESContext : public IRenderContext {
 public:
  // sharedCtx外部传入的上下文
  GLESContext();
  virtual ~GLESContext();

 protected:
  uint32_t textureId = 0;
  ImageFormat eglSize = {};

 protected:
  EGLContext shardCtx = EGL_NO_CONTEXT;
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLConfig config = EGL_NO_CONFIG_KHR;
  EGLContext selfCtx = EGL_NO_CONTEXT;
  EGLSurface eglsurface = EGL_NO_SURFACE;
  EGLSurface preSurface = EGL_NO_SURFACE;
  // 是否是自身初始化，需要释放
  bool bSelfCtx = false;
  // 自身创建，则管理释放
  bool bSelfSurface = false;

 public:
  void initContext(EGLContext sharedCtx);
  void createSurface(EGLNativeWindowType window);
  void createSurface(ImageFormat format);
  void createSurface();
  void updateTextureId(int64_t textureId);
  void unInit();

 public:
  virtual RenderType getRenderType() override { return RenderType::OpenGLES; }
  uint32_t getImage() { return textureId; };
  ImageFormat getImageFormat() { return eglSize; };

 public:
  EGLContext getContext();
  // 在当前线程调用后，相应GL操作需要在当前线程进行
  bool makeCurrent();
  void unMakeCurrent();
  void dachCurrent();

 public:
  // 释放时，如MediaCocde需要调用
  virtual void onFrameRelease(bool bRender, const GpuFrame& frame) {};
};

}
