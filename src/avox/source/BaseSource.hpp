#pragma once

#include <string>

#include "../AvoxCodec.h"
#include "../module/HighClock.hpp"
#include "../module/JsonOption.hpp"
#include "../module/LogHelper.hpp"
#include "../module/Observer.hpp"
#include "../player/Player.hpp"
#include "PacketBuf.hpp"

namespace avox {

// 字幕轨描述实现: 字符串留在这里(公共头 ISTrackDesc 不出现 STL),
// 对外只经 const char* 暴露, 生存期随本对象
class STrackDescImpl : public ISTrackDesc {
 public:
  STrackDescImpl(int32_t id, SCodecId codec, const std::string& langText,
                 const std::string& titleText, bool forcedFlag)
      : track_id(id), codec_id(codec), lang_text(langText),
        title_text(titleText), forced_flag(forcedFlag) {}
  virtual int32_t trackId() const override { return track_id; }
  virtual SCodecId codecId() const override { return codec_id; }
  virtual const char* lang() const override { return lang_text.c_str(); }
  virtual const char* title() const override { return title_text.c_str(); }
  virtual bool forced() const override { return forced_flag; }

 private:
  int32_t track_id;
  SCodecId codec_id;
  std::string lang_text;
  std::string title_text;
  bool forced_flag;
};

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
  // 字幕轨描述(只有内封字幕轨, 外挂文件不经源)
  std::vector<STrackDescImpl> subtitleTracks;

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
  virtual int32_t subtitleSize() override;
  virtual const ISTrackDesc* getSubtitleDesc(int32_t index) override;
  virtual bool canSeek() override;

 public:
  virtual void onOptionChange(const char* key, ArgType option) override {}

 public:
  // 在open之后调用
  void addVideoDesc(const VTrackDesc& desc);
  void addAudioDesc(const ATrackDesc& desc);
  // 字幕轨登记(生产端只传值, 字符串在源内持有)
  void addSubtitleDesc(int32_t trackId, SCodecId codecId,
                       const std::string& lang, const std::string& title,
                       bool forced);

 public:
  // 当音频与视频都准备好了，请调用开始下一步
  void trackReady();

 protected:
  virtual void onTrackOpen() {};
};

}