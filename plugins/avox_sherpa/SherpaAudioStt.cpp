#include "SherpaAudioStt.hpp"

#include <chrono>
#include <cstring>

#include "SherpaRecognizer.hpp"
#include "SherpaSenseVoice.hpp"
#include "avox/Avox.hpp"

namespace avox {

SherpaAudioStt::SherpaAudioStt() : frameQueue(100) {
  taskName = "sherpa audio stt task";
}

SherpaAudioStt::~SherpaAudioStt() {
  stop();
  releaseEngine();
}

bool SherpaAudioStt::loading() { return running(); }

void SherpaAudioStt::setAudioDesc(AudioDesc desc) {
  std::lock_guard<std::mutex> lock(mutex);
  // 设置 AudioReshaper 参数
  srcDesc = desc;
  outDesc = desc;
  // sherpa-onnx 需要 16kHz mono float
  if (srcDesc.format != AudioFormat::AVOX_AUDIO_FLT) {
    outDesc.format = AudioFormat::AVOX_AUDIO_FLT;
  }
  if (srcDesc.channels != 1) {
    outDesc.channels = 1;
  }
  if (srcDesc.sampleRate != 16000) {
    outDesc.sampleRate = 16000;
  }
  // 重组成100ms一帧
  frameMs = 100;
  if (!initConfig(srcDesc, outDesc)) {
    LOGFLF(LogLevel::warn, "reshaper init failed");
  }
}

void SherpaAudioStt::start() {
  // 幂等: 已在跑则不重启 (voice 每轮开始调 start, 首轮启动时已 start 加载过模型)
  if (!running()) {
    // 排空上一轮残留: 清帧队列 + reset 识别上下文。
    // 上一轮末尾的音频帧可能因音频管线延迟, 在 stop 的 drain 之后才入队;
    // 若不清, 会被本轮 RunTask 当成本轮音频 → 跨会话识别串话。
    frameQueue.clear();
    reset();
    startTask();
  }
}

void SherpaAudioStt::recognize(const AvoxData& adata, int64_t pts) {
  // 如果没初始化,当前数据不需要
  if (!running()) {
    start();
  }
  if (!resample) {
    return;
  }
  // 使用 AudioReshaper 进行重采样
  AvoxAFrame frame = {};
  frame.buffer = adata;
  frame.pts = pts == 0 ? timeStampMS() : pts;
  AudioReshaper::process(frame);
}

void SherpaAudioStt::stop() { stopTask(); }

void SherpaAudioStt::onRecognizerChange() {
  // 换识别器类型: 停当前任务 + 释放当前 engine (下次 start 时 initEngine 按新 type 重建)
  stop();
  releaseEngine();
}
// ========== RunTask 接口 ==========

void SherpaAudioStt::onRunTask() {
  // 初始化新的引擎
  if (!initEngine()) {
    LOGFLF(LogLevel::warn, "Failed to init recognizer engine");
    return;
  }
  while (running()) {
    // 取一帧处理（每帧 100ms）
    SherpaFrameItem item = {};
    if (frameQueue.dequeue(item)) {
      if (!item.data.empty()) {
        const float* samples = reinterpret_cast<const float*>(item.data.data());
        int32_t sampleCount =
            static_cast<int32_t>(item.data.size() / sizeof(float));
        if (sampleCount > 0) {
          processFrame(samples, sampleCount, item.pts);
        }
      }
    } else {
      // 没有数据，休眠
      sleepTask(false, 10);
    }
  }
  // running()=false (stop/stopTask): 先出 reshaper(swr/curFrame) 残余尾帧入队,
  // 再排空队列 + 触发最终结果 + reset stream。
  // 不 releaseEngine — 模型对象级常驻, 下次 start 复用 (避免每轮重载秒级模型)
  // 此时 AudioTap 已被 closeTap join, recognize/process 不会再并发动 curFrame/swr, flush 安全。
  flush();
  auto drainDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < drainDeadline) {
    SherpaFrameItem item = {};
    if (!frameQueue.dequeue(item)) {
      // 二次 dequeue: 捕获 VoiceTapOb::onFrame 读 recording 后被置 false 的 TOCTOU 残留帧
      sleepTask(false, 20);
      if (!frameQueue.dequeue(item)) break;
    }
    if (!item.data.empty()) {
      const float* samples = reinterpret_cast<const float*>(item.data.data());
      int32_t sampleCount =
          static_cast<int32_t>(item.data.size() / sizeof(float));
      if (sampleCount > 0) processFrame(samples, sampleCount, item.pts);
    }
  }
  // 触发最终结果: 空音频 flushing → sherpa tail decode
  // (最后一段未到端点的音频出 final result)
  isFlushing = true;
  float eosDummy = 0.f;
  processFrame(&eosDummy, 0, timeStampMS());
  // 清队列 + reset stream 状态 (下次 start 复用干净)
  frameQueue.clear();
  std::lock_guard<std::mutex> lock(mutex);
  lastPartialText.clear();
  accumulatedFinal.clear();
  streamBasePts = 0;
  totalStreamSamples = 0;
  segmentStartPts = 0;
  totalSegmentSamples = 0;
  if (streamingEngine) streamingEngine->reset();
  if (offlineEngine) offlineEngine->reset();
}

// ========== 配置接口 ==========
void SherpaAudioStt::setHotwords(const char* hotwords_) {
  std::lock_guard<std::mutex> lock(mutex);
  cachedHotwords = hotwords_ ? hotwords_ : "";
  if (streamingEngine) streamingEngine->setHotwords(cachedHotwords.c_str());
  if (offlineEngine) offlineEngine->setHotwords(cachedHotwords.c_str());
}

void SherpaAudioStt::setEndpoint(int trailingSilenceMs_,
                                 int utteranceLengthMs_) {
  std::lock_guard<std::mutex> lock(mutex);
  cachedTrailingSilenceMs = trailingSilenceMs_;
  cachedUtteranceLengthMs = utteranceLengthMs_;
  if (streamingEngine)
    streamingEngine->setEndpoint(trailingSilenceMs_, utteranceLengthMs_);
  if (offlineEngine)
    offlineEngine->setEndpoint(trailingSilenceMs_, utteranceLengthMs_);
}

void SherpaAudioStt::flush() { isFlushing = true; }

void SherpaAudioStt::reset() {
  std::lock_guard<std::mutex> lock(mutex);
  lastPartialText.clear();
  accumulatedFinal.clear();
  // Offline 模式
  streamBasePts = 0;
  totalStreamSamples = 0;
  // Streaming 模式
  segmentStartPts = 0;
  totalSegmentSamples = 0;
  if (streamingEngine) streamingEngine->reset();
  if (offlineEngine) offlineEngine->reset();
}

// ========== ISherpaRecognizerOb 接口实现 ==========

static Language parseLanguage(const char* lang) {
  // LOGFLF(LogLevel::info, "parseLanguage: ", lang ? lang : "null");
  if (!lang) return Language::none;
  std::string langStr = lang;
  if (langStr == "zh" || langStr == "zh_CN" || langStr == "zh-CN" ||
      langStr == "Chinese" || langStr == "<|zh|>") {
    return Language::zh;
  }
  if (langStr == "en" || langStr == "en_US" || langStr == "en-US" ||
      langStr == "English" || langStr == "<|en|>") {
    return Language::en;
  }
  if (langStr == "ja" || langStr == "jp" || langStr == "ja-JP" ||
      langStr == "ja_JP" || langStr == "Japanese" || langStr == "<|ja|>") {
    return Language::ja;
  }
  return Language::other;
}

// 去重叠: 若 incoming 的前缀 == accumulated 的后缀(端点 reset 保留的 context 被
// flush 再吐), 截掉该前缀; 否则原样返回。仅对 flush(非端点)最终结果用。
static std::string trimOverlap(const std::string& accumulated,
                               const std::string& incoming) {
  if (accumulated.empty() || incoming.empty()) {
    return incoming;
  }
  size_t maxLen = std::min(accumulated.size(), incoming.size());
  for (size_t len = maxLen; len > 0; --len) {
    if (accumulated.compare(accumulated.size() - len, len, incoming, 0, len) ==
        0) {
      return incoming.substr(len);
    }
  }
  return incoming;
}

void SherpaAudioStt::onSherpaResult(const SherpaResult& result) {
  if (!result.text || strlen(result.text) == 0) {
    return;
  }
  // 流式结果去重
  if (!result.isFinal) {
    if (lastPartialText == result.text) {
      return;
    }
    lastPartialText = result.text;
  } else {
    lastPartialText.clear();
  }
  SttResult sttResult = {};
  sttResult.startPts = result.startPts;
  sttResult.endPts = result.endPts;
  sttResult.lang = parseLanguage(result.lang);
  sttResult.isFinal = result.isFinal;
  if (result.isFinal) {
    // flush(非端点)最终结果去重叠: 端点的 OnlineStreamReset 保留 encoder/context,
    // stop 的 flush(InputFinished) 会把上一段末尾(乃至整段)再吐一次 → 结束时复制。
    // 仅对非端点的最终结果(即 flush)按 accumulatedFinal 后缀裁掉重叠前缀;
    // 端点分段是正常新段, 不裁(保留用户 legitimately 重复的内容)。
    std::string text = result.text;
    if (!result.isEndpoint) {
      text = trimOverlap(accumulatedFinal, text);
    }
    if (!text.empty()) {
      accumulatedFinal += text;
      dispatch(&IAudioSttOb::onResult, sttResult, text.c_str());
    }
    // Streaming 模式：端点后重置段 PTS
    if (result.isEndpoint) {
      // 新段从当前段的结束位置开始
      segmentStartPts = result.endPts;
      totalSegmentSamples = 0;
    }
  } else {
    dispatch(&IAudioSttOb::onPartialResult, result.text);
  }
}

// ========== AudioReshaper 接口 ==========

void SherpaAudioStt::onProcess() {
  // AudioReshaper 处理后的数据，深拷贝入队
  uint8_t* data = curFrame.point();
  int32_t size = curFrame.getSize();
  if (size <= 0) {
    return;
  }
  SherpaFrameItem item = {};
  item.data.assign(data, data + size);
  item.pts = curFrame.getPts();
  frameQueue.enqueue(item, true);
}

// ========== 内部方法 ==========

bool SherpaAudioStt::initEngine() {
  std::lock_guard<std::mutex> lock(mutex);
  if (currentType == RecognizerType::streaming) {
    if (!streamingEngine) {
      streamingEngine = std::make_unique<SherpaRecognizer>();
      streamingEngine->setModelLevel(cachedModelLevel);
      streamingEngine->setHotwords(cachedHotwords.c_str());
      streamingEngine->setEndpoint(1200, 10000);
      // 订阅识别结果
      streamingEngine->setObserver(this);
      // 仅首次加载; 后续 start 复用 (对象级常驻, 不每轮重载秒级模型)
      streamingEngine->loadModel();
    }
    return streamingEngine->bLoad();
  }
  if (currentType == RecognizerType::offline) {
    if (!offlineEngine) {
      offlineEngine = std::make_unique<SherpaSenseVoice>();
      offlineEngine->setModelLevel(cachedModelLevel);
      offlineEngine->setHotwords(cachedHotwords.c_str());
      offlineEngine->setEndpoint(200, 2000);
      // 订阅识别结果
      offlineEngine->setObserver(this);
      offlineEngine->loadModel();
    }
    return offlineEngine->bLoad();
  }
  return false;
}

void SherpaAudioStt::releaseEngine() {
  std::lock_guard<std::mutex> lock(mutex);
  if (streamingEngine) {
    streamingEngine->removeObserver(this);
    streamingEngine->unloadModel();
    streamingEngine.reset();
  }
  if (offlineEngine) {
    offlineEngine->removeObserver(this);
    offlineEngine->unloadModel();
    offlineEngine.reset();
  }
}

void SherpaAudioStt::processFrame(const float* samples, int32_t count,
                                  int64_t pts) {
  std::lock_guard<std::mutex> lock(mutex);
  // ========== Offline 模式 PTS 跟踪 ==========
  if (streamBasePts == 0) {
    streamBasePts = pts;
  } else if (totalStreamSamples > 0) {
    // 检测 PTS 跳变（误差超过 500ms）
    int64_t expectedPts = streamBasePts + totalStreamSamples * 1000LL / 16000;
    int64_t diff = pts - expectedPts;
    if (diff > 500 || diff < -500) {
      LOGFLF(LogLevel::info,
             "PTS jump detected, recalibrate: expected=", expectedPts,
             " actual=", pts, " diff=", diff);
      streamBasePts = pts - totalStreamSamples * 1000LL / 16000;
    }
  }
  totalStreamSamples += count;
  // ========== Streaming 模式 PTS 跟踪 ==========
  if (segmentStartPts == 0) {
    segmentStartPts = pts;
  }
  totalSegmentSamples += count;
  if (streamingEngine) {
    // 流式识别：传入当前段开始 PTS
    streamingEngine->processAudio(samples, count, isFlushing.load(),
                                  segmentStartPts);
  }
  if (offlineEngine) {
    // 离线识别：传入流开始 PTS
    offlineEngine->processAudio(samples, count, isFlushing.load(),
                                streamBasePts);
  }
  isFlushing = false;
}

}
