#include "WebrtcModule.hpp"

#include "RtcParse.hpp"
#include "RtcPlayer.hpp"
#include "audio/RtcAudioProcess.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/source/RawSource.hpp"

namespace avox {

bool WebrtcModule::loadModule(IOption* option) {
  (void)option;
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
