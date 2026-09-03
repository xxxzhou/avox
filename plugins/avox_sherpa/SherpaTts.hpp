#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "SherpaHelper.hpp"
#include "avox/AvoxAudio.h"
#include "avox/module/Observer.hpp"

// sherpa-onnx C API 前向声明
typedef struct SherpaOnnxOfflineTts SherpaOnnxOfflineTts;

namespace avox {

/**
 * @brief Sherpa-onnx 离线 TTS 引擎 (对称 SherpaRecognizer)
 *
 * 纯同步合成引擎, 不管理线程/队列; 由 SherpaAudioTts 在 RunTask 线程调用。
 * 模型对象级常驻 (首次 loadModel, 跨句复用, 避免每句重载)。
 * generate 用 sherpa progress callback 增量投递 PCM 块 (句级流式);
 * cancelFlag 控制 barge-in 中断 (callback 返回 0 提前停止)。
 */
class SherpaTts : public avox::Observer<ISherpaTtsOb> {
 public:
  SherpaTts();
  virtual ~SherpaTts();

  // ========== 配置 ==========
  void setModelLevel(ModelLevel level);

  // ========== 模型生命周期 ==========
  void loadModel();
  bool bLoad() const;
  void unloadModel();

  // ========== 同步合成接口 ==========
  // 合成一段文本; PCM 块经 Observer 增量回调 (onSherpaTtsAudio);
  // cancelFlag 非 null 且 *flag=true 时中断合成 (barge-in)
  void generate(const char* text, float speed, int sid,
                const std::atomic<bool>* cancelFlag);
  int32_t sampleRate() const;
  int32_t numSpeakers() const;

 private:
  // sherpa progress callback (静态, arg=this): 增量投递 PCM 块; 返回 0 中断
  static int32_t progressCb(const float* samples, int32_t n, float p,
                            void* arg);
  const SherpaOnnxOfflineTts* tts = nullptr;
  ModelLevel modelLevel = ModelLevel::base;
  int32_t cachedSampleRate = 0;
  int32_t cachedNumSpeakers = 0;
  // generate 期间的临时状态 (仅在 worker 线程单次合成内有效)
  const std::atomic<bool>* curCancel = nullptr;
  int64_t totalSamples = 0;
};

}
