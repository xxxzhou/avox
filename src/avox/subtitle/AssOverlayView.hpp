#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "../AvoxLayer.h"
#include "IAssOverlay.hpp"
#include "../player/Clock.hpp"

namespace avox {

class ISurfaceRender;

// ASS/PGS 字幕轨消费视图(计划 doc/plan/player/ASS字幕渲染计划.md §3.5):
// IO 线程 pushChunk → 渲染线程 onRender 里按播放时钟 drain 喂 libass →
// render 出 RGBA canvas(bbox 裁剪) → ICanvasLayer(Vulkan 图内 source-over)。
// 未装 avox_ass 插件时 overlay 建不出来, 全链自然降级为不渲染字幕轨。
// 线程约定: open/close/loadTrack/resetEvents 只在播放器线程;
// pushChunk 在 IO 线程; onRender 在渲染线程(内部互斥)。
class AssOverlayView : public ISurfaceRenderOb {
 public:
  AssOverlayView() = default;
  virtual ~AssOverlayView() override;

  // VideoTrack 接线(等价 SubtitleView::setWindowRender): 注册每帧回调
  void setSurfaceRender(ISurfaceRender* render);
  Clock* getClock() { return &clock; }

  // 建立通道: assOverlayHub 查表 + libass init(视频分辨率坐标系)。
  // 返回 false = 无插件/初始化失败(降级), 调用方按无字幕轨处理。
  bool open(int32_t storageWidth, int32_t storageHeight);
  void close();
  bool opened() const { return overlay != nullptr; }
  // libass 轨是否已就绪(未就绪时 pushChunk 丢弃, 调用方应先排队)
  bool isTrackLoaded() const { return trackLoaded.load(); }

  // 内封轨: 喂 ASS/SSA 剧本头(MKV extradata), 之后 processChunk
  bool loadTrack(const char* extradata, int32_t size);
  // 外挂文件: .ass 直载, .srt 转 ASS(插件内实现)
  bool loadFile(const char* path);

  // IO 线程: 字幕包入队(拷贝, 有界, 溢出丢最旧)
  void pushChunk(const char* data, int32_t size, int64_t ptsMs,
                 int64_t durationMs);
  // PGS 位图画布(拷贝持有; 与 libass 通道互斥, 选中 PGS 轨时到达)
  void setPgsCanvas(const AssCanvas& canvas);
  // 播放器线程(seek/换轨): 清队列 + flush libass 事件
  void resetEvents();

  // 渲染线程每帧回调
  virtual void onRender() override;

 private:
  void ensureLayer();

  ISurfaceRender* surfaceRender = nullptr;
  IAssOverlay* overlay = nullptr;  // assOverlayHub.create("libass"), 消费方持有
  ICanvasLayer* canvasLayer = nullptr;
  int32_t lastSeq = 0;
  Clock clock;

  // libass 轨就绪(loadTrack/loadFile 成功)前, chunk 全部丢弃:
  // ass_process_chunk 无 track 时是空操作, 排队反而会白占内存
  std::atomic<bool> trackLoaded{false};
  std::mutex mtx;
  struct SubChunk {
    std::vector<char> data;
    int64_t ptsMs = 0;
    int64_t durationMs = 0;
  };
  std::deque<SubChunk> chunks;

  // PGS 画布(视图持有拷贝): seq 变化即上屏, 呈现集语义由包序决定
  std::vector<uint8_t> pgsBuf;
  AssCanvas pgsCanvas = {};
  AssCanvas pgsSnapshot = {};  // onRender 锁内取快照用
  int32_t lastPgsSeq = 0;
  bool hasPgs = false;
};

}
