#include "AndCommon.hpp"
#include "JniSurfaceTexture.hpp"
#include "avox/layer/VInputLayer.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/video/ImageBuffer.hpp"
#include "avox/video/Window.hpp"
#include "avox_egl/EglWindow.hpp"
#ifdef AVOX_ENABLE_WEBRTC
#include "sdk/android/native_api/base/init.h"
#endif

namespace avox {

// 给JAVA使用的native方法，方法名需要和JAVA保持一致
extern "C" {

AndroidEnv aenv = {};
static bool isInited = false;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* jvm, void*) {
  __android_log_print(ANDROID_LOG_INFO, "avox", "%s", "avox JNI_OnLoad");
  // 引发静态变量的初始化，注册所有IO,解码器等
  AvoxManager::Get().init();
  aenv.vm = jvm;
  // 初始化EGL环境
  aenv.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (aenv.display == EGL_NO_DISPLAY) {
    __android_log_print(ANDROID_LOG_WARN, "avox", "%s",
                        "avox eglGetDisplay failed");
    return JNI_VERSION_1_6;
  }
  EGLint majorVersion = 0;
  EGLint minorVersion = 0;
  if (!eglInitialize(aenv.display, &majorVersion, &minorVersion)) {
    __android_log_print(ANDROID_LOG_WARN, "avox", "%s",
                        "avox eglInitialize failed");
  }
  __android_log_print(ANDROID_LOG_INFO, "avox", "avox eglInitialize %d.%d",
                      majorVersion, minorVersion);
  __android_log_print(ANDROID_LOG_INFO, "avox", "%s", "avox JNI_OnLoad end");
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* jvm, void*) {
  if (aenv.display != EGL_NO_DISPLAY) {
    eglTerminate(aenv.display);
    __android_log_print(ANDROID_LOG_INFO, "avox", "%s", "avox eglTerminate");
  }
  __android_log_print(ANDROID_LOG_INFO, "avox", "%s", "avox JNI_OnUnload");
}

JNIEXPORT jint JNICALL Java_avox_android_library_JNIHelper_add(JNIEnv* env,
                                                              jclass clazz,
                                                              jint a, jint b) {
  return a + b;
}

JNIEXPORT void JNICALL Java_avox_android_library_JNIHelper_jniSetup(
    JNIEnv* env, jclass clazz, jobject activity) {
  log(LogLevel::info, "jniSetup activity:", activity);
  if (activity) {
    if (aenv.activity) {
      env->DeleteGlobalRef(aenv.activity);
    }
    // 必须将局部引用转换为全局引用
    aenv.activity = env->NewGlobalRef(activity);
    // 获取activity对象的class
    jclass activityClass = env->GetObjectClass(activity);
    // 转换为全局引用
    aenv.activityClass = (jclass)env->NewGlobalRef(activityClass);
    // 删除局部引用
    env->DeleteLocalRef(activityClass);
  }
  // 初始化android相关的资源
  AvoxManager::Get().initAndroid(aenv);
  // 下面内容只需要更新一次
  if (isInited) {
    log(LogLevel::info, "jniSetup already called, skipping...");
    return;
  }
#ifdef AVOX_ENABLE_WEBRTC
  log(LogLevel::info, "start webrtc jni init");
  AndroidEnv genv = AvoxManager::Get().getAppEnv();
  jclass context_utils_class = env->FindClass("org/webrtc/ContextUtils");
  if (context_utils_class) {
    jmethodID init_method = env->GetStaticMethodID(
        context_utils_class, "initialize", "(Landroid/content/Context;)V");
    if (init_method && genv.application) {
      log(LogLevel::info, "jvm:", genv.vm, " context:", genv.application);
      env->CallStaticVoidMethod(context_utils_class, init_method,
                                genv.application);
      if (!env->ExceptionCheck()) {
        // webrtc::InitAndroid(genv.vm) 移至 avox_webrtc 插件 WebrtcModule::loadModule:
        // 核心不引 webrtc 符号, JVM 指针经 AndroidEnv 传给插件自行初始化
        log(LogLevel::info, "webrtc java context ready (native init in plugin)");
      } else {
        log(LogLevel::warn, "ContextUtils.initialize threw an exception");
        env->ExceptionDescribe();
        env->ExceptionClear();
      }
    } else {
      log(LogLevel::warn, "init_method or genv.application is NULL");
    }
  } else {
    log(LogLevel::warn,
        "Could not find org.webrtc.ContextUtils. Check Proguard!");
  }
#endif
  // 注册AudioTrack相关方法
  getAudiotrackFields();
  // 注册AudioRecord相关方法
  getAudioRecordFields();
  // 注册SurfaceTexture相关方法
  getSurfaceTextureFields();
  // 注册Surface相关方法
  getSurfaceFields();
  // 注册AvoxSurfaceTextureOb相关方法
  getSurfaceTextureObFields();
  isInited = true;
  __android_log_print(ANDROID_LOG_INFO, "avox", "%s", "avox jniSetup end");
}

JNIEXPORT jboolean JNICALL Java_avox_android_library_JNIHelper_loadBitmap(
    JNIEnv* env, jclass clazz, jlong inputLayerPtr, jobject bitmap) {
  IVInputLayer* iinputLayer = *(IVInputLayer**)&inputLayerPtr;
  VInputLayer* inputLayer = dynamic_cast<VInputLayer*>(iinputLayer);
  int result;
  // 获取源Bitmap相关信息：宽、高等
  AndroidBitmapInfo sourceInfo = {};
  result = AndroidBitmap_getInfo(env, bitmap, &sourceInfo);
  if (result < 0) {
    log(LogLevel::warn, "android bitmap getInfo error");
    return false;
  }
  // 获取源Bitmap像素数据
  uint8_t* sourceData = nullptr;
  result = AndroidBitmap_lockPixels(env, bitmap, (void**)&sourceData);
  if (result < 0) {
    log(LogLevel::warn, "android bitmap lockPixels error");
    return false;
  }
  ImageType imageType = getBitmapType(sourceInfo.format);
  if (imageType == ImageType::other) {
    log(LogLevel::warn, "android bitmap only support rgba8 or r8");
    return false;
  }
  // AndroidBitmapFormat
  ImageFormat imageFormat = {};
  imageFormat.width = sourceInfo.width;
  imageFormat.height = sourceInfo.height;
  imageFormat.imageType = imageType;
  inputLayer->inputCpuData(sourceData, imageFormat, true);
  AndroidBitmap_unlockPixels(env, bitmap);
  return true;
}

JNIEXPORT void JNICALL Java_avox_android_library_JNIHelper_setRenderSurface(
    JNIEnv* env, jclass clazz, jlong windowRender, jobject surface) {
  ANativeWindow* winSurf = nullptr;
  int32_t width = 0;
  int32_t height = 0;
  if (surface) {
    winSurf = ANativeWindow_fromSurface(env, surface);
    width = ANativeWindow_getWidth(winSurf);
    height = ANativeWindow_getHeight(winSurf);
  }
  ISurfaceRender* wRender = *(ISurfaceRender**)&windowRender;
  wRender->setSurface(winSurf);
}

JNIEXPORT jlong JNICALL Java_avox_android_library_JNIHelper_getNativeSurface(
    JNIEnv* env, jclass clazz, jobject surface) {
  ANativeWindow* winSurf = nullptr;
  int32_t width = 0;
  int32_t height = 0;
  if (surface) {
    winSurf = ANativeWindow_fromSurface(env, surface);
  }
  return reinterpret_cast<jlong>(winSurf);
}

JNIEXPORT void JNICALL
Java_avox_android_library_AvoxSurfaceTextureOb_nativeOnFrameAvailable(
    JNIEnv* env, jclass clazz, jlong texturePtr) {
  JniSurfaceTexture* surfaceTexture =
      reinterpret_cast<JniSurfaceTexture*>(texturePtr);
  if (surfaceTexture) {
    surfaceTexture->onFrameAvailable();
  }
}
}

}