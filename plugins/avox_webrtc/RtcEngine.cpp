#include "RtcEngine.hpp"

#include "api/enable_media.h"
#include "api/environment/environment_factory.h"
#ifdef __ANDROID__
#include "sdk/android/native_api/audio_device_module/audio_device_android.h"
#endif
#ifdef __APPLE__
#include "avox_apple/IOSHelper.h"
#endif
#include "audio/RtcAudioDecoderFactory.hpp"
#include "audio/RtcAudioEncoderFactory.hpp"
#include "avox/module/AvoxManager.hpp"
#include "video/RtcVideoBuffer.hpp"
#include "video/RtcVideoDecoderFactory.hpp"
#include "video/RtcVideoEncoderFactory.hpp"

namespace avox {

using namespace webrtc;

RtcEngine& RtcEngine::Get() {
  // 故意泄漏, 不做静态析构: PCF 析构会同步等 webrtc 线程停止(Event::Wait),
  // 进程退出阶段线程已被 ExitProcess 强杀 → ZwWaitForSingleObject 永挂
  // (窗口模式 Godot 实测复现); 与 Chrome 同策略, 退出时交由 OS 回收。
  static RtcEngine* instance = new RtcEngine();
  return *instance;
}

bool RtcEngine::ensureInitialized() {
  std::lock_guard<std::mutex> lock(mutex);
  // 如果工厂已存在，说明环境已就绪，直接返回
  if (factory) {
    return true;
  }
  LOGFLF(LogLevel::info, "start create peerconnection factory");
  // M138 中 Thread::CreateWithSocketServer 会返回 unique_ptr
  network_thread = webrtc::Thread::CreateWithSocketServer();
  worker_thread = webrtc::Thread::Create();
  signaling_thread = webrtc::Thread::Create();
  if (!network_thread->Start() || !worker_thread->Start() ||
      !signaling_thread->Start()) {
    LOGFLF(LogLevel::warn, "failed to start webrtc network thread");
    return false;
  }
  webrtc::PeerConnectionFactoryDependencies pcf_deps;
  pcf_deps.network_thread = network_thread.get();
  pcf_deps.worker_thread = worker_thread.get();
  pcf_deps.signaling_thread = signaling_thread.get();

  pcf_deps.video_decoder_factory = std::make_unique<RtcVideoDecoderFactory>();
  pcf_deps.video_encoder_factory = std::make_unique<RtcVideoEncoderFactory>();
  pcf_deps.audio_decoder_factory = CreateAvoxAudioDecoderFactory();
  pcf_deps.audio_encoder_factory = CreateAvoxAudioEncoderFactory();
#ifdef __ANDROID__
  // CreateEnvironment 会自动关联全局的 TaskQueue 和 FieldTrials
  webrtc::Environment webrtc_env = webrtc::CreateEnvironment();
  jobject context = AvoxManager::Get().getAppEnv().application;
  JNIEnv* env = AvoxManager::Get().getEnv();
  jobject local_context = env->NewLocalRef(context);
  pcf_deps.adm =
      webrtc::CreateJavaAudioDeviceModule(env, webrtc_env, local_context);
  env->DeleteLocalRef(local_context);
#endif
  // 4. M138 必须显式调用 EnableMedia 才能加载音视频支持
  webrtc::EnableMedia(pcf_deps);
  // 5. 创建模块化工厂
  factory = webrtc::CreateModularPeerConnectionFactory(std::move(pcf_deps));
  if (factory) {
    PeerConnectionFactoryInterface::Options options = {};
    // zlmediakit需要encryption
    // options.disable_encryption = true;    
    factory->SetOptions(options);
    LOGFLF(LogLevel::info, "create peerconnection factory success");
    return true;
  }
  LOGFLF(LogLevel::warn,"create peerconnection factory failed");
  return false;
}

void RtcEngine::uninit() {
  std::lock_guard<std::mutex> lock(mutex);
  // 关键：先清空工厂引用，确保底层对象在线程停止前尝试析构
  factory = nullptr;
  // 依次停止线程
  if (signaling_thread) {
    signaling_thread->Stop();
    signaling_thread.reset();
  }
  if (worker_thread) {
    worker_thread->Stop();
    worker_thread.reset();
  }
  if (network_thread) {
    network_thread->Stop();
    network_thread.reset();
  }
}

}