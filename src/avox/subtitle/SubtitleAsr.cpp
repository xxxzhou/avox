#include "SubtitleAsr.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/module/ModuleMgr.hpp"

namespace avox {

SubtitleAsr::SubtitleAsr() {
  taskName = "SubtitleAsr";
  // 触发 plugin lazy 加载(首次调执行 startup: 扫描 plugins/ + 注册所有工厂),
  // 确保 sherpa/translation 工厂就绪; 之后幂等直接返回。无需业务程序显式 checkLoadModel
  ModuleMgr::Get().ensureStarted();
  // 通过 AvoxManager 工厂表拿 ASR 实例(avox_sherpa 组件 loadModule 时注册),
  // 解除对 avox_sherpa 的编译期 include 依赖; 组件未加载时返回 nullptr, ASR 自动降级
  audioStt = std::unique_ptr<AudioStt>(AvoxManager::Get().audioSttHub.create("sherpa"));
  if (audioStt) {
    audioStt->setObserver(this);
  }
}

SubtitleAsr::~SubtitleAsr() {
  stopTask();
  if (audioStt) {
    audioStt->removeObserver(this);
  }
}

void SubtitleAsr::onRunTask() {
  LOGFLF(LogLevel::info, "SubtitleAsr onRunTask running...");
  if (audioStt) {
    if (asrMode == AsrMode::streaming) {
      audioStt->setRecognizerType(RecognizerType::streaming);
    } else if (asrMode == AsrMode::ptsSync) {
      audioStt->setRecognizerType(RecognizerType::offline);
    }
    audioStt->start();
  }
  while (running()) {
    checkTranslationState();
    // 从结果队列取，翻译后存入字幕队列
    SttTextResult item = {};
    if (resultQueue.dequeue(item)) {
      processResult(item);
    } else {
      sleepTask(true, 10);
    }
  }
  if (audioStt) {
    audioStt->stop();
  }
  if (translator) {
    translator->close();
  }
}

AudioStt* SubtitleAsr::getAudioStt() { return audioStt.get(); }

void SubtitleAsr::loadAsr() { startTask(); }

void SubtitleAsr::unloadAsr() { stopTask(); }

void SubtitleAsr::setAsrMode(AsrMode mode) { asrMode = mode; }

AsrMode SubtitleAsr::getAsrMode() const { return asrMode; }

void SubtitleAsr::setAudioDesc(AudioDesc desc) {
  audioDesc = desc;
  if (audioStt) {
    audioStt->setAudioDesc(desc);
  }
}

void SubtitleAsr::enableTranslation() { translationEnabled = true; }

void SubtitleAsr::disableTranslation() { translationEnabled = false; }

void SubtitleAsr::checkTranslationState() {
  if (!translator) {
    // 通过 AvoxManager 工厂表拿翻译器(avox_translation 组件 loadModule 时注册),
    // 解除对 avox_translation 的编译期 include 依赖; 组件未加载返回 nullptr, 翻译降级
    translator = std::unique_ptr<BaseTranslator>(AvoxManager::Get().translatorHub.create("http"));
    if (translator) {
      bHttp = translator->open();
    }
    // http 不可用或加载失败(如无密钥), 改用 onnx
    if (!bHttp) {
      translator = std::unique_ptr<BaseTranslator>(AvoxManager::Get().translatorHub.create("onnx"));
    }
    std::string msg = bHttp ? "load http translator" : "load onnx translator";
    LOGFLF(LogLevel::info, msg);
  }
  if (!translator) {
    return;
  }
  if (translationEnabled && !translator->ready()) {
    translator->open();
  } else if (!translationEnabled && translator->ready()) {
    translator->close();
  }
}

const char* SubtitleAsr::translateText(const char* text) {
  if (translator && translator->ready() && text) {
    const char* result = translator->translate(text);
    if (result && result[0] != '\0') {
      return result;
    }
  }
  return text;
}

const char* SubtitleAsr::getStreamingText() {
  std::lock_guard<std::mutex> lock(queueMutex);
  return streamingText.c_str();
}

const SubtitleItem* SubtitleAsr::getCurrent(int64_t ptsMs) {
  if (asrMode != AsrMode::ptsSync) return nullptr;
  std::lock_guard<std::mutex> lock(queueMutex);
  if (subtitleQueue.empty()) return nullptr;
  int32_t left = 0;
  int32_t right = static_cast<int32_t>(subtitleQueue.size()) - 1;
  while (left <= right) {
    int32_t mid = left + (right - left) / 2;
    const auto& item = subtitleQueue[mid];
    if (ptsMs >= item.startMs && ptsMs <= item.endMs) {
      return &item;
    } else if (ptsMs < item.startMs) {
      right = mid - 1;
    } else {
      left = mid + 1;
    }
  }
  // 未精确匹配
  // 情况1：播放时间已超过所有字幕（识别延迟），显示最后一条，最多延迟2秒
  if (subtitleQueue.back().endMs < ptsMs) {
    if (ptsMs - subtitleQueue.back().endMs < 2000) {
      return &subtitleQueue.back();
    }
    return nullptr;
  }
  // 情况2：ptsMs 落在两个字幕之间的间隙中，只在间隙较小时回退，避免大间隙残留
  for (int32_t i = static_cast<int32_t>(subtitleQueue.size()) - 1; i >= 0;
       --i) {
    const auto& item = subtitleQueue[i];
    if (item.endMs < ptsMs) {
      if (i + 1 < subtitleQueue.size()) {
        int64_t gap = subtitleQueue[i + 1].startMs - item.endMs;
        if (gap < 3000 && ptsMs - item.endMs < gap) {
          return &item;
        }
      }
      break;
    }
  }
  return nullptr;
}

void SubtitleAsr::inputSpeech(const AvoxData& data, int64_t pts) {
  // 直接识别，AudioStt 自己管理线程
  if (audioStt) {
    audioStt->recognize(data, pts);
  }
}

void SubtitleAsr::onResult(const SttResult& result, const char* text) {
  if (!text || text[0] == '\0') {
    return;
  }
  if (asrMode == AsrMode::streaming) {
    // streaming 模式：不翻译，直接显示
    std::lock_guard<std::mutex> lock(queueMutex);
    streamingText = text;
  } else {
    // ptsSync 不堵塞队列，如果满了就丢弃，避免堵塞线程
    SttTextResult item = {};
    item.result = result;
    item.text = text;
    resultQueue.enqueue(item, true);
  }
  // LOGFLF(LogLevel::info, "stt text: ", text);
}

void SubtitleAsr::onPartialResult(const char* text) {
  if (asrMode == AsrMode::streaming && text && text[0] != '\0') {
    std::lock_guard<std::mutex> lock(queueMutex);
    streamingText = text;
  }
}

void SubtitleAsr::onEndpoint() {}

void SubtitleAsr::processResult(const SttTextResult& item) {
  SubtitleItem sub = {};
  sub.startMs = item.result.startPts;
  sub.endMs = item.result.endPts;
  sub.original = item.text;
  sub.language = item.result.lang;
  sub.text = item.text;
  if (translationEnabled) {
    bool btrans = bHttp || (!bHttp && item.result.lang == Language::ja);
    if (translator && btrans) {
      translator->setSourceLanguage(item.result.lang);
      const char* translated = translateText(item.text.c_str());
      // LOGFLF(LogLevel::info, "translated lang:", getLangCode(item.result.lang), "
      // ",
      //        item.text, "->", translated);
      if (translated && translated[0] != '\0') {
        sub.text = translated;
      }
    }
  }
  std::lock_guard<std::mutex> lock(queueMutex);
  subtitleQueue.push_back(sub);
}

}
