#include "SherpaRecognizer.hpp"

#include <algorithm>
#include <cstring>

#include "avox/Avox.hpp"
#include "avox/module/AssetLoader.hpp"
#include "avox/module/AvoxManager.hpp"

// sherpa-onnx C API
#include "sherpa-onnx/c-api/c-api.h"

namespace avox {

SherpaRecognizer::SherpaRecognizer() = default;

SherpaRecognizer::~SherpaRecognizer() { unloadModel(); }

// ========== 配置设置 ==========

void SherpaRecognizer::setModelLevel(ModelLevel level) { modelLevel = level; }

void SherpaRecognizer::setHotwords(const char* hotwords_) {
  hotwords = hotwords_ ? hotwords_ : "";
}

void SherpaRecognizer::setEndpoint(int trailingSilenceMs_, int utteranceLengthMs_) {
  trailingSilenceMs = trailingSilenceMs_;
  utteranceLengthMs = utteranceLengthMs_;
}

// ========== 模型生命周期 ==========

bool SherpaRecognizer::bLoad() const {
  return recognizer != nullptr && stream != nullptr;
}

void SherpaRecognizer::loadModel() {
  // 只支持中英文流式模型
  std::string selectedModel = "stt/zh-en";
  std::string encoderFile = "encoder-epoch-99-avg-1.int8.onnx";
  std::string decoderFile = "decoder-epoch-99-avg-1.onnx";
  std::string joinerFile = "joiner-epoch-99-avg-1.int8.onnx";

  std::string modelPath = getModelFilePath(selectedModel.c_str());
  LOGFLF(LogLevel::info, "load model path: ", modelPath.c_str());

  // 使用 AssetLoader 加载 tokens (支持 Android assets)
  std::string tokensAssetPath = "models/" + selectedModel + "/tokens.txt";
  auto tokensData = AssetLoader::loadToMemory(tokensAssetPath.c_str());
  if (tokensData.empty()) {
    LOGFLF(LogLevel::warn, "Failed to load tokens: ", tokensAssetPath);
    return;
  }
  tokensData.push_back('\0');

  // 构建模型配置
  SherpaOnnxOnlineRecognizerConfig config;
  memset(&config, 0, sizeof(config));
  config.feat_config.sample_rate = 16000;
  config.feat_config.feature_dim = 80;

#ifdef __ANDROID__
  // Android: 把模型从 assets 复制到缓存目录
  std::string encoderAsset = "models/" + selectedModel + "/" + encoderFile;
  std::string decoderAsset = "models/" + selectedModel + "/" + decoderFile;
  std::string joinerAsset = "models/" + selectedModel + "/" + joinerFile;

  std::string encoderPath =
      AssetLoader::copyAssetToCache(encoderAsset.c_str(), "sherpa_models");
  std::string decoderPath =
      AssetLoader::copyAssetToCache(decoderAsset.c_str(), "sherpa_models");
  std::string joinerPath =
      AssetLoader::copyAssetToCache(joinerAsset.c_str(), "sherpa_models");

  if (encoderPath.empty() || decoderPath.empty() || joinerPath.empty()) {
    LOGFLF(LogLevel::warn, "Failed to copy model files to cache: ",
           AssetLoader::getLastError());
    return;
  }
#else
  std::string encoderPath = modelPath + "/" + encoderFile;
  std::string decoderPath = modelPath + "/" + decoderFile;
  std::string joinerPath = modelPath + "/" + joinerFile;
#endif

  config.model_config.transducer.encoder = encoderPath.c_str();
  config.model_config.transducer.decoder = decoderPath.c_str();
  config.model_config.transducer.joiner = joinerPath.c_str();
  config.model_config.tokens_buf = reinterpret_cast<const char*>(tokensData.data());
  config.model_config.tokens_buf_size = static_cast<int32_t>(tokensData.size() - 1);
  config.model_config.num_threads = 4;
  config.model_config.provider = "cpu";
  config.decoding_method = "greedy_search";
  config.enable_endpoint = 1;
  config.rule1_min_trailing_silence = static_cast<float>(trailingSilenceMs) / 1000.0f;
  config.rule2_min_trailing_silence = static_cast<float>(trailingSilenceMs) / 1000.0f;
  config.rule3_min_utterance_length = static_cast<float>(utteranceLengthMs) / 1000.0f;

  if (!hotwords.empty()) {
    config.hotwords_file = hotwords.c_str();
    config.hotwords_score = 1.5f;
  }

  recognizer = SherpaOnnxCreateOnlineRecognizer(&config);
  if (!recognizer) {
    LOGFLF(LogLevel::warn, "SherpaOnnxCreateOnlineRecognizer failed");
    return;
  }

  stream = SherpaOnnxCreateOnlineStream(recognizer);
  if (!stream) {
    LOGFLF(LogLevel::warn, "SherpaOnnxCreateOnlineStream failed");
    SherpaOnnxDestroyOnlineRecognizer(recognizer);
    recognizer = nullptr;
    return;
  }

  lastText.clear();
  LOGFLF(LogLevel::info, "SherpaRecognizer init success");
}

void SherpaRecognizer::unloadModel() {
  if (stream) {
    SherpaOnnxDestroyOnlineStream(stream);
    stream = nullptr;
  }
  if (recognizer) {
    SherpaOnnxDestroyOnlineRecognizer(recognizer);
    recognizer = nullptr;
  }
  lastText.clear();
}

void SherpaRecognizer::reset() {
  if (recognizer && stream) {
    // OnlineStreamReset 非完全重置: 上轮结果非空时保留 encoder 隐状态 + 末尾
    // context_size 个 token 作下段上下文, 且不丢弃底层未消费音频
    // (见 sherpa-onnx online-recognizer-transducer-impl.h Reset)。跨录音会串话。
    // 这里销毁重建 stream 拿到全新状态 (recognizer 模型对象级常驻, 只换轻量 stream)。
    SherpaOnnxDestroyOnlineStream(stream);
    stream = SherpaOnnxCreateOnlineStream(recognizer);
    lastText.clear();
    totalSamples = 0;
  }
}

// ========== 同步识别接口 ==========

void SherpaRecognizer::processAudio(const float* samples, int32_t count,
                                    bool flushing, int64_t streamBasePts) {
  if (!recognizer || !stream) {
    return;
  }
  if (flushing) {
    // EOF: 强制排干流内部 lookahead / 已 accept 但未解码的尾部, 拿到完整最终结果。
    // 调用后该流不可再 AcceptWaveform, 由上层 SherpaAudioStt::reset() 销毁重建。
    SherpaOnnxOnlineStreamInputFinished(stream);
  } else {
    // 喂入音频数据
    SherpaOnnxOnlineStreamAcceptWaveform(stream, 16000, samples, count);
    totalSamples += count;
  }
  // 检查是否可以解码
  while (SherpaOnnxIsOnlineStreamReady(recognizer, stream)) {
    SherpaOnnxDecodeOnlineStream(recognizer, stream);
  }
  // 检查端点
  bool isEndpoint = SherpaOnnxOnlineStreamIsEndpoint(recognizer, stream);
  if (isEndpoint || flushing) {
    // 获取最终结果
    const SherpaOnnxOnlineRecognizerResult* result =
        SherpaOnnxGetOnlineStreamResult(recognizer, stream);
    if (result && result->text) {
      // 打印时间戳信息
      // LOGFLF(LogLevel::info, "=== Streaming Result ===");
      // LOGFLF(LogLevel::info, "streamBasePts: ", streamBasePts);
      // LOGFLF(LogLevel::info, "totalSamples: ", totalSamples, " (", totalSamples / 16, "ms)");
      // LOGFLF(LogLevel::info, "text: ", result->text);
      // LOGFLF(LogLevel::info, "token count: ", result->count);
      // if (result->timestamps && result->count > 0) {
      //   LOGFLF(LogLevel::info, "timestamps[0]: ", result->timestamps[0], "s");
      //   LOGFLF(LogLevel::info, "timestamps[last]: ", result->timestamps[result->count - 1], "s");
      // }
      SherpaResult sr = {};
      sr.text = result->text;
      sr.lang = "zh";
      sr.isFinal = true;
      sr.isEndpoint = isEndpoint;  // 通知上层端点状态

      // 使用 token 时间戳计算 PTS
      if (result->timestamps && result->count > 0) {
        sr.startPts = streamBasePts + static_cast<int64_t>(result->timestamps[0] * 1000);
        // 最后一个 token 的时间作为结束时间
        sr.endPts = streamBasePts + static_cast<int64_t>(result->timestamps[result->count - 1] * 1000);
      } else {
        // 如果没有时间戳，使用累积样本数估算
        int64_t currentPts = streamBasePts + totalSamples * 1000 / 16000;
        sr.startPts = streamBasePts;
        sr.endPts = currentPts;
      }

      dispatch(&ISherpaRecognizerOb::onSherpaResult, sr);
    }
    SherpaOnnxDestroyOnlineRecognizerResult(result);

    // 重置流状态
    if (isEndpoint) {
      SherpaOnnxOnlineStreamReset(recognizer, stream);
      totalSamples = 0;  // 重置样本计数
    }
  } else {
    // 获取部分结果
    const SherpaOnnxOnlineRecognizerResult* result =
        SherpaOnnxGetOnlineStreamResult(recognizer, stream);
    if (result && result->text) {
      // 去重：如果是相同文本，跳过
      if (lastText != result->text) {
        lastText = result->text;
        SherpaResult sr = {};
        sr.text = result->text;
        sr.lang = "zh";
        sr.isFinal = false;
        sr.isEndpoint = false;
        // partial 结果不需要 PTS
        sr.startPts = 0;
        sr.endPts = 0;
        dispatch(&ISherpaRecognizerOb::onSherpaResult, sr);
      }
    }
    SherpaOnnxDestroyOnlineRecognizerResult(result);
  }
}

}
