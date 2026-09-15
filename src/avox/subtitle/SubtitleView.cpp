#include "SubtitleView.hpp"

#include "../module/AvoxManager.hpp"
#include "../module/LogHelper.hpp"
#include "../video/WindowRender.hpp"

namespace avox {

static constexpr int32_t kMaxPendingChunks = 512;

SubtitleView::SubtitleView() = default;

SubtitleView::~SubtitleView() {
  close();
  if (windowRender) {
    if (canvasLayer) {
      disableRenderCanvas(windowRender);
      canvasLayer = nullptr;
    }
    removeSurfaceRenderOb(windowRender, this);
  }
}

void SubtitleView::setAsrMode(AsrMode mode) { subtitleAsr.setAsrMode(mode); }

void SubtitleView::setWindowRender(ISurfaceRender* render) {
  if (windowRender) {
    removeSurfaceRenderOb(windowRender, this);
  }
  windowRender = render;
  if (windowRender) {
    addSurfaceRenderOb(windowRender, this);
  }
  // 渲染对象换了(图重建): 旧画布层作废; 新层是空的, 置 -1 强制重传内容
  canvasLayer = nullptr;
  lastSeq = -1;
}

void SubtitleView::setStorageSize(int32_t width, int32_t height) {
  if (width > 0 && height > 0) {
    storageW = width;
    storageH = height;
  }
}

void SubtitleView::checkWindowRender() {
  if (!windowRender) {
    return;
  }
  // 有内容的槽位才挂层: 轨槽看通道, 外挂看两路文件态, ASR 看引擎
  const Slot cur = slots.active();
  const bool bShow = (cur == Slot::track && overlay) ||
                     (cur == Slot::file && (fileEnabled || overlay)) ||
                     (cur == Slot::asr && asrEnabled);
  if (bShow && !canvasLayer) {
    canvasLayer = enableRenderCanvas(windowRender);
    lastSeq = -1;
  } else if (!bShow && canvasLayer) {
    disableRenderCanvas(windowRender);
    canvasLayer = nullptr;
    lastSeq = 0;
  }
}

// ---- 三槽位仲裁 ----

void SubtitleView::teardownSlot(Slot slot) {
  switch (slot) {
    case Slot::track:
      closeTrackChannel();
      break;
    case Slot::file:
      closeFileContent();
      break;
    case Slot::asr:
      closeAsrContent();
      break;
    default:
      break;
  }
}

SubtitleView::Slot SubtitleView::activateTrack() {
  const Slot prev = slots.activate(Slot::track);
  if (prev != Slot::track) {
    teardownSlot(prev);
  }
  lastSeq = -1;  // 换槽/换轨: 画布旧内容不可复用, 下帧强制重传
  return prev;
}

SubtitleView::Slot SubtitleView::activateFile() {
  const Slot prev = slots.activate(Slot::file);
  if (prev != Slot::file) {
    teardownSlot(prev);
  }
  // 同槽位换渲染路径(.srt↔.ass)或重复加载: 两路文件内容态都清(轨通道整关,
  // 下次 .ass 外挂经 openTrackChannel 重建)
  closeFileContent();
  closeTrackChannel();
  lastSeq = -1;
  return prev;
}

SubtitleView::Slot SubtitleView::activateAsr() {
  const Slot prev = slots.activate(Slot::asr);
  if (prev != Slot::asr) {
    teardownSlot(prev);
  }
  subtitleAsr.loadAsr();
  asrEnabled = true;
  lastSeq = -1;
  return prev;
}

bool SubtitleView::deactivateTrack() {
  if (slots.active() != Slot::track) {
    return false;
  }
  slots.deactivateIf(Slot::track);
  closeTrackChannel();
  return true;
}

void SubtitleView::enableAsr() { activateAsr(); }

void SubtitleView::close() {
  slots.reset();
  closeTrackChannel();
  closeFileContent();
  closeAsrContent();
}

// ---- 轨槽通道 ----

bool SubtitleView::openTrackChannel() {
  if (overlay) {
    return true;
  }
  // 未装插件 → create 返回 nullptr → 降级为无字幕轨(不崩)
  overlay = AvoxManager::Get().assOverlayHub.create("libass");
  if (!overlay) {
    LOGFLF(LogLevel::info, "subtitle view: no libass plugin, track off");
    return false;
  }
  if (!overlay->init(storageW, storageH)) {
    delete overlay;
    overlay = nullptr;
    return false;
  }
  LOGFLF(LogLevel::info, "subtitle view: track channel open storage:",
         storageW, "x", storageH);
  return true;
}

void SubtitleView::closeTrackChannel() {
  {
    std::lock_guard<std::mutex> lock(mtx);
    chunks.clear();
  }
  if (overlay) {
    overlay->shutdown();
    delete overlay;
    overlay = nullptr;
  }
  trackLoaded = false;
  hasPgs = false;
  lastPgsSeq = 0;
  if (canvasLayer && windowRender) {
    disableRenderCanvas(windowRender);
    canvasLayer = nullptr;
  }
  lastSeq = 0;
}

bool SubtitleView::loadTrack(const char* extradata, int32_t size) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!overlay) {
    return false;
  }
  bool ok = overlay->loadTrack(extradata, size);
  chunks.clear();  // 换轨: 旧事件作废
  overlay->flush();
  lastSeq = -1;  // 强制清层
  trackLoaded = ok;
  return ok;
}

bool SubtitleView::loadTrackFile(const char* path) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!overlay) {
    return false;
  }
  bool ok = overlay->loadFile(path);
  chunks.clear();
  overlay->flush();
  lastSeq = -1;
  trackLoaded = ok;
  return ok;
}

bool SubtitleView::loadTextFile(const char* path) {
  if (!path) {
    return false;
  }
  closeFileContent();
  fileEnabled = subtitleFile.loadFile(path);
  return fileEnabled;
}

void SubtitleView::pushChunk(const char* data, int32_t size, int64_t ptsMs,
                             int64_t durationMs) {
  if (!data || size <= 0 || !trackLoaded.load()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mtx);
  if (!overlay) {
    return;
  }
  if (chunks.size() >= kMaxPendingChunks) {
    chunks.pop_front();  // 消费端卡死时丢最旧, 防无限堆积
  }
  SubChunk& c = chunks.emplace_back();
  c.data.assign(data, data + size);
  c.ptsMs = ptsMs;
  c.durationMs = durationMs;
}

void SubtitleView::setPgsCanvas(const AssCanvas& canvas) {
  if (canvas.seq == lastPgsSeq) {
    return;
  }
  std::lock_guard<std::mutex> lock(mtx);
  if (canvas.rgba && canvas.width > 0 && canvas.height > 0) {
    pgsBuf.assign(canvas.rgba,
                  canvas.rgba + (size_t)canvas.stride * canvas.height);
    pgsCanvas = canvas;
    pgsCanvas.rgba = pgsBuf.data();
    hasPgs = true;
  } else {
    pgsBuf.clear();
    pgsCanvas = AssCanvas{};
    pgsCanvas.seq = canvas.seq;
    hasPgs = true;
  }
  lastPgsSeq = canvas.seq;
  lastSeq = -1;  // 源切换(ASS↔PGS 的 seq 域不同), 强制下一帧重传
}

void SubtitleView::resetEvents() {
  std::lock_guard<std::mutex> lock(mtx);
  chunks.clear();
  if (overlay) {
    overlay->flush();
  }
  lastSeq = 0;
  // seek 后事件从头喂, 但轨本身仍有效 — trackLoaded 不动
}

// ---- 渲染 ----

void SubtitleView::onRender() {
  checkWindowRender();
  if (!canvasLayer) {
    return;
  }
  const int64_t pts = clock.clock();
  // 胜者槽位独占画布: 空窗就空屏, 不回落(后激活者胜)
  switch (slots.active()) {
    case Slot::track:
      renderTrack(pts);
      break;
    case Slot::file:
    case Slot::asr:
#ifdef AVOX_ENABLE_FREETYPE
      renderText(pts);
#endif
      break;
    default:
      break;
  }
}

void SubtitleView::renderTrack(int64_t ptsMs) {
  if (!overlay) {
    return;
  }
  const AssCanvas* canvas = nullptr;
  {
    // 锁内只做短操作(喂包/取快照); 长持锁会卡死 IO 线程的 pushChunk
    std::lock_guard<std::mutex> lock(mtx);
    if (trackLoaded.load()) {
      // ASS 轨: 播放时钟前的小窗口预喂(补偿帧间隔与渲染延迟)
      while (!chunks.empty() && chunks.front().ptsMs <= ptsMs + 120) {
        SubChunk& c = chunks.front();
        // FFmpeg 的 MKV ASS packet 自带 ReadOrder 头("0,0,Default,..."),
        // 恰好是 ass_process_chunk 要的格式, 原样直喂
        overlay->processChunk(c.data.data(), (int32_t)c.data.size(), c.ptsMs,
                              c.durationMs);
        chunks.pop_front();
      }
      // libass 画布双缓冲, 内容到下一次 render 前有效 → 锁外使用安全
      canvas = overlay->render(ptsMs);
    } else if (hasPgs) {
      // PGS 轨: 锁内把像素拷到稳定缓冲(pgsBuf 归 IO 线程的 setPgsCanvas
      // 重排), 锁外上屏
      if (pgsCanvas.rgba) {
        pgsStable.assign(pgsBuf.begin(), pgsBuf.end());
        pgsSnapshot = pgsCanvas;
        pgsSnapshot.rgba = pgsStable.data();
      } else {
        pgsSnapshot = AssCanvas{};
        pgsSnapshot.seq = pgsCanvas.seq;
      }
      canvas = &pgsSnapshot;
    }
  }
  if (!canvas) {
    return;  // 轨未加载且无 PGS 画布: 本帧无内容
  }
  if (canvas->seq == lastSeq) {
    return;  // 内容未变, 零上传
  }
  lastSeq = canvas->seq;
  if (canvas->rgba) {
    canvasLayer->updateCanvas(canvas->rgba, canvas->width, canvas->height,
                              canvas->stride, canvas->x, canvas->y);
  } else {
    canvasLayer->clearCanvas();
  }
}

void SubtitleView::renderText(int64_t ptsMs) {
  if (storageW <= 0 || storageH <= 0) {
    (void)ptsMs;
    return;
  }
#ifdef AVOX_ENABLE_FREETYPE
  const char* text = nullptr;
  // 槽位定源: ASR 槽只查识别结果(流式部分结果优先兜底), 文件槽只查文件
  if (slots.active() == Slot::asr) {
    auto* item = subtitleAsr.getCurrent(ptsMs);
    if (item && !item->text.empty()) {
      text = item->text.c_str();
    }
    if (!text) {
      const char* streaming = subtitleAsr.getStreamingText();
      if (streaming && streaming[0] != '\0') {
        text = streaming;
      }
    }
  } else {
    auto* item = subtitleFile.getCurrent(ptsMs);
    if (item && !item->text.empty()) {
      text = item->text.c_str();
    }
  }
  // 文本 → RGBA bbox canvas → 统一混合层(内容变化才上传)
  const int32_t seq = rasterizer.render(text, storageW, storageH);
  if (seq == 0) {
    if (lastSeq != 0) {
      canvasLayer->clearCanvas();
      lastSeq = 0;
    }
    return;
  }
  if (seq == lastSeq) {
    return;
  }
  lastSeq = seq;
  canvasLayer->updateCanvas(rasterizer.rgba(), rasterizer.width(),
                            rasterizer.height(), rasterizer.stride(),
                            rasterizer.offsetX(), rasterizer.offsetY());
#else
  (void)ptsMs;
#endif
}

SubtitleAsr* SubtitleView::getSubtitleAsr() { return &subtitleAsr; }

void SubtitleView::setAudioDesc(AudioDesc desc) { subtitleAsr.setAudioDesc(desc); }

void SubtitleView::inputSpeech(const AvoxData& data, int64_t pts) {
  if (!asrEnabled) {
    return;
  }
  subtitleAsr.inputSpeech(data, pts);
}

void SubtitleView::closeFileContent() {
  if (fileEnabled) {
    subtitleFile.clear();
    fileEnabled = false;
  }
}

void SubtitleView::closeAsrContent() {
  if (asrEnabled) {
    subtitleAsr.unloadAsr();
    asrEnabled = false;
  }
}

}
