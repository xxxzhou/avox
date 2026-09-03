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