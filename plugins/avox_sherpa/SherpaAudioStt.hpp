#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "SherpaExport.h"
#include "SherpaHelper.hpp"
#include "avox/audio/AudioReshaper.hpp"
#include "avox/audio/AudioStt.hpp"
#include "avox/module/Ringbuffer.hpp"
#include "avox/module/RunTask.hpp"

namespace avox {

// 前向声明
class SherpaRecognizer;
class SherpaSenseVoice;

/**
 * @brief Sherpa-onnx 语音识别统一管理层
 *
 * 统一管理音频队列、任务调度和识别器切换
 * 内部封装 SherpaRecognizer(流式) 和 SherpaSenseVoice(离线) 两个引擎
 */
class SherpaAudioStt : public AudioStt,
                       public AudioReshaper,
                       public RunTask,
                       public ISherpaRecognizerOb {
 private:
  // 识别引擎（按需创建）
  std::unique_ptr<SherpaRecognizer> streamingEngine;
  std::unique_ptr<SherpaSenseVoice> offlineEngine;

  // 音频队列（统一由 SherpaAudioStt 管理）
  RingBuffer<SherpaFrameItem> frameQueue;
  std::atomic<bool> isFlushing{false};

  // 线程安全锁
  std::mutex mutex;

  // PTS 跟踪 - Offline 模式（全局）
  int64_t streamBasePts = 0;       // 流开始 PTS
  int64_t totalStreamSamples = 0;  // 全局累积样本数

  // PTS 跟踪 - Streaming 模式（按段）
  int64_t segmentStartPts = 0;      // 当前段开始 PTS
  int64_t totalSegmentSamples = 0;  // 当前段累积样本数

  std::string cachedHotwords;
  int cachedTrailingSilenceMs = 500;
  int cachedUtteranceLengthMs = 4000;
  ModelLevel cachedModelLevel = ModelLevel::base;

  // 流式状态（用于去重）
  std::string lastPartialText;
  // 累积的最终文本(按段拼接), 用于 flush 最终结果去重叠
  std::string accumulatedFinal;

 public:
  SherpaAudioStt();
  ~SherpaAudioStt() override;

  // ========== AudioStt 纯虚接口实现 ==========
  void setAudioDesc(AudioDesc desc) override;
  void start() override;
  void recognize(const AvoxData& adata, int64_t pts) override;
  void stop() override;
  bool loading() override;
  void onRecognizerChange() override;

  // ========== 配置接口 ==========
  void setHotwords(const char* hotwords);
  void setEndpoint(int trailingSilenceMs, int utteranceLengthMs);
  void flush();
  void reset();

  // ========== ISherpaRecognizerOb 接口实现 ==========
  // 接收识别引擎的回调结果
  void onSherpaResult(const SherpaResult& result) override;

 protected:
  // RunTask 接口 - 音频处理主循环
  void onRunTask() override;

  // AudioReshaper 接口 - 音频格式转换回调
  void onProcess() override;
  // 辅助方法
  bool initEngine();
  void releaseEngine();
  void processFrame(const float* samples, int32_t count, int64_t pts);
};

}
