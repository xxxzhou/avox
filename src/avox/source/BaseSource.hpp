#pragma once

#include "../AvoxCodec.h"
#include "../module/HighClock.hpp"
#include "../module/JsonOption.hpp"
#include "../module/LogHelper.hpp"
#include "../module/Observer.hpp"
#include "../player/Player.hpp"
#include "PacketBuf.hpp"

namespace avox {

// IAVSource表示编码音视频流
// IRawSource表示PCM/YUV/GPUTexture原始数据流
// IAVSource与IRawSource相同抽象部分
class AVOX_EXPORT BaseSource : public OptionLink, public ISourceInfo {
 public:
  BaseSource();
  virtual ~BaseSource() {};

 protected:
  // 音频和视频轨道描述
  std::vector<VTrackDesc> videoTracks;
  std::vector<ATrackDesc> audioTracks;

  // 是否不处理视频,默认处理,子类不要改这个,只用来表示用户是否处理
  bool bDisableVideo = false;
  // 是否不处理音频,默认处理
  bool bDisableAudio = false;

  // 是否调用open，open为true后
  // bDisableVideo,bDisableAudio不能在改
  bool bOpen = false;
  bool bSeek = false;

 public:
  const std::vector<VTrackDesc>& getVideoTracks() const { return videoTracks; }
  const std::vector<ATrackDesc>& getAudioTracks() const { return audioTracks; }

  // 关闭视频，源里有视频也不会处理，需要在open之前调用
  virtual void disableVideo(bool bDisable);
  // 关闭音频，源里有音频也不会处理，需要在open之前调用
  virtual void disableAudio(bool bDisable);

 public:
  virtual int32_t videoSize() override;
  virtual int32_t audioSize() override;
  virtual VTrackDesc getVideoDesc(int32_t index) override;
  virtual ATrackDesc getAudioDesc(int32_t index) override;
  virtual bool canSeek() override;

 public:
  virtual void onOptionChange(const char* key, ArgType option) override {}

 public:
  // 在open之后调用
  void addVideoDesc(const VTrackDesc& desc);
  void addAudioDesc(const ATrackDesc& desc);

 public:
  // 当音频与视频都准备好了，请调用开始下一步
  void trackReady();

 protected:
  virtual void onTrackOpen() {};
};

}