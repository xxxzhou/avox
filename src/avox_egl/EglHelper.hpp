#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "EglExport.h"
#include "GLES2/gl2.h"
#include "GLES2/gl2ext.h"
#include "avox/module/LogHelper.hpp"

namespace avox {

#define AVOX_GL_LOG(...)                                       \
  do {                                                         \
    GLenum ret = glGetError();                                 \
    if (ret != GL_NO_ERROR) {                                  \
      LOGFLF(LogLevel::warn, __VA_ARGS__, " gl error: ", ret); \
    }                                                          \
  } while (0)

#define AVOX_EGL_LOG(...)                                       \
  do {                                                         \
    EGLenum ret = eglGetError();                                 \
    if (ret != EGL_SUCCESS) {                                  \
      LOGFLF(LogLevel::warn, __VA_ARGS__, " egl error: ", ret); \
    }                                                          \
  } while (0)

uint32_t loadGLShader(int32_t type, const char* code);
uint32_t createGLProgram(const char* vertex, const char* fragment);



}