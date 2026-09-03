#pragma once

#include "EglHelper.hpp"

namespace avox {

// 1 所有线程同一EGLDisplay,不同线程使用不同EGLContext
// 这种方式，使用不同EGLContext需要eglMakeCurrent切换
// 不同线程使用不同EGLDisplay/EGLContext
// 这次样不需要eglMakeCurrent切换，但是资源会多些
// 2 不同线程独立的EGLContext,如果需要共享,在创建时传入
// 3 GL相关函数需要EGL上下文与渲染目标，需要在EGL创建suface后才能使用
// 4 不渲染到窗口，使用eglCreatePbufferSurface
// 5 渲染到窗口，使用eglCreateWindowSurface
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