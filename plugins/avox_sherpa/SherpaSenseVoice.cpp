#include "SherpaSenseVoice.hpp"

#include <algorithm>
#include <cstring>

#include "avox/Avox.hpp"
#include "avox/module/AssetLoader.hpp"
#include "avox/module/AvoxManager.hpp"

// sherpa-onnx C API
#include "sherpa-onnx/c-api/c-api.h"

namespace avox {

SherpaSenseVoice::SherpaSenseVoice() = default;

SherpaSenseVoice::~SherpaSenseVoice() { unloadModel(); }

// ========== 配置设置 ==========

void SherpaSenseVoice::setModelLevel(ModelLevel level) { modelLevel = level; }

void SherpaSenseVoice::setHotwords(const char* hotwords_) {
  hotwords = hotwords_ ? hotwords_ : "";
}

void SherpaSenseVoice::setEndpoint(int trailingSilenceMs_,
                                   int utteranceLengthMs_) {
  trailingSilenceMs = trailingSilenceMs_;
  utteranceLengthMs = utteranceLengthMs_;
  LOGFLF(LogLevel::info, "Set endpoint: trailingSilenceMs=", trailingSilenceMs,
         " utteranceLengthMs=", utteranceLengthMs);
}
// ========== 模型生命周期 ==========

bool SherpaSenseVoice::bLoad() const { return recognizer != nullptr; }

void SherpaSenseVoice::loadModel() {
  // 加载 VAD 模型
  loadVadModel();
  // 加载 SenseVoice 模型
  loadSenseVoiceModel();
  if (recognizer) {
    LOGFLF(LogLevel::info, "SherpaSenseVoice init success");
  } else {
    LOGFLF(LogLevel::warn, "SherpaSenseVoice init failed");
  }
}

void SherpaSenseVoice::loadVadModel() {
  std::string vadModelPath;
#ifdef __ANDROID__
  vadModelPath = AssetLoader::copyAssetToCache(
      "models/stt/sense-voice/silero_vad.onnx", "sherpa_models");
  if (vadModelPath.empty()) {
    LOGFLF(LogLevel::warn,
           "Failed to copy VAD model to cache: ", AssetLoader::getLastError());
    return;
  }
#else
  vadModelPath = getModelFilePath("stt/sense-voice/silero_vad.onnx");
#endif
  SherpaOnnxSileroVadModelConfig sileroVad = {};
  sileroVad.model = vadModelPath.c_str();
  sileroVad.threshold = 0.4f;
  sileroVad.min_silence_duration =
      static_cast<float>(trailingSilenceMs) / 1000.0f;
  // min_silence_duration 最小不低于 50ms，否则切分过碎
  if (sileroVad.min_silence_duration < 0.05f) {
    sileroVad.min_silence_duration = 0.05f;
  }
  sileroVad.min_speech_duration = 0.1f;
  // 超过此时长，VAD 会提高阈值鼓励在短暂停顿处切分
  sileroVad.max_speech_duration = 2.0f;
  sileroVad.window_size = 512;

  SherpaOnnxVadModelConfig vadConfig = {};
  vadConfig.silero_vad = sileroVad;
  vadConfig.sample_rate = 16000;
  vadConfig.num_threads = 2;
  vadConfig.provider = "cpu";
  vad = SherpaOnnxCreateVoiceActivityDetector(&vadConfig, 30.0f);
  if (!vad) {
    LOGFLF(LogLevel::warn, "Failed to create VAD");
    return;
  }
  LOGFLF(LogLevel::info, "VAD model loaded: ", vadModelPath.c_str());
}

void SherpaSenseVoice::loadSenseVoiceModel() {
  std::string selectedModel = "stt/sense-voice";
  std::string modelFile = "model.int8.onnx";

#ifdef __ANDROID__
  // Android: 把模型文件和 tokens 从 assets 复制到缓存目录
  std::string modelAsset = "models/" + selectedModel + "/" + modelFile;
  std::string modelFullPath =
      AssetLoader::copyAssetToCache(modelAsset.c_str(), "sherpa_models");
  if (modelFullPath.empty()) {
    LOGFLF(LogLevel::warn,
           "Failed to copy model to cache: ", AssetLoader::getLastError());
    return;
  }
  std::string tokensAsset = "models/" + selectedModel + "/tokens.txt";
  std::string tokensPath =
      AssetLoader::copyAssetToCache(tokensAsset.c_str(), "sherpa_models");
  if (tokensPath.empty()) {
    LOGFLF(LogLevel::warn,
           "Failed to copy tokens to cache: ", AssetLoader::getLastError());
    return;
  }
#else
  std::string modelPath = getModelFilePath(selectedModel.c_str());
  std::string modelFullPath = modelPath + "/" + modelFile;
  std::string tokensPath = modelPath + "/tokens.txt";
#endif

  LOGFLF(LogLevel::info, "load model path: ", modelFullPath.c_str());
  // 构建模型配置
  SherpaOnnxOfflineRecognizerConfig config = {};
  // 特征配置
  config.feat_config.sample_rate = 16000;
  config.feat_config.feature_dim = 80;
  // SenseVoice 配置
  config.model_config.sense_voice.model = modelFullPath.c_str();
  config.model_config.sense_voice.language = "";
  config.model_config.sense_voice.use_itn = 1;
  config.model_config.tokens = tokensPath.c_str();
  config.model_config.num_threads = 4;
  config.model_config.provider = "cpu";
  // 解码配置
  config.decoding_method = "greedy_search";
  // 创建离线识别器
  recognizer = SherpaOnnxCreateOfflineRecognizer(&config);
  if (!recognizer) {
    LOGFLF(LogLevel::warn, "Failed to create SenseVoice recognizer");
    return;
  }
  LOGFLF(LogLevel::info, "SenseVoice model loaded: ", modelFullPath.c_str());
}

void SherpaSenseVoice::unloadModel() {
  if (vad) {
    SherpaOnnxDestroyVoiceActivityDetector(vad);
    vad = nullptr;
  }
  if (recognizer) {
    SherpaOnnxDestroyOfflineRecognizer(recognizer);
    recognizer = nullptr;
  }
}

void SherpaSenseVoice::reset() {
  if (vad) {
    SherpaOnnxVoiceActivityDetectorReset(vad);
  }
}

void SherpaSenseVoice::processAudio(const float* samples, int32_t count,
                                    bool flushing, int64_t streamBasePts) {
  if (!vad || !recognizer) {
    return;
  }
  // 输入音频到 VAD
  SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad, samples, count);
  // 处理检测到的语音段
  while (!SherpaOnnxVoiceActivityDetectorEmpty(vad)) {
    const SherpaOnnxSpeechSegment* segment =
        SherpaOnnxVoiceActivityDetectorFront(vad);
    int64_t segmentStartPts = streamBasePts + segment->start * 1000LL / 16000;
    processSegment(segment->samples, segment->n, segmentStartPts);
    SherpaOnnxDestroySpeechSegment(segment);
    SherpaOnnxVoiceActivityDetectorPop(vad);
  }
  // 处理 flush 模式
  if (flushing) {
    SherpaOnnxVoiceActivityDetectorFlush(vad);
    while (!SherpaOnnxVoiceActivityDetectorEmpty(vad)) {
      const SherpaOnnxSpeechSegment* segment =
          SherpaOnnxVoiceActivityDetectorFront(vad);
      int64_t segmentStartPts = streamBasePts + segment->start * 1000LL / 16000;
      processSegment(segment->samples, segment->n, segmentStartPts);
      SherpaOnnxDestroySpeechSegment(segment);
      SherpaOnnxVoiceActivityDetectorPop(vad);
    }
  }
}

void SherpaSenseVoice::processSegment(const float* samples, int32_t count,
                                      int64_t segmentStartPts) {
  if (!recognizer) return;
  // 用 SenseVoice 识别这个段落
  const SherpaOnnxOfflineStream* stream =
      SherpaOnnxCreateOfflineStream(recognizer);
  SherpaOnnxAcceptWaveformOffline(stream, 16000, samples, count);
  SherpaOnnxDecodeOfflineStream(recognizer, stream);
  const SherpaOnnxOfflineRecognizerResult* result =
      SherpaOnnxGetOfflineStreamResult(stream);
  if (result && result->text) {
    // 无 token 时间戳，回退到原有逻辑
    SherpaResult sr = {};
    sr.text = result->text;
    sr.lang = result->lang ? result->lang : "";
    sr.isFinal = true;
    sr.isEndpoint = false;
    if (result->timestamps && result->count > 0) {
      sr.startPts =
          segmentStartPts + static_cast<int64_t>(result->timestamps[0] * 1000);
      if (result->durations) {
        sr.endPts =
            segmentStartPts +
            static_cast<int64_t>((result->timestamps[result->count - 1] +
                                  result->durations[result->count - 1]) *
                                 1000);
      } else {
        sr.endPts =
            segmentStartPts +
            static_cast<int64_t>(result->timestamps[result->count - 1] * 1000);
      }
    } else {
      sr.startPts = segmentStartPts;
      sr.endPts = segmentStartPts + count * 1000LL / 16000;
    }
    dispatch(&ISherpaRecognizerOb::onSherpaResult, sr);
  }
  SherpaOnnxDestroyOfflineRecognizerResult(result);
  SherpaOnnxDestroyOfflineStream(stream);
}

// 判断 token 是否为切分标点（中日英）
static bool isPunctuationToken(const char* token) {
  if (!token || token[0] == '\0') return false;
  // 支持的标点：中文、日语、英文
  // 中文：。 ， 、 ； ： ？ ！
  // 日语：。 、 ？ ！（逗号用 、）
  // 英文：. , ? !
  // UTF-8 编码：
  //   。 = E3 80 82
  //   ， = EF BC 8C
  //   、 = E3 80 81
  //   ； = EF BC 9B
  //   ： = EF BC 9A
  //   ？ = EF BC 9F
  //   ！ = EF BC 81
  //   .  = 2E
  //   ,  = 2C
  //   ?  = 3F
  //   !  = 21
  const uint8_t* s = reinterpret_cast<const uint8_t*>(token);
  // 英文标点（单字节）
  if (s[0] == '.' || s[0] == ',' || s[0] == '?' || s[0] == '!') {
    return s[1] == '\0';  // 确保是单字符
  }
  // 中文/日语标点（多字节 UTF-8）
  // 。 (E3 80 82)
  if (s[0] == 0xE3 && s[1] == 0x80 && s[2] == 0x82 && s[3] == '\0') return true;
  // 、 (E3 80 81)
  if (s[0] == 0xE3 && s[1] == 0x80 && s[2] == 0x81 && s[3] == '\0') return true;
  // ， (EF BC 8C)
  if (s[0] == 0xEF && s[1] == 0xBC && s[2] == 0x8C && s[3] == '\0') return true;
  // ； (EF BC 9B)
  if (s[0] == 0xEF && s[1] == 0xBC && s[2] == 0x9B && s[3] == '\0') return true;
  // ： (EF BC 9A)
  if (s[0] == 0xEF && s[1] == 0xBC && s[2] == 0x9A && s[3] == '\0') return true;
  // ？ (EF BC 9F)
  if (s[0] == 0xEF && s[1] == 0xBC && s[2] == 0x9F && s[3] == '\0') return true;
  // ！ (EF BC 81)
  if (s[0] == 0xEF && s[1] == 0xBC && s[2] == 0x81 && s[3] == '\0') return true;
  return false;
}

std::vector<SherpaSubSentence> SherpaSenseVoice::splitByPunctuation(
    const char* text, const char* const* tokens_arr, const float* timestamps,
    const float* durations, int32_t count, int64_t segmentStartPts) {
  std::vector<SherpaSubSentence> results;
  if (!tokens_arr || !timestamps || count <= 0) {
    // 无效输入，返回单个子句
    if (text && text[0] != '\0') {
      SherpaSubSentence sub;
      sub.text = text;
      sub.startPts = segmentStartPts;
      sub.endPts =
          segmentStartPts + static_cast<int64_t>(timestamps[count - 1] * 1000);
      results.push_back(sub);
    }
    return results;
  }
  // 遍历 tokens，记录标点位置
  std::vector<int32_t> punctPositions;
  for (int32_t i = 0; i < count; ++i) {
    if (isPunctuationToken(tokens_arr[i])) {
      punctPositions.push_back(i);
    }
  }
  // 无标点，返回单个子句
  if (punctPositions.empty()) {
    SherpaSubSentence sub;
    sub.text = text;
    sub.startPts = segmentStartPts + static_cast<int64_t>(timestamps[0] * 1000);
    if (durations) {
      sub.endPts = segmentStartPts +
                   static_cast<int64_t>(
                       (timestamps[count - 1] + durations[count - 1]) * 1000);
    } else {
      sub.endPts =
          segmentStartPts + static_cast<int64_t>(timestamps[count - 1] * 1000);
    }
    results.push_back(sub);
    return results;
  }

  // 按标点切分
  // 注意：标点的时间戳可能不可靠，使用标点前一个内容 token 的结束时间
  // 跳过没有内容 token 的切分段（如开头的标点）
  int32_t lastEnd = 0;
  for (int32_t punctIdx : punctPositions) {
    // 检查是否有内容 token（lastEnd 到 punctIdx-1 之间有非标点 token）
    bool hasContent = false;
    for (int32_t i = lastEnd; i < punctIdx; ++i) {
      if (!isPunctuationToken(tokens_arr[i])) {
        hasContent = true;
        break;
      }
    }
    if (!hasContent) {
      // 没有内容，跳过这个标点，继续累积
      lastEnd = punctIdx + 1;
      continue;
    }
    // 拼接文本 [lastEnd, punctIdx]
    std::string segmentText;
    for (int32_t i = lastEnd; i <= punctIdx; ++i) {
      if (tokens_arr[i]) {
        segmentText += tokens_arr[i];
      }
    }
    if (segmentText.empty()) {
      lastEnd = punctIdx + 1;
      continue;
    }
    // 找最后一个内容 token 作为结束时间
    int32_t lastContentIdx = punctIdx - 1;
    while (lastContentIdx >= lastEnd &&
           isPunctuationToken(tokens_arr[lastContentIdx])) {
      lastContentIdx--;
    }
    if (lastContentIdx < lastEnd) lastContentIdx = lastEnd;
    SherpaSubSentence sub;
    sub.text = segmentText;
    sub.startPts =
        segmentStartPts + static_cast<int64_t>(timestamps[lastEnd] * 1000);
    if (durations && lastContentIdx >= 0) {
      sub.endPts =
          segmentStartPts +
          static_cast<int64_t>(
              (timestamps[lastContentIdx] + durations[lastContentIdx]) * 1000);
    } else {
      sub.endPts = segmentStartPts +
                   static_cast<int64_t>(timestamps[lastContentIdx] * 1000);
    }
    // 确保最小时间窗口 200ms
    if (sub.endPts <= sub.startPts) {
      sub.endPts = sub.startPts + 200;
    }
    results.push_back(sub);
    lastEnd = punctIdx + 1;
  }

  // 处理剩余部分（最后一个标点后的文本）
  if (lastEnd < count) {
    std::string segmentText;
    for (int32_t i = lastEnd; i < count; ++i) {
      if (tokens_arr[i]) {
        segmentText += tokens_arr[i];
      }
    }
    if (!segmentText.empty()) {
      // 找最后一个非标点 token
      int32_t lastContentIdx = count - 1;
      while (lastContentIdx >= lastEnd &&
             isPunctuationToken(tokens_arr[lastContentIdx])) {
        lastContentIdx--;
      }
      if (lastContentIdx < lastEnd) lastContentIdx = lastEnd;

      SherpaSubSentence sub;
      sub.text = segmentText;
      sub.startPts =
          segmentStartPts + static_cast<int64_t>(timestamps[lastEnd] * 1000);
      if (durations && lastContentIdx >= 0) {
        sub.endPts =
            segmentStartPts + static_cast<int64_t>((timestamps[lastContentIdx] +
                                                    durations[lastContentIdx]) *
                                                   1000);
      } else if (lastContentIdx >= 0) {
        sub.endPts = segmentStartPts +
                     static_cast<int64_t>(timestamps[lastContentIdx] * 1000);
      } else {
        sub.endPts = sub.startPts;
      }
      // 确保最小时间窗口 200ms
      if (sub.endPts <= sub.startPts) {
        sub.endPts = sub.startPts + 200;
      }
      results.push_back(sub);
    }
  }
  return results;
}

}
