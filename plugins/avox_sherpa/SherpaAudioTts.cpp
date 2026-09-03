#include "SherpaAudioTts.hpp"

#include <cstdint>
#include <vector>

#include "SherpaTts.hpp"
#include "avox/Avox.hpp"

namespace avox {

SherpaAudioTts::SherpaAudioTts() : textQueue(64) {
  taskName = "sherpa audio tts task";
}

SherpaAudioTts::~SherpaAudioTts() {
  stop();
  releaseEngine();
}

// ========== AudioTts 接口 ==========

bool SherpaAudioTts::loading() { return running(); }

void SherpaAudioTts::setAudioDesc(AudioDesc desc) {
  std::lock_guard<std::mutex> lock(mutex);
  outDesc = desc;
  // Phase 1: 固定输出 s16 mono @ sherpa 模型原生采样率;
  // 用户 outDesc.sampleRate 的重采样留后续 (AudioReshaper 复用)。
}

void SherpaAudioTts::start() {
  // 幂等: 已在跑不重启; 清上一轮残留队列 + 中断标志
  if (!running()) {
    textQueue.clear();
    cancelFlag = false;
    startTask();
  }
}

void SherpaAudioTts::synthesize(const char* text) {
  if (!text || !text[0]) {
    return;
  }
  if (!running()) {
    start();
  }
  textQueue.enqueue(std::string(text), true);
}

void SherpaAudioTts::stop() {
  // 先置 cancelFlag: 正在跑的 generate 其 callback 见之返回 0, sherpa 提前停;
  // 再清队列 + stopTask(join worker)。保证 stop 不被长合成卡住。
  cancelFlag = true;
  textQueue.clear();
  stopTask();
  cancelFlag = false;
}

// ========== ISherpaTtsOb 接口 ==========

void SherpaAudioTts::onSherpaTtsDesc(int32_t sampleRate,
                                     int32_t /*numSpeakers*/) {
  ttsSampleRate = sampleRate;
  AudioDesc desc = {};
  desc.channels = 1;
  desc.format = AudioFormat::AVOX_AUDIO_S16;
  desc.sampleRate = sampleRate;
  dispatch(&IAudioTtsOb::onTtsDesc, desc);
}

void SherpaAudioTts::onSherpaTtsAudio(const SherpaTtsChunk& chunk) {
  // SherpaTts worker 线程回调 -> float->s16 打包 -> dispatch 对外
  emitChunk(chunk);
}

// ========== RunTask ==========

void SherpaAudioTts::onRunTask() {
  if (!initEngine()) {
    LOGFLF(LogLevel::warn, "SherpaAudioTts init engine failed (model missing?)");
    dispatch(&IAudioTtsOb::onTtsError, "tts model load failed");
    return;
  }
  while (running()) {
    std::string text;
    if (textQueue.dequeue(text) && !text.empty()) {
      cancelFlag = false;  // 新句合成前清中断标志
      // speed/sid 来自 AudioTts 基类 (setSpeed/setSpeaker 赋值)
      engine->generate(text.c_str(), speed, sid, &cancelFlag);
    } else {
      sleepTask(false, 10);
    }
  }
  // 模型对象级常驻: 不 releaseEngine (下次 start 复用, 避免秒级重载)
}

// ========== 内部方法 ==========

bool SherpaAudioTts::initEngine() {
  std::lock_guard<std::mutex> lock(mutex);
  if (!engine) {
    engine = std::make_unique<SherpaTts>();
    engine->setModelLevel(modelLevel);
    engine->setObserver(this);  // 订阅 SherpaTts 回调 (desc + audio)
    engine->loadModel();
  }
  return engine->bLoad();
}

void SherpaAudioTts::releaseEngine() {
  std::lock_guard<std::mutex> lock(mutex);
  if (engine) {
    engine->removeObserver(this);
    engine->unloadModel();
    engine.reset();
  }
}

void SherpaAudioTts::emitChunk(const SherpaTtsChunk& chunk) {
  size_t numSamples = chunk.data.size() / sizeof(float);
  if (numSamples == 0 && !chunk.final) {
    return;
  }
  // sherpa float [-1,1] mono -> s16 interleaved (通用播放格式)
  std::vector<int16_t> s16(numSamples);
  if (numSamples > 0) {
    const float* src = reinterpret_cast<const float*>(chunk.data.data());
    for (size_t i = 0; i < numSamples; ++i) {
      float v = src[i];
      if (v > 1.0f) {
        v = 1.0f;
      } else if (v < -1.0f) {
        v = -1.0f;
      }
      s16[i] = static_cast<int16_t>(v * 32767.0f);
    }
  }
  // bRef=true 指向本栈 s16; dispatch 同步, 回调内 (TtsNode ob 深拷贝) 有效
  AvoxData raw = {};
  raw.data = reinterpret_cast<uint8_t*>(s16.data());
  raw.size = static_cast<int32_t>(numSamples * sizeof(int16_t));
  raw.bRef = true;
  dispatch(&IAudioTtsOb::onTtsAudio, raw, chunk.pts, chunk.final);
}

}
