#pragma once

#include <string>

#include "SherpaHelper.hpp"
#include "avox/module/Observer.hpp"

// sherpa-onnx C API 前向声明
typedef struct SherpaOnnxOnlineRecognizer SherpaOnnxOnlineRecognizer;
typedef struct SherpaOnnxOnlineStream SherpaOnnxOnlineStream;

namespace avox {

/**
 * @brief Sherpa-onnx 流式语音识别引擎
 *
 * 纯同步识别引擎，不管理线程/队列
 * 通过 Observer 回调识别结果
 * 由 SherpaAudioStt 统一管理层调用
 */
class SherpaRecognizer : public avox::Observer<ISherpaRecognizerOb> {
 public:
  SherpaRecognizer();
  virtual ~SherpaRecognizer();

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
  const SherpaOnnxOnlineRecognizer* recognizer = nullptr;
  const SherpaOnnxOnlineStream* stream = nullptr;

  // 配置
  std::string modelPath;
  int trailingSilenceMs = 600;    // 端点检测：尾部静音时间
  int utteranceLengthMs = 10000;  // 端点检测：最大说话时长
  std::string hotwords;
  ModelLevel modelLevel = ModelLevel::base;

  // 流式状态
  std::string lastText;  // 用于去重
  int64_t totalSamples = 0;  // 累积的样本数，用于计算时间戳
};

}
