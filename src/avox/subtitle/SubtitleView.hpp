#pragma once

#include <memory>

#include "Subtitle.hpp"
#include "SubtitleAsr.hpp"
#include "SubtitleFile.hpp"
#include "../AvoxAudio.h"
#include "../AvoxLayer.h"
#include "../player/Clock.hpp"

#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FreetypeExport.h"
#endif

namespace avox {

// 继承 ISurfaceRenderOb，在渲染线程同步显示字幕
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
  bool translationEnabled = false;
  bool bLoadFont = false;
#ifdef AVOX_ENABLE_FREETYPE
  IFontLayer* fontLayer = nullptr;
#endif 
  // 时钟：外部通过 getClock() 直接操作
  std::unique_ptr<Clock> clock = nullptr;

 public:
  // ISubtitleView 接口
  virtual bool loadSrt(const char* path) override;
  virtual void enableAsr() override;
  virtual void close() override;
  virtual void enableTranslation() override;
  virtual void disableTranslation() override;

  // ISurfaceRenderOb 接口
  virtual void onRender() override;

  // 内部接口
  void setAsrMode(AsrMode mode);
  void setWindowRender(ISurfaceRender* render);
  void checkWindowRender();
  void update(int64_t ptsMs);
  Clock* getClock();
  SubtitleAsr* getSubtitleAsr();
  void setAudioDesc(AudioDesc desc);
  void inputSpeech(const AvoxData& data, int64_t pts);
};

}
