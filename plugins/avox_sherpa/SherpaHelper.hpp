#pragma once

#include "SherpaExport.h"
#include "avox/AvoxLayer.h"
#include "avox/audio/AudioStt.hpp"
#include "avox/audio/AudioTts.hpp"

namespace avox {

// 音频帧存储结构（深拷贝，用于异步处理）
struct SherpaFrameItem {
  std::vector<uint8_t> data;
  int64_t pts = 0;
};

// 识别结果结构体
struct SherpaResult {
  const char* text = nullptr;
  const char* lang = nullptr;
  bool isFinal = false;
  bool isEndpoint = false;
  int64_t startPts = 0;
  int64_t endPts = 0;
};

// 切分后的子句结果
struct SherpaSubSentence {
  std::string text;      // 子句文本（包含标点）
  int64_t startPts = 0;  // 子句开始 PTS (毫秒)
  int64_t endPts = 0;    // 子句结束 PTS (毫秒)
};

// Sherpa 识别结果回调接口
class ISherpaRecognizerOb {
 public:
  virtual ~ISherpaRecognizerOb() = default;
  virtual void onSherpaResult(const SherpaResult& result) = 0;
};

// Sherpa TTS 合成 PCM 块 (深拷贝, 异步处理用); float interleaved mono, [-1,1]
struct SherpaTtsChunk {
  std::vector<uint8_t> data;
  int64_t pts = 0;   // 毫秒
  bool final = false;
};

// Sherpa TTS 回调接口 (SherpaTts -> SherpaAudioTts)
class ISherpaTtsOb {
 public:
  virtual ~ISherpaTtsOb() = default;
  // 模型加载就绪: 输出采样率 + 说话人数 (合成前一次)
  virtual void onSherpaTtsDesc(int32_t sampleRate, int32_t numSpeakers) {}
  // 增量 PCM 块 (final=true 表示本次合成结束块)
  virtual void onSherpaTtsAudio(const SherpaTtsChunk& chunk) {}
  // 合成失败
  virtual void onSherpaTtsError(const char* err) {}
};

// Sherpa-onnx 工厂函数
extern "C" {
AVOX_EXPORT AudioStt* createAudioSttSherpa();
AVOX_EXPORT AudioTts* createAudioTtsSherpa();
}

}
