#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "../AvoxAudio.h"
#include "../AvoxLayer.h"
#include "../player/Clock.hpp"
#include "IAssOverlay.hpp"
#include "Subtitle.hpp"
#include "SubtitleAsr.hpp"
#include "SubtitleFile.hpp"
#include "SubtitleSlots.hpp"

#ifdef AVOX_ENABLE_FREETYPE
#include "TextRasterizer.hpp"
#endif

namespace avox {

class ISurfaceRender;

// 统一字幕视图(计划 doc/plan/player/字幕模块合并计划.md P3): 三槽位(内封轨/
// 外挂文件/ASR)状态与仲裁内聚, 后激活者胜, 空窗不回落。全部内容统一产出
// RGBA8 bbox canvas 经同一个 ICanvasLayer sourceOver 混合: 轨槽走 libass
// 插件(chunk 流)/PGS 画布; 外挂按扩展名分流(.ass/.ssa→插件, 其余→
// TextRasterizer); ASR 走 TextRasterizer。未装 avox_ass 插件时轨通道降级
// 为不渲染, 纯文本照常。
// 线程约定: activate*/load*/close/resetEvents 只在播放器线程; pushChunk/
// setPgsCanvas 在 IO 线程; onRender 在渲染线程(内部互斥)。
class SubtitleView : public ISubtitle, public ISurfaceRenderOb {
 public:
  using Slot = SubtitleSlots::Slot;

  SubtitleView();
  virtual ~SubtitleView() override;

  // ISubtitle 接口
  virtual void enableAsr() override;
  // 全复位: 三槽位 + 轨通道 + 文件/ASR 内容(渲染对象注册保留, 供重开复用)
  virtual void close() override;

  // ISurfaceRenderOb 接口
  virtual void onRender() override;

  // ---- 接线/公共 ----
  void setAsrMode(AsrMode mode);
  void setWindowRender(ISurfaceRender* render);
  // 画布坐标系(storage)尺寸, 开流描述就绪时下发
  void setStorageSize(int32_t width, int32_t height);
  Clock* getClock() { return &clock; }
  SubtitleAsr* getSubtitleAsr() { return &subtitleAsr; }
  void setAudioDesc(AudioDesc desc);
  void inputSpeech(const AvoxData& data, int64_t pts);

  // ---- 三槽位仲裁(视图侧拆除内聚执行; 返回被顶掉槽, 轨槽需播放器复位
  // IO 路由: 轨号 + PGS 解码开关) ----
  // 激活轨槽; 重复激活免拆除(内容由 loadTrack 换)
  Slot activateTrack();
  // 激活外挂槽: 先清两路文件内容态(.srt/.ass 换路径), 再拆被顶掉槽
  Slot activateFile();
  // 激活 ASR 槽(含识别引擎启动), 拆被顶掉槽
  Slot activateAsr();
  // 关轨槽(仅当轨本就是胜者, 不影响外挂/ASR), 返回是否真的关了
  bool deactivateTrack();

  // ---- 轨槽通道(内封 ASS/PGS 与外挂样式文件共用) ----
  // 建通道: assOverlayHub 查表 + libass init(视频分辨率坐标系)。
  // 返回 false = 无插件/初始化失败(降级), 调用方按无字幕轨处理。
  bool openTrackChannel();
  bool trackOpened() const { return overlay != nullptr; }
  // libass 轨是否已就绪(未就绪时 pushChunk 丢弃, 调用方应先排队)
  bool isTrackLoaded() const { return trackLoaded.load(); }
  // 内封轨: 喂 ASS/SSA 剧本头(MKV extradata), 之后 processChunk
  bool loadTrack(const char* extradata, int32_t size);
  // 外挂样式文件: .ass 直载, .srt 转 ASS(插件内实现)
  bool loadTrackFile(const char* path);
  // 外挂纯文本文件(.srt 等): TextRasterizer 路径
  bool loadTextFile(const char* path);
  // IO 线程: 字幕包入队(拷贝, 有界, 溢出丢最旧)
  void pushChunk(const char* data, int32_t size, int64_t ptsMs,
                 int64_t durationMs);
  // PGS 位图画布(拷贝持有; 与 libass 通道互斥, 选中 PGS 轨时到达)
  void setPgsCanvas(const AssCanvas& canvas);
  // 播放器线程(seek/换轨): 清队列 + flush libass 事件
  void resetEvents();

 private:
  // 画布层按需挂/摘(有内容才挂); lastSeq=-1 表示画布持有异源内容须先清
  void checkWindowRender();
  void closeTrackChannel();
  void closeFileContent();
  void closeAsrContent();
  // 槽位被顶掉时的视图侧拆除(轨=关通道; 外挂=清文件; ASR=停识别)
  void teardownSlot(Slot slot);
  void renderTrack(int64_t ptsMs);
  void renderText(int64_t ptsMs);

  // ---- 公共 ----
  ISurfaceRender* windowRender = nullptr;
  // 统一混合层(enableRenderCanvas 单例层, 生命周期跟随 windowRender)
  ICanvasLayer* canvasLayer = nullptr;
  // 内容去重序号: 轨( libass/PGS)与文本两路 seq 域不同, 槽位切换置 -1
  int32_t lastSeq = 0;
  Clock clock;
  // 画布坐标系(storage)尺寸
  int32_t storageW = 0;
  int32_t storageH = 0;
  SubtitleSlots slots;

  // ---- 外挂文件/ASR 槽(文本路径) ----
  SubtitleFile subtitleFile;
  SubtitleAsr subtitleAsr;
  bool fileEnabled = false;
  bool asrEnabled = false;
#ifdef AVOX_ENABLE_FREETYPE
  TextRasterizer rasterizer;
#endif

  // ---- 轨槽通道(ASS/PGS) ----
  IAssOverlay* overlay = nullptr;  // assOverlayHub.create("libass"), 消费方持有
  // libass 轨就绪(loadTrack/loadTrackFile 成功)前, chunk 全部丢弃:
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
  std::vector<uint8_t> pgsStable;  // 渲染线程持有的稳定拷贝(updateCanvas 用)
  AssCanvas pgsCanvas = {};
  AssCanvas pgsSnapshot = {};
  int32_t lastPgsSeq = 0;
  bool hasPgs = false;
};

}
