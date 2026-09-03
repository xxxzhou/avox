#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if AVOX_ENABLE_TRANSLATION
#include "avox/module/LogHelper.hpp"
#include "avox/vision/IONNXSession.hpp"
#include "avox/vision/OnnxModelUser.hpp"
#include "sentencepiece_processor.h"
#endif

#include "avox/subtitle/BaseTranslator.hpp"

namespace avox {

#if AVOX_ENABLE_TRANSLATION
/**
 * @brief ONNX Runtime 翻译器
 *
 * 使用 ONNX Runtime 加载 opus-mt-ja-zh MarianMT 模型进行翻译
 * 模型架构: encoder-decoder 分离
 */
class OnnxTranslator : public BaseTranslator, public OnnxModelUser {
 public:
  OnnxTranslator();
  ~OnnxTranslator();

 private:
  // ONNX sessions (encoder + decoder); 借用自 OnnxSessionCache(Shared), 对象不释放
  IONNXSession* encoderSession = nullptr;
  IONNXSession* decoderSession = nullptr;

  // Tokenizers (source for encoding input, target for decoding output)
  std::unique_ptr<sentencepiece::SentencePieceProcessor> sourceSpm;
  std::unique_ptr<sentencepiece::SentencePieceProcessor> targetSpm;

  // 配置
  bool loaded = false;
  bool useGPU = false;
  int gpuDeviceIndex = 0;
  std::string modelDir;

  // 特殊 token IDs
  int32_t padTokenId = 0;
  int32_t eosTokenId = 0;
  int32_t bosTokenId = 0;

  // Beam search 参数
  int numBeams = 4;
  float lengthPenalty = 0.6f;

 public:
  // 打开 (经缓存取 session + 加载 tokenizer; 幂等)
  bool open() override;
  void close() override;

  // 设置是否使用 GPU（ONNX 特有）
  void setUseGPU(bool useGPU, int deviceIndex = 0);

  // 翻译文本
  const char* translate(const char* text) override;
  bool ready() const override;

 private:
  // Tokenizer 辅助
  bool loadTokenizer(const std::string& modelDir);
  std::vector<int32_t> encodeText(const std::string& text);
  std::string decodeTokens(const std::vector<int32_t>& tokens);

  // ONNX 推理
  std::vector<float> runEncoder(const std::vector<int32_t>& inputIds);
  std::vector<float> runDecoderStepLogits(
      const std::vector<int32_t>& decoderInputIds,
      const std::vector<float>& encoderHiddenStates,
      const std::vector<int64_t>& encoderShape);

  // 生成翻译
  std::vector<int32_t> generateTranslation(
      const std::vector<int32_t>& inputIds);
};

#endif  // AVOX_ENABLE_TRANSLATION

}
