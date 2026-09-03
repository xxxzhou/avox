#include "OnnxTranslator.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numeric>

#include "avox/module/AvoxManager.hpp"

#ifdef __ANDROID__
#include "avox/module/AssetLoader.hpp"
#endif

namespace avox {

#if AVOX_ENABLE_TRANSLATION

OnnxTranslator::OnnxTranslator() = default;

OnnxTranslator::~OnnxTranslator() { close(); }

bool OnnxTranslator::loadTokenizer(const std::string& modelDir) {
  // MarianMT 使用 source.spm (日语) 编码输入, target.spm (中文) 解码输出
  std::string sourceSpmPath = modelDir + "/source.spm";
  std::string targetSpmPath = modelDir + "/target.spm";

  // 加载 source tokenizer (日语)
  sourceSpm = std::make_unique<sentencepiece::SentencePieceProcessor>();
  auto status = sourceSpm->Load(sourceSpmPath);
  if (!status.ok()) {
    lastError =
        "Failed to load source tokenizer: " + std::string(status.message());
    LOGFLF(LogLevel::error, lastError.c_str());
    sourceSpm.reset();
    return false;
  }
  LOGFLF(LogLevel::info,
         "Source tokenizer loaded from: ", sourceSpmPath.c_str());

  // 加载 target tokenizer (中文)
  targetSpm = std::make_unique<sentencepiece::SentencePieceProcessor>();
  status = targetSpm->Load(targetSpmPath);
  if (!status.ok()) {
    lastError =
        "Failed to load target tokenizer: " + std::string(status.message());
    LOGFLF(LogLevel::error, lastError.c_str());
    targetSpm.reset();
    return false;
  }
  LOGFLF(LogLevel::info,
         "Target tokenizer loaded from: ", targetSpmPath.c_str());

  // 尝试加载特殊 token 配置
  // MarianMT: eos=</s>=0, pad=<pad>=65000
  eosTokenId = 0;
  padTokenId = 65000;
  bosTokenId = 0;

  return true;
}

bool OnnxTranslator::open() {
  if (loaded) return true;

#ifdef __ANDROID__
  // Android: 把模型和 tokenizer 从 assets 复制到缓存目录
  modelDir = AssetLoader::copyDirectoryToCache(
      "models/translation/opus-mt-ja-zh", "translation_models");
#else
  // 模型目录
  modelDir = getModelFilePath("translation/opus-mt-ja-zh");
#endif
  if (modelDir.empty()) {
    lastError = "Model directory not found";
    LOGFLF(LogLevel::error, lastError.c_str());
    return false;
  }
  // 加载 tokenizer
  if (!loadTokenizer(modelDir)) return false;
  // 加载 encoder 模型 (优先 INT8); 经 OnnxModelUser 从全局缓存取 (路径用已解析的 encoderPath,
  // 兼容 Android 的 cache 拷贝目录)
  std::string encoderPath = modelDir + "/encoder_model_int8.onnx";
  bool useInt8 = true;
  if (!std::ifstream(encoderPath).good()) {
    encoderPath = modelDir + "/encoder_model.onnx";
    useInt8 = false;
  }
  encoderSession = session(encoderPath, useGPU, gpuDeviceIndex, 4);
  if (!encoderSession) {
    lastError = "Failed to load encoder model: " + encoderPath;
    LOGFLF(LogLevel::error, lastError.c_str());
    close();
    return false;
  }
  LOGFLF(LogLevel::info, "[OnnxTranslator] Using: ",
         useInt8 ? "INT8 quantized" : "FP32", " encoder");

  // 加载 decoder 模型 (优先 INT8)
  std::string decoderPath = modelDir + "/decoder_model_int8.onnx";
  if (!std::ifstream(decoderPath).good()) {
    decoderPath = modelDir + "/decoder_model.onnx";
    useInt8 = false;
  }
  decoderSession = session(decoderPath, useGPU, gpuDeviceIndex, 4);
  if (!decoderSession) {
    lastError = "Failed to load decoder model: " + decoderPath;
    LOGFLF(LogLevel::error, lastError.c_str());
    close();
    return false;
  }
  if (!useInt8) {
    LOGFLF(LogLevel::info, "[OnnxTranslator] Using: FP32 decoder");
  }

  // 打印 encoder 信息
  auto encInputNames = encoderSession->getInputNames();
  auto encOutputNames = encoderSession->getOutputNames();

  LOGFLF(LogLevel::info, "[OnnxTranslator] Encoder loaded");
  LOGFLF(LogLevel::info, "  Inputs: ", encInputNames.size());
  for (const auto& name : encInputNames) {
    auto shape = encoderSession->getInputShape(name);
    LOGFLF(LogLevel::info, "    ", name, ": [", shape[0], ", ", shape[1], "]");
  }
  LOGFLF(LogLevel::info, "  Outputs: ", encOutputNames.size());

  // 打印 decoder 信息
  auto decInputNames = decoderSession->getInputNames();
  auto decOutputNames = decoderSession->getOutputNames();

  LOGFLF(LogLevel::info, "[OnnxTranslator] Decoder loaded");
  LOGFLF(LogLevel::info, "  Inputs: ", decInputNames.size());
  for (const auto& name : decInputNames) {
    auto shape = decoderSession->getInputShape(name);
    LOGFLF(LogLevel::info, "    ", name);
  }
  LOGFLF(LogLevel::info, "  Outputs: ", decOutputNames.size());

  // 获取 vocab size
  if (!decOutputNames.empty()) {
    auto shape = decoderSession->getOutputShape(decOutputNames[0]);
    if (shape.size() >= 3) {
      int vocabSize = static_cast<int>(shape[2]);
      LOGFLF(LogLevel::info, "  Vocab size: ", vocabSize);
    }
  }

  loaded = true;
  LOGFLF(LogLevel::info, "OnnxTranslator loaded from: ", modelDir.c_str());
  return true;
}

void OnnxTranslator::close() {
  // session 借用自缓存(Shared), 不释放模型; 仅清自身指针
  encoderSession = nullptr;
  decoderSession = nullptr;
  sourceSpm.reset();
  targetSpm.reset();
  loaded = false;
}

void OnnxTranslator::setUseGPU(bool useGPU_, int deviceIndex) {
  useGPU = useGPU_;
  gpuDeviceIndex = deviceIndex;
}

std::vector<int32_t> OnnxTranslator::encodeText(const std::string& text) {
  std::vector<int32_t> tokenIds;
  if (!sourceSpm) return tokenIds;

  std::vector<std::string> pieces;
  sourceSpm->Encode(text, &pieces);

  for (const auto& piece : pieces) {
    int id = sourceSpm->PieceToId(piece);
    if (id >= 0) tokenIds.push_back(id);
  }
  return tokenIds;
}

std::string OnnxTranslator::decodeTokens(const std::vector<int32_t>& tokens) {
  if (!targetSpm || tokens.empty()) return "";

  std::vector<std::string> pieces;
  for (int id : tokens) {
    pieces.push_back(targetSpm->IdToPiece(id));
  }

  std::string text;
  targetSpm->Decode(pieces, &text);
  return text;
}

std::vector<float> OnnxTranslator::runEncoder(
    const std::vector<int32_t>& inputIds) {
  std::vector<float> output;
  if (!encoderSession || !encoderSession->isLoaded()) return output;

  // 准备输入 tensor - 使用 int64
  int seqLen = static_cast<int>(inputIds.size());
  std::vector<int64_t> inputInt64(inputIds.begin(), inputIds.end());
  std::vector<int64_t> attentionMask(seqLen, 1);

  // 使用 runAuto 自动推导动态形状
  std::vector<std::tuple<std::string, const void*, size_t, bool>> inputs = {
      {"input_ids", inputInt64.data(), inputInt64.size(), false},
      {"attention_mask", attentionMask.data(), attentionMask.size(), false}};

  std::vector<std::string> outputNames = {"last_hidden_state"};
  std::vector<std::vector<float>> outputs;

  if (!encoderSession->runAuto(inputs, outputNames, outputs) ||
      outputs.empty()) {
    return output;
  }

  return outputs[0];
}

std::vector<float> OnnxTranslator::runDecoderStepLogits(
    const std::vector<int32_t>& decoderInputIds,
    const std::vector<float>& encoderHiddenStates,
    const std::vector<int64_t>& encoderShape) {
  std::vector<float> lastLogits;
  if (!decoderSession || !decoderSession->isLoaded()) return lastLogits;

  int decSeqLen = static_cast<int>(decoderInputIds.size());
  std::vector<int64_t> decoderInput(decoderInputIds.begin(),
                                    decoderInputIds.end());

  // encoder attention mask (全1)
  std::vector<int64_t> encAttentionMask(encoderShape[1], 1);

  // 使用 runAuto 自动推导动态形状
  std::vector<std::tuple<std::string, const void*, size_t, bool>> inputs = {
      {"input_ids", decoderInput.data(), decoderInput.size(), false},
      {"encoder_attention_mask", encAttentionMask.data(),
       encAttentionMask.size(), false},
      {"encoder_hidden_states", encoderHiddenStates.data(),
       encoderHiddenStates.size(), true}};

  std::vector<std::string> outputNames = {"logits"};
  std::vector<std::vector<float>> outputs;

  if (!decoderSession->runAuto(inputs, outputNames, outputs) ||
      outputs.empty()) {
    LOGFLF(LogLevel::error, "Decoder runAuto failed");
    return lastLogits;
  }

  // 输出是 3D [batch=1, seq_len, vocab]，取最后一个位置的 logits
  int vocabSize = static_cast<int>(outputs[0].size()) / decSeqLen;
  const auto& logits = outputs[0];
  int lastPos = (decSeqLen - 1) * vocabSize;

  lastLogits.resize(vocabSize);
  for (int i = 0; i < vocabSize && (lastPos + i) < static_cast<int>(logits.size()); i++) {
    lastLogits[i] = logits[lastPos + i];
  }

  return lastLogits;
}

std::vector<int32_t> OnnxTranslator::generateTranslation(
    const std::vector<int32_t>& inputIds) {
  std::vector<int32_t> outputIds;
  if (!encoderSession || !decoderSession || inputIds.empty()) return outputIds;

  // Run encoder
  auto encoderOutput = runEncoder(inputIds);
  if (encoderOutput.empty()) return outputIds;

  // encoder 输出形状: [batch, seq_len, hidden_size]
  int64_t encSeqLen = static_cast<int64_t>(inputIds.size());
  int64_t hiddenSize = static_cast<int64_t>(encoderOutput.size()) / encSeqLen;
  std::vector<int64_t> encoderShape = {1, encSeqLen, hiddenSize};

  // MarianMT 使用 decoder_start_token_id 作为初始输入 (65000 = pad token)
  const int32_t decoderStartTokenId = 65000;

  // ===== Beam Search =====
  struct Beam {
    std::vector<int32_t> tokenIds;  // 已生成的 token (不含 start token)
    std::vector<int32_t> decoderInput;  // 完整 decoder 输入 (start + tokens)
    float score = 0.0f;  // 累积 log 概率
    bool finished = false;
  };

  // 初始化: 1 个 beam，后续第一步扩展到 numBeams
  std::vector<Beam> beams(1);
  beams[0].decoderInput = {decoderStartTokenId};

  // 存放已完成的 beam
  std::vector<Beam> finishedBeams;

  // 输出长度上限: 输入长度的 2 倍 + 10，翻译输出通常远短于 256
  const int maxLength = std::min(256, static_cast<int>(inputIds.size()) * 2 + 10);

  for (int step = 0; step < maxLength; step++) {
    // 收集所有活跃 beam 的候选扩展
    struct Candidate {
      int parentIdx;
      int32_t tokenId;
      float score;
    };
    std::vector<Candidate> candidates;

    for (int b = 0; b < static_cast<int>(beams.size()); b++) {
      if (beams[b].finished) continue;

      auto logits = runDecoderStepLogits(
          beams[b].decoderInput, encoderOutput, encoderShape);
      if (logits.empty()) continue;

      int vocabSize = static_cast<int>(logits.size());

      // log-softmax (仅对 top-K 区域计算，避免全词表求和)
      float maxLogit = *std::max_element(logits.begin(), logits.end());

      // 取 top numBeams 候选
      int topK = numBeams;
      std::vector<int> indices(vocabSize);
      std::iota(indices.begin(), indices.end(), 0);
      std::partial_sort(indices.begin(), indices.begin() + topK, indices.end(),
                        [&logits](int a, int b) { return logits[a] > logits[b]; });

      // 仅对 top-K 计算 log-softmax
      float logSumExp = 0.0f;
      for (int k = 0; k < topK; k++) {
        logSumExp += std::exp(logits[indices[k]] - maxLogit);
      }
      // 加上 EOS 的贡献
      logSumExp += std::exp(logits[eosTokenId] - maxLogit);
      float logNorm = maxLogit + std::log(logSumExp);

      // 候选: top-K tokens
      for (int k = 0; k < topK; k++) {
        int32_t tid = indices[k];
        float logProb = logits[tid] - logNorm;
        float newScore = beams[b].score + logProb;
        candidates.push_back({b, tid, newScore});
      }
      // 候选: EOS token
      {
        float logProb = logits[eosTokenId] - logNorm;
        float newScore = beams[b].score + logProb;
        candidates.push_back({b, eosTokenId, newScore});
      }
    }

    if (candidates.empty()) break;

    // 按 score 降序排列，保留 top numBeams
    std::partial_sort(candidates.begin(),
                      candidates.begin() + std::min(numBeams, static_cast<int>(candidates.size())),
                      candidates.end(),
                      [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    std::vector<Beam> newBeams;
    for (int i = 0; i < std::min(numBeams, static_cast<int>(candidates.size())); i++) {
      const auto& c = candidates[i];
      Beam newBeam;
      newBeam.tokenIds = beams[c.parentIdx].tokenIds;
      newBeam.decoderInput = beams[c.parentIdx].decoderInput;
      newBeam.score = c.score;

      if (c.tokenId == eosTokenId) {
        newBeam.finished = true;
        finishedBeams.push_back(std::move(newBeam));
        continue;
      }

      // 跳过 pad token 在输出中，但仍加入 decoderInput
      if (c.tokenId != padTokenId) {
        newBeam.tokenIds.push_back(c.tokenId);
      }
      newBeam.decoderInput.push_back(c.tokenId);
      newBeams.push_back(std::move(newBeam));
    }

    beams = std::move(newBeams);

    // 所有 beam 都完成了
    if (beams.empty()) break;
  }

  // 将剩余未完成的 beam 也加入候选
  for (auto& b : beams) {
    if (!b.tokenIds.empty()) {
      finishedBeams.push_back(std::move(b));
    }
  }

  if (finishedBeams.empty()) return outputIds;

  // 按 length-penalized score 选择最佳 beam
  // score / (length ^ lengthPenalty)，MarianMT 常用
  float bestScore = -1e9f;
  int bestIdx = 0;
  for (int i = 0; i < static_cast<int>(finishedBeams.size()); i++) {
    int len = static_cast<int>(finishedBeams[i].tokenIds.size()) + 1;  // +1 避免 0
    float penalizedScore = finishedBeams[i].score / std::pow(static_cast<float>(len), lengthPenalty);
    if (penalizedScore > bestScore) {
      bestScore = penalizedScore;
      bestIdx = i;
    }
  }

  return finishedBeams[bestIdx].tokenIds;
}

const char* OnnxTranslator::translate(const char* text) {
  if (!text || !*text) return "";
  if (!loaded) {
    LOGFLF(LogLevel::warn, "OnnxTranslator not loaded");
    return "";
  }
  if (sourceLangCode == targetLangCode) {
    LOGFLF(LogLevel::warn, "sourceLang == targetLang: ", sourceLangCode.c_str());
    return "";
  }
  try {
    // Tokenize 输入 (日语)
    std::vector<int32_t> inputIds = encodeText(text);
    if (inputIds.empty()) {
      LOGFLF(LogLevel::warn, "encodeText failed for: ", text);
      return "";
    }
    // 添加 EOS
    inputIds.push_back(eosTokenId > 0 ? eosTokenId : 0);
    // 生成翻译
    std::vector<int32_t> outputIds = generateTranslation(inputIds);
    if (outputIds.empty()) {
      LOGFLF(LogLevel::warn, "generateTranslation returned empty");
      return "";
    }
    // 解码输出 (中文) 并存储到缓冲区
    resultBuffer = decodeTokens(outputIds);
    // LOGFLF(LogLevel::info, "translate: ", text, " -> ", resultBuffer);
    return resultBuffer.c_str();
  } catch (const std::exception& e) {
    lastError = std::string("Translation error: ") + e.what();
    LOGFLF(LogLevel::error, lastError.c_str());
    return "";
  }
}

bool OnnxTranslator::ready() const { return loaded; }

#endif

}