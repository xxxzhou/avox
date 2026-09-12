#pragma once

#ifdef __ONLY_LINUX__

#ifdef AVOX_ENABLE_PULSE

#include <atomic>
#include <mutex>

#include "avox/audio/AudioOutput.hpp"

#include <pulse/pulseaudio.h>

#ifdef AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFResample.hpp"
#endif

namespace avox {

// Linux PulseAudio 输出(WSLg/桌面发行版通用, PipeWire 兼容 pulse 协议)
// 契约对齐 WasAudioRender: FFResample 重采样到设备格式, 软件音量缩放,
// 队列水位经 timing info(read_index) 刷新
class PulseAudioRender : public AudioOutput {
 public:
  PulseAudioRender();
  virtual ~PulseAudioRender();

 private:
  // pulse 回调入口(pulse线程), 仅 signal 唤醒 wait 方
  static void contextStateCb(pa_context* c, void* userdata);
  static void streamStateCb(pa_stream* s, void* userdata);
  static void streamSuccessCb(pa_stream* s, int success, void* userdata);
  static void timingUpdateCb(pa_stream* s, int success, void* userdata);

 protected:
  virtual void onInit() override;
  virtual void onRender(const AvoxData& frame) override;
  virtual void onClose() override;

 public:
  virtual bool empty() override;
  virtual int32_t getQueueMS() override;
  virtual bool full() override;
  virtual void pause(bool pause) override;
  virtual void flush() override;
  virtual void speed(double speed) override;
  virtual void setVolume(float volume) override;
  virtual float getVolume() override;

 private:
  // 队列水位计算(调用方已持有 mtx), getQueueMS/empty/full 共用
  int32_t queueMSLocked();
  pa_threaded_mainloop* mainloop = nullptr;
  pa_context* context = nullptr;
  pa_stream* stream = nullptr;
  pa_sample_spec spec = {};
  std::mutex mtx;
  AudioDesc renderDesc = {};
  // 平滑用的上次值(对齐 WasAudioRender)
  int32_t lastQueueMs = 0;
  // 写入/已播字节计数; playBytes 由 timing info 回调刷新
  std::atomic<int64_t> writeBytes{0};
  std::atomic<int64_t> playBytes{0};
#ifdef AVOX_ENABLE_FFMPEG
  // 重采样: 输入desc(常为fltp等planar) -> 设备packed格式
  std::unique_ptr<FFResample> resample = nullptr;
#endif
};

}

#endif  // AVOX_ENABLE_PULSE
#endif  // __ONLY_LINUX__
