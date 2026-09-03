#pragma once

#include <string>
#include <vector>

#include "SherpaHelper.hpp"
#include "avox/module/Observer.hpp"

// sherpa-onnx C API 前向声明
typedef struct SherpaOnnxOfflineRecognizer SherpaOnnxOfflineRecognizer;
typedef struct SherpaOnnxVoiceActivityDetector SherpaOnnxVoiceActivityDetector;

namespace avox {

/**
 * @brief Sherpa SenseVoice 离线识别引擎
 *
 * 纯同步识别引擎，不管理线程/队列
 * 通过 Observer 回调识别结果
 * 由 SherpaAudioStt 统一管理层调用
 */
class SherpaSenseVoice : public avox::Observer<ISherpaRecognizerOb> {
 public:
  SherpaSenseVoice();
  virtual ~SherpaSenseVoice();

  // ========== 配置设置 ==========
  void setModelLevel(ModelLevel level);
  void setHotwords(const char* hotwords);
  void setEndpoint(int trailingSilenceMs, int utteranceLengthMs);

  // ========== 模型生命周期 ==========
  void loadModel();
  bool bLoad() const;
  void unloadModel();

  // ========== 同步识别接口 ==========
  // 处理音频数据（16kHz mono float）
  // streamBasePts: 识别流开始时的 PTS 基准
  // 识别结果通过 Observer 回调给订阅者
  void processAudio(const float* samples, int32_t count, bool flushing,
                    int64_t streamBasePts);

  // 重置状态
  void reset();

 private:
  // sherpa-onnx 上下文
  const SherpaOnnxVoiceActivityDetector* vad = nullptr;
  const SherpaOnnxOfflineRecognizer* recognizer = nullptr;

  // 配置
  std::string modelPath;
  // VAD 段尾静音时间 (影视台词间隔短，160ms 即切分)
  int trailingSilenceMs = 200;  
  // 最大说话时长  
  int utteranceLengthMs = 2000;  
  std::string hotwords;
  ModelLevel modelLevel = ModelLevel::base;

  // 内部方法
  void loadVadModel();
  void loadSenseVoiceModel();
  void processSegment(const float* samples, int32_t count, int64_t segmentStartPts);
  // 根据标点切分文本
  std::vector<SherpaSubSentence> splitByPunctuation(
      const char* text,
      const char* const* tokens_arr,
      const float* timestamps,
      const float* durations,
      int32_t count,
      int64_t segmentStartPts);
};

}
