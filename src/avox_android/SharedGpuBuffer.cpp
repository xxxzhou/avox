#include "SharedGpuBuffer.hpp"

#include <dlfcn.h>

namespace avox {

#define LOAD_PROC(NAME, TYPE) \
  NAME = reinterpret_cast<TYPE>(eglGetProcAddress(#NAME))

using PFNEGLGETNATIVECLIENTBUFFERANDROID =
    EGLClientBuffer(EGLAPIENTRYP)(const AHardwareBuffer* buffer);
using PFNGLEGLIMAGETARGETTEXTURE2DOESPROC = void(GL_APIENTRYP)(GLenum target,
                                                               void* image);
using PFNGLBUFFERSTORAGEEXTERNALEXTPROC = void(GL_APIENTRYP)(GLenum target,
                                                             GLintptr offset,
                                                             GLsizeiptr size,
                                                             void* clientBuffer,
                                                             GLbitfield flags);
using PFNGLMAPBUFFERRANGEPROC = void*(GL_APIENTRYP)(GLenum target,
                                                    GLintptr offset,
                                                    GLsizeiptr length,
                                                    GLbitfield access);
using PFNGLUNMAPBUFFERPROC = void*(GL_APIENTRYP)(GLenum target);

static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES =
    nullptr;
static PFNEGLGETNATIVECLIENTBUFFERANDROID eglGetNativeClientBufferANDROID =
    nullptr;
static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = nullptr;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = nullptr;
static PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC
    glFramebufferTextureMultiviewOVR = nullptr;
static PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC
    glFramebufferTextureMultisampleMultiviewOVR = nullptr;
static PFNGLBUFFERSTORAGEEXTERNALEXTPROC glBufferStorageExternalEXT = nullptr;
static PFNGLMAPBUFFERRANGEPROC glMapBufferRange = nullptr;
static PFNGLUNMAPBUFFERPROC glUnmapBuffer = nullptr;

// 用的NDK小于26,但是手机用的NDK大于26,动态加载函数
static PFN_AHardwareBuffer_allocate pAHardwareBuffer_allocate = nullptr;
static PFN_AHardwareBuffer_release pAHardwareBuffer_release = nullptr;
static PFN_AHardwareBuffer_describe pAHardwareBuffer_describe = nullptr;

bool supportSharedGpuBuffer() {
  static bool supported = []() {
#if __ANDROID_API__ < 26
    void* handle = dlopen("libnativewindow.so", RTLD_NOW);
    if (handle) {
      pAHardwareBuffer_allocate = (PFN_AHardwareBuffer_allocate)dlsym(
          handle, "AHardwareBuffer_allocate");
      pAHardwareBuffer_release =
          (PFN_AHardwareBuffer_release)dlsym(handle, "AHardwareBuffer_release");
      pAHardwareBuffer_describe = (PFN_AHardwareBuffer_describe)dlsym(
          handle, "AHardwareBuffer_describe");
    }
    if (!pAHardwareBuffer_allocate || !pAHardwareBuffer_release) {
      log(LogLevel::warn, "run ndk less 26");
      return false;
    }
    LOGFLF(LogLevel::info,
           "AHardwareBuffer_allocate:", pAHardwareBuffer_allocate,
           " AHardwareBuffer_release:", pAHardwareBuffer_release);
#endif
    if (eglGetProcAddress == nullptr) {
      log(LogLevel::warn, "eglGetProcAddress is null");
      return false;
    }
    LOAD_PROC(glEGLImageTargetTexture2DOES,
              PFNGLEGLIMAGETARGETTEXTURE2DOESPROC);
    if (!glEGLImageTargetTexture2DOES) {
      log(LogLevel::warn, "glEGLImageTargetTexture2DOES is null");
      return false;
    }
    LOAD_PROC(eglGetNativeClientBufferANDROID,
              PFNEGLGETNATIVECLIENTBUFFERANDROID);
    if (!eglGetNativeClientBufferANDROID) {
      log(LogLevel::warn, "eglGetNativeClientBufferANDROID is null");
      return false;
    }
    LOAD_PROC(eglCreateImageKHR, PFNEGLCREATEIMAGEKHRPROC);
    if (!eglCreateImageKHR) {
      log(LogLevel::warn, "eglCreateImageKHR is null");
      return false;
    }
    LOAD_PROC(eglDestroyImageKHR, PFNEGLDESTROYIMAGEKHRPROC);
    if (!eglDestroyImageKHR) {
      log(LogLevel::warn, "eglDestroyImageKHR is null");
      return false;
    }
    return true;
  }();
  return supported;
}

bool getHardwareBuffer(AHardwareBuffer* buffer, AHardwareBuffer_Desc* desc) {
#if __ANDROID_API__ >= 26
  AHardwareBuffer_describe(buffer, desc);
  return true;
#else
  if (pAHardwareBuffer_describe) {
    pAHardwareBuffer_describe(buffer, desc);
    return true;
  }
#endif
  return false;
}

SharedGpuBuffer::SharedGpuBuffer(/* args */) {
  bSupport = supportSharedGpuBuffer();
}

SharedGpuBuffer::~SharedGpuBuffer() { close(); }

void SharedGpuBuffer::createAndroidBuffer(const ImageFormat& format_) {
  // 释放已有的资源
  release();
  format = format_;
  AHardwareBuffer_Desc usage = {};
  // filling in the usage for HardwareBuffer
  usage.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  usage.height = format.height;
  usage.width = format.width;
  usage.layers = 1;
  usage.rfu0 = 0;
  usage.rfu1 = 0;
  // usage.stride = 0;
  //  AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
  usage.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER |
                AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
#if __ANDROID_API__ >= 26
  AHardwareBuffer_allocate(&usage, &hardwareBuffer);
#else
  pAHardwareBuffer_allocate(&usage, &hardwareBuffer);
#endif
  LOGFLF(LogLevel::info, "hardware buffer:", hardwareBuffer);
  bindEGL();
  onInit();
}

void SharedGpuBuffer::bindEGL() {
  if (!hardwareBuffer) {
    return;
  }
  // android绑定AHardwareBuffer与egl image
  EGLClientBuffer eglbuffer = eglGetNativeClientBufferANDROID(hardwareBuffer);
  if (!eglbuffer) {
    log(LogLevel::warn, "eglGetNativeClientBufferANDROID failed");
    return;
  }
  EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
  eglImage =
      eglCreateImageKHR(eglGetDisplay(EGL_DEFAULT_DISPLAY), EGL_NO_CONTEXT,
                        EGL_NATIVE_BUFFER_ANDROID, eglbuffer, attrs);
  // assert(image != EGL_NO_IMAGE_KHR);
  if (!eglImage) {
    int32_t errorId = eglGetError();
    log(LogLevel::warn, "not create hardware image,error id", errorId);
  } else {
    log(LogLevel::info, "hardware image create success.");
  }
}

void SharedGpuBuffer::bindGL(uint32_t textureId, uint32_t texType) {
  if (!hardwareBuffer) {
    return;
  }
  if (!eglImage) {
    log(LogLevel::warn, "eglImage is null");
    return;
  }
  int bindType = GL_TEXTURE_2D;
  if (texType > 0) {
    bindType = texType;
  }
  glBindTexture(bindType, textureId);
  glEGLImageTargetTexture2DOES(bindType, eglImage);
  glBindTexture(bindType, 0);
}

void SharedGpuBuffer::renderGL(uint32_t textureId, uint32_t texType) {
  if (!eglImage) {
    log(LogLevel::warn, "eglImage is null");
    return;
  }
  glEGLImageTargetTexture2DOES(texType, eglImage);
}

void SharedGpuBuffer::close() {
  LOGFLF(LogLevel::info, "harder/egl buffer");
  if (hardwareBuffer != nullptr) {
#if __ANDROID_API__ >= 26
    AHardwareBuffer_release(hardwareBuffer);
#else
    pAHardwareBuffer_release(hardwareBuffer);
#endif
    hardwareBuffer = nullptr;
  }
  if (eglImage) {
    eglDestroyImageKHR(eglGetDisplay(EGL_DEFAULT_DISPLAY), eglImage);
    eglImage = nullptr;
  }
}

void SharedGpuBuffer::release() {
  close();
  onRelease();
}

void SharedGpuBuffer::logData() {
  if (!hardwareBuffer) {
    return;
  }
#if __ANDROID_API__ >= 26
  // 如何验证hardwareBuffer里数据？验证是有数据的
  void* mappedData = nullptr;
  const int lockFlags = AHARDWAREBUFFER_USAGE_CPU_READ_RARELY;
  int ret =
      AHardwareBuffer_lock(hardwareBuffer, lockFlags, -1, nullptr, &mappedData);
  if (ret == 0 && mappedData) {
    AvoxData data = {};
    data.data = (uint8_t*)mappedData + 12300;
    data.size = 100;
    log(LogLevel::info, "hardwareBuffer data:", data);
    AHardwareBuffer_unlock(hardwareBuffer, nullptr);
  } else {
    log(LogLevel::error, "AHardwareBuffer_lock failed:", ret);
  }
#endif
}
}