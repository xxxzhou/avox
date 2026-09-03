#include "EglHelper.hpp"

#include "EglExport.h"
#include "EglWindow.hpp"

namespace avox {

IRenderContext* createGLESContext(int64_t sharedCtx) {
  GLESContext* context = new GLESContext();
  context->initContext(reinterpret_cast<EGLContext>(sharedCtx));
  return context;
}

void updateGLESContextTexture(IRenderContext* context, int32_t image,

                              ImageFormat format) {
  GLESContext* glesContext = static_cast<GLESContext*>(context);
  if (!glesContext) {
    LOGFLF(LogLevel::warn, "context is null or not gles context");
    return;
  }
  glesContext->updateTextureId(image);
  // glesContext->updateImageFormat(format);
}

int32_t renderEglTexture(ISurfaceRender* render) {
  EGLDisplay dpy = eglGetCurrentDisplay();
  EGLContext ctx = eglGetCurrentContext();
  EGLint error = eglGetError();  // 获取错误码

  if (dpy == EGL_NO_DISPLAY) {
    LOGFLF(LogLevel::warn, "egl display is null, EGL Error: 0x",
           error);  // 打印十六进制错误码
    return -1;
  }
  return 0;
}

uint32_t loadGLShader(int32_t type, const char* code) {
#ifndef WIN32
  // 编译shader
  uint32_t shader = glCreateShader(type);
  glShaderSource(shader, 1, &code, nullptr);
  glCompileShader(shader);
  // 编译结果
  int compileStatus = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
  if (compileStatus == 0) {
    glDeleteShader(shader);
    shader = 0;
  }
  return shader;
#endif
  return 0;
}

uint32_t createGLProgram(const char* vertex, const char* fragment) {
#ifndef WIN32
  uint32_t vertShader = loadGLShader(GL_VERTEX_SHADER, vertex);
  if (vertShader == 0) {
    LOGFLF(LogLevel::warn, "vert shader load failed");
    return 0;
  }
  uint32_t fragShader = loadGLShader(GL_FRAGMENT_SHADER, fragment);
  if (fragShader == 0) {
    LOGFLF(LogLevel::warn, "frag shader load failed");
    return 0;
  }
  int program = glCreateProgram();
  if (program != 0) {
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);
    int32_t linkStatus = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
      glDeleteProgram(program);
      program = 0;
    }
  }
  return program;
#endif
  return 0;
}

}
