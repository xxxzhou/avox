#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "SherpaExport.h"
#include "SherpaHelper.hpp"
#include "avox/audio/AudioTts.hpp"
#include "avox/module/Ringbuffer.hpp"
#include "avox/module/RunTask.hpp"

namespace avox {

// 前向声明
class SherpaTts;

/**
 * @brief Sherpa-onnx TTS 统一管理层 (对称 SherpaAudioStt)
 *
 * 继承 AudioTts (对外接口) + RunTask (worker 合成) + ISherpaTtsOb (SherpaTts 回调)。
 * synthesize 文本入队 -> RunTask worker 取出 -> SherpaTts::generate (sherpa progress
 * callback 增量产 PCM) -> onSherpaTtsAudio -> float->s16 打包 -> dispatch 对外 onTtsAudio。
 * stop() 置 cancelFlag: generate 的 callback 见之返回 0, sherpa 提前停 (barge-in)。
 * 模型对象级常驻 (首次 initEngine, 跨句/跨轮复用, 不每句重载)。
 */
class SherpaAudioTts : public AudioTts,
                      public RunTask,
                      public ISherpaTtsOb {
 public:
  SherpaAudioTts();
  ~SherpaAudioTts() override;

  // ========== AudioTts 接口 ==========
  void setAudioDesc(AudioDesc desc) override;
  void start() override;
  void synthesize(const char* text) override;
  void stop() override;
  bool loading() override;

  // ========== ISherpaTtsOb 接口 ==========
  void onSherpaTtsDesc(int32_t sampleRate, int32_t numSpeakers) override;
  void onSherpaTtsAudio(const SherpaTtsChunk& chunk) override;

 protected:
  // RunTask - 合成主循环
  void onRunTask() override;

 private:
  bool initEngine();
  void releaseEngine();
  // sherpa float PCM -> s16 + dispatch 对外 onTtsAudio
  void emitChunk(const SherpaTtsChunk& chunk);
  std::unique_ptr<SherpaTts> engine;
  RingBuffer<std::string> textQueue;
  std::mutex mutex;
  std::atomic<bool> cancelFlag{false};  // stop() 置 -> generate callback 中断
  AudioDesc outDesc;                     // 用户期望输出 (Phase 1 固定 s16@模型采样率)
  int32_t ttsSampleRate = 0;
};

}
