#include "SubtitleView.hpp"

#include "../AvoxLayer.h"
#include "SubtitleAsr.hpp"
#include "SubtitleFile.hpp"

#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FreetypeExport.h"
#endif

namespace avox {

SubtitleView::SubtitleView() : clock(std::make_unique<Clock>()) {}

SubtitleView::~SubtitleView() {
  if (windowRender) {
#ifdef AVOX_ENABLE_FREETYPE
    disableRenderFont(windowRender);
#endif
    removeSurfaceRenderOb(windowRender, this);
  }
}

void SubtitleView::setAsrMode(AsrMode mode) { subtitleAsr->setAsrMode(mode); }

void SubtitleView::setWindowRender(ISurfaceRender* render) {
  if (windowRender) {
    removeSurfaceRenderOb(windowRender, this);
  }
  windowRender = render;
  if (windowRender) {
    addSurfaceRenderOb(windowRender, this);
  }
}

void SubtitleView::checkWindowRender() {
  bool bShow = fileEnabled || asrEnabled;
  if (!windowRender) {
    return;
  }
#ifdef AVOX_ENABLE_FREETYPE
  if (bShow && !bLoadFont) {
    fontLayer = enableRenderFont(windowRender);
    fontLayer->setFont("simhei.ttf", 40);    
    bLoadFont = true;
  }
  if (!bShow && bLoadFont) {
    disableRenderFont(windowRender);
    fontLayer = nullptr;
    bLoadFont = false;
  }
#endif
}

bool SubtitleView::loadSrt(const char* path) {
  if (!path) return false;
  close();
  fileEnabled = subtitleFile->loadFile(path);
  return fileEnabled;
}

void SubtitleView::enableAsr() {
  subtitleAsr->loadAsr();
  asrEnabled = true;
}

void SubtitleView::close() {
  if (fileEnabled) {
    subtitleFile->clear();
    fileEnabled = false;
  }
  if (asrEnabled) {    
    subtitleAsr->unloadAsr();
    asrEnabled = false;
  }
}

void SubtitleView::enableTranslation() { subtitleAsr->enableTranslation(); }

void SubtitleView::disableTranslation() { subtitleAsr->disableTranslation(); }

Clock* SubtitleView::getClock() { return clock.get(); }

void SubtitleView::onRender() {
  checkWindowRender();
  if (clock) {
    update(clock->clock());
  }
}

SubtitleAsr* SubtitleView::getSubtitleAsr() { return subtitleAsr.get(); }

void SubtitleView::setAudioDesc(AudioDesc desc) {
  subtitleAsr->setAudioDesc(desc);
}

void SubtitleView::inputSpeech(const AvoxData& data, int64_t pts) {
  if (!asrEnabled) {
    return;
  }
  subtitleAsr->inputSpeech(data, pts);
}

void SubtitleView::update(int64_t ptsMs) {
#ifdef AVOX_ENABLE_FREETYPE
  if (!fontLayer) return;
  const char* text = nullptr;
  // 优先 ASR，其次文件
  if (asrEnabled) {
    // ASR 字幕已在入队时翻译
    auto* item = subtitleAsr->getCurrent(ptsMs);
    if (item && !item->text.empty()) {
      text = item->text.c_str();
    }
    // 流式模式
    if (!text) {
      const char* streaming = subtitleAsr->getStreamingText();
      if (streaming && streaming[0] != '\0') {
        text = streaming;
      }
    }
  }
  if (!text) {
    // 文件字幕在查找时翻译
    auto* item = subtitleFile->getCurrent(ptsMs);
    if (item && !item->text.empty()) {
      text = item->text.c_str();
    }
  }
  if (text) {
    fontLayer->drawText(text);
  } else {
    fontLayer->drawText("");
  }
#else
  (void)ptsMs;
#endif
}

}
