#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "../AvoxAudio.h"
#include "../AvoxLayer.h"
#include "../player/Clock.hpp"
#include "IAssOverlay.hpp"
#include "Subtitle.hpp"
#include "SubtitleAsr.hpp"
#include "SubtitleCanvas.hpp"
#include "SubtitleFile.hpp"
#include "SubtitleScan.hpp"
#include "SubtitleSlots.hpp"

#ifdef AVOX_ENABLE_FREETYPE
#include "TextRasterizer.hpp"
#endif

namespace avox {

class ISurfaceRender;

// 扫描缓存项(a01-T5): ISubtitleCandidate 的视图侧实现(持拷贝), 生命周期随扫描缓存
class SubtitleCandidateItem : public ISubtitleCandidate {
 public:
  explicit SubtitleCandidateItem(const SubtitleCandidateInfo& info) : info(info) {}
  virtual const char* getPath() const override { return info.path.c_str(); }
  virtual SCodecId getCodec() const override { return info.codecId; }
  virtual const char* getLang() const override { return info.lang.c_str(); }
  virtual int32_t getGbkHint() const override { return info.gbkHint ? 1 : 0; }

 private:
  SubtitleCandidateInfo info;
};

// 统一字幕视图(计划 doc/plan/player/字幕模块合并计划.md P3): 三槽位(内封轨/
// 外挂文件/ASR)状态与仲裁内聚, 后激活者胜, 空窗不回落。全部内容统一产出
// RGBA8 bbox canvas 经同一个 ICanvasLayer sourceOver 混合: 轨槽走 libass
// 插件(chunk 流)/PGS 画布; 外挂按扩展名分流(.ass/.ssa→插件, 其余→
// TextRasterizer); ASR 走 TextRasterizer。未装 avox_ass 插件时轨通道降级
// 为不渲染, 纯文本照常。
// 线程约定: activate*/deactivate*/load*/closeSubtitle/resetEvents 只在播放器
// 线程; pushChunk/setPgsCanvas 在 IO 线程; onRender 在渲染线程(内部互斥);
// ISubtitle 观感 setter 任意线程可调(含 open 前, 不触发图重建)。
class SubtitleView : public ISubtitle, public ISurfaceRenderOb {
 public:
  using Slot = SubtitleSlots::Slot;

  SubtitleView();
  virtual ~SubtitleView() override;

  // ISubtitle 接口
  virtual void enableAsr() override;
  virtual void disableAsr() override;
  // 全局变换(三层通用): 文本槽在 CPU 侧消费(重栅格化), 轨槽直发 canvas 合成
  virtual void setScale(float s) override;
  virtual void setOffset(float offsetX, float offsetY) override;
  virtual void setOpacity(float o) override;
  // 纯文本样式(仅 SRT/ASR 文本槽生效; ASS/PGS 槽静默忽略)
  virtual void setFont(const char* fontName, int32_t fontSize) override;
  virtual void setColor(float r, float g, float b) override;
  virtual void setAlign(HAlignType h, VAlignType v) override;
  virtual void setPosition(float anchorX, float anchorY) override;
  virtual void setPositionMargin(float marginX, float marginY) override;
  virtual void setMaxWidth(float ratio) override;
  // 外挂编码探测结果(.ass/.ssa 走 overlay 记录, 文本路径走 SubtitleFile;
  // 未探测/卸载后 unknown)
  virtual SubtitleEncoding getFileEncoding() override {
    if (overlay) {
      const SubtitleEncoding enc = overlay->getFileEncoding();
      if (enc != SubtitleEncoding::unknown) {
        return enc;
      }
    }
    return subtitleFile.getEncoding();
  }
  // 字幕整体延迟: 正=延后, 负=提前; 内容选择按 (播放pts - delay) 查询
  virtual void setDelay(int64_t delayMs) override {
    delayMs_.store(delayMs, std::memory_order_relaxed);
  }
  // 外挂候选枚举(a01-T5): 纯文件系统查询, 结果入缓存(scanMtx), 经 getSubtitleCandidate 取用
  virtual int32_t listSubtitleCandidates(const char* videoUrl) override;
  virtual ISubtitleCandidate* getSubtitleCandidate(int32_t index) override;
  // ASS 轨样式覆盖(a01-T3): 下发 overlay(libass 排版层); 无插件时存值,
  // openTrackChannel 建通道补发
  virtual void setAssScale(float scale) override;
  virtual void setAssFont(const char* family) override;
  // 全复位(内部用: 播放器 close/换源/析构): 三槽位 + 轨通道 + 文件/ASR 内容
  // (渲染对象注册保留, 供重开复用)
  void closeSubtitle();

  // ISurfaceRenderOb 接口
  virtual void onRender(const SurfaceRenderEvent* ev) override;

  // ---- 接线/公共 ----
  void setAsrMode(AsrMode mode);
  void setWindowRender(ISurfaceRender* render);
  // 画布坐标系(storage)尺寸, 开流描述就绪时下发
  void setStorageSize(int32_t width, int32_t height);
  Clock* getClock() { return &clock; }
  SubtitleAsr* getSubtitleAsr() { return &subtitleAsr; }
  void setAudioDesc(AudioDesc desc);
  void inputSpeech(const AvoxData& data, int64_t pts);

  // ---- 三槽位仲裁(视图侧拆除内聚执行; 返回被顶掉槽) ----
  // 轨槽被顶掉时回调(播放器构造时注册一次): 复位 IO 侧轨号与 PGS 解码路由
  // (视图不持有 IO 对象; 回调内只碰 atomic 槽位与 IO 开关, 任意线程安全)
  void setTrackResetCb(std::function<void()> cb) {
    trackResetCb = std::move(cb);
  }
  // 激活轨槽; 重复激活免拆除(内容由 loadTrack 换)
  Slot activateTrack();
  // 激活外挂槽: 先清两路文件内容态(.srt/.ass 换路径), 再拆被顶掉槽
  Slot activateFile();
  // 激活 ASR 槽(含识别引擎启动), 拆被顶掉槽
  Slot activateAsr();
  // 关轨槽(仅当轨本就是胜者, 不影响外挂/ASR), 返回是否真的关了
  bool deactivateTrack();
  // 关外挂槽(仅当外挂本就是胜者, 不影响轨/ASR), 返回是否真的关了
  bool deactivateFile();
  // 关 ASR 槽(仅当 ASR 本就是胜者, 不影响轨/外挂), 返回是否真的关了
  bool deactivateAsr();

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
  // 外挂槽是否由 libass 通道驱动(.ass/.ssa; 其余 .srt 走文本光栅化)
  bool fileUseLibass() const { return overlay != nullptr && trackLoaded.load(); }
  // 全局变换按当前胜者槽位下发(文本槽=单位值避免双份, 轨槽=用户值);
  // 变换 setter/槽位变化/画布层挂载时调
  void pushCanvasTransform();

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
  std::function<void()> trackResetCb;  // 轨槽被顶掉时通知播放器复位 IO 侧

  // ---- 观感样式(权威副本): setter 任意线程写(styleMtx), 渲染线程拷贝消费;
  // 不触发图重建 ----
  mutable std::mutex styleMtx;
  int32_t styleSeq = 0;  // 任一样式 setter 递增, 参与 rasterizer 缓存判定
  float scale = 1.f;     // 全局变换(轨槽直发 canvas; 文本槽由 CPU 侧消费)
  float offsetX = 0.f;
  float offsetY = 0.f;
  float opacity = 1.f;
  Slot pushedSlot = Slot::none;  // 已按此槽位下发变换(onRender 渲染线程检测变化)

  // ---- 外挂文件/ASR 槽(文本路径) ----
  SubtitleFile subtitleFile;
  SubtitleAsr subtitleAsr;
  bool fileEnabled = false;
  bool asrEnabled = false;
  // 字幕整体延迟 ms(setDelay 写, 渲染线程读)
  std::atomic<int64_t> delayMs_{0};

  // 外挂候选扫描缓存(a01-T5): deque 保条目指针稳定, 至下次扫描/closeSubtitle
  mutable std::mutex scanMtx;
  std::deque<SubtitleCandidateItem> scanCache;
#ifdef AVOX_ENABLE_FREETYPE
  TextRasterizer rasterizer;
  TextCanvasStyle style;  // 纯文本样式副本(setFont/setColor/setAlign/... 写入)
#endif

  // ---- 轨槽通道(ASS/PGS) ----
  IAssOverlay* overlay = nullptr;  // assOverlayHub.create("libass"), 消费方持有
  // ASS 轨样式覆盖存档(a01-T3): setter 写, openTrackChannel 建通道补发;
  // scale 原子, font 独立小锁(不与 mtx 嵌套)
  std::atomic<float> assScale{1.f};
  mutable std::mutex assStyleMtx;
  std::string assFontFamily;
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

  // PGS 画布帧(视图持有拷贝): IO 侧解码可能先于选轨全量到达(旁路包不进
  // 同步时钟, 全速读), 按 pts 排队、渲染按播放位置取帧; 有界, 溢出丢最旧
  struct PgsFrame {
    int64_t ptsMs = 0;
    int32_t seq = 0;
    bool empty = true;  // 清屏帧
    int32_t width = 0;
    int32_t height = 0;
    int32_t stride = 0;
    std::vector<uint8_t> rgba;
  };
  std::deque<PgsFrame> pgsFrames;
  std::vector<uint8_t> pgsStable;  // 渲染线程持有的稳定拷贝(updateCanvas 用)
  AssCanvas pgsSnapshot = {};
  int32_t lastPgsSeq = 0;
};

}
