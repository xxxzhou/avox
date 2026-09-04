#include "AvoxManager.hpp"

#include "../AvoxVersion.h"
#include "LogHelper.hpp"

namespace avox {

extern void regDeviceRawSource();
// 静态链接可能忽略注册那种方式(因为注册的类很多没被别的CPP文件调用)
#ifdef AVOX_ENABLE_FAAD2
// 多平台集成FAAD的音频解码方案
extern void regFaadDecoder();
#endif
#ifdef AVOX_ENABLE_FAAC
// 多平台集成FAAC的音频编码方案
extern void regFaacEncoder();
#endif
#ifdef AVOX_ENABLE_FDKAAC
// 多平台集成FDKAAC的音频编码方案
extern void regFdkaacEncoder();
extern void regFdkaacDecoder();
#endif
#ifdef AVOX_ENABLE_VULKAN
// 多平台集成Vulkan渲染方案
extern void regVkRender();
#endif
#ifdef AVOX_ENABLE_ZLMEDIAKIT
// 多平台集成ZLMEDIAKIT的网络协议解析方案
extern void regZmIO();
extern void regZmMuxer();
// HttpTranslator(腾讯云HTTP翻译)工厂注册, SubtitleAsr 走 translatorHub.create("http") 查表
extern void regHttpTranslator();
#endif
#ifdef AVOX_ENABLE_FFMPEG
// 链接FFADecoder.cpp
// 多平台集成FFmpeg的软解/软编方案
extern void regFFCodec();
// 多平台集成FFmpeg的网络协议解析方案
extern void regFFIO();
extern void regFFMuxer();
#if AVOX_ENABLE_VULKAN
// 多平台集成FFmpeg的Vulkan硬解方案
extern void regFFVkDecoder();
#endif
#if _WIN32
// Windows平台集成FFmpeg的DX11VA硬解方案
extern void regFFDx11Decoder();
extern void regFFDx11Encoder();
extern void regDx11Render();
extern void regWinCaptureDevice();
// Windows平台原生WASAPI音频渲染
extern void regWasAudioRender();
// Windows平台原生WASAPI音频采集
extern void regWasAudioDevice();
#endif
#endif
#if _WIN32
// Windows平台Media Foundation相机设备(不依赖ffmpeg)
extern void regWinMfCameraDevice();
#endif
#ifdef __ANDROID__
// Android平台原生音频渲染
extern void regAndATRender();
// Android平台原生硬解MediaCodec
extern void regVDecoderReg();
// Android平台EGL渲染
extern void regEglRender();
// Android音频设备
extern void regAndAudioDevice();
// Android Camera设备
extern void regAndCameraSource();
// Android 编码器
extern void regVEncoderReg();
#endif
#ifdef __APPLE__
// IOS平台原生硬解VideoToolbox
extern void regIOSVDecoder();
// IOS平台原生硬编
extern void regIOSVEncoder();
// IOS平台Metal渲染
extern void regIOSVRender();
// IOS平台相机
extern void regIOSCameraSource();
// IOS平台音频渲染
extern void regIOSAudioRender();
// IOS平台音频设备
extern void regIOSAudioSource();
#endif

AvoxManager* AvoxManager::instance = nullptr;

AvoxManager& AvoxManager::Get() {
  if (instance == nullptr) {
    instance = new AvoxManager();
  }
  return *instance;
}

AvoxManager::AvoxManager(/* args */) {
#ifdef __ANDROID__
  // androidEnv = std::make_unique<AndroidEnv>();
#endif
  log(LogLevel::info, "avox git branch:", AVOX_BRANCH_NAME,
      " commit_hash:", AVOX_COMMIT_HASH, " commit_time:", AVOX_COMMIT_TIME,
      " build_time:", AVOX_BUILD_TIME);
}

AvoxManager::~AvoxManager() {
#ifdef __ANDROID__
  JNIEnv* env = androidEnv.env;
  if (!env) {
    return;
  }
  if (androidEnv.activity) {
    env->DeleteGlobalRef(androidEnv.activity);
    androidEnv.activity = nullptr;
  }
  if (androidEnv.activityClass) {
    env->DeleteGlobalRef(androidEnv.activityClass);
    androidEnv.activityClass = nullptr;
  }
  if (androidEnv.application) {
    env->DeleteGlobalRef(androidEnv.application);
    androidEnv.application = nullptr;
  }
  detachThread();
#endif
  bInit = false;
}

void AvoxManager::init() {
  if (bInit) {
    return;
  }
  regDeviceRawSource();
#ifdef AVOX_ENABLE_FAAD2
  regFaadDecoder();
#endif
#ifdef AVOX_ENABLE_FDKAAC
  regFdkaacEncoder();
  regFdkaacDecoder();
#endif
#ifdef AVOX_ENABLE_FAAC
  regFaacEncoder();
#endif
#ifdef AVOX_ENABLE_VULKAN
  regVkRender();
#endif
#ifdef AVOX_ENABLE_ZLMEDIAKIT
  regZmIO();
  regZmMuxer();
  regHttpTranslator();
#endif
#ifdef AVOX_ENABLE_FFMPEG
  regFFCodec();
  regFFIO();
  regFFMuxer();
#if AVOX_ENABLE_VULKAN
  regFFVkDecoder();
#endif
#if _WIN32
  regFFDx11Decoder();
  regFFDx11Encoder();
  regDx11Render();
  regWinCaptureDevice();
  regWasAudioRender();
  regWasAudioDevice();
#endif
#endif
#if _WIN32
  regWinMfCameraDevice();
#endif
#ifdef __ANDROID__
  regAndATRender();
  regVDecoderReg();
  regEglRender();
  regAndAudioDevice();
  regAndCameraSource();
  regVEncoderReg();
#endif
#ifdef __APPLE__
  regIOSVDecoder();
  regIOSVEncoder();
  regIOSVRender();
  regIOSCameraSource();
  regIOSAudioRender();
  regIOSAudioSource();
#endif
  // 初始化所有注册的模块
  for (const auto& regFunc : initFuncs) {
    if (!regFunc.func) {
      continue;
    }
    log(LogLevel::info, regFunc.desc);
    regFunc.func();
  }
  // 注: 插件扫描(ModuleMgr::startup)改为 lazy —— 首次 create() 时触发,
  // 避免 DllMain(DLL_PROCESS_ATTACH) 里 LoadLibrary 触发 loader lock 死锁
  bInit = true;
}

void AvoxManager::enterBack(bool back) {
  background = back;
  if (back) {
    LOGFLF(LogLevel::info, "app enter background");
  } else {
    LOGFLF(LogLevel::info, "app enter foreground");
  }
}

bool AvoxManager::getBackground() { return background; }


#ifdef __ANDROID__

void AvoxManager::initAndroid(android_app* app_) {
  app = app_;
  AndroidEnv temp = {};
  temp.vm = app->activity->vm;
  temp.assetManager = app->activity->assetManager;
  temp.sdkVersion = app->activity->sdkVersion;
  initAndroid(temp);
}

void AvoxManager::initAndroid(const AndroidEnv& andEnv) {
  log(LogLevel::info,
      "AvoxManager::initAndroid sdk version:", andEnv.sdkVersion);
  androidEnv = andEnv;
  if (androidEnv.sdkVersion == 0) {
    androidEnv.sdkVersion = JNI_VERSION_1_6;
  }
  if (!androidEnv.env) {
    androidEnv.env = getEnv(&bAttach);
  }
  JNIEnv* env = androidEnv.env;
  jobject applicationContext = nullptr;
  jobject active = androidEnv.activity;
  if (active) {
    jclass contextClass = env->FindClass("android/content/Context");
    jmethodID getApplicationContextMethod = env->GetMethodID(
        contextClass, "getApplicationContext", "()Landroid/content/Context;");
    assert(getApplicationContextMethod != 0);
    applicationContext =
        env->CallObjectMethod(active, getApplicationContextMethod);
  } else {
    // 获取Activity Thread的实例对象
    jclass activityThreadCls = env->FindClass("android/app/ActivityThread");
    jmethodID currentActivityThread =
        env->GetStaticMethodID(activityThreadCls, "currentActivityThread",
                               "()Landroid/app/ActivityThread;");
    jobject activityThread =
        env->CallStaticObjectMethod(activityThreadCls, currentActivityThread);
    // 获取Application,也就是全局的Context
    jmethodID getApplication = env->GetMethodID(
        activityThreadCls, "getApplication", "()Landroid/app/Application;");
    applicationContext = env->CallObjectMethod(activityThread, getApplication);
  }
  // 转换为全局引用
  androidEnv.application = env->NewGlobalRef(applicationContext);
  // 删除局部引用
  env->DeleteLocalRef(applicationContext);
  if (androidEnv.application && !androidEnv.assetManager) {
    jmethodID methodGetAssets =
        env->GetMethodID(env->GetObjectClass(androidEnv.application),
                         "getAssets", "()Landroid/content/res/AssetManager;");
    jobject localAssetManager =
        env->CallObjectMethod(androidEnv.application, methodGetAssets);
    jobject globalAssetManager = env->NewGlobalRef(localAssetManager);
    env->DeleteLocalRef(localAssetManager);
    androidEnv.assetManager = AAssetManager_fromJava(env, globalAssetManager);
  }
}

// 要使用jni里的如findcalss/GetStaticMethodID,必需附加到主线程里调用
JNIEnv* AvoxManager::getEnv(bool* bAttach) {
  // AndroidEnv 未接线时(如 Godot GDExtension 集成, 宿主未走 initAndroid),
  // vm 为空: 返回 null 让调用方走非 JNI 分支, 不可解引用空 vm
  if (androidEnv.vm == nullptr) {
    if (bAttach) {
      *bAttach = false;
    }
    return nullptr;
  }
  JNIEnv* threadEnv = nullptr;
  jint ret = androidEnv.vm->GetEnv(reinterpret_cast<void**>(&threadEnv),
                                   androidEnv.sdkVersion);
  if (bAttach) {
    *bAttach = false;
  }
  // 没有附加
  // if (ret == JNI_EDETACHED) {
  if (ret < 0) {
    ret = androidEnv.vm->AttachCurrentThread(&threadEnv, 0);
    if (ret < 0) {
      LOGFLF(avox::LogLevel::warn, "andorid jni attach thread failed");
      return nullptr;
    }
    if (bAttach) {
      *bAttach = true;
    }
    LOGFLF(avox::LogLevel::info, "andorid jni attach thread success");
  }
  return threadEnv;
}

void AvoxManager::detachThread() {
  // AndroidEnv 未接线时 vm 为空, 线程从未 attach, 无需 detach
  // (守卫前: 渲染/工作线程退出路径无条件解引用空 vm → SIGSEGV)
  if (androidEnv.vm == nullptr) {
    return;
  }
  // 直接DetachCurrentThread可能出问题,先检测是否已经附加到线程
  JNIEnv* threadEnv = nullptr;
  jint ret = androidEnv.vm->GetEnv(reinterpret_cast<void**>(&threadEnv),
                                   androidEnv.sdkVersion);
  if (ret >= 0 && threadEnv != nullptr) {
    androidEnv.vm->DetachCurrentThread();
    LOGFLF(avox::LogLevel::info, "andorid jni detach thread success");
  }
}

jobject AvoxManager::getActivityApplication(jobject activity, JNIEnv* env) {
  jmethodID methodGetApplication =
      env->GetMethodID(env->GetObjectClass(androidEnv.activity),
                       "getApplication", "()Landroid/app/Application;");
  jobject application = env->CallObjectMethod(activity, methodGetApplication);
  return application;
}

std::string AvoxManager::getObjClassName(jobject obj, JNIEnv* env) {
  if (env == nullptr) {
    env = androidEnv.env;
  }
  jclass cls = env->GetObjectClass(obj);
  // First get the class object
  jmethodID mid = env->GetMethodID(cls, "getClass", "()Ljava/lang/Class;");
  jobject clsObj = env->CallObjectMethod(androidEnv.activity, mid);

  // Now get the class object's class descriptor
  cls = env->GetObjectClass(clsObj);
  // Find the getName() method on the class object
  mid = env->GetMethodID(cls, "getName", "()Ljava/lang/String;");

  // Call the getName() to get a jstring object back
  jstring strObj = (jstring)env->CallObjectMethod(clsObj, mid);
  const char* str = env->GetStringUTFChars(strObj, NULL);
  // copy
  std::string result = str;
  env->ReleaseStringUTFChars(strObj, str);
  return result;
}
#endif

// AvoxBase.h 出口: 创建数据源探测器 (经 sourceProbeHub 工厂表, avox_torrent
// loadModule 注册 "torrent"; create 内部触发插件懒加载, 未装插件返回 nullptr)
ISourceProbe* createSourceProbe(const char* type) {
  if (type == nullptr || *type == '\0') {
    return nullptr;
  }
  return AvoxManager::Get().sourceProbeHub.create(type);
}

}
