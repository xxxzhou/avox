#include "SherpaModule.hpp"

#include "SherpaAudioStt.hpp"
#include "SherpaAudioTts.hpp"
#include "avox/audio/AudioStt.hpp"
#include "avox/module/AvoxManager.hpp"

namespace avox {

bool SherpaModule::loadModule(IOption* option) {
  (void)option;
  // 把 SherpaAudioStt 工厂注册到 AvoxManager; SubtitleAsr 走 audioSttHub.create("sherpa") 拿实例。
  // 库已链接(plugin link sherpa-onnx), 模型在 SherpaAudioStt::load() 运行期 init, 失败由调用方降级。
  AvoxManager::Get().audioSttHub.reg(
      "sherpa", []() -> AudioStt* { return new SherpaAudioStt(); });
  // TTS 工厂 (与 STT 同源 sherpa-onnx); 走 audioTtsHub.create("sherpa") 拿实例。
  // 模型在 SherpaAudioTts::start() 运行期 init, 失败由调用方降级。
  AvoxManager::Get().audioTtsHub.reg(
      "sherpa", []() -> AudioTts* { return new SherpaAudioTts(); });
  return true;
}

AVOX_REGISTER_MODULE(SherpaModule, avox_sherpa)

}
