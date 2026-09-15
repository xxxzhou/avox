#pragma once

#include <memory>

#include "Subtitle.hpp"
#include "SubtitleAsr.hpp"
#include "SubtitleFile.hpp"
#include "../AvoxAudio.h"
#include "../AvoxLayer.h"
#include "../player/Clock.hpp"

#ifdef AVOX_ENABLE_FREETYPE
#include "TextRasterizer.hpp"
#endif

namespace avox {

class ISurfaceRender;

// 字幕文本视图(ASR/外挂文本槽位): ISurfaceRenderOb 每帧回调, 文本经
// TextRasterizer 产出 RGBA canvas 走 ICanvasLayer 统一混合通道(计划
// doc/plan/player/字幕模块合并计划.md P2)。与 ASS/PGS 轨视图共享
// VkCanvasLayer, 互斥由 MediaPlayer 三槽位仲裁保证。
class SubtitleView : public ISubtitle, public ISurfaceRenderOb {
 public:
  SubtitleView();
  virtual ~SubtitleView();

 private:
  ISurfaceRender* windowRender = nullptr;
  std::unique_ptr<SubtitleFile> subtitleFile = std::make_unique<SubtitleFile>();
  std::unique_ptr<SubtitleAsr> subtitleAsr = std::make_unique<SubtitleAsr>();
  bool fileEnabled = false;
  bool asrEnabled = false;
  // 文本画布层(enableRenderCanvas 单例层, 生命周期跟随 windowRender)
  ICanvasLayer* canvasLayer = nullptr;
  int32_t lastCanvasSeq = 0;
#ifdef AVOX_ENABLE_FREETYPE
  TextRasterizer rasterizer;
#endif
  // 视频 storage 分辨率(画布坐标系), VideoTrack::onVideoDesc / SourcePlayer 下发
  int32_t storageW = 0;
  int32_t storageH = 0;
  // 时钟：外部通过 getClock() 直接操作
  std::unique_ptr<Clock> clock = nullptr;

 public:
  // ISubtitle 接口
  virtual void enableAsr() override;
  virtual void close() override;

  // ISurfaceRenderOb 接口
  virtual void onRender() override;

  // 内部接口
  bool loadFile(const char* path);
  // 只关文件字幕槽(保留 ASR), 供三槽位仲裁用
  void closeFile();
  void setAsrMode(AsrMode mode);
  void setWindowRender(ISurfaceRender* render);
  // 画布坐标系(storage)尺寸, 开流描述就绪时下发
  void setStorageSize(int32_t width, int32_t height);
  void checkWindowRender();
  void update(int64_t ptsMs);
  Clock* getClock();
  SubtitleAsr* getSubtitleAsr();
  void setAudioDesc(AudioDesc desc);
  void inputSpeech(const AvoxData& data, int64_t pts);
};

}
