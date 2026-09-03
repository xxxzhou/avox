#include "SherpaTts.hpp"

#include <cstring>

#include "avox/Avox.hpp"
#include "avox/module/AssetLoader.hpp"

// sherpa-onnx C API
#include "sherpa-onnx/c-api/c-api.h"

namespace avox {

SherpaTts::SherpaTts() = default;

SherpaTts::~SherpaTts() { unloadModel(); }

// ========== 配置 ==========

void SherpaTts::setModelLevel(ModelLevel level) { modelLevel = level; }

// ========== 模型生命周期 ==========

bool SherpaTts::bLoad() const { return tts != nullptr; }

int32_t SherpaTts::sampleRate() const { return cachedSampleRate; }

int32_t SherpaTts::numSpeakers() const { return cachedNumSpeakers; }

void SherpaTts::loadModel() {
  // Kokoro (Phase 1 首选; 中文质量最佳)。模型布局: tts/kokoro/{model.onnx,
  // voices.bin, tokens.txt, espeak-ng-data/, lexicon-us-en.txt, lexicon-zh.txt}
  // 中文音素前端必须带 lexicon-zh.txt (否则中文无声, sherpa-onnx issue #2248)
  std::string selectedModel = "tts/kokoro";
  std::string dir = getModelFilePath(selectedModel.c_str());
  LOGFLF(LogLevel::info, "tts load model path: ", dir.c_str());
  // 路径必须存局部 string 保活 (c_str() 传给 sherpa, 同步调用内有效)
  std::string modelFile;
  std::string voicesFile;
  std::string tokensFile;
  std::string dataDir;
  std::string lexiconFile;
#ifdef __ANDROID__
  // Android: 模型从 assets 复制到缓存目录 (espeak-ng-data 为目录, 逐文件复制待补)
  std::string base = "models/" + selectedModel + "/";
  modelFile = AssetLoader::copyAssetToCache((base + "model.onnx").c_str(), "sherpa_models");
  voicesFile = AssetLoader::copyAssetToCache((base + "voices.bin").c_str(), "sherpa_models");
  tokensFile = AssetLoader::copyAssetToCache((base + "tokens.txt").c_str(), "sherpa_models");
  dataDir = AssetLoader::copyAssetToCache((base + "espeak-ng-data").c_str(), "sherpa_models");
  std::string lexEn = AssetLoader::copyAssetToCache((base + "lexicon-us-en.txt").c_str(), "sherpa_models");
  std::string lexZh = AssetLoader::copyAssetToCache((base + "lexicon-zh.txt").c_str(), "sherpa_models");
  lexiconFile = lexEn + "," + lexZh;
#else
  modelFile = dir + "/model.onnx";
  voicesFile = dir + "/voices.bin";
  tokensFile = dir + "/tokens.txt";
  dataDir = dir + "/espeak-ng-data";
  // sherpa lexicon 字段按逗号分割多文件 (中英双 lexicon, 中文音素前端必需)
  lexiconFile = dir + "/lexicon-us-en.txt," + dir + "/lexicon-zh.txt";
#endif

  SherpaOnnxOfflineTtsConfig config;
  memset(&config, 0, sizeof(config));
  config.model.kokoro.model = modelFile.c_str();
  config.model.kokoro.voices = voicesFile.c_str();
  config.model.kokoro.tokens = tokensFile.c_str();
  config.model.kokoro.data_dir = dataDir.c_str();
  config.model.kokoro.lexicon = lexiconFile.c_str();
  config.model.num_threads = 4;
  config.model.provider = "cpu";
  config.model.debug = 0;
  // 句级流式: 每处理完 1 句回调一次 (progressCb 增量投递 PCM)
  config.max_num_sentences = 1;
  config.silence_scale = 0.2f;

  tts = SherpaOnnxCreateOfflineTts(&config);
  if (!tts) {
    LOGFLF(LogLevel::warn,
           "SherpaOnnxCreateOfflineTts failed (model missing? dir=", dir, ")");
    return;
  }
  cachedSampleRate = SherpaOnnxOfflineTtsSampleRate(tts);
  cachedNumSpeakers = SherpaOnnxOfflineTtsNumSpeakers(tts);
  // 通知上层输出格式
  dispatch(&ISherpaTtsOb::onSherpaTtsDesc, cachedSampleRate, cachedNumSpeakers);
  LOGFLF(LogLevel::info, "SherpaTts init success, sampleRate=", cachedSampleRate,
         " speakers=", cachedNumSpeakers);
}

void SherpaTts::unloadModel() {
  if (tts) {
    SherpaOnnxDestroyOfflineTts(tts);
    tts = nullptr;
  }
  cachedSampleRate = 0;
  cachedNumSpeakers = 0;
}

// ========== 同步合成接口 ==========

// 静态 progress callback: sherpa 在合成线程同步调用, samples 仅回调内有效
int32_t SherpaTts::progressCb(const float* samples, int32_t n, float /*p*/,
                              void* arg) {
  SherpaTts* self = static_cast<SherpaTts*>(arg);
  if (!self || !samples || n <= 0) {
    return 1;
  }
  // barge-in: stop() 置 cancelFlag -> 返回 0 让 sherpa 提前停止
  if (self->curCancel && self->curCancel->load(std::memory_order_relaxed)) {
    return 0;
  }
  // 深拷贝 PCM (sherpa: callback 返回后 samples 可能失效)
  SherpaTtsChunk chunk;
  chunk.data.assign(reinterpret_cast<const uint8_t*>(samples),
                    reinterpret_cast<const uint8_t*>(samples) +
                        static_cast<size_t>(n) * sizeof(float));
  chunk.pts = self->cachedSampleRate > 0
                  ? self->totalSamples * 1000 / self->cachedSampleRate
                  : 0;
  chunk.final = false;
  self->totalSamples += n;
  self->dispatch(&ISherpaTtsOb::onSherpaTtsAudio, chunk);
  return 1;
}

void SherpaTts::generate(const char* text, float speed, int sid,
                         const std::atomic<bool>* cancelFlag) {
  if (!tts || !text) {
    return;
  }
  curCancel = cancelFlag;
  totalSamples = 0;
  // 使用新版 GenerationConfig API (preferred); speed/sid 运行期可变, 不重载模型
  SherpaOnnxGenerationConfig genConfig;
  memset(&genConfig, 0, sizeof(genConfig));
  genConfig.speed = speed > 0 ? speed : 1.0f;
  genConfig.sid = sid;
  genConfig.silence_scale = 0.2f;
  // progressCb 增量投递 PCM; 返回的完整 audio 不再用 (已在 callback 投递), 直接 destroy
  const SherpaOnnxGeneratedAudio* audio = SherpaOnnxOfflineTtsGenerateWithConfig(
      tts, text, &genConfig, &SherpaTts::progressCb, this);
  if (audio) {
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
  }
  bool canceled = curCancel && curCancel->load(std::memory_order_relaxed);
  curCancel = nullptr;
  // 正常结束发 final 块 (上层据此收尾口型/字幕); 中断不发 (上层 stop 已感知)
  if (!canceled) {
    SherpaTtsChunk chunk;
    chunk.pts = cachedSampleRate > 0 ? totalSamples * 1000 / cachedSampleRate : 0;
    chunk.final = true;
    dispatch(&ISherpaTtsOb::onSherpaTtsAudio, chunk);
  }
}

}
