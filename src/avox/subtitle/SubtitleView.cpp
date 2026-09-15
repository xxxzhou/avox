#include "SubtitleView.hpp"

#include "../AvoxLayer.h"
#include "SubtitleAsr.hpp"
#include "SubtitleFile.hpp"

namespace avox {

SubtitleView::SubtitleView() : clock(std::make_unique<Clock>()) {}

SubtitleView::~SubtitleView() {
  if (windowRender) {
    if (canvasLayer) {
      disableRenderCanvas(windowRender);
      canvasLayer = nullptr;
    }
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
  // 渲染对象换了(图重建): 旧画布层作废, checkWindowRender 重新挂
  canvasLayer = nullptr;
  lastCanvasSeq = 0;
}

void SubtitleView::setStorageSize(int32_t width, int32_t height) {
  if (width > 0 && height > 0) {
    storageW = width;
    storageH = height;
  }
}

void SubtitleView::checkWindowRender() {
  bool bShow = fileEnabled || asrEnabled;
  if (!windowRender) {
    return;
  }
  if (bShow && !canvasLayer) {
    canvasLayer = enableRenderCanvas(windowRender);
  } else if (!bShow && canvasLayer) {
    disableRenderCanvas(windowRender);
    canvasLayer = nullptr;
    lastCanvasSeq = 0;
  }
}

bool SubtitleView::loadFile(const char* path) {
  if (!path) return false;
  closeFile();
  fileEnabled = subtitleFile->loadFile(path);
  return fileEnabled;
}

void SubtitleView::closeFile() {
  if (fileEnabled) {
    subtitleFile->clear();
    fileEnabled = false;
  }
}

void SubtitleView::enableAsr() {
  subtitleAsr->loadAsr();
  asrEnabled = true;
}

void SubtitleView::close() {
  closeFile();
  if (asrEnabled) {
    subtitleAsr->unloadAsr();
    asrEnabled = false;
  }
}

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
  if (!canvasLayer || storageW <= 0 || storageH <= 0) {
    (void)ptsMs;
    return;
  }
#ifdef AVOX_ENABLE_FREETYPE
  const char* text = nullptr;
  // 优先 ASR，其次文件
  if (asrEnabled) {
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
    auto* item = subtitleFile->getCurrent(ptsMs);
    if (item && !item->text.empty()) {
      text = item->text.c_str();
    }
  }
  // 文本 → RGBA bbox canvas → 统一混合层(内容变化才上传)
  const int32_t seq = rasterizer.render(text, storageW, storageH);
  if (seq == 0) {
    if (lastCanvasSeq != 0) {
      canvasLayer->clearCanvas();
      lastCanvasSeq = 0;
    }
    return;
  }
  if (seq == lastCanvasSeq) {
    return;
  }
  lastCanvasSeq = seq;
  canvasLayer->updateCanvas(rasterizer.rgba(), rasterizer.width(),
                            rasterizer.height(), rasterizer.stride(),
                            rasterizer.offsetX(), rasterizer.offsetY());
#else
  (void)ptsMs;
#endif
}

}
