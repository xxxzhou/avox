#pragma once

#include "AndCommon.hpp"
#include "avox/audio/AudioOutput.hpp"
#include "avox/module/RunTask.hpp"
#ifdef AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFResample.hpp"
#endif

namespace avox {

class AndAudioRender : public AudioOutput {
public:
  AndAudioRender();
  virtual ~AndAudioRender();

private:
  jobject audioTrack = nullptr;
  int64_t baseTime = 0;
  int64_t baseOffset = 0;
#ifdef AVOX_ENABLE_FFMPEG
  // 设备当前输出(蓝牙A2DP等)建不出多声道PCM轨时降混双声道重开
  FFResample devResample;
  bool devConvert = false;
#endif

protected:
  virtual void onInit() override;
  virtual void onRender(const AvoxData &frame) override;
  virtual void onClose() override;
  int64_t getPlayPosition();
  jobject createTrack(JNIEnv *env, int32_t sampleRate, int32_t channels,
                      int32_t bits);

public:
  virtual bool empty() override;
  virtual int32_t getQueueMS() override;
  virtual bool full() override;
  virtual void pause(bool pause) override;
  virtual void flush() override;  
  virtual void speed(double speed) override;
  virtual void setVolume(float volume) override;
  virtual float getVolume() override;
};

}