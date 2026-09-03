#include "AndCommon.hpp"

#include <media/NdkMediaFormat.h>

#include "AndHelper.h"
#include "avox/module/AvoxManager.hpp"
#include "avox/player/MediaPlayer.hpp"
#include "avox/player/VideoTrack.hpp"
#include "avox/video/WindowRender.hpp"
#include "avox_egl/EglVideoRender.hpp"
#include "avox_vulkan/vulkan/VkVideoRender.hpp"

namespace avox {

// https://www.androidos.net.cn/android/9.0.0_r8/xref/frameworks/native/headers/media_plugin/media/openmax/OMX_IVCommon.h
// Android原生定义 (对应NDK r25b)
#define COLOR_FormatYUV420Planar 0x13
#define COLOR_FormatYUV420SemiPlanar 0x15
// 与SemiPlanar共享值
#define COLOR_FormatNV12 0x15
#define COLOR_FormatYUVP010 0x36
// 灵活YUV422格式
#define COLOR_FormatYUV422Flexible 0x7F422888
// YUYV打包格式
#define COLOR_FormatYUYV 0x59595559
// YUV422平面格式
#define COLOR_FormatYUV422Planar 0x14

AndAudioTrack jmAudioTrack = {};
AndAudioRecord jmAudioRecord = {};
AndSurfaceTexture jmSurfaceTexture = {};
AndSurface jmSurface = {};
AndSurfaceTextureOb jmSurfaceTextureOb = {};

ImageType getBitmapType(int32_t format) {
  switch (format) {
    case ANDROID_BITMAP_FORMAT_RGBA_8888:
      return ImageType::rgba8;
    case ANDROID_BITMAP_FORMAT_A_8:
      return ImageType::r8;
    default:
      return ImageType::other;
  }
}

// https://www.androidos.net.cn/android/9.0.0_r8/xref/frameworks/native/headers/media_plugin/media/openmax/OMX_IVCommon.h
YuvType andYuvType(int32_t androidFormat) {
  switch (androidFormat) {
    case COLOR_FormatYUV420Planar:  // 0x13
      return YuvType::yuv420P;
    case COLOR_FormatYUV420SemiPlanar:  // 0x15
      return YuvType::nv12;
    case COLOR_FormatYUV422Flexible:  // 0x7F422888
      return YuvType::yuv422P;
    case COLOR_FormatYUVP010:  // 0x36
      return YuvType::uyvy422_10B;
    default:
      return YuvType::other;
  }
}

int32_t getYuvType(YuvType yuvType) {
  switch (yuvType)  // 修复表达式为枚举参数
  {
    case YuvType::yuv420P:
      return COLOR_FormatYUV420Planar;  // 0x13
    case YuvType::nv12:
      return COLOR_FormatYUV420SemiPlanar;  // 0x15
    case YuvType::yuv422P:
      return COLOR_FormatYUV422Flexible;  // 0x7F422888
    case YuvType::uyvy422_10B:
      return COLOR_FormatYUVP010;  // 0x36
    default:
      return COLOR_FormatNV12;
  }
}

void renderContext(ISurfaceRender* wrender, IRenderContext* context) {
  WindowRender* render = static_cast<WindowRender*>(wrender);
  if (!render) {
    return;
  }
  render->renderOut(context);
}

jint getAudiotrackFields() {
  JNIEnv* env = AvoxManager::Get().getEnv();
  jint result = JNI_ERR;
  // audio Track
  jclass classAudioTrack = env->FindClass("avox/android/library/AvoxAudioTrack");
  if (!classAudioTrack) {
    return JNI_ERR;
  }
  jmAudioTrack.init = env->GetMethodID(classAudioTrack, "<init>", "()V");
  if (nullptr == jmAudioTrack.init) {
    goto FUNC_EXIT;
  }
  jmAudioTrack.create = env->GetMethodID(classAudioTrack, "Create", "(III)I");
  if (nullptr == jmAudioTrack.create) {
    goto FUNC_EXIT;
  }
  jmAudioTrack.setVolume =
      env->GetMethodID(classAudioTrack, "SetVolume", "(FF)I");
  if (nullptr == jmAudioTrack.setVolume) {
    goto FUNC_EXIT;
  }

  jmAudioTrack.getDataBuffer =
      env->GetMethodID(classAudioTrack, "GetDataBuffer", "()[B");
  if (nullptr == jmAudioTrack.getDataBuffer) {
    goto FUNC_EXIT;
  }
  jmAudioTrack.write = env->GetMethodID(classAudioTrack, "Write", "(I)I");
  if (nullptr == jmAudioTrack.write) {
    goto FUNC_EXIT;
  }
  jmAudioTrack.flush = env->GetMethodID(classAudioTrack, "Flush", "()V");
  if (nullptr == jmAudioTrack.flush) {
    goto FUNC_EXIT;
  }
  jmAudioTrack.start = env->GetMethodID(classAudioTrack, "Start", "()V");
  if (nullptr == jmAudioTrack.start) {
    goto FUNC_EXIT;
  }
  jmAudioTrack.pause = env->GetMethodID(classAudioTrack, "Pause", "()V");
  if (nullptr == jmAudioTrack.pause) {
    goto FUNC_EXIT;
  }
  jmAudioTrack.stop = env->GetMethodID(classAudioTrack, "Stop", "()V");
  if (nullptr == jmAudioTrack.stop) {
    goto FUNC_EXIT;
  }
  jmAudioTrack.destroy = env->GetMethodID(classAudioTrack, "Destroy", "()V");
  if (nullptr == jmAudioTrack.destroy) {
    goto FUNC_EXIT;
  }
  jmAudioTrack.getPosition =
      env->GetMethodID(classAudioTrack, "GetPosition", "()I");
  if (nullptr == jmAudioTrack.destroy) {
    goto FUNC_EXIT;
  }
  jmAudioTrack.getFrameSize =
      env->GetMethodID(classAudioTrack, "GetFrameSize", "()I");
  if (nullptr == jmAudioTrack.getFrameSize) {
    goto FUNC_EXIT;
  }
  jmAudioTrack.audioTrackClass = (jclass)env->NewGlobalRef(classAudioTrack);
  result = JNI_OK;
FUNC_EXIT:
  env->DeleteLocalRef(classAudioTrack);
  __android_log_print(ANDROID_LOG_INFO, "avox", "%s:%d",
                      "avox getAudiotrackFields", result);
  return result;
}

jint getAudioRecordFields() {
  JNIEnv* env = AvoxManager::Get().getEnv();
  jint result = JNI_ERR;
  // android/media/AudioRecord
  jclass classAudioRecord = env->FindClass("android/media/AudioRecord");
  if (!classAudioRecord) {
    return JNI_ERR;
  }
  jmAudioRecord.init = env->GetMethodID(classAudioRecord, "<init>", "(IIIII)V");
  if (nullptr == jmAudioRecord.init) {
    goto FUNC_EXIT;
  }
  jmAudioRecord.getMinBufferSize =
      env->GetStaticMethodID(classAudioRecord, "getMinBufferSize", "(III)I");
  if (nullptr == jmAudioRecord.getMinBufferSize) {
    goto FUNC_EXIT;
  }
  jmAudioRecord.startrecording =
      env->GetMethodID(classAudioRecord, "startRecording", "()V");
  if (nullptr == jmAudioRecord.startrecording) {
    goto FUNC_EXIT;
  }
  jmAudioRecord.stop = env->GetMethodID(classAudioRecord, "stop", "()V");
  if (nullptr == jmAudioRecord.stop) {
    goto FUNC_EXIT;
  }
  jmAudioRecord.read = env->GetMethodID(classAudioRecord, "read", "([BII)I");
  if (nullptr == jmAudioRecord.read) {
    goto FUNC_EXIT;
  }
  jmAudioRecord.release = env->GetMethodID(classAudioRecord, "release", "()V");
  if (nullptr == jmAudioRecord.release) {
    goto FUNC_EXIT;
  }
  jmAudioRecord.getAudioSessionId =
      env->GetMethodID(classAudioRecord, "getAudioSessionId", "()I");
  if (nullptr == jmAudioRecord.getAudioSessionId) {
    goto FUNC_EXIT;
  }
  jmAudioRecord.audioRecordClass = (jclass)env->NewGlobalRef(classAudioRecord);
  result = JNI_OK;
FUNC_EXIT:
  env->DeleteLocalRef(classAudioRecord);
  __android_log_print(ANDROID_LOG_INFO, "avox", "%s:%d",
                      "avox getAudioRecordFields", result);
  return result;
}

jint getSurfaceTextureFields() {
  JNIEnv* env = AvoxManager::Get().getEnv();
  jint result = JNI_OK;
  jclass surfaceTexClass = env->FindClass("android/graphics/SurfaceTexture");
  if (!surfaceTexClass) {
    return JNI_ERR;
  }
  jmSurfaceTexture.init = env->GetMethodID(surfaceTexClass, "<init>", "(I)V");
  jmSurfaceTexture.attachToGLContext =
      env->GetMethodID(surfaceTexClass, "attachToGLContext", "(I)V");
  jmSurfaceTexture.detachFromGLContext =
      env->GetMethodID(surfaceTexClass, "detachFromGLContext", "()V");
  jmSurfaceTexture.setDefaultBufferSize =
      env->GetMethodID(surfaceTexClass, "setDefaultBufferSize", "(II)V");
  jmSurfaceTexture.updateTexImage =
      env->GetMethodID(surfaceTexClass, "updateTexImage", "()V");
  jmSurfaceTexture.getTransformMatrix =
      env->GetMethodID(surfaceTexClass, "getTransformMatrix", "([F)V");
  jmSurfaceTexture.release =
      env->GetMethodID(surfaceTexClass, "release", "()V");
  jmSurfaceTexture.setOnFrameAvailableListener = env->GetMethodID(
      surfaceTexClass, "setOnFrameAvailableListener",
      "(Landroid/graphics/SurfaceTexture$OnFrameAvailableListener;)V");
  jmSurfaceTexture.surfaceTexureClass =
      (jclass)env->NewGlobalRef(surfaceTexClass);
  env->DeleteLocalRef(surfaceTexClass);
  __android_log_print(ANDROID_LOG_INFO, "avox", "%s:%d",
                      "avox getSurfaceTextureFields", result);
  return JNI_OK;
}

jint getSurfaceFields() {
  JNIEnv* env = AvoxManager::Get().getEnv();
  jint result = JNI_OK;
  jclass surfaceClass = env->FindClass("android/view/Surface");
  if (!surfaceClass) {
    return JNI_ERR;
  }
  jmSurface.init = env->GetMethodID(surfaceClass, "<init>",
                                    "(Landroid/graphics/SurfaceTexture;)V");
  jmSurface.surfaceClass = (jclass)env->NewGlobalRef(surfaceClass);
  env->DeleteLocalRef(surfaceClass);
  __android_log_print(ANDROID_LOG_INFO, "avox", "%s", "avox getSurfaceFields");
  return JNI_OK;
}

jint getSurfaceTextureObFields() {
  JNIEnv* env = AvoxManager::Get().getEnv();
  jint result = JNI_ERR;
  jclass surfaceTextureObClass =
      env->FindClass("avox/android/library/AvoxSurfaceTextureOb");
  if (!surfaceTextureObClass) {
    return JNI_ERR;
  }
  jmSurfaceTextureOb.init =
      env->GetMethodID(surfaceTextureObClass, "<init>", "(J)V");
  jmSurfaceTextureOb.detach =
      env->GetMethodID(surfaceTextureObClass, "detach", "()V");
  jmSurfaceTextureOb.surfaceTexureObClass =
      (jclass)env->NewGlobalRef(surfaceTextureObClass);
  env->DeleteLocalRef(surfaceTextureObClass);
  __android_log_print(ANDROID_LOG_INFO, "avox", "%s",
                      "avox getSurfaceTextureObFields");
  return JNI_OK;
}

}