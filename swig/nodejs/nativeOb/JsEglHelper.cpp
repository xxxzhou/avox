#include "../../../src/avox/video/WindowRender.hpp"
#include "AvoxJsOb.h"
#include "JsObserver.hpp"

#if _WIN32
#include <windows.h>

#include "EGL/egl.h"
#include "EGL/eglext.h"
#include "GLES2/gl2.h"
#endif

namespace avox {

typedef EGLContext(EGLAPIENTRYP PFNEGLGETCURRENTCONTEXTPROC)(void);
typedef void(GL_APIENTRYP PFNGLGETINTEGERVPROC)(GLenum pname, GLint* data);

// 全局变量，用于存储函数地址
PFNEGLGETCURRENTDISPLAYPROC pfn_eglGetCurrentDisplay = nullptr;
PFNEGLGETCURRENTCONTEXTPROC pfn_eglGetCurrentContext = nullptr;
PFNGLGETINTEGERVPROC pfn_glGetIntegerv = nullptr;

void InitEGLFunctions() {
  HMODULE hEGL = GetModuleHandleA("libEGL.dll");
  HMODULE hGLES = GetModuleHandleA("libGLESv2.dll");
  if (hEGL && hGLES) {
    pfn_eglGetCurrentDisplay = (PFNEGLGETCURRENTDISPLAYPROC)GetProcAddress(
        hEGL, "eglGetCurrentDisplay");
    pfn_eglGetCurrentContext = (PFNEGLGETCURRENTCONTEXTPROC)GetProcAddress(
        hEGL, "eglGetCurrentContext");
    // 使用 GetProcAddress 从 GLES DLL 中获取 OpenGL ES 函数
    pfn_glGetIntegerv =
        (PFNGLGETINTEGERVPROC)GetProcAddress(hGLES, "glGetIntegerv");
  }
}

int32_t renderEglResource(ISurfaceRender* render) {
  if (!pfn_eglGetCurrentDisplay) {
    InitEGLFunctions();
  }
  EGLDisplay dpy = pfn_eglGetCurrentDisplay();
  if (dpy == EGL_NO_DISPLAY) {
    LOGFLF(LogLevel::warn, "egl display is null");
    return -1;
  }
  return 0;
}

}