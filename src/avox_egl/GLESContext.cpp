#include "GLESContext.hpp"

#include "avox/module/AvoxManager.hpp"

namespace avox {

GLESContext::GLESContext() {
#ifdef __ANDROID__
  // AndroidEnv &env = AvoxManager::Get().getAppEnv();
  // 如果使用统一的display
  // 在切换不同的EGLContext需要管理eglMakeCurrent
  // 后面再看看这块逻辑怎么改
  // display = env.display;
#endif
}

GLESContext::~GLESContext() {
#ifdef __ANDROID__
  unInit();
#endif
}

void GLESContext::initContext(EGLContext sharedCtx) {
  shardCtx = sharedCtx;
#ifdef __ANDROID__
  // 一个线程只有一个context保持激活
  selfCtx = eglGetCurrentContext();
  eglsurface = eglGetCurrentSurface(EGL_DRAW);
  // 当前线程还没创建上下文，则先初始化EGLDisplay
  if (selfCtx == EGL_NO_CONTEXT) {
    bSelfCtx = true;
    EGLint majorVersion = 0;
    EGLint minorVersion = 0;
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
      LOGFLF(LogLevel::warn, "egl Display is null");
      return;
    }
    if (!eglInitialize(display, &majorVersion, &minorVersion)) {
      AVOX_EGL_LOG("eglInitialize failed.");
      return;
    }
    LOGFLF(LogLevel::info, "eglInitialize success, egl display:", display,
           " majorVersion:", majorVersion, " minorVersion:", minorVersion);
  } else {
    bSelfCtx = false;
    // 使用之前初始化的display,只用来创建surface
    display = eglGetCurrentDisplay();
    EGLint eglVersion = 0;
    eglQueryContext(display, selfCtx, EGL_CONTEXT_CLIENT_VERSION, &eglVersion);
    LOGFLF(LogLevel::info, "using existing egl display:", display,
           " version:", eglVersion);
  }
  EGLint numConfigs = 0;
  EGLint attribList[] = {EGL_RED_SIZE,
                         8,
                         EGL_GREEN_SIZE,
                         8,
                         EGL_BLUE_SIZE,
                         8,
                         EGL_ALPHA_SIZE,
                         8,
                         EGL_DEPTH_SIZE,
                         8,
                         EGL_RENDERABLE_TYPE,
                         EGL_OPENGL_ES2_BIT,
                         EGL_SURFACE_TYPE,
                         EGL_WINDOW_BIT,
                         EGL_NONE};
  EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  if (!eglChooseConfig(display, attribList, &config, 1, &numConfigs)) {
    LOGFLF(LogLevel::warn, "eglChooseConfig failed");
    return;
  }
  if (numConfigs < 1) {
    LOGFLF(LogLevel::warn, "eglChooseConfig failed, no valid config found");
    return;
  }
  // 如果没有外部上下文，创建一个，shardCtx是和别的EGL共享可以访问的资源
  if (selfCtx == EGL_NO_CONTEXT) {
    if (shardCtx) {
      EGLint eglVersion = 0;
      if (!eglQueryContext(display, shardCtx, EGL_CONTEXT_CLIENT_VERSION,
                           &eglVersion)) {
        AVOX_EGL_LOG("eglQueryContext failed.");
      } else {
        contextAttribs[1] = eglVersion;
      }
      LOGFLF(LogLevel::info, "init self context with shard context:", shardCtx);
    }
    selfCtx = eglCreateContext(display, config, shardCtx, contextAttribs);
    if (selfCtx == EGL_NO_CONTEXT) {
      log(LogLevel::warn, "eglCreateContext failed");
      return;
    }
    LOGFLF(LogLevel::info, "create egl context success, context:", selfCtx);
  }
  bSelfSurface = false;
  bool bmake = makeCurrent();
  if (!bmake) {
    AVOX_EGL_LOG("egl detach current failed.");
  }
#endif
}

void GLESContext::createSurface(EGLNativeWindowType window) {
  if (display == EGL_NO_DISPLAY) {
    LOGFLF(LogLevel::warn, "egl display is null");
    return;
  }
  eglsurface = eglGetCurrentSurface(EGL_DRAW);
  if (eglsurface != EGL_NO_SURFACE) {
    // 如果已经有surface,则渲染到当前的surface需要 eglMakeCurrent
    LOGFLF(LogLevel::info,
           "current egl context surface exist,surface:", eglsurface,
           ",need make current");
  }
  eglsurface = eglCreateWindowSurface(display, config, window, nullptr);
  LOGFLF(LogLevel::info, "create egl window surface:", eglsurface,
         " have window:", window);
  createSurface();
  bSelfSurface = true;
}

void GLESContext::createSurface(ImageFormat format) {
  if (display == EGL_NO_DISPLAY) {
    LOGFLF(LogLevel::warn, "egl display is null");
    return;
  }
  eglsurface = eglGetCurrentSurface(EGL_DRAW);
  if (eglsurface != EGL_NO_SURFACE) {
    // 如果已经有surface,则渲染到当前的surface需要 eglMakeCurrent
    LOGFLF(LogLevel::info,
           "current egl context surface exist, need make current");
  }
  const EGLint pBufferAttrs[] = {
      EGL_WIDTH,          format.width,       EGL_HEIGHT,
      format.height,      EGL_TEXTURE_TARGET, EGL_NO_TEXTURE,
      EGL_TEXTURE_FORMAT, EGL_NO_TEXTURE,     EGL_NONE};
  eglsurface = eglCreatePbufferSurface(display, config, pBufferAttrs);
  LOGFLF(LogLevel::info, "create egl pbuffer surface:", eglsurface,
         " pbo width:", format.width, " height:", format.height);
  createSurface();
  bSelfSurface = true;
}

void GLESContext::createSurface() {
  int32_t width = 0;
  int32_t height = 0;
  if (eglsurface != EGL_NO_SURFACE) {
    eglQuerySurface(display, eglsurface, EGL_WIDTH, &width);
    eglQuerySurface(display, eglsurface, EGL_HEIGHT, &height);
  }
  eglSize.width = width;
  eglSize.height = height;
  bool bmake = makeCurrent();
  if (!bmake) {
    AVOX_EGL_LOG("egl detach current failed.");
  }
}

void GLESContext::updateTextureId(int64_t id) { textureId = id; }

void GLESContext::unInit() {
#ifdef __ANDROID__
  LOGFLF(LogLevel::info, "egl start unit, selfCtx:", bSelfCtx,
         " selfSurface:", bSelfSurface);
  // 只有自己拥有上下文时，才执行解绑和销毁
  if (bSelfCtx && selfCtx != EGL_NO_CONTEXT) {
    // 解绑当前线程的上下文
    if (display != EGL_NO_DISPLAY) {
      eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    // 销毁自己创建的上下文
    eglDestroyContext(display, selfCtx);
    selfCtx = EGL_NO_CONTEXT;
    bSelfCtx = false;
  }
  // 只有自己拥有 Surface 时，才销毁 Surface
  if (bSelfSurface && eglsurface != EGL_NO_SURFACE) {
    if (display != EGL_NO_DISPLAY) {
      eglDestroySurface(display, eglsurface);
    }
    eglsurface = EGL_NO_SURFACE;
    bSelfSurface = false;
  }
  // 注意：不要随意清空 display，除非你确定 eglTerminate 了
  // display = EGL_NO_DISPLAY;
  selfCtx = EGL_NO_CONTEXT;
  eglsurface = EGL_NO_SURFACE;
  LOGFLF(LogLevel::info, "egl uninit success");
#endif
}

EGLContext GLESContext::getContext() { return selfCtx; }

bool GLESContext::makeCurrent() {
  preSurface = eglGetCurrentSurface(EGL_DRAW);
#ifdef __ANDROID__
  if (!eglMakeCurrent(display, eglsurface, eglsurface, selfCtx)) {
    AVOX_EGL_LOG("detach eglMakeCurrent failed.");
    return false;
  }
  return true;
#endif
  return false;
}

void GLESContext::unMakeCurrent() {
#ifdef __ANDROID__
  if (!eglMakeCurrent(display, preSurface, preSurface, selfCtx)) {
    AVOX_EGL_LOG("detach eglMakeCurrent failed.");
  }
#endif
}

void GLESContext::dachCurrent() {
#ifdef __ANDROID__
  if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                      EGL_NO_CONTEXT)) {
    AVOX_EGL_LOG("undetach eglMakeCurrent failed.");
  }
#endif
}

}