#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/bitmap.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include "AndHelper.h"
#include "avox/AvoxLayer.h"
#include "avox_egl/EglHelper.hpp"
namespace avox {

#define AVOX_GET_ENV                           \
  JNIEnv* env = AvoxManager::Get().getEnv();   \
  if (!env) {                                 \
    LOGFLF(LogLevel::warn, "not get jnienv"); \
    return;                                   \
  }

#define AVOX_GET_ENV_RETURN(retsult)           \
  JNIEnv* env = AvoxManager::Get().getEnv();   \
  if (!env) {                                 \
    LOGFLF(LogLevel::warn, "not get jnienv"); \
    return retsult;                           \
  }

struct AndAudioTrack {
  jclass audioTrackClass;
  jmethodID init;
  jmethodID create;
  jmethodID setVolume;
  jmethodID getDataBuffer;
  jmethodID write;
  jmethodID flush;
  jmethodID start;
  jmethodID pause;
  jmethodID stop;
  jmethodID destroy;
  jmethodID getPosition;
  jmethodID getFrameSize;
};

struct AndAudioRecord {
  // android/media/AudioRecord
  jclass audioRecordClass;
  jmethodID init;
  jmethodID startrecording;
  jmethodID stop;
  jmethodID release;
  jmethodID read;
  jmethodID getMinBufferSize;
  jmethodID getAudioSessionId;
  jbyteArray read_buff;
  int ibuffsize;
};

struct AndSurfaceTexture {
  jclass surfaceTexureClass = nullptr;
  jmethodID init = nullptr;
  jmethodID attachToGLContext = nullptr;
  jmethodID detachFromGLContext = nullptr;
  jmethodID setDefaultBufferSize = nullptr;
  jmethodID updateTexImage = nullptr;
  jmethodID getTransformMatrix = nullptr;
  jmethodID release = nullptr;
  jmethodID setOnFrameAvailableListener = nullptr;
};

struct AndSurfaceTextureOb {
  jclass surfaceTexureObClass = nullptr;
  // 只需要init时传入SurfaceTexture对象，其他方法不需要
  jmethodID init = nullptr;
  // 底层JniSurfaceTexture在关闭前一定要调用
  // 因为回调是SurfaceTexture自己线程在管理
  // 不同步可能导致回调过去,指针已销毁
  jmethodID detach = nullptr;
};

struct AndSurface {
  jclass surfaceClass = nullptr;
  jmethodID init = nullptr;
};

extern AndAudioTrack jmAudioTrack;
extern AndAudioRecord jmAudioRecord;
extern AndSurfaceTexture jmSurfaceTexture;
extern AndSurface jmSurface;
extern AndSurfaceTextureOb jmSurfaceTextureOb;

// MediaCodec 颜色格式常量 (OMX_IVCommon / NDK MediaFormat 口径)。
// 放头文件: andYuvType/getYuvType 映射契约在 AndCommon.cpp 与 AndVDecoder.cpp
// 两处消费, 常量只许一份定义。
// https://www.androidos.net.cn/android/9.0.0_r8/xref/frameworks/native/headers/media_plugin/media/openmax/OMX_IVCommon.h
#define COLOR_FormatYUV420Planar 0x13
#define COLOR_FormatYUV422Planar 0x14
// 与SemiPlanar共享值
#define COLOR_FormatNV12 0x15
#define COLOR_FormatYUV420SemiPlanar 0x15
// YUV422灵活格式
#define COLOR_FormatYUV422Flexible 0x7F422888
// YUYV打包格式
#define COLOR_FormatYUYV 0x59595559
// P010 (10bit semi-planar, NV12 的 16bit 容器形态)
#define COLOR_FormatYUVP010 0x36

YuvType andYuvType(int32_t androidFormat);
int32_t getYuvType(YuvType yuvType);

extern "C" {
jint getAudiotrackFields();
jint getAudioRecordFields();
jint getSurfaceTextureFields();
jint getSurfaceFields();
jint getSurfaceTextureObFields();
}

}