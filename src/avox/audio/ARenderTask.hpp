#pragma once

#include "../module/RunTask.hpp"
#include "../player/MPCommon.hpp"
#include "../player/Player.hpp"
#include "AudioOutput.hpp"

#ifdef AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFResample.hpp"
#endif

namespace avox {

// 单独线程从AudioTrack队列读取帧数据,调用渲染器渲染
class ARenderTask : public RunTask, public IPlayerContext {
public:
  ARenderTask();
  virtual ~ARenderTask();

protected:
  // 有些信息在AudioTrack上,在这不复制，直接拿AudioTrack的
  class AudioTrack *trackContext = nullptr;
  // 音频渲染器
  std::unique_ptr<AudioOutput> audioRender = nullptr;
  // 音频渲染描述
  AudioDesc renderDesc = {};
  int32_t frameMs = 40;
  // 当前播放数据PTS
  int64_t framePts = 0;
#ifdef AVOX_ENABLE_FFMPEG
  // 变速
  std::unique_ptr<FFResample> speedResample;
  // 同步视频
  std::unique_ptr<FFResample> syncResmaple;
#endif

public:
  // 这时解码器已初始化
  void start(class AudioTrack *trackContext);
  IAudioRender* getAudioRender();
  void pause(bool pause);
  void speed(double speed);
  void flush();
  void close();

protected:
  virtual void onRunTask() override;
};

}