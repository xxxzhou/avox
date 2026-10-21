#include "JniSurfaceTexture.hpp"

#include "avox/module/AvoxManager.hpp"

namespace avox {

JniSurfaceTexture::JniSurfaceTexture() {}

JniSurfaceTexture::~JniSurfaceTexture() {
  AVOX_GET_ENV
  // 关闭surface/nativewindow
  close();
}

void JniSurfaceTexture::init(int32_t textureId) {
  close();
  AVOX_GET_ENV
  jobject jSurfaceTexture = env->NewObject(jmSurfaceTexture.surfaceTexureClass,
                                           jmSurfaceTexture.init, textureId);
  surfaceTexture = env->NewGlobalRef(jSurfaceTexture);
  env->DeleteLocalRef(jSurfaceTexture);

  jobject jSurface =
      env->NewObject(jmSurface.surfaceClass, jmSurface.init, surfaceTexture);
  surface = env->NewGlobalRef(jSurface);
  env->DeleteLocalRef(jSurface);
  // native window
  nativeWindow = ANativeWindow_fromSurface(env, surface);
}

void JniSurfaceTexture::close() {
  AVOX_GET_ENV
  // 一定要把surfaceTexture回调断开,下面surfaceTexture不能用了
  if (surfaceTextureOb) {
    env->CallVoidMethod(surfaceTextureOb, jmSurfaceTextureOb.detach);
  }
  if (nativeWindow) {
    ANativeWindow_release(nativeWindow);
    nativeWindow = nullptr;
  }
  if (surface) {
    env->DeleteGlobalRef(surface);
    surface = nullptr;
  }
  if (surfaceTexture) {
    env->CallVoidMethod(surfaceTexture,
                        jmSurfaceTexture.setOnFrameAvailableListener, nullptr);
    env->CallVoidMethod(surfaceTexture, jmSurfaceTexture.release);
    if (env->ExceptionCheck()) {
      // 打印错误栈，方便调试
      env->ExceptionDescribe();
      // 清除异常，确保后续 DeleteGlobalRef 能执行
      env->ExceptionClear();
    }
    env->DeleteGlobalRef(surfaceTexture);
    surfaceTexture = nullptr;
  }
  if (surfaceTextureOb) {
    env->DeleteGlobalRef(surfaceTextureOb);
    surfaceTextureOb = nullptr;
  }
}

void JniSurfaceTexture::setImageSize(int32_t width, int32_t height) {
  AVOX_GET_ENV
  env->CallVoidMethod(surfaceTexture, jmSurfaceTexture.setDefaultBufferSize,
                      width, height);
}

bool JniSurfaceTexture::updateTexImage() {
  AVOX_GET_ENV_RETURN(false)
  if (!surfaceTexture) {
    return false;
  }
  env->CallVoidMethod(surfaceTexture, jmSurfaceTexture.updateTexImage);
  if (env->ExceptionCheck()) {
    // 上下文不匹配时每帧都抛: 不打栈不刷屏(每帧 5 行会拖慢渲染), 交调用方计数
    env->ExceptionClear();
    return false;
  }
  return true;
}

void JniSurfaceTexture::getTransformMatrix(float* mat) {
  AVOX_GET_ENV
  jfloatArray jmat = env->NewFloatArray(4 * 4);
  env->CallVoidMethod(surfaceTexture, jmSurfaceTexture.getTransformMatrix,
                      jmat);
  env->GetFloatArrayRegion(jmat, 0, 4 * 4, mat);
  env->DeleteLocalRef(jmat);
}

bool JniSurfaceTexture::attachToGLContext(int32_t texId) {
  AVOX_GET_ENV_RETURN(false)
  if (!surfaceTexture) {
    return false;
  }
  env->CallVoidMethod(surfaceTexture, jmSurfaceTexture.attachToGLContext,
                      texId);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return false;
  }
  return true;
}

bool JniSurfaceTexture::detachFromGLContext() {
  AVOX_GET_ENV_RETURN(false)
  if (!surfaceTexture) {
    return false;
  }
  env->CallVoidMethod(surfaceTexture, jmSurfaceTexture.detachFromGLContext);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return false;
  }
  return true;
}

void JniSurfaceTexture::onFrameAvailable() {
  if (frameAvailableCallback) {
    frameAvailableCallback();
  }
}

void JniSurfaceTexture::setOnFrameAvailableListener(
    FrameAvailableCallback callback) {
  frameAvailableCallback = callback;
  AVOX_GET_ENV
  // 如果没有，则创建一个，否则不需要再次创建了
  if (!surfaceTextureOb && frameAvailableCallback) {
    if (!jmSurfaceTextureOb.surfaceTexureObClass || !jmSurfaceTextureOb.init) {
      LOGFLF(LogLevel::warn, "surfaceTextureObClass or init is nullptr");
      return;
    }
    // 将this指针转换为jlong传递给Java
    jlong nativePtr = reinterpret_cast<jlong>(this);
    jobject jSurfaceTextureOb =
        env->NewObject(jmSurfaceTextureOb.surfaceTexureObClass,
                       jmSurfaceTextureOb.init, nativePtr);
    surfaceTextureOb = env->NewGlobalRef(jSurfaceTextureOb);
    env->DeleteLocalRef(jSurfaceTextureOb);
  }
  env->CallVoidMethod(surfaceTexture,
                      jmSurfaceTexture.setOnFrameAvailableListener,
                      callback ? surfaceTextureOb : nullptr);
}

}
