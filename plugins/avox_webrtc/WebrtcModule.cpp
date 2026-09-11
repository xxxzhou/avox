#include "WebrtcModule.hpp"

#include "RtcParse.hpp"
#include "RtcPlayer.hpp"
#include "audio/RtcAudioProcess.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/source/RawSource.hpp"
#ifdef __ANDROID__
#include "sdk/android/native_api/base/init.h"
#endif

namespace avox {

bool WebrtcModule::loadModule(IOption* option) {
  (void)option;
#ifdef __ANDROID__
  // android 插件模型: JVM 初始化随插件走 (核心 AndHelper 只做 ContextUtils.initialize),
  // vm 由 jniSetup 经 AndroidEnv 存入 AvoxManager, 此处必然已就绪
  {
    AndroidEnv genv = AvoxManager::Get().getAppEnv();
    if (genv.vm) {
      // InitAndroid 内部 CHECK(vm->GetEnv): 调用线程必须先 attach (loadModule 在
      // 普通后台线程), 否则 EDETACHED 直接 abort (真机实测)
      JNIEnv* env = nullptr;
      genv.vm->AttachCurrentThread(&env, nullptr);
      webrtc::InitAndroid(genv.vm);
      log(LogLevel::info, "webrtc InitAndroid done (plugin)");
    } else {
      log(LogLevel::warn, "webrtc InitAndroid skipped: no jvm (console?)");
    }
  }
#endif
#ifdef __ANDROID__
  // android 插件模型: JVM 初始化随插件走 (核心 AndHelper 只做 ContextUtils.initialize),
  // vm 由 jniSetup 经 AndroidEnv 存入 AvoxManager, 此处必然已就绪
  {
    AndroidEnv genv = AvoxManager::Get().getAppEnv();
    if (genv.vm) {
      // InitAndroid 内部 CHECK(vm->GetEnv): 调用线程必须先 attach (loadModule 在
      // 普通后台线程), 否则 EDETACHED 直接 abort (真机实测)
      JNIEnv* env = nullptr;
      genv.vm->AttachCurrentThread(&env, nullptr);
      webrtc::InitAndroid(genv.vm);
      log(LogLevel::info, "webrtc InitAndroid done (plugin)");
    } else {
      log(LogLevel::warn, "webrtc InitAndroid skipped: no jvm (console?)");
    }
  }
#endif
  // RtcPlayer 工厂: createWebRtcPlayer() 走 AvoxManager::rtcPlayerHub.create("webrtc") 查表
  AvoxManager::Get().rtcPlayerHub.reg(
      "webrtc", []() -> IRtcPlayer* { return new RtcPlayer(); });
  // AudioProcess 工厂: createWebRtcAudioProcess() 走 AvoxManager::audioProcessHub.create("webrtc") 查表
  AvoxManager::Get().audioProcessHub.reg(
      "webrtc", []() -> AudioProcess* { return new RtcAudioProcess(); });
  // RawSource 注册: WebRTC 源通过 AvoxManager::rawSources 查表
  RawSourceDesc rawSource = {};
  rawSource.name = "webrtc";
  AvoxManager::Get().rawSources.regInitFunc(
      RawSourceType::WebRTC, rawSource,
      []() -> RawSource* { return new RtcParse(); });
  return true;
}

AVOX_REGISTER_MODULE(WebrtcModule, avox_webrtc)

}
