#pragma once

#include "Subtitle.hpp"
#include "../audio/AudioStt.hpp"
#include "../module/RunTask.hpp"
#include "../module/Ringbuffer.hpp"
#include "../AvoxPlayer.h"
#include <memory>
#include <vector>
#include <mutex>

namespace avox {

// 识别结果+文本（队列深拷贝用，内部类型）
struct SttTextResult {
  SttResult result = {};
  std::string text;
};

// 继承 RunTask，独立线程处理：
// - ptsSync 模式：从 resultQueue 取识别结果，存入字幕队列
// - streaming 模式：不使用线程，直接识别显示
class SubtitleAsr : public IAudioSttOb, public RunTask {
 public:
  SubtitleAsr();
  ~SubtitleAsr();

 private:
  std::unique_ptr<AudioStt> audioStt;
  // 识别结果队列（ptsSync 模式入队）
  RingBuffer<SttTextResult> resultQueue{50};
  // 流式模式临时文本
  std::string streamingText;
  // PTS 同步模式字幕队列
  std::vector<SubtitleItem> subtitleQueue;
  mutable std::mutex queueMutex;
  AsrMode asrMode = AsrMode::streaming;
  AudioDesc audioDesc = {};

 protected:
  // RunTask 接口
  void onRunTask() override;

 public:
  AudioStt* getAudioStt();
  void loadAsr();
  void unloadAsr();
  void setAsrMode(AsrMode mode);
  AsrMode getAsrMode() const;
  const char* getStreamingText();
  const SubtitleItem* getCurrent(int64_t ptsMs);
  void inputSpeech(const AvoxData& data, int64_t pts);
  void setAudioDesc(AudioDesc desc);

  // IAudioSttOb
  void onResult(const SttResult& result, const char* text) override;
  void onPartialResult(const char* text) override;
  void onEndpoint() override;

 private:
  void processResult(const SttTextResult& item);
};

}