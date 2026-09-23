#include "SubtitleView.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../module/AvoxManager.hpp"
#include "../module/LogHelper.hpp"
#include "../video/WindowRender.hpp"

namespace avox {

static constexpr int32_t kMaxPendingChunks = 512;

// 内封文本采样 → ass_process_chunk 对白行("ReadOrder,Layer,Style,...,Text",
// 时间走调用入参)。mov_text(tx3g) 样本是 2 字节大端长度 + UTF-8 文本(可尾随
// 样式 atom); MKV SRT 无前缀 — 前缀长度对不上就整包按文本。剥富文本标签,
// 换行折 \N, 剥花括号防 ASS 覆盖块注入。
static std::string textSampleToAssChunk(const char* data, int32_t size) {
  if (!data || size <= 0) {
    return {};
  }
  const auto* d = (const uint8_t*)data;
  size_t off = 0;
  if (size >= 2) {
    const uint32_t len = (uint32_t)((d[0] << 8) | d[1]);
    if (len > 0 && 2 + len <= (size_t)size) {
      off = 2;
    }
  }
  std::string line;
  line.reserve((size_t)size);
  bool inTag = false;
  for (size_t i = off; i < (size_t)size; ++i) {
    const char ch = (char)d[i];
    if (ch == 0) {
      break;  // 尾随样式 atom 的容错截断
    }
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      line += "\\N";
      continue;
    }
    if (ch == '<') {
      inTag = true;
      continue;
    }
    if (ch == '>') {
      inTag = false;
      continue;
    }
    if (inTag || ch == '{' || ch == '}') {
      continue;
    }
    line += ch;
  }
  while (line.size() >= 2 && line.compare(line.size() - 2, 2, "\\N") == 0) {
    line.resize(line.size() - 2);
  }
  if (line.empty()) {
    return {};
  }
  std::string out = "0,0,Default,,0,0,0,,";
  out += line;
  return out;
}

SubtitleView::SubtitleView() = default;

SubtitleView::~SubtitleView() {
  closeSubtitle();
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
    // 新挂层/新渲染对象: 层侧变换是默认值, 立即按当前槽位补发一次
    pushedSlot = Slot::none;
    pushCanvasTransform();
  } else if (!bShow && canvasLayer) {
    // 粘性层: 播放期摘/挂层都会全图重建(VK 设备资源再建在部分驱动上会
    // 死锁, 0923 内封→外挂切换崩), 空窗只清内容不清层
    if (lastSeq != 0) {
      canvasLayer->clearCanvas();
      lastSeq = 0;
    }
  }
}

// ---- 三槽位仲裁 ----

void SubtitleView::teardownSlot(Slot slot) {
  switch (slot) {
    case Slot::track:
      closeTrackChannel();
      // 轨槽被顶掉: 通知播放器复位 IO 侧(轨号 + PGS 解码路由)
      if (trackResetCb) {
        trackResetCb();
      }
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
  lastSeq = -1;  // 换源强制重传
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

bool SubtitleView::deactivateFile() {
  if (slots.active() != Slot::file) {
    return false;
  }
  slots.deactivateIf(Slot::file);
  // 两路文件内容态都清: .srt 文本 + .ass(经 overlay 轨通道), 同 activateFile
  closeFileContent();
  closeTrackChannel();
  return true;
}

bool SubtitleView::deactivateAsr() {
  if (slots.active() != Slot::asr) {
    return false;
  }
  slots.deactivateIf(Slot::asr);
  closeAsrContent();
  return true;
}

void SubtitleView::enableAsr() { activateAsr(); }

void SubtitleView::disableAsr() { deactivateAsr(); }

// ---- ISubtitle: 观感样式(任意线程可调, 不触发图重建; 生效矩阵见 AvoxPlayer.h) ----

void SubtitleView::setScale(float s) {
  if (s <= 0.f) {
    return;  // <=0 忽略, 保持上次有效值
  }
  {
    std::lock_guard<std::mutex> lock(styleMtx);
    scale = s;
  }
  pushCanvasTransform();
}

void SubtitleView::setOffset(float offsetX_, float offsetY_) {
  {
    std::lock_guard<std::mutex> lock(styleMtx);
    offsetX = offsetX_;
    offsetY = offsetY_;
  }
  pushCanvasTransform();
}

void SubtitleView::setOpacity(float o) {
  o = std::min(std::max(o, 0.f), 1.f);
  {
    std::lock_guard<std::mutex> lock(styleMtx);
    opacity = o;
  }
  pushCanvasTransform();
}

void SubtitleView::setFont(const char* fontName, int32_t fontSize) {
#ifdef AVOX_ENABLE_FREETYPE
  std::lock_guard<std::mutex> lock(styleMtx);
  // 存副本: const char* 可能来自脚本层的临时串(FontCache 亦不收 nullptr)
  if (fontName && fontName[0] != '\0') {
    style.fontName = fontName;
  }
  if (fontSize > 0) {
    style.fontSize = fontSize;
  }
  ++styleSeq;
#endif
}

void SubtitleView::setColor(float r, float g, float b) {
#ifdef AVOX_ENABLE_FREETYPE
  std::lock_guard<std::mutex> lock(styleMtx);
  style.colorR = std::min(std::max(r, 0.f), 1.f);
  style.colorG = std::min(std::max(g, 0.f), 1.f);
  style.colorB = std::min(std::max(b, 0.f), 1.f);
  ++styleSeq;
#endif
}

void SubtitleView::setAlign(HAlignType h, VAlignType v) {
#ifdef AVOX_ENABLE_FREETYPE
  std::lock_guard<std::mutex> lock(styleMtx);
  if (h != HAlignType::none) {
    style.hAlign = h;
  }
  if (v != VAlignType::none) {
    style.vAlign = v;
  }
  ++styleSeq;
#endif
}

void SubtitleView::setPosition(float anchorX, float anchorY) {
#ifdef AVOX_ENABLE_FREETYPE
  std::lock_guard<std::mutex> lock(styleMtx);
  style.anchorXRatio = std::min(std::max(anchorX, 0.f), 1.f);
  style.anchorYRatio = std::min(std::max(anchorY, 0.f), 1.f);
  ++styleSeq;
#endif
}

void SubtitleView::setPositionMargin(float marginX_, float marginY_) {
#ifdef AVOX_ENABLE_FREETYPE
  std::lock_guard<std::mutex> lock(styleMtx);
  style.marginX = std::max(marginX_, 0.f);
  style.marginY = std::max(marginY_, 0.f);
  ++styleSeq;
#endif
}

void SubtitleView::setMaxWidth(float ratio) {
#ifdef AVOX_ENABLE_FREETYPE
  if (ratio <= 0.f) {
    return;
  }
  std::lock_guard<std::mutex> lock(styleMtx);
  style.maxWidthRatio = std::min(ratio, 1.f);
  ++styleSeq;
#endif
}

// 全局变换按胜者槽位二选一下发到画布层前端: 文本槽的 scale/offset/opacity
// 已被 CPU 侧重栅格化消费, 层侧必须置单位值, 否则双份缩放; 轨槽(ASS/PGS)与
// 外挂 .ass/.ssa(libass 通道)由 canvas 合成消费, 直发用户值
void SubtitleView::pushCanvasTransform() {
  if (!canvasLayer) {
    return;
  }
  float s, ox, oy, op;
  {
    std::lock_guard<std::mutex> lock(styleMtx);
    s = scale;
    ox = offsetX;
    oy = offsetY;
    op = opacity;
  }
  const Slot cur = slots.active();
  // 外挂槽要看内容在哪: .ass/.ssa 在 libass 通道里(走 GPU), 其余是纯文本(CPU)
  const bool bTextSlot =
      (cur == Slot::asr) || (cur == Slot::file && !fileUseLibass());
  if (bTextSlot) {
    canvasLayer->setCanvasTransform(1.f, 0.f, 0.f, 1.f);
  } else {
    canvasLayer->setCanvasTransform(s, ox, oy, op);
  }
}

void SubtitleView::closeSubtitle() {
  slots.reset();
  closeTrackChannel();
  closeFileContent();
  closeAsrContent();
  {
    std::lock_guard<std::mutex> lock(scanMtx);
    scanCache.clear();  // 候选缓存随源失效
  }
}

// ---- 轨槽通道 ----

bool SubtitleView::openTrackChannel() {
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (overlay) {
      return true;
    }
  }
  // 未装插件 → create 返回 nullptr → 降级为无字幕轨(不崩)
  IAssOverlay* created = AvoxManager::Get().assOverlayHub.create("libass");
  if (!created) {
    LOGFLF(LogLevel::info, "subtitle view: no libass plugin, track off");
    return false;
  }
  if (!created->init(storageW, storageH)) {
    delete created;
    return false;
  }
  // 轨级样式覆盖补发(a01-T3): 建通道前设置的值在 init 后生效
  created->setStyleScale(assScale.load(std::memory_order_relaxed));
  {
    std::lock_guard<std::mutex> lock(assStyleMtx);
    created->setStyleFont(assFontFamily.c_str());
  }
  // 发布走 mtx: 渲染/IO 线程锁内读 overlay 指针
  std::lock_guard<std::mutex> lock(mtx);
  if (overlay) {
    delete created;
    return true;
  }
  overlay = created;
  LOGFLF(LogLevel::info, "subtitle view: track channel open storage:",
         storageW, "x", storageH);
  return true;
}

void SubtitleView::closeTrackChannel() {
  // 锁内完成全部状态摘除(trackLoaded/overlay 指针), 锁外析构: 渲染/IO 线程
  // 都是锁内解引用 overlay, 摘除后不可能再碰已 shutdown/delete 的对象
  IAssOverlay* dead = nullptr;
  {
    std::lock_guard<std::mutex> lock(mtx);
    chunks.clear();
    pgsFrames.clear();
    lastPgsSeq = 0;
    trackLoaded = false;
    textMode_ = false;
    dead = overlay;
    overlay = nullptr;
    // lastSeq 不动: 粘性层上画布内容仍在屏, 空窗清屏交给 checkWindowRender
    // 的 lastSeq!=0 判定 (在此谎报 0 会跳过 clearCanvas, 字幕残影不散)
  }
  if (dead) {
    dead->shutdown();
    delete dead;
  }
  // 画布层摘除不在这里做: 播放器线程碰 disableRenderCanvas/图重建旗标会与
  // 渲染线程的 resetGraph 互踩(0923 内封→外挂切换崩)。checkWindowRender
  // 每帧按胜者槽位重算 bShow, 下一帧自会摘/挂
}

bool SubtitleView::loadTrack(const char* extradata, int32_t size) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!overlay) {
    return false;
  }
  bool ok = overlay->loadTrack(extradata, size);
  chunks.clear();  // 换轨: 旧事件作废
  // 不 flush: 插件侧 loadTrack 已 unload + ass_new_track, 这里再 flush
  // (ass_flush_events) 会把 extradata 里可能带的事件一并清掉; 只有
  // resetEvents(seek 后旧事件作废) 才需要 flush
  lastSeq = -1;  // 强制清层
  textMode_ = false;
  trackLoaded = ok;
  return ok;
}

bool SubtitleView::loadTextTrack() {
  std::lock_guard<std::mutex> lock(mtx);
  textMode_ = true;
  if (!overlay) {
    return false;
  }
  // 合成最小 ASS 剧本(PlayRes=storage + Default 底部居中, 同插件外挂
  // srt 路径口径): 内封文本包(mov_text/SRT)采样转对白行后喂 libass
  char head[512];
  std::snprintf(head, sizeof(head),
                "[Script Info]\nScriptType: v4.00+\nPlayResX: %d\nPlayResY: %d\n\n"
                "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, "
                "OutlineColour, BackColour, Bold, Italic, BorderStyle, Outline, "
                "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                "Style: Default,sans-serif,54,&H00FFFFFF,&H00000000,&H00000000,"
                "0,0,1,2,0,2,60,60,40,1\n",
                storageW > 0 ? storageW : 1920, storageH > 0 ? storageH : 1080);
  bool ok = overlay->loadTrack(head, (int32_t)std::strlen(head));
  chunks.clear();
  lastSeq = -1;
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
  // 不 flush: 外挂 .ass 的事件就是 loadFile(ass_read_file) 载进来的, 这里 flush
  // 会当场清空 —— 曾导致"加载成功(subOk=1)却零渲染"; 清层交给 lastSeq=-1
  lastSeq = -1;
  textMode_ = false;
  trackLoaded = ok;
  return ok;
}

bool SubtitleView::loadTextFile(const char* path) {
  if (!path) {
    return false;
  }
  // 持 mtx 解析: 渲染线程锁内读 subtitleFile, 不隔离则 items 重建期即 UAF
  std::lock_guard<std::mutex> lock(mtx);
  closeFileContentUnlocked();
  fileEnabled = subtitleFile.loadFile(path);
  lastSeq = -1;  // 换源强制重传, 防画布 seq 域撞车残留旧内容
  return fileEnabled;
}

int32_t SubtitleView::listSubtitleCandidates(const char* videoUrl) {
  if (!videoUrl) {
    return -1;
  }
  std::vector<SubtitleCandidateInfo> found;
  // 同名遮蔽: 成员与非成员同名, 显式限定走自由函数
  avox::listSubtitleCandidates(std::string(videoUrl), &found);
  std::lock_guard<std::mutex> lock(scanMtx);
  scanCache.clear();
  for (const SubtitleCandidateInfo& c : found) {
    scanCache.emplace_back(c);
  }
  return (int32_t)scanCache.size();
}

ISubtitleCandidate* SubtitleView::getSubtitleCandidate(int32_t index) {
  std::lock_guard<std::mutex> lock(scanMtx);
  if (index < 0 || index >= (int32_t)scanCache.size()) {
    return nullptr;
  }
  return &scanCache[(size_t)index];
}

void SubtitleView::setAssScale(float scale) {
  if (!(scale > 0.f)) {
    return;  // <=0/NaN 忽略保持现值(同 setScale 口径)
  }
  assScale.store(scale, std::memory_order_relaxed);
  // overlay 的 setStyleScale 在 plugin 侧即刻生效; 无插件时 openTrackChannel 补发
  std::lock_guard<std::mutex> lock(mtx);
  if (overlay) {
    overlay->setStyleScale(scale);
  }
}

void SubtitleView::setAssFont(const char* family) {
  {
    std::lock_guard<std::mutex> lock(assStyleMtx);
    assFontFamily = family != nullptr ? family : "";
  }
  // 局部拷贝保证 c_str() 有效(plugin 同步拷走); overlay 调用走 mtx 防
  // 与通道拆除并发
  std::string familyCopy;
  {
    std::lock_guard<std::mutex> lock(assStyleMtx);
    familyCopy = assFontFamily;
  }
  std::lock_guard<std::mutex> lock(mtx);
  if (overlay) {
    overlay->setStyleFont(familyCopy.c_str());
  }
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
  const char* feed = data;
  int32_t feedSize = size;
  std::string converted;
  if (textMode_) {
    // 内封文本采样(mov_text/SRT)不是 ASS 对白, 先转 chunk(堆拷贝换指针)
    converted = textSampleToAssChunk(data, size);
    if (converted.empty()) {
      return;
    }
    feed = converted.data();
    feedSize = (int32_t)converted.size();
  }
  if (chunks.size() >= kMaxPendingChunks) {
    chunks.pop_front();  // 消费端卡死时丢最旧, 防无限堆积
  }
  SubChunk& c = chunks.emplace_back();
  c.data.assign(feed, feed + feedSize);
  c.ptsMs = ptsMs;
  c.durationMs = durationMs;
}

void SubtitleView::setPgsCanvas(const AssCanvas& canvas) {
  if (canvas.seq == lastPgsSeq) {
    return;
  }
  std::lock_guard<std::mutex> lock(mtx);
  PgsFrame f;
  f.ptsMs = canvas.ptsMs;
  f.seq = canvas.seq;
  f.empty = !(canvas.rgba && canvas.width > 0 && canvas.height > 0);
  if (!f.empty) {
    f.width = canvas.width;
    f.height = canvas.height;
    f.stride = canvas.stride;
    f.rgba.assign(canvas.rgba,
                  canvas.rgba + (size_t)canvas.stride * canvas.height);
  }
  pgsFrames.push_back(std::move(f));
  while (pgsFrames.size() > 8) {
    pgsFrames.pop_front();  // 有界: 溢出丢最旧(正常按 pts 消费不积压)
  }
  lastPgsSeq = canvas.seq;
  lastSeq = -1;  // 源切换(ASS↔PGS 的 seq 域不同), 强制下一帧重传
}

void SubtitleView::resetEvents() {
  std::lock_guard<std::mutex> lock(mtx);
  chunks.clear();
  pgsFrames.clear();  // seek/换轨: PGS 缓冲帧与 ASS 事件一并作废
  if (overlay) {
    overlay->flush();
  }
  lastSeq = 0;
  // seek 后事件从头喂, 但轨本身仍有效 — trackLoaded 不动
}

// ---- 渲染 ----

void SubtitleView::onRender(const SurfaceRenderEvent* ev) {
  (void)ev;
  checkWindowRender();
  if (!canvasLayer) {
    return;
  }
  // 图重建后新层为空(内容不随层迁移): 强制本帧重传, 否则字幕消失到下次内容变化
  if (canvasLayer->takeContentStale()) {
    lastSeq = -1;
  }
  // 胜者槽位变化 → 变换通道换源(文本槽单位值/轨槽用户值), 当帧生效
  const Slot cur = slots.active();
  if (cur != pushedSlot) {
    pushedSlot = cur;
    pushCanvasTransform();
  }
  const int64_t pts = clock.clock();
  // 胜者槽位独占画布: 空窗就空屏, 不回落(后激活者胜)
  switch (cur) {
    case Slot::track:
      renderTrack(pts);
      break;
    case Slot::file:
    case Slot::asr:
      // 外挂 .ass/.ssa 的内容在 libass 通道里(loadTrackFile 只喂 overlay),
      // 必须走 track 渲染; 纯文本 .srt 与 ASR 走文本光栅化
      if (cur == Slot::file && fileUseLibass()) {
        renderTrack(pts);
      } else {
#ifdef AVOX_ENABLE_FREETYPE
        renderText(pts);
#endif
      }
      break;
    default:
      break;
  }
}

void SubtitleView::renderTrack(int64_t ptsMs) {
  if (!overlay) {
    return;
  }
  // 字幕延迟(a01-T3): 内容选择用平移时钟 subPts = pts - delay(正=延后);
  // ASS 预喂窗口随平移, PGS 画布按到期放行(delay=0 保持原无条件快照)
  const int64_t delay = delayMs_.load(std::memory_order_relaxed);
  const int64_t subPts = ptsMs - delay;
  AssCanvas canvas = {};  // 锁内组装的稳定快照, 锁外只碰视图自有内存
  bool changed = false;
  {
    // 锁内只做短操作(喂包/取快照); 长持锁会卡死 IO 线程的 pushChunk
    std::lock_guard<std::mutex> lock(mtx);
    if (trackLoaded.load() && overlay) {
      // ASS 轨: 播放时钟前的小窗口预喂(补偿帧间隔与渲染延迟)
      while (!chunks.empty() && chunks.front().ptsMs <= subPts + 120) {
        SubChunk& c = chunks.front();
        // FFmpeg 的 MKV ASS packet 自带 ReadOrder 头("0,0,Default,..."),
        // 恰好是 ass_process_chunk 要的格式, 原样直喂
        overlay->processChunk(c.data.data(), (int32_t)c.data.size(), c.ptsMs,
                              c.durationMs);
        chunks.pop_front();
      }
      // 画布拷进 assStable: overlay 可能在锁外被拆除, 锁外引用插件内存即 UAF
      const AssCanvas* c = overlay->render(subPts);
      if (c && c->seq != lastSeq) {
        if (c->rgba) {
          assStable.assign(c->rgba, c->rgba + (size_t)c->stride * c->height);
          canvas = *c;
          canvas.rgba = assStable.data();
        } else {
          canvas = *c;  // 空帧(无像素): 只递 seq 让消费方清层
        }
        changed = true;
      }
    } else if (!pgsFrames.empty()) {
      // PGS 轨: 按(延迟平移后的)播放位置取不越于它的最新一帧, 其之前的帧
      // 就地回收; 有延迟时未到期帧不显示(等价上一条延长, 与旧口径一致)
      size_t curIdx = SIZE_MAX;
      for (size_t i = 0; i < pgsFrames.size(); ++i) {
        if (pgsFrames[i].ptsMs <= subPts) {
          curIdx = i;
        }
      }
      if (curIdx != SIZE_MAX) {
        if (curIdx > 0) {
          pgsFrames.erase(pgsFrames.begin(), pgsFrames.begin() + curIdx);
        }
        const PgsFrame& f = pgsFrames.front();
        if (!f.empty) {
          pgsStable = f.rgba;
          pgsSnapshot = AssCanvas{};
          pgsSnapshot.width = f.width;
          pgsSnapshot.height = f.height;
          pgsSnapshot.stride = f.stride;
          pgsSnapshot.ptsMs = f.ptsMs;
          pgsSnapshot.seq = f.seq;
          pgsSnapshot.rgba = pgsStable.data();
        } else {
          pgsSnapshot = AssCanvas{};
          pgsSnapshot.seq = f.seq;
        }
      }
      // 无到期帧: 维持当前快照(初始空快照 seq=0 与 lastSeq 初值相同, 零上传)
      canvas = pgsSnapshot;
      changed = canvas.seq != lastSeq;
    }
  }
  if (!changed) {
    return;  // 轨未加载且无 PGS 画布, 或内容未变 — 本帧零上传
  }
  lastSeq = canvas.seq;
  if (canvas.rgba) {
    canvasLayer->updateCanvas(canvas);
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
  std::string textBuf;  // 文件槽锁内拷串 — 播放器线程可能正在重建 items
  // 槽位定源: ASR 槽只查识别结果(流式部分结果优先兜底, 实时口播不吃延迟),
  // 文件槽只查文件(按平移时钟查, 字幕延迟生效)
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
    // 外挂文本槽: 字幕延迟生效(正=延后查询)
    std::lock_guard<std::mutex> lock(mtx);
    auto* item = subtitleFile.getCurrent(
        ptsMs - delayMs_.load(std::memory_order_relaxed));
    if (item && !item->text.empty()) {
      textBuf = item->text;
      text = textBuf.c_str();
    }
  }
  // 样式快照灌入(锁内拷副本, 锁外渲染): 文本槽把全局变换吃进样式,
  // 由 CPU 侧重栅格化消费(scale→字号, offset→落点, opacity→alpha)
  {
    std::lock_guard<std::mutex> lock(styleMtx);
    TextCanvasStyle snap = style;
    snap.scale = scale;
    snap.offsetXRatio = offsetX;
    snap.offsetYRatio = offsetY;
    snap.opacity = opacity;
    rasterizer.setStyle(snap, styleSeq);
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
  canvasLayer->updateCanvas(AssCanvas{rasterizer.rgba(), rasterizer.width(),
                                      rasterizer.height(), rasterizer.stride(),
                                      rasterizer.offsetX(), rasterizer.offsetY()});
#else
  (void)ptsMs;
#endif
}

void SubtitleView::setAudioDesc(AudioDesc desc) { subtitleAsr.setAudioDesc(desc); }

void SubtitleView::inputSpeech(const AvoxData& data, int64_t pts) {
  if (!asrEnabled) {
    return;
  }
  subtitleAsr.inputSpeech(data, pts);
}

void SubtitleView::closeFileContent() {
  std::lock_guard<std::mutex> lock(mtx);
  closeFileContentUnlocked();
}

void SubtitleView::closeFileContentUnlocked() {
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
