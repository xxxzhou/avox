#include "AssOverlayView.hpp"

#include "../module/AvoxManager.hpp"
#include "../module/LogHelper.hpp"
#include "../video/WindowRender.hpp"

namespace avox {

static constexpr int32_t kMaxPendingChunks = 512;

AssOverlayView::~AssOverlayView() { close(); }

void AssOverlayView::setSurfaceRender(ISurfaceRender* render) {
  if (surfaceRender) {
    removeSurfaceRenderOb(surfaceRender, this);
  }
  surfaceRender = render;
  if (surfaceRender) {
    addSurfaceRenderOb(surfaceRender, this);
  }
  // 渲染对象换了(图重建): 旧层作废, onRender 里 ensureLayer 重新挂
  canvasLayer = nullptr;
}

bool AssOverlayView::open(int32_t storageWidth, int32_t storageHeight) {
  if (overlay) {
    return true;
  }
  // 未装插件 → create 返回 nullptr → 降级为无字幕轨(不崩)
  overlay = AvoxManager::Get().assOverlayHub.create("libass");
  if (!overlay) {
    LOGFLF(LogLevel::info, "ass overlay: no libass plugin, subtitle track off");
    return false;
  }
  if (!overlay->init(storageWidth, storageHeight)) {
    delete overlay;
    overlay = nullptr;
    return false;
  }
  LOGFLF(LogLevel::info, "ass overlay: open storage:", storageWidth, "x",
         storageHeight);
  return true;
}

void AssOverlayView::close() {
  {
    std::lock_guard<std::mutex> lock(mtx);
    chunks.clear();
  }
  if (overlay) {
    overlay->shutdown();
    delete overlay;
    overlay = nullptr;
  }
  if (canvasLayer && surfaceRender) {
    disableRenderCanvas(surfaceRender);
  }
  canvasLayer = nullptr;
  lastSeq = 0;
  trackLoaded = false;
}

bool AssOverlayView::loadTrack(const char* extradata, int32_t size) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!overlay) {
    return false;
  }
  bool ok = overlay->loadTrack(extradata, size);
  chunks.clear();  // 换轨: 旧事件作废
  overlay->flush();
  lastSeq = 0;  // 强制清层
  trackLoaded = ok;
  return ok;
}

bool AssOverlayView::loadFile(const char* path) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!overlay) {
    return false;
  }
  bool ok = overlay->loadFile(path);
  chunks.clear();
  overlay->flush();
  lastSeq = 0;
  trackLoaded = ok;
  return ok;
}

void AssOverlayView::pushChunk(const char* data, int32_t size, int64_t ptsMs,
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

void AssOverlayView::setPgsCanvas(const AssCanvas& canvas) {
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
  lastSeq = 0;  // 源切换(ASS↔PGS 的 seq 域不同), 强制下一帧重传
}

void AssOverlayView::resetEvents() {
  std::lock_guard<std::mutex> lock(mtx);
  chunks.clear();
  if (overlay) {
    overlay->flush();
  }
  lastSeq = 0;
  // seek 后事件从头喂, 但轨本身仍有效 — trackLoaded 不动
}

void AssOverlayView::ensureLayer() {
  if (canvasLayer || !surfaceRender || !overlay) {
    return;
  }
  canvasLayer = enableRenderCanvas(surfaceRender);
  if (canvasLayer) {
    LOGFLF(LogLevel::info, "ass overlay: canvas layer enabled");
  }
}

void AssOverlayView::onRender() {
  if (!overlay) {
    return;
  }
  ensureLayer();
  if (!canvasLayer) {
    return;  // 无 Vulkan 图(CPU 兜底路径): v1 明确不支持, 计划 §5 风险已记
  }
  const int64_t pts = clock.clock();
  const AssCanvas* canvas = nullptr;
  {
    // 锁内只做短操作(喂包/取快照); 长持锁会卡死 IO 线程的 pushChunk
    std::lock_guard<std::mutex> lock(mtx);
    if (trackLoaded.load()) {
      // ASS 轨: 播放时钟前的小窗口预喂(补偿帧间隔与渲染延迟)
      while (!chunks.empty() && chunks.front().ptsMs <= pts + 120) {
        SubChunk& c = chunks.front();
        // FFmpeg 的 MKV ASS packet 自带 ReadOrder 头("0,0,Default,..."),
        // 恰好是 ass_process_chunk 要的格式, 原样直喂
        overlay->processChunk(c.data.data(), (int32_t)c.data.size(), c.ptsMs,
                              c.durationMs);
        chunks.pop_front();
      }
      // libass 画布双缓冲, 内容到下一次 render 前有效 → 锁外使用安全
      canvas = overlay->render(pts);
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
  if (canvas && canvas->seq == lastSeq) {
    return;  // 内容未变, 零上传
  }
  if (!canvas) {
    return;  // 轨未加载且无 PGS 画布: 本帧无内容
  }
  lastSeq = canvas->seq;
  if (canvas->rgba) {
    canvasLayer->updateCanvas(canvas->rgba, canvas->width, canvas->height,
                              canvas->stride, canvas->x, canvas->y);
  } else {
    canvasLayer->clearCanvas();
  }
}

}
