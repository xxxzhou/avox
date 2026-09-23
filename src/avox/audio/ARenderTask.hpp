#pragma once

#include "../module/RunTask.hpp"
#include "../player/MPCommon.hpp"
#include "../player/Player.hpp"
#include "AudioOutput.hpp"
#include "IAudioTempo.hpp"

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
  // 变速不变调处理器(首个变速帧时经 audioTempoHub 懒创建, 缺位降级变调重采样)
  std::unique_ptr<IAudioTempo> tempo;
  // 工厂只探一次(create 触发插件懒加载扫描, 失败不复扫)
  bool tempoTried = false;
  // tempo 是否生效(回常速时 reset 丢弃残留窗)
  bool tempoActive = false;
  // 输出↔源时间映射锚(seek冲刷/回常速后重锚): emitMs=下一输出块源pts, feedMs=已喂源结束pts
  bool tempoAnchorSet = false;
  int64_t tempoEmitMs = 0;
  int64_t tempoFeedMs = 0;
  // 当前档位(只在渲染线程调 setTempo, 避免与 process 跨线程竞争)
  double tempoSpeed = 1.0;

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